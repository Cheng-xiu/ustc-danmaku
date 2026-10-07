import { loadCore } from './wasm/client';
import { GameScene } from './render/scene';
const core = await loadCore();
const snapshot = core.reset(20261006, 0, 3);
core.dispose();
snapshot.bullets = Array.from({length:800}, (_, i) => ({id:String(i+1), faction:i%5===0?2:1, pattern:i%4, x:0, y:0, vx:0, vy:0, radius:6, source:1}));
snapshot.warnings = [];
const scene = await GameScene.create(document.querySelector('#arena')!);
let frame = 0, last = 0;
const intervals:number[] = [], cpu:number[] = [];
const percentile = (a:number[], q:number) => [...a].sort((x,y)=>x-y)[Math.floor((a.length-1)*q)];
function update(now:number) {
  const start = performance.now();
  snapshot.tick = frame;
  snapshot.bullets.forEach((bullet,i) => {bullet.x = 30+(i%40)*23+Math.sin(frame/40+i)*8;bullet.y = 50+Math.floor(i/40)*31+Math.cos(frame/60+i)*12;});
  scene.draw(snapshot);scene.render();
  if (frame >= 60) { intervals.push(now-last);cpu.push(performance.now()-start); }
  last=now;frame++;
  if (frame<360) requestAnimationFrame(update);
  else {
    const report = { fixture:'800 sprites, render only, no gameplay core stress', frames:intervals.length, browser:navigator.userAgent, dpr:devicePixelRatio, logicResolution:'960x720', intervalMedianMs:percentile(intervals,.5), intervalP95Ms:percentile(intervals,.95), cpuMedianMs:percentile(cpu,.5), cpuP95Ms:percentile(cpu,.95), maxIntervalMs:Math.max(...intervals) };
    document.querySelector('#result')!.textContent=JSON.stringify(report,null,2);
    Object.assign(window,{__bench:report});
  }
}
requestAnimationFrame(update);
