import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import vm from 'node:vm';
import ts from 'typescript';

// EventTarget fixture for the real input class. This verifies event ownership
// and packets, not gameplay rules or a rendered browser session.
const root = fileURLToPath(new URL('../../', import.meta.url));
const sourcePath = path.join(root, 'apps/web/src/input/browserInput.ts');
const source = readFileSync(sourcePath, 'utf8');
const compiled = ts.transpileModule(source, {
  compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.CommonJS },
}).outputText;
const checks = [];
let assertions = 0;
const eq = (actual, expected) => { assertions++; assert.equal(actual, expected); };
const near = (actual, expected) => { assertions++; assert.ok(Math.abs(actual - expected) < 1e-10, `${actual} != ${expected}`); };
const plain = value => JSON.parse(JSON.stringify(value));

function fixture({ legacy = false, noBossGetter = false } = {}) {
  const window = new EventTarget();
  const state = {
    playing: true, boss: { x: 100, y: 100 }, scale: 1,
    bounds: { left: 0, top: 0, width: 960, height: 720 }, pauses: 0, restarts: 0,
  };
  class HTMLElement extends EventTarget {
    tagName = 'CANVAS';
    isContentEditable = false;
    setAttribute() {}
    focus() {}
    getBoundingClientRect() { return state.bounds; }
  }
  const canvas = new HTMLElement();
  const exports = {};
  vm.runInNewContext(compiled, { exports, window, HTMLElement, Number, Math }, { filename: sourcePath });
  const callbacks = {
    pause() { state.pauses++; }, restart() { state.restarts++; }, isPlaying: () => state.playing,
    ...(noBossGetter ? {} : { getBoss: () => state.boss }),
  };
  const project = legacy ? undefined : (x, y) => ({
    x: (x - state.bounds.left) / state.scale,
    y: (y - state.bounds.top) / state.scale,
  });
  const input = new exports.BrowserInput(canvas, callbacks, project);
  function dispatch(target, type, fields = {}) {
    const event = Object.assign(new Event(type, { cancelable: true }), fields);
    target.dispatchEvent(event);
    return event;
  }
  const down = (code, fields = {}) => dispatch(window, 'keydown', { code, repeat: false, ...fields });
  const up = code => dispatch(window, 'keyup', { code });
  const pointer = (x, y) => dispatch(window, 'pointermove', { clientX: x, clientY: y });
  const tick = () => input.consume(state.boss);
  const selected = () => plain(input.getSelection());
  const noFire = () => { const packet = tick(); eq(packet.attacks, 0); eq(packet.aimValid, false); return packet; };
  const fire = (pattern, x = 0, y = -1) => {
    const packet = tick(); eq(packet.attacks, 1 << pattern); eq(packet.aimValid, true);
    near(packet.aimX, x); near(packet.aimY, y); return packet;
  };
  return { input, window, canvas, state, HTMLElement, dispatch, down, up, pointer, tick, selected, noFire, fire };
}

function test(name, run, options) {
  const f = fixture(options);
  const initialAssertions = assertions;
  try {
    run(f);
    checks.push({ name, pass: true, assertions: assertions - initialAssertions });
  } catch (error) {
    checks.push({ name, pass: false, assertions: assertions - initialAssertions, error: String(error.stack ?? error) });
  } finally {
    f.input.dispose();
  }
}

test('keydown immediately selects without firing', f => {
  f.down('Digit1'); eq(f.selected().pattern, 0); near(f.selected().dirY, -1); f.noFire();
});
test('instant key press and release emits one copied packet', f => {
  f.down('Digit4'); f.up('Digit4'); eq(f.selected(), null); f.fire(3); f.noFire();
});
test('holding through many ticks never autofires', f => {
  f.down('Digit2'); for (let i = 0; i < 60; i++) f.noFire(); eq(f.selected().pattern, 1); f.up('Digit2'); f.fire(1);
});
test('repeat events cannot replace a newer skill', f => {
  f.down('Digit1'); f.down('Digit2'); f.down('Digit1', { repeat: true });
  eq(f.selected().pattern, 1); f.up('Digit1'); f.noFire(); f.up('Digit2'); f.fire(1);
});
test('latest key selection wins and old keyup is ignored', f => {
  f.down('Digit1'); f.down('Digit3'); f.up('Digit1'); eq(f.selected().pattern, 2); f.noFire();
  f.up('Digit3'); f.fire(2);
});
test('Digit and Numpad have distinct owners', f => {
  f.down('Digit2'); f.down('Numpad2'); f.up('Digit2'); eq(f.selected().pattern, 1); f.noFire();
  f.up('Numpad2'); f.fire(1);
});
test('Space cancels held selection and suppresses its keyup', f => {
  f.down('Digit2'); const event = f.down('Space'); eq(event.defaultPrevented, true);
  eq(f.selected(), null); f.up('Digit2'); f.noFire();
});
test('Space cancels a release before the next tick', f => {
  f.down('Digit3'); f.up('Digit3'); f.down('Space'); f.noFire();
});
test('Space cancellation preserves WASD motion', f => {
  f.down('KeyW'); f.down('KeyD'); f.down('Digit1'); f.down('Space');
  const packet = f.noFire(); near(packet.moveX, Math.SQRT1_2); near(packet.moveY, -Math.SQRT1_2);
});
test('clear prevents delayed key and pointer release', f => {
  f.down('Digit1'); f.input.clear(); f.up('Digit1'); f.input.endSelection('pointer:1'); f.noFire();
});
test('Escape clears input before invoking pause', f => {
  f.down('KeyW'); f.down('Digit4'); f.down('Escape'); eq(f.state.pauses, 1); eq(f.selected(), null);
  f.up('Digit4'); eq(f.noFire().moveY, 0);
});
test('R clears current and released attacks before restart', f => {
  f.down('Digit1'); f.up('Digit1'); f.down('Digit4'); f.down('KeyR');
  eq(f.state.restarts, 1); eq(f.selected(), null); f.up('Digit4'); f.noFire();
});
test('window blur cancels held input and pending releases', f => {
  f.down('Digit1'); f.up('Digit1'); f.down('Digit4'); f.dispatch(f.window, 'blur');
  eq(f.selected(), null); f.up('Digit4'); f.noFire();
});
test('canvas pointercancel cancels its matching pointer owner', f => {
  f.pointer(250, 100); f.input.beginSelection(1, 'pointer:7');
  f.dispatch(f.canvas, 'pointercancel', { pointerId: 7 }); eq(f.selected(), null);
  f.input.endSelection('pointer:7'); eq(f.noFire().pointerValid, false);
});
test('old canvas pointercancel leaves new keyboard selection intact', f => {
  f.input.beginSelection(1, 'pointer:7'); f.down('Digit3');
  f.dispatch(f.canvas, 'pointercancel', { pointerId: 7 }); eq(f.selected().pattern, 2);
  f.input.endSelection('pointer:7'); f.noFire(); f.up('Digit3'); f.fire(2);
});
test('right click clears and invokes pause', f => {
  f.down('Digit2'); const event = f.dispatch(f.canvas, 'contextmenu'); eq(event.defaultPrevented, true);
  eq(f.state.pauses, 1); eq(f.selected(), null); f.up('Digit2'); f.noFire();
});
test('non-playing phase blocks keyboard and HUD selection', f => {
  f.state.playing = false; f.down('Digit1'); f.input.beginSelection(1, 'pointer:1');
  eq(f.selected(), null); f.up('Digit1'); f.input.endSelection('pointer:1'); f.noFire();
});
test('phase changes before release suppress firing', f => {
  f.down('Digit3'); f.state.playing = false; f.up('Digit3'); eq(f.selected(), null); f.noFire();
});
test('Boss crossing pointer never turns a held aim', f => {
  f.pointer(250, 100); f.down('Digit2'); f.state.boss.x = 300;
  for (let i = 0; i < 5; i++) { f.noFire(); near(f.selected().dirX, 1); near(f.selected().dirY, 0); }
  f.up('Digit2'); f.fire(1, 1, 0);
});
test('same client coordinates after Boss motion never turn aim', f => {
  f.pointer(250, 100); f.down('Digit1'); f.state.boss.x = 300; f.pointer(250, 100);
  near(f.selected().dirX, 1); f.up('Digit1'); f.fire(0, 1, 0);
});
test('resize reprojects movement target without turning aim', f => {
  f.pointer(250, 100); f.down('Digit3'); f.state.bounds.top = 30; f.state.scale = 2;
  const packet = f.noFire(); near(packet.pointerX, 125); near(packet.pointerY, 35);
  near(f.selected().dirX, 1); near(f.selected().dirY, 0); f.up('Digit3'); f.fire(2, 1, 0);
});
test('actual screen pointer change updates aim from latest Boss', f => {
  f.pointer(250, 100); f.down('Digit2'); f.state.boss = { x: 250, y: 150 }; f.pointer(250, 250);
  near(f.selected().dirX, 0); near(f.selected().dirY, 1); f.up('Digit2'); f.fire(1, 0, 1);
});
test('pointer movement inside twelve pixel dead zone preserves aim', f => {
  f.pointer(250, 100); f.down('Digit4'); f.pointer(88, 100); near(f.selected().dirX, 1);
  f.pointer(101, 100); near(f.selected().dirX, 1); f.up('Digit4'); f.fire(3, 1, 0);
});
test('keyboard direction is fallback without a pointer', f => {
  f.down('KeyD'); f.down('Digit1'); near(f.selected().dirX, 1); near(f.selected().dirY, 0);
  f.up('Digit1'); f.fire(0, 1, 0);
});
test('diagonal keyboard fallback is normalized', f => {
  f.down('KeyD'); f.down('KeyW'); f.down('Digit1'); f.up('Digit1'); f.fire(0, Math.SQRT1_2, -Math.SQRT1_2);
});
test('last nonzero keyboard direction survives keyup', f => {
  f.down('KeyA'); f.up('KeyA'); f.down('Digit4'); f.up('Digit4'); f.fire(3, -1, 0);
});
test('keyboard motion does not rotate an already held aim', f => {
  f.down('Digit4'); f.down('KeyD'); near(f.selected().dirX, 0); near(f.selected().dirY, -1);
  f.up('Digit4'); f.fire(3);
});
test('valid mouse facing takes precedence over keyboard fallback', f => {
  f.pointer(100, 250); f.down('KeyD'); f.down('Digit1'); f.up('Digit1'); f.fire(0, 0, 1);
});
test('getSelection returns a defensive copy', f => {
  f.down('Digit1'); const selected = f.input.getSelection(); selected.pattern = 3; selected.dirX = 999;
  eq(f.selected().pattern, 0); near(f.selected().dirX, 0); f.up('Digit1'); f.fire(0);
});
test('wrong owner release neither clears nor fires selection', f => {
  f.input.beginSelection(1, 'pointer:2'); f.input.endSelection('pointer:9'); eq(f.selected().pattern, 1);
  f.noFire(); f.input.endSelection('pointer:2'); f.fire(1);
});
test('HUD pointer begin and end produce one release edge', f => {
  f.pointer(250, 100); f.input.beginSelection(3, 'pointer:2'); f.noFire();
  f.input.endSelection('pointer:2'); f.fire(3, 1, 0); f.input.endSelection('pointer:2'); f.noFire();
});
test('old pointer capture loss cannot cancel a new keyboard owner', f => {
  f.input.beginSelection(0, 'pointer:2'); f.down('Digit3'); f.input.cancelSelection('pointer:2');
  eq(f.selected().pattern, 2); f.input.endSelection('pointer:2'); f.noFire(); f.up('Digit3'); f.fire(2);
});
test('old pointer cancellation preserves a new keyboard release packet', f => {
  f.input.beginSelection(0, 'pointer:2'); f.down('Digit3'); f.up('Digit3');
  f.input.cancelSelection('pointer:2'); f.fire(2);
});
test('matching owner cancellation clears only its pending packet', f => {
  f.input.beginSelection(0, 'pointer:2'); f.input.endSelection('pointer:2'); f.down('Digit3');
  f.input.cancelSelection('pointer:2'); eq(f.selected().pattern, 2); f.noFire(); f.up('Digit3'); f.fire(2);
});
test('cancelling current owner preserves another owner pending packet', f => {
  f.input.beginSelection(0, 'pointer:2'); f.input.endSelection('pointer:2'); f.down('Digit3');
  f.input.cancelSelection('key:Digit3'); eq(f.selected(), null); f.fire(0);
});
test('unscoped cancellation clears all owners and legacy edges', f => {
  f.input.beginSelection(0, 'pointer:2'); f.input.endSelection('pointer:2'); f.down('Digit3'); f.input.requestAttack(3);
  f.input.cancelSelection(); eq(f.selected(), null); f.up('Digit3'); f.noFire();
});
test('legacy requestAttack mask remains single-consumption compatible', f => {
  f.input.requestAttack(0); f.input.requestAttack(2); const packet = f.tick();
  eq(packet.attacks, 5); eq(packet.aimValid, false); f.noFire();
});
test('scoped cancel does not erase an unrelated legacy edge', f => {
  f.input.requestAttack(2); f.input.cancelSelection('pointer:2'); eq(f.tick().attacks, 4); f.noFire();
});
test('legacy projection respects resized canvas and clear stays invalid', f => {
  f.state.bounds = { left: 30, top: 70, width: 480, height: 360 }; f.pointer(270, 250);
  const first = f.tick(); near(first.pointerX, 480); near(first.pointerY, 360); eq(first.pointerValid, true);
  f.state.bounds.width = 960; f.state.bounds.height = 720; const resized = f.tick();
  near(resized.pointerX, 240); near(resized.pointerY, 180); f.input.clear(); eq(f.tick().pointerValid, false);
}, { legacy: true });
test('page pointerout retains observable pointer position and facing', f => {
  f.pointer(250, 100); f.down('Digit1');
  f.dispatch(f.window, 'pointerout', { clientX: 260, clientY: 100, relatedTarget: null });
  eq(f.noFire().pointerValid, true); near(f.selected().dirX, 1); f.up('Digit1'); f.fire(0, 1, 0);
});
test('keyboard overrides pointer movement without altering mouse aim', f => {
  f.pointer(250, 100); f.down('KeyW'); f.down('Digit1'); const packet = f.noFire();
  eq(packet.pointerValid, false); near(packet.moveY, -1); f.up('Digit1'); f.fire(0, 1, 0);
});
test('editable controls ignore skill and Space keydown', f => {
  const target = new f.HTMLElement(); target.tagName = 'INPUT';
  for (const code of ['Digit2', 'Space']) {
    const event = Object.assign(new Event('keydown', { cancelable: true }), { code, repeat: false });
    Object.defineProperty(event, 'target', { value: target }); f.window.dispatchEvent(event);
    eq(event.defaultPrevented, false);
  }
  eq(f.selected(), null); f.noFire();
});
test('Space prevents native button activation', f => {
  const target = new f.HTMLElement(); target.tagName = 'BUTTON';
  const event = Object.assign(new Event('keydown', { cancelable: true }), { code: 'Space', repeat: false });
  Object.defineProperty(event, 'target', { value: target }); f.window.dispatchEvent(event);
  eq(event.defaultPrevented, true); f.noFire();
});
test('invalid patterns and owners cannot replace a valid selection', f => {
  f.down('Digit2');
  for (const pattern of [-1, 4, 0.5, NaN, Infinity]) f.input.beginSelection(pattern, 'pointer:1');
  f.input.beginSelection(2, ''); f.input.beginSelection(2, null); eq(f.selected().pattern, 1);
  f.up('Digit2'); f.fire(1);
});
test('two taps before a tick do not queue or combine aimed attacks', f => {
  f.down('Digit1'); f.up('Digit1'); f.down('Digit3'); f.up('Digit3'); f.fire(0); f.noFire();
});
test('released packet retains direction when pointer subsequently moves', f => {
  f.pointer(250, 100); f.down('Digit1'); f.up('Digit1'); f.pointer(100, 250); f.fire(0, 1, 0);
});
test('Boss fallback supplied by consume supports old callbacks', f => {
  f.tick(); f.pointer(250, 100); f.down('Digit2'); f.up('Digit2'); f.fire(1, 1, 0);
}, { noBossGetter: true });
test('zero sized canvas invalidates pointer without cancelling keyboard aim', f => {
  f.pointer(250, 100); f.down('Digit2'); f.state.bounds.width = 0;
  eq(f.noFire().pointerValid, false); near(f.selected().dirX, 1); f.up('Digit2'); f.fire(1, 1, 0);
});
test('new hold rebases stationary pointer from the current Boss', f => {
  f.pointer(250, 100); f.state.boss = { x: 300, y: 100 }; f.tick();
  f.down('Digit1'); near(f.selected().dirX, -1); near(f.selected().dirY, 0);
  f.state.boss.x = 200; f.noFire(); near(f.selected().dirX, -1); f.up('Digit1'); f.fire(0, -1, 0);
});
test('replacement hold rebases facing but earlier hold remains stable', f => {
  f.pointer(250, 100); f.down('Digit1'); f.state.boss.x = 300; f.noFire(); near(f.selected().dirX, 1);
  f.down('Digit2'); near(f.selected().dirX, -1); f.up('Digit1'); f.noFire(); f.up('Digit2'); f.fire(1, -1, 0);
});
test('new hold projects stationary screen pointer through resized transform', f => {
  f.pointer(250, 100); f.state.bounds.top = 30; f.state.scale = 2;
  f.down('Digit3'); const distance = Math.hypot(25, -65);
  near(f.selected().dirX, 25 / distance); near(f.selected().dirY, -65 / distance);
  f.up('Digit3'); f.fire(2, 25 / distance, -65 / distance);
});
test('new hold inside dead zone preserves last valid mouse direction', f => {
  f.pointer(250, 100); f.state.boss.x = 245; f.down('Digit4');
  near(f.selected().dirX, 1); near(f.selected().dirY, 0); f.up('Digit4'); f.fire(3, 1, 0);
});
test('HUD new hold rebases from latest Boss without pointer events', f => {
  f.pointer(250, 100); f.state.boss = { x: 250, y: 200 }; f.input.beginSelection(1, 'pointer:2');
  near(f.selected().dirX, 0); near(f.selected().dirY, -1); f.input.endSelection('pointer:2'); f.fire(1);
});
test('legacy Boss fallback rebases a new hold from the latest consumed Boss', f => {
  f.tick(); f.pointer(250, 100); f.state.boss.x = 300; f.tick(); f.down('Digit2');
  near(f.selected().dirX, -1); f.up('Digit2'); f.fire(1, -1, 0);
}, { noBossGetter: true });

const report = {
  method: 'DOM EventTarget fixture runs transpiled current BrowserInput source; no core/world mutation, no rendered browser claims.',
  sourcePath, sourceSha256: createHash('sha256').update(source).digest('hex'),
  cases: checks.length, assertions, passed: checks.every(check => check.pass), checks,
};
const output = path.join(root, 'build/web-validation');
mkdirSync(output, { recursive: true });
writeFileSync(path.join(output, 'aim-input-probe-results.json'), `${JSON.stringify(report, null, 2)}\n`);
console.log(JSON.stringify({ cases: report.cases, assertions, passed: report.passed, failed: checks.filter(check => !check.pass) }, null, 2));
if (!report.passed) process.exitCode = 1;
