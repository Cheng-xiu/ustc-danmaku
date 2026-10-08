import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import vm from 'node:vm';
import ts from 'typescript';

// Run the real input adapter with EventTarget fixtures. Phone detection,
// pointer capture and browser layout are covered separately by browser checks.
const root = fileURLToPath(new URL('../../', import.meta.url));
const sourcePath = path.join(root, 'apps/web/src/input/browserInput.ts');
const source = readFileSync(sourcePath, 'utf8');
const compiled = ts.transpileModule(source, {
  compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.CommonJS },
}).outputText;
const checks = [];
let assertions = 0;
const equal = (actual, expected) => { assertions++; assert.equal(actual, expected); };
const near = (actual, expected) => {
  assertions++;
  assert.ok(Number.isFinite(actual) && Math.abs(actual - expected) < 1e-10, `${actual} != ${expected}`);
};

function fixture() {
  const window = new EventTarget();
  const state = { playing: true, boss: { x: 100, y: 100 }, projects: 0, focuses: 0, pauses: 0, restarts: 0 };
  class HTMLElement extends EventTarget {
    tagName = 'CANVAS';
    isContentEditable = false;
    setAttribute() {}
    focus() { state.focuses++; }
    getBoundingClientRect() { return { left: 0, top: 0, width: 960, height: 720 }; }
  }
  const canvas = new HTMLElement();
  const exports = {};
  vm.runInNewContext(compiled, { exports, window, HTMLElement, Number, Math }, { filename: sourcePath });
  const input = new exports.BrowserInput(canvas, {
    pause() { state.pauses++; },
    restart() { state.restarts++; },
    isPlaying: () => state.playing,
    getBoss: () => state.boss,
  }, (x, y) => { state.projects++; return { x, y }; });
  const dispatch = (target, type, fields = {}) => {
    const event = Object.assign(new Event(type, { cancelable: true }), fields);
    target.dispatchEvent(event);
    return event;
  };
  const down = code => dispatch(window, 'keydown', { code, repeat: false });
  const up = code => dispatch(window, 'keyup', { code });
  const pointer = (type, fields = {}) => dispatch(window, type, {
    pointerType: 'touch', pointerId: 2, clientX: 500, clientY: 500, relatedTarget: null, ...fields,
  });
  const tick = () => input.consume(state.boss);
  const selected = () => input.getSelection();
  const idle = () => { const p = tick(); equal(p.attacks, 0); equal(p.aimValid, false); return p; };
  const axes = (x, y) => { const p = idle(); near(p.moveX, x); near(p.moveY, y); equal(p.pointerValid, false); return p; };
  const aim = (x, y, pattern = 0) => {
    const s = selected(); equal(s?.pattern, pattern); near(s.dirX, x); near(s.dirY, y);
  };
  const fire = (pattern, x, y) => {
    const p = tick(); equal(p.attacks, 1 << pattern); equal(p.aimValid, true); near(p.aimX, x); near(p.aimY, y);
    idle();
  };
  return { input, state, window, canvas, dispatch, down, up, pointer, tick, selected, idle, axes, aim, fire };
}
function test(name, run) {
  const f = fixture();
  const start = assertions;
  try { run(f); checks.push({ name, pass: true, assertions: assertions - start }); }
  catch (error) { checks.push({ name, pass: false, assertions: assertions - start, error: String(error.stack ?? error) }); }
  finally { f.input.dispose(); }
}

test('touch mode starts stationary with upward default aim', f => {
  f.input.setTouchMode(true); f.axes(0, 0);
  f.input.beginSelection(0, 'pointer:2'); f.aim(0, -1);
  f.input.endSelection('pointer:2'); f.fire(0, 0, -1);
});
test('partial joystick deflection keeps analog movement magnitude', f => {
  f.input.setTouchMode(true); f.input.setJoystick(0.24, -0.32); f.axes(0.24, -0.32);
  f.input.beginSelection(1, 'pointer:2'); f.aim(0.6, -0.8, 1);
});
test('diagonal deflection clamps to unit speed without distorting direction', f => {
  f.input.setTouchMode(true); f.input.setJoystick(1, 1); f.axes(Math.SQRT1_2, Math.SQRT1_2);
  f.input.beginSelection(0, 'pointer:2'); f.aim(Math.SQRT1_2, Math.SQRT1_2);
});
test('out of range finite axes preserve ratio and clamp magnitude', f => {
  f.input.setTouchMode(true); f.input.setJoystick(-8, 6); f.axes(-0.8, 0.6);
  f.input.beginSelection(0, 'pointer:2'); f.aim(-0.8, 0.6);
});
test('enormous finite axes cannot overflow normalization', f => {
  f.input.setTouchMode(true); f.input.setJoystick(Number.MAX_VALUE, -Number.MAX_VALUE);
  f.axes(Math.SQRT1_2, -Math.SQRT1_2);
  f.input.beginSelection(0, 'pointer:2'); f.aim(Math.SQRT1_2, -Math.SQRT1_2);
});
test('invalid axes immediately stop and preserve the last valid facing', f => {
  f.input.setTouchMode(true); f.input.setJoystick(-0.5, 0); f.input.beginSelection(0, 'pointer:2');
  for (const pair of [[NaN, 0], [0, Infinity], [-Infinity, 0], [undefined, 1]]) {
    f.input.setJoystick(...pair); f.axes(0, 0); f.aim(-1, 0);
  }
  f.input.endSelection('pointer:2'); f.fire(0, -1, 0);
});
test('recentring stops at the next tick without rotating a held aim', f => {
  f.input.setTouchMode(true); f.input.setJoystick(0, 0.8); f.input.beginSelection(2, 'pointer:2');
  f.input.setJoystick(0, 0); f.axes(0, 0); f.aim(0, 1, 2);
  f.input.endSelection('pointer:2'); f.fire(2, 0, 1);
});
test('last nonzero direction survives recentering for the next selection', f => {
  f.input.setTouchMode(true); f.input.setJoystick(0.3, 0.4); f.input.setJoystick(0, 0);
  f.input.beginSelection(3, 'pointer:2'); f.aim(0.6, 0.8, 3);
});
test('real joystick axis changes update an existing preview', f => {
  f.input.setTouchMode(true); f.input.setJoystick(1, 0); f.input.beginSelection(1, 'pointer:2');
  f.input.setJoystick(0, -0.25); f.aim(0, -1, 1); f.axes(0, -0.25);
  f.input.endSelection('pointer:2'); f.fire(1, 0, -1);
});
test('Boss motion and repeated axes do not rebase mobile aiming', f => {
  f.input.setTouchMode(true); f.input.setJoystick(0.6, 0.8); f.input.beginSelection(0, 'pointer:2');
  for (let i = 0; i < 12; i++) {
    f.state.boss = { x: 100 + i * 100, y: 500 - i * 30 };
    f.input.setJoystick(0.6, 0.8); f.axes(0.6, 0.8); f.aim(0.6, 0.8);
  }
  equal(f.state.projects, 0);
});
test('every screen pointer type is ignored while touch mode is active', f => {
  f.input.setTouchMode(true); f.input.setJoystick(-1, 0); f.input.beginSelection(0, 'pointer:2');
  for (const pointerType of ['touch', 'pen', 'mouse', '']) {
    f.pointer('pointermove', { pointerType }); f.pointer('pointerout', { pointerType });
    f.dispatch(f.canvas, 'pointerdown', { pointerType, button: 0, clientX: 20, clientY: 20 });
    f.axes(-1, 0); f.aim(-1, 0);
  }
  equal(f.state.projects, 0); equal(f.state.focuses, 0);
});
test('right finger motion cannot move the Boss when the left stick is centred', f => {
  f.input.setTouchMode(true); f.input.beginSelection(2, 'pointer:42');
  for (const type of ['pointermove', 'pointerout']) {
    f.pointer(type, { pointerId: 42, clientX: 800, clientY: 600 }); f.axes(0, 0); f.aim(0, -1, 2);
  }
});
test('touch release is copied before later axis or screen pointer changes', f => {
  f.input.setTouchMode(true); f.input.setJoystick(1, 0); f.input.beginSelection(2, 'pointer:2');
  f.input.endSelection('pointer:2'); f.input.setJoystick(0, -1);
  f.pointer('pointermove', { pointerType: 'mouse' }); f.fire(2, 1, 0);
});
test('holding and releasing mobile skill remains one edge without auto fire', f => {
  f.input.setTouchMode(true); f.input.setJoystick(-1, 0); f.input.beginSelection(3, 'pointer:2');
  for (let i = 0; i < 60; i++) f.idle();
  f.input.endSelection('pointer:2'); f.input.endSelection('pointer:2'); f.fire(3, -1, 0);
});
test('mobile latest owner wins and old pointer release is ignored', f => {
  f.input.setTouchMode(true); f.input.setJoystick(1, 0);
  f.input.beginSelection(0, 'pointer:2'); f.input.beginSelection(2, 'pointer:3');
  f.input.endSelection('pointer:2'); f.aim(1, 0, 2); f.idle();
  f.input.endSelection('pointer:3'); f.fire(2, 1, 0);
});
test('old pointer cancellation preserves a replaced mobile owner', f => {
  f.input.setTouchMode(true); f.input.beginSelection(0, 'pointer:2'); f.input.beginSelection(1, 'pointer:3');
  f.input.cancelSelection('pointer:2'); f.aim(0, -1, 1);
  f.input.endSelection('pointer:3'); f.fire(1, 0, -1);
});
test('matching pointer cancellation cancels its mobile release packet', f => {
  f.input.setTouchMode(true); f.input.beginSelection(1, 'pointer:3'); f.input.endSelection('pointer:3');
  f.input.cancelSelection('pointer:3'); f.idle();
});
test('mobile cancel preserves movement while discarding held or pending skills', f => {
  f.input.setTouchMode(true); f.input.setJoystick(0.4, 0);
  f.input.beginSelection(0, 'pointer:2'); f.input.endSelection('pointer:2');
  f.input.beginSelection(3, 'pointer:3'); f.input.cancelSelection(); equal(f.selected(), null);
  f.input.endSelection('pointer:3'); f.axes(0.4, 0);
});
test('Space cancels mobile aim and retains analog movement', f => {
  f.input.setTouchMode(true); f.input.setJoystick(0, -0.75);
  f.input.beginSelection(3, 'pointer:2'); f.down('Space'); equal(f.selected(), null);
  f.input.endSelection('pointer:2'); f.axes(0, -0.75);
});
test('keyboard movement temporarily overrides joystick and releases back to analog', f => {
  f.input.setTouchMode(true); f.input.setJoystick(-0.4, 0);
  f.down('KeyD'); f.down('KeyW'); f.axes(Math.SQRT1_2, -Math.SQRT1_2);
  f.up('KeyW'); f.axes(1, 0); f.up('KeyD'); f.axes(-0.4, 0);
});
test('currently held keyboard direction supplies new aim before joystick fallback', f => {
  f.input.setTouchMode(true); f.input.setJoystick(-1, 0); f.down('KeyD'); f.down('KeyW');
  f.input.beginSelection(1, 'pointer:2'); f.aim(Math.SQRT1_2, -Math.SQRT1_2, 1);
  f.input.endSelection('pointer:2'); f.fire(1, Math.SQRT1_2, -Math.SQRT1_2);
});
test('keyboard remains a remembered facing fallback when no joystick direction exists', f => {
  f.input.setTouchMode(true); f.down('KeyS'); f.up('KeyS');
  f.down('Digit4'); f.aim(0, 1, 3); f.up('Digit4'); f.fire(3, 0, 1);
});
test('keyboard movement does not rotate an existing touch aim by itself', f => {
  f.input.setTouchMode(true); f.input.setJoystick(-1, 0); f.input.beginSelection(0, 'pointer:2');
  f.down('KeyS'); f.axes(0, 1); f.aim(-1, 0); f.up('KeyS'); f.aim(-1, 0);
});
test('switching into touch mode clears held keys, mouse and released skill edges', f => {
  f.pointer('pointermove', { pointerType: 'mouse', clientX: 500, clientY: 100 });
  f.down('KeyD'); f.down('Digit2'); f.up('Digit2'); f.input.beginSelection(3, 'pointer:3');
  f.input.requestAttack(0); f.input.setTouchMode(true); equal(f.selected(), null); f.axes(0, 0);
  f.up('Digit2'); f.input.endSelection('pointer:3'); f.axes(0, 0);
  f.input.beginSelection(0, 'pointer:4'); f.aim(0, -1);
});
test('repeated touch mode assignment does not interrupt a gesture', f => {
  f.input.setTouchMode(true); f.input.setJoystick(0.5, 0); f.input.beginSelection(0, 'pointer:2');
  f.input.setTouchMode(true); f.axes(0.5, 0); f.aim(1, 0);
});
test('switching back to mouse cancels mobile state and requires a fresh mouse event', f => {
  f.input.setTouchMode(true); f.input.setJoystick(1, 0); f.input.beginSelection(0, 'pointer:2');
  f.input.endSelection('pointer:2'); f.input.setTouchMode(false);
  const cleared = f.idle(); near(cleared.moveX, 0); near(cleared.moveY, 0); equal(cleared.pointerValid, false);
  f.input.beginSelection(0, 'pointer:3'); f.aim(0, -1); f.input.cancelSelection();
  f.pointer('pointermove', { pointerType: 'mouse', clientX: 500, clientY: 100 });
  equal(f.idle().pointerValid, true);
  f.input.beginSelection(0, 'pointer:3'); f.aim(1, 0);
});
test('clear resets joystick and facing while preserving mobile mode', f => {
  f.input.setTouchMode(true); f.input.setJoystick(-1, 0); f.input.beginSelection(0, 'pointer:2');
  f.input.endSelection('pointer:2'); f.input.clear(); f.axes(0, 0);
  f.input.beginSelection(3, 'pointer:2'); f.aim(0, -1, 3); f.input.cancelSelection();
  f.input.setJoystick(0, 0.3); f.axes(0, 0.3);
});
test('window blur clears mobile movement, preview and delayed release', f => {
  f.input.setTouchMode(true); f.input.setJoystick(0, 0.8); f.input.beginSelection(0, 'pointer:2');
  f.input.endSelection('pointer:2'); f.input.beginSelection(1, 'pointer:3'); f.dispatch(f.window, 'blur');
  equal(f.selected(), null); f.input.endSelection('pointer:3'); f.axes(0, 0);
  f.input.beginSelection(3, 'pointer:4'); f.aim(0, -1, 3);
});
test('restart clears joystick and pending releases before callback', f => {
  f.input.setTouchMode(true); f.input.setJoystick(0.3, 0.4); f.input.beginSelection(0, 'pointer:2');
  f.input.endSelection('pointer:2'); f.down('KeyR');
  equal(f.state.restarts, 1); equal(f.selected(), null); f.axes(0, 0);
  f.input.beginSelection(0, 'pointer:3'); f.aim(0, -1);
});
test('pause clears mobile input before invoking the existing pause callback', f => {
  f.input.setTouchMode(true); f.input.setJoystick(0.3, 0.4); f.input.beginSelection(0, 'pointer:2');
  f.down('Escape'); equal(f.state.pauses, 1); equal(f.selected(), null);
  f.input.endSelection('pointer:2'); f.axes(0, 0);
});
test('nonplaying phase prevents mobile selection and later release', f => {
  f.input.setTouchMode(true); f.state.playing = false; f.input.beginSelection(0, 'pointer:2');
  equal(f.selected(), null); f.input.endSelection('pointer:2'); f.idle();
  f.state.playing = true; f.input.beginSelection(0, 'pointer:2');
  f.state.playing = false; f.input.endSelection('pointer:2'); f.idle();
});
test('desktop ignores touch and pen contacts without losing the real mouse target', f => {
  f.pointer('pointermove', { pointerType: 'mouse', clientX: 500, clientY: 100 });
  const projects = f.state.projects;
  for (const pointerType of ['touch', 'pen']) {
    f.pointer('pointermove', { pointerType, clientX: 100, clientY: 600 });
    f.pointer('pointerout', { pointerType, clientX: 100, clientY: 600 });
    f.dispatch(f.canvas, 'pointerdown', { pointerType, button: 0, clientX: 100, clientY: 600 });
  }
  equal(f.state.projects, projects); equal(f.state.focuses, 0);
  f.input.beginSelection(0, 'pointer:7'); f.aim(1, 0);
  const p = f.idle(); equal(p.pointerValid, true); near(p.pointerX, 500); near(p.pointerY, 100);
});
test('desktop accepts empty synthetic pointerType and ignores inactive joystick updates', f => {
  f.input.setJoystick(1, 0); f.pointer('pointermove', { pointerType: '', clientX: 100, clientY: 400 });
  f.input.beginSelection(1, 'pointer:2'); f.aim(0, 1, 1);
  f.input.endSelection('pointer:2'); f.fire(1, 0, 1);
  f.input.setTouchMode(true); f.axes(0, 0);
});
test('getSelection stays defensive for mobile previews', f => {
  f.input.setTouchMode(true); f.input.setJoystick(0, 1); f.input.beginSelection(2, 'pointer:2');
  const s = f.selected(); s.dirX = 9; s.dirY = -9; s.pattern = 3; f.aim(0, 1, 2);
});
test('dispose detaches pointer and keyboard listeners and clears axes', f => {
  f.input.setTouchMode(true); f.input.setJoystick(1, 0); f.input.dispose();
  f.down('KeyD'); f.down('Digit1'); f.pointer('pointermove', { pointerType: 'mouse' });
  equal(f.selected(), null); f.axes(0, 0);
});

const report = {
  method: 'EventTarget fixture executes current BrowserInput TypeScript; validates axis/aim/edge ownership and device isolation, without changing or mocking game rules.',
  sourcePath,
  sourceSha256: createHash('sha256').update(source).digest('hex'),
  cases: checks.length, assertions, passed: checks.every(check => check.pass), checks,
};
const output = path.join(root, 'build/mobile');
mkdirSync(output, { recursive: true });
writeFileSync(path.join(output, 'mobile-input-probe-results.json'), `${JSON.stringify(report, null, 2)}\n`);
console.log(JSON.stringify({ cases: report.cases, assertions, passed: report.passed, failed: checks.filter(check => !check.pass) }, null, 2));
if (!report.passed) process.exitCode = 1;
