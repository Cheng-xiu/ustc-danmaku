import './style.css';
import { loadCore } from './wasm/client';
import { BrowserInput } from './input/browserInput';
import { FixedStepClock } from './runtime/gameLoop';
import { GameScene } from './render/scene';
import { HUD } from './ui/hud';
import type { CoreClient, Phase, Snapshot, GameEvent } from './types';

const arena = document.querySelector<HTMLElement>('#arena')!;
const loading = document.querySelector<HTMLElement>('#load-status')!;
const hud = new HUD(document.querySelector<HTMLElement>('#hud')!);
let phase: Phase = 'menu';
let reason = '';
let reasonUntil = 0;
let snapshot: Snapshot | null = null;
let core: CoreClient;
let scene: GameScene;
let input: BrowserInput;
const clock = new FixedStepClock();
let lastTime = 0;
let raf = 0;
const samples: { interval: number; frame: number; core: number; decode: number; render: number; ticks: number }[] = [];
function resize() {
  if (!scene) return;
  const bounds = arena.getBoundingClientRect();
  scene.resize(bounds.width, bounds.height);
}
new ResizeObserver(resize).observe(arena);
window.addEventListener('resize', resize);

function roundFieldSize() {
  const bounds = arena.getBoundingClientRect();
  const aspect = bounds.width / Math.max(1, bounds.height);
  // Keep playable area constant while matching this round's window aspect.
  const area = 960 * 720;
  return { width: Math.sqrt(area * aspect), height: Math.sqrt(area / aspect) };
}

function changePhase(next: Phase, message = '') {
  phase = next;
  reason = message;
  reasonUntil = message ? Infinity : 0;
  clock.reset();
  // Establish the baseline using the next RAF timestamp, from the same clock domain.
  lastTime = 0;
  input?.clear();
  hud.render(snapshot, phase, reason);
}
function restart() {
  const field = roundFieldSize();
  try {
    snapshot = core.reset(20261006, 0, 3, field.width, field.height);
  } catch {
    // The bridge preserves the previous match when the new field is invalid.
    changePhase(phase === 'playing' ? 'paused' : phase, '窗口空间不足，请调整窗口大小后重试。');
    return;
  }
  scene.reset();
  scene.draw(snapshot);
  resize();
  samples.length = 0;
  changePhase('playing');
}
function start() {
  if (phase === 'paused') changePhase('playing');
  else if (phase === 'menu' || phase === 'over') restart();
}
function pause(message = '战斗已暂停') {
  if (phase === 'playing') changePhase('paused', message);
  else if (phase === 'paused') start();
}
function processEvents(events: GameEvent[], now: number) {
  for (const event of events) {
    if (event.type === 2) {
      const messages = ['出生几何不安全，移动后重试', '能量不足，稍等恢复后再出招', '当前招式尚未结束', '没有存活目标', '没有出招请求'];
      reason = messages[event.reject] ?? '本次出招未被接受';
      reasonUntil = now + 2000;
    } else if (event.type === 1) {
      reason = '已锁定目标与弹道，预警后释放';
      reasonUntil = now + 1200;
    } else if (event.type === 14) {
      reason = `下一波 ${event.amount} 名学生即将出场，留意场内出生标记。`;
      reasonUntil = now + 2000;
    } else if (event.type === 15) {
      reason = `新一波 ${event.amount} 名学生已入场。`;
      reasonUntil = now + 1200;
    }
  }
}
function frame(now: number) {
  try {
    const begin = performance.now();
    const delta = lastTime ? now - lastTime : 0;
    lastTime = now;
    let coreMs = 0, decodeMs = 0, count = 0;
    if (phase === 'playing') {
      count = clock.advance(delta);
      if (clock.tooSlow) {
        changePhase('paused', '页面运行较慢，已暂停。继续后从当前位置开始。');
        count = 0;
      }
      const frameEvents: GameEvent[] = [];
      for (let i = 0; i < count; i++) {
        let t = performance.now();
        core.step(input.consume(snapshot?.actors[0]));
        coreMs += performance.now() - t;
        t = performance.now();
        snapshot = core.snapshot();
        decodeMs += performance.now() - t;
        frameEvents.push(...snapshot.events);
        if (snapshot.status !== 0) {
          changePhase('over');
          break;
        }
      }
      processEvents(frameEvents, now);
      if (snapshot) snapshot.events = frameEvents;
    }
    if (reasonUntil < now) reason = '';
    if (snapshot) scene.draw(snapshot);
    hud.render(snapshot, phase, reason);
    const renderStart = performance.now();
    scene.render();
    const renderMs = performance.now() - renderStart;
    if (phase === 'playing') {
      samples.push({ interval: delta, frame: performance.now() - begin, core: coreMs, decode: decodeMs, render: renderMs, ticks: count });
      if (samples.length > 600) samples.shift();
    }
    raf = requestAnimationFrame(frame);
  } catch (error) {
    changePhase('error', `运行错误：${error instanceof Error ? error.message : String(error)}`);
    loading.textContent = reason;
  }
}

async function boot() {
  [core, scene] = await Promise.all([loadCore(), GameScene.create(arena)]);
  input = new BrowserInput(scene.canvas, { pause: () => pause(), restart }, (x, y) => scene.screenToWorld(x, y));
  hud.setCallbacks({ start, pause: () => pause(), restart, attack: p => { if (phase === 'playing') input.requestAttack(p); } });
  snapshot = core.reset(20261006, 0, 3);
  loading.hidden = true;
  hud.render(snapshot, phase);
  scene.draw(snapshot);
  resize();
  raf = requestAnimationFrame(frame);
  // QA only reads the public display snapshot and timings. It cannot alter core state.
  if (new URLSearchParams(location.search).has('qa')) {
    Object.assign(window, { __demo: { inspect: () => ({ phase, snapshot, samples: [...samples], reason }) } });
  }
  document.addEventListener('visibilitychange', () => {
    if (document.hidden && phase === 'playing') changePhase('paused', '页面已隐藏，战斗自动暂停。请点击继续。');
  });
  window.addEventListener('blur', () => {
    if (phase === 'playing') changePhase('paused', '窗口失去焦点，战斗自动暂停。请点击继续。');
  });
  window.addEventListener('pagehide', (event: PageTransitionEvent) => {
    cancelAnimationFrame(raf);
    raf = 0;
    if (event.persisted) {
      if (phase === 'playing') changePhase('paused', '返回页面后，请点击继续对局。');
      input.clear();
    } else { input.dispose(); core.dispose(); hud.dispose(); scene.destroy(); }
  });
  window.addEventListener('pageshow', (event: PageTransitionEvent) => {
    if (event.persisted) {
      clock.reset(); lastTime = 0; input.clear();
      if (phase === 'playing') changePhase('paused', '返回页面后，请点击继续对局。');
      if (!raf) raf = requestAnimationFrame(frame);
    }
  });
}
boot().catch(error => {
  loading.textContent = `游戏加载失败：${error instanceof Error ? error.message : String(error)}。请重新打开完整的单文件 HTML；源码运行时请检查本地 HTTP 服务及 Wasm 资源。`;
  changePhase('error', loading.textContent);
});
