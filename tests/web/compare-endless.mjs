import { readFileSync, writeFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';
import path from 'node:path';
const directory=path.resolve('build/web-validation');
const replay=JSON.parse(readFileSync(path.join(directory,'endless-inputs.json'),'utf8'));
const native=readFileSync(path.join(directory,'endless-native-snapshots.bin'));
const factory=(await import(pathToFileURL(path.resolve('apps/web/public/wasm/demo-core.mjs')).href)).default;
const core=await factory();
let position=0,compared=0,maxFloatDelta=0;
function compare(tick) {
  const size=native.readUInt32LE(position);position+=4;
  const a=native.subarray(position,position+size);position+=size;
  const pointer=core._demo_snapshot(),length=core._demo_snapshot_size();
  const b=Buffer.from(core.HEAPU8.subarray(pointer,pointer+length));
  if(!pointer||length!==size)throw Error(`Size mismatch at ${tick}`);
  const floats=new Set([29,30]);
  const groups=[
    [a.readUInt32LE(160)/4,1+a.readUInt32LE(24),10,[2,3,4]],
    [a.readUInt32LE(164)/4,a.readUInt32LE(28),10,[4,5,6,7,8]],
    [a.readUInt32LE(168)/4,a.readUInt32LE(32),8,[0,1,2,3,4]],
    [a.readUInt32LE(172)/4,a.readUInt32LE(36),9,[7,8]],
    [a.readUInt32LE(216)/4,a.readUInt32LE(212),2,[0,1]],
  ];
  for(const [base,count,stride,slots]of groups)for(let i=0;i<count;i++)for(const slot of slots)floats.add(base+i*stride+slot);
  for(let word=0;word<size/4;word++){
    if(floats.has(word)){
      const x=a.readFloatLE(word*4),y=b.readFloatLE(word*4),delta=Math.abs(x-y);
      if(!Number.isFinite(x)||!Number.isFinite(y)||delta>0.002)throw Error(`Float mismatch ${tick}/${word}`);
      maxFloatDelta=Math.max(maxFloatDelta,delta);
    }else if(a.readUInt32LE(word*4)!==b.readUInt32LE(word*4))throw Error(`Discrete mismatch ${tick}/${word}`);
  }
  compared++;return b;
}
if(!core._demo_reset(replay.seed_lo,replay.seed_hi,replay.students))throw Error('Reset failed');
compare(0);
let last;
for(const [index,input]of replay.inputs.entries()){
  if(input.tick!==index||Math.hypot(input.move_x,input.move_y)>1.00001||input.attack_mask>15)throw Error('Invalid input');
  core._demo_step(input.move_x,input.move_y,input.pointer_valid,input.pointer_x,input.pointer_y,input.attack_mask);
  last=compare(index+1);
}
if(position!==native.length)throw Error('Unconsumed native records');
const actorOffset=last.readUInt32LE(160);
const actual={status:last.readUInt32LE(16),tick:last.readUInt32LE(12),boss_hp:last.readInt32LE(actorOffset+20),boss_hits:last.readUInt32LE(96),student_hits:last.readUInt32LE(100),kills:last.readUInt32LE(204),gpa_hundredths:last.readUInt32LE(200),wave_index:last.readUInt32LE(180),waves_cleared:last.readUInt32LE(184),student_count:last.readUInt32LE(24)};
for(const [key,value]of Object.entries(actual))if(value!==replay.expected[key])throw Error(`Final mismatch ${key}`);
const result={passed:true,configVersion:last.readUInt32LE(20),abiVersion:last.readUInt32LE(4),seedLo:replay.seed_lo,seedHi:replay.seed_hi,snapshotsCompared:compared,floatAbsoluteTolerance:0.002,maxFloatDelta,actual};
writeFileSync(path.join(directory,'endless-parity.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));
core._demo_dispose();
