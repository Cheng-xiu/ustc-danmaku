import { spawnSync } from 'node:child_process';
import { readFileSync, mkdirSync, writeFileSync } from 'node:fs';
import { fileURLToPath, pathToFileURL } from 'node:url';
import path from 'node:path';
const root = fileURLToPath(new URL('../../', import.meta.url));
const out = path.join(root, 'build', 'web-validation');
mkdirSync(out, { recursive: true });
const executable = process.env.NATIVE_REPLAY ?? path.join(root, 'build', 'native', 'web_native_replay.exe');
const factory = (await import(pathToFileURL(path.join(root, 'web/public/wasm/demo-core.mjs')).href)).default;
const module = await factory();
const wasmSnapshot = () => {
  const ptr = module._demo_snapshot();
  const size = module._demo_snapshot_size();
  if (!ptr || !size) throw new Error('Invalid Wasm snapshot');
  return Buffer.from(module.HEAPU8.subarray(ptr, ptr + size));
};
const floats = new Set([29, 30, 57, 58]);
function floatSlots(buffer) {
  const result = new Set(floats);
  const groups = [
    [buffer.readUInt32LE(40 * 4) / 4, 1 + buffer.readUInt32LE(6 * 4), 10, [2, 3, 4]],
    [buffer.readUInt32LE(41 * 4) / 4, buffer.readUInt32LE(7 * 4), 10, [4, 5, 6, 7, 8]],
    [buffer.readUInt32LE(42 * 4) / 4, buffer.readUInt32LE(8 * 4), 8, [0, 1, 2, 3, 4]],
    [buffer.readUInt32LE(43 * 4) / 4, buffer.readUInt32LE(9 * 4), 9, [7, 8]],
    [buffer.readUInt32LE(54 * 4) / 4, buffer.readUInt32LE(53 * 4), 2, [0, 1]],
  ];
  for (const [base, count, stride, offsets] of groups) for (let i = 0; i < count; i++) for (const slot of offsets) result.add(base + i * stride + slot);
  return result;
}
let maxFloatDelta = 0, ticksCompared = 0;
const cases = [[20261006, 0, 3, 960, 720], [12345, 0, 1, 960, 720], [0x9abcdef0, 0x12345678, 8, 960, 720],
  [20261007, 0, 3, 1120, 617.1428833], [7654321, 0, 8, 1280, 540], [13579, 0, 3, 650, 1063.38464],
  [20261006,0,3,960,720,1], [20261007,0,3,1120,617.1428833,1], [7654321,0,8,1280,540,1], [13579,0,3,650,1063.38464,1]];
const reports = [];
for (const [lo, hi, students, width, height, manual = 0] of cases) {
  const filename = path.join(out, `native-${lo}-${hi}-${students}-${width}-${manual}.bin`);
  const run = spawnSync(executable, [filename, String(lo), String(hi), String(students), String(width), String(height), String(manual)], { encoding: 'utf8' });
  if (run.status !== 0) throw new Error(`Native replay failed ${run.status}: ${run.stderr}`);
  const native = readFileSync(filename);
  let position = 0;
  function compare(tick) {
    const size = native.readUInt32LE(position); position += 4;
    const a = native.subarray(position, position + size); position += size;
    const b = wasmSnapshot();
    if (a.length !== b.length) throw new Error(`Snapshot length mismatch ${tick}`);
    const floatWords = floatSlots(a);
    for (let word = 0; word < size / 4; word++) {
      if (floatWords.has(word)) {
        const x = a.readFloatLE(word * 4), y = b.readFloatLE(word * 4);
        const delta = Math.abs(x - y);
        if (!Number.isFinite(x) || !Number.isFinite(y) || delta > 0.002) throw new Error(`Float mismatch tick=${tick} word=${word}: ${x}/${y}`);
        maxFloatDelta = Math.max(maxFloatDelta, delta);
      } else if (a.readUInt32LE(word * 4) !== b.readUInt32LE(word * 4)) {
        throw new Error(`Discrete mismatch tick=${tick} word=${word}: ${a.readUInt32LE(word * 4)}/${b.readUInt32LE(word * 4)}`);
      }
    }
    ticksCompared++;
    return b;
  }
  if (!module._demo_reset_sized(lo, hi, students, width, height)) throw new Error('Reset failed');
  compare(-1);
  let final;
  for (let tick = 0; tick < 3600; tick++) {
    const segment = Math.floor(tick / 120) % 4;
    const mx = segment === 0 ? 1 : segment === 2 ? -1 : 0;
    const my = segment === 1 ? -1 : segment === 3 ? 1 : 0;
    let attacks = tick % 240 === 0 ? 1 << (Math.floor(tick / 240) % 4) : 0;
    if (tick % 240 === 10) attacks = 8;
    const pointer = Math.floor(tick / 300) % 3 === 1 ? 1 : 0;
    const dirs = [[1,0],[Math.fround(.70710677),Math.fround(.70710677)],[0,1],[-Math.fround(.70710677),Math.fround(.70710677)],
      [-1,0],[-Math.fround(.70710677),-Math.fround(.70710677)],[0,-1],[Math.fround(.70710677),-Math.fround(.70710677)]];
    const args = [mx, my, pointer, tick % 600 < 300 ? 260 : 720, tick % 480 < 240 ? 540 : 180, attacks];
    if (manual) module._demo_step_aim(...args, ...dirs[Math.floor(tick / 300) % 8]);
    else module._demo_step(...args);
    final = compare(tick);
  }
  if (position !== native.length) throw new Error('Unconsumed native records');
  reports.push({ manual: Boolean(manual), seedLo: lo, seedHi: hi, students, fieldW: final.readFloatLE(29 * 4), fieldH: final.readFloatLE(30 * 4), tick: final.readUInt32LE(12), status: final.readUInt32LE(16), accepted: final.readUInt32LE(88), bossBullets: final.readUInt32LE(104), studentBullets: final.readUInt32LE(108) });
}
module._demo_dispose();
const report = { nativeCompiler: 'see web-demo-validation.md', floatAbsoluteTolerance: 0.002, maxFloatDelta, ticksCompared, cases: reports };
writeFileSync(path.join(out, 'core-parity.json'), JSON.stringify(report, null, 2));
console.log(JSON.stringify(report, null, 2));
