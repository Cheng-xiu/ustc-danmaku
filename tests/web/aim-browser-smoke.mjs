import { chromium } from '@playwright/test';
import { createHash } from 'node:crypto';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath, pathToFileURL } from 'node:url';
import path from 'node:path';

const root = fileURLToPath(new URL('../../', import.meta.url));
const output = path.join(root, 'build/web-validation');
mkdirSync(output, { recursive: true });
const htmlPath = path.resolve(process.argv[2] ?? path.join(root, 'demo/ustc-danmaku.html'));
const url = pathToFileURL(htmlPath);
url.searchParams.set('qa', '1');
const started = performance.now(), deadline = started + 120000;
const report = {
  method: 'Actual offline single HTML and C/Wasm; real keyboard and captured mouse events; clock installed and paused before navigation. Only public inspect() snapshots are read. Lifecycle/capture-loss events are labelled when synthetic. No world/core state, RNG, positions, energy or plans are injected.',
  htmlPath, htmlSha256: createHash('sha256').update(readFileSync(htmlPath)).digest('hex'),
  expectedConfigVersion: 6, expectedAbiVersion: 5,
  checks: [], attacks: [], viewports: [], syntheticEvents: [], errors: [], externalRequests: [], passed: false,
};
let browser;
function check(name, condition, detail) {
  report.checks.push({ name, pass: Boolean(condition), ...(detail === undefined ? {} : { detail }) });
  if (!condition) throw Error(`${name}: ${JSON.stringify(detail)}`);
}
function budget() { if (performance.now() > deadline) throw Error('Aim browser smoke exceeded 120-second wall-time budget.'); }
const near = (a, b, tolerance = .001) => Number.isFinite(a) && Number.isFinite(b) && Math.abs(a - b) <= tolerance;
const sameDirection = (a, b) => a && b && near(a.dirX, b.dirX, 1e-6) && near(a.dirY, b.dirY, 1e-6);

try {
  check('single HTML has no external script or stylesheet references', !/<script[^>]*\bsrc=|<link[^>]*\bhref=/i.test(readFileSync(htmlPath, 'utf8')));
  browser = await chromium.launch({ channel: 'chrome', headless: true });
  report.browser = await browser.version();
  for (const size of [{ width: 1440, height: 900 }, { width: 1024, height: 768 }, { width: 390, height: 844 }]) {
    budget();
    const context = await browser.newContext({ viewport: size, offline: true });
    let closing = false;
    try {
      const page = await context.newPage();
      page.on('pageerror', error => report.errors.push({ viewport: size.width, error: error.message,
        stack: error.stack, duringContextClose: closing }));
      page.on('request', request => { if (/^https?:/.test(request.url())) report.externalRequests.push(request.url()); });
      const time = new Date('2026-10-07T10:00:00+08:00');
      await page.clock.install({ time });
      await page.clock.pauseAt(new Date(time.getTime() + 1000));
      await page.goto(url.href);
      await page.waitForFunction(() => window.__USTC_DEMO_QA__ || window.__demo, null, { timeout: 30000, polling: 50 });
      const inspect = () => page.evaluate(() => (window.__USTC_DEMO_QA__ ?? window.__demo).inspect());
      const prefix = `${size.width}x${size.height}`;
      const c = (name, condition, detail) => check(`${prefix}: ${name}`, condition, detail);
      const held = new Set();
      let mouseDown = false;
      async function down(key) { await page.keyboard.down(key); held.add(key); }
      async function up(key) { await page.keyboard.up(key); held.delete(key); }
      async function releaseAll() {
        if (mouseDown) { await page.mouse.up(); mouseDown = false; }
        for (const key of held) await page.keyboard.up(key);
        held.clear();
      }
      function playing(state, action) {
        if (state.phase !== 'playing') throw Error(`${prefix}: ${action}: phase=${state.phase}, tick=${state.snapshot.tick}, hp=${state.snapshot.actors[0].hp}, reason=${state.reason}`);
      }
      async function ticks(count = 1) {
        const before = await inspect(), target = before.snapshot.tick + count;
        let state = before, frames = 0;
        while (state.snapshot.tick < target) {
          budget(); playing(state, 'controlled advance');
          if (frames++ > count * 3 + 6) throw Error('Logical tick did not advance under RAF clock.');
          // At most one RAF per call; a fresh phase's baseline frame may take
          // no logical step. Observe actual ticks instead of assuming wall time.
          await page.clock.runFor(16);
          state = await inspect();
          if (state.snapshot.tick > target) throw Error(`Clock overshot logical boundary ${target} -> ${state.snapshot.tick}`);
        }
        return state;
      }
      async function restart() {
        await releaseAll(); await page.keyboard.press('r');
        const state = await ticks(1); playing(state, 'restart');
        c('restart clears selection and public plan counters', !state.aim && !state.preview && state.snapshot.accepted === 0 && state.snapshot.rejected === 0);
        return state;
      }
      async function screenPoint(x, y, snapshot) {
        return page.locator('#arena canvas').evaluate((node, data) => {
          const b = node.getBoundingClientRect();
          const screenW = Math.floor(b.width), screenH = Math.floor(b.height);
          const scale = Math.min(screenW / data.w, screenH / data.h);
          return {
            x: b.left + ((screenW - data.w * scale) / 2 + data.x * scale) * b.width / screenW,
            y: b.top + ((screenH - data.h * scale) / 2 + data.y * scale) * b.height / screenH,
          };
        }, { x, y, w: snapshot.fieldW, h: snapshot.fieldH });
      }
      async function pointerAt(x, y, snapshot) {
        const point = await screenPoint(x, y, snapshot); await page.mouse.move(point.x, point.y); return point;
      }
      async function setFacing(dx, dy) {
        const state = await inspect(), s = state.snapshot, b = s.actors[0];
        await pointerAt(b.x + dx * 90, b.y + dy * 90, s);
        // A real move back into the dead zone preserves the last valid facing
        // while removing the mouse movement target. No world values are written.
        await pointerAt(b.x, b.y, s);
      }
      async function heldPreview(pattern, ticksHeld = 12) {
        await setFacing(.6, -.8);
        const before = await inspect(); await down(String(pattern + 1));
        const immediate = await inspect();
        c(`skill ${pattern + 1} immediately selects without a minimum hold`, immediate.aim?.pattern === pattern && immediate.snapshot.accepted === before.snapshot.accepted);
        const state = await ticks(ticksHeld);
        c(`skill ${pattern + 1} hold never charges or locks a plan`, state.snapshot.accepted === before.snapshot.accepted
          && state.snapshot.rejected === before.snapshot.rejected && state.snapshot.attackState === 0
          && state.snapshot.energy >= before.snapshot.energy && state.snapshot.warnings.length === 0,
        { beforeEnergy: before.snapshot.energy, afterEnergy: state.snapshot.energy, tick: state.snapshot.tick });
        c(`skill ${pattern + 1} displays C candidate rays and current origin`, state.aim?.pattern === pattern && state.preview?.pattern === pattern
          && state.preview.rays.length > 0 && near(state.preview.originX, state.snapshot.actors[0].x)
          && near(state.preview.originY, state.snapshot.actors[0].y) && sameDirection(state.aim, state.preview),
        { aim: state.aim, ready: state.preview?.valid, rays: state.preview?.rays.length });
        return state;
      }
      function compareRays(preview, accepted) {
        const candidates = preview.rays, rays = accepted.warnings;
        let maxError = 0;
        const tickShift = accepted.startTick - preview.tick;
        const equal = candidates.length === rays.length && candidates.every((candidate, index) => {
          const ray = rays[index];
          for (const field of ['x', 'y', 'vx', 'vy', 'radius']) maxError = Math.max(maxError, Math.abs(candidate[field] - ray[field]));
          return ['x', 'y', 'vx', 'vy', 'radius'].every(field => near(candidate[field], ray[field]))
            && candidate.wave === ray.wave && candidate.pattern === ray.pattern && ray.spawnTick === candidate.spawnTick + tickShift;
        });
        return { equal, count: candidates.length, actualCount: rays.length, maxError, tickShift };
      }
      async function verifyRelease(pattern, before) {
        c(`skill ${pattern + 1} candidate is ready before release`, before.preview?.valid === true, { ready: before.preview?.valid, reason: before.preview?.reason, energy: before.snapshot.energy });
        await up(String(pattern + 1));
        const edge = await inspect();
        c(`skill ${pattern + 1} keyup clears preview selection without early cost`, edge.aim === null
          && edge.snapshot.accepted === before.snapshot.accepted && edge.snapshot.energy === before.snapshot.energy);
        const state = await ticks(1), s = state.snapshot;
        c(`skill ${pattern + 1} release accepts exactly once and charges its C cost`, s.accepted === before.snapshot.accepted + 1
          && s.pattern === pattern && s.attackState === 1 && s.manualAim === true
          && s.energy >= before.snapshot.energy - s.costs[pattern] && s.energy <= before.snapshot.energy - s.costs[pattern] + 1
          && near(s.aimDirX, before.aim.dirX) && near(s.aimDirY, before.aim.dirY),
        { before: before.snapshot.energy, after: s.energy, cost: s.costs[pattern], aimDirX: s.aimDirX, aimDirY: s.aimDirY });
        const comparison = compareRays(before.preview, s);
        c(`skill ${pattern + 1} released warning rays exactly match the C candidate`, comparison.equal, comparison);
        c(`skill ${pattern + 1} removes pre-aim after release`, !state.aim && !state.preview);
        const firstSpawn = Math.min(...s.warnings.map(ray => ray.spawnTick));
        const first = await ticks(firstSpawn + 1 - s.tick);
        const bullets = first.snapshot.bullets.filter(bullet => bullet.faction === 1 && bullet.pattern === pattern);
        const firstRays = s.warnings.filter(ray => ray.spawnTick === firstSpawn);
        const matching = bullets.some(bullet => firstRays.some(ray => near(bullet.vx, ray.vx) && near(bullet.vy, ray.vy)
          && near(bullet.radius, ray.radius) && near(bullet.x, ray.x + ray.vx / 60, .01) && near(bullet.y, ray.y + ray.vy / 60, .01)));
        c(`skill ${pattern + 1} actual first emission follows the locked rays`, first.snapshot.bossBullets > 0 && matching,
          { firstSpawn, observedTick: first.snapshot.tick, emitted: first.snapshot.bossBullets, live: bullets.length });
        c(`skill ${pattern + 1} release is consumed once`, first.snapshot.accepted === s.accepted);
        report.attacks.push({ viewport: size.width, pattern, acceptedTick: s.startTick, cost: s.costs[pattern], comparison,
          firstSpawn, firstObservedTick: first.snapshot.tick, bullets: bullets.length });
        return first;
      }
      async function regainEnergy() {
        let state = await inspect(); let leg = 0; let moves = new Set();
        while (state.snapshot.energy < 100) {
          budget(); playing(state, 'natural energy regeneration');
          const s = state.snapshot, b = s.actors[0];
          const route = [[.82, .78], [.82, .18], [.18, .18], [.18, .78]];
          let target = { x: route[leg][0] * s.fieldW, y: route[leg][1] * s.fieldH };
          if (Math.hypot(target.x - b.x, target.y - b.y) < 38) {
            leg = (leg + 1) % route.length; target = { x: route[leg][0] * s.fieldW, y: route[leg][1] * s.fieldH };
          }
          const next = new Set();
          if (target.x - b.x > 15) next.add('d'); if (target.x - b.x < -15) next.add('a');
          if (target.y - b.y > 15) next.add('s'); if (target.y - b.y < -15) next.add('w');
          for (const key of moves) if (!next.has(key)) await up(key);
          for (const key of next) if (!moves.has(key)) await down(key);
          moves = next; state = await ticks(6);
        }
        for (const key of moves) await up(key);
        c('fourth skill reaches full energy through legal movement and time', state.snapshot.energy === 100 && state.phase === 'playing', { tick: state.snapshot.tick, hp: state.snapshot.actors[0].hp });
      }

      const menu = await inspect();
      c('current cfg6 and release-aware public interface load', menu.snapshot.configVersion === 6 && 'aim' in menu && 'preview' in menu, { config: menu.snapshot.configVersion });
      await page.locator('[data-overlay="action"]').click(); await ticks(1);
      const layout = await page.locator('#arena').boundingBox();
      const initial = await inspect();
      c('new round field matches the available window aspect', near(initial.snapshot.fieldW / initial.snapshot.fieldH, layout.width / layout.height, .002));
      report.viewports.push({ ...size, fieldW: initial.snapshot.fieldW, fieldH: initial.snapshot.fieldH });

      await restart(); await setFacing(.6, -.8); await down('3'); await up('3');
      const instantEdge = await inspect();
      c('a tap shorter than one tick already queues a release without charging', !instantEdge.aim && instantEdge.snapshot.accepted === 0 && instantEdge.snapshot.energy === 60);
      const instant = await ticks(1);
      c('instant tap fires once without a minimum hold threshold', instant.snapshot.accepted === 1 && instant.snapshot.pattern === 2
        && instant.snapshot.manualAim === true && instant.snapshot.energy >= 40 && instant.snapshot.energy <= 41);
      await ticks(2); c('instant tap never repeats', (await inspect()).snapshot.accepted === 1);

      for (let pattern = 0; pattern < 4; pattern++) {
        await restart();
        if (pattern === 3) {
          let insufficient = await heldPreview(pattern, 2);
          c('fourth pre-aim reports insufficient energy without hiding its rays', insufficient.preview.valid === false && insufficient.preview.reason === 1 && insufficient.preview.rays.length > 0);
          await page.keyboard.press('Space'); await up('4'); await ticks(1);
          c('cancelled low-energy preview never submits a rejected request', (await inspect()).snapshot.rejected === 0);
          await regainEnergy();
        }
        const candidate = await heldPreview(pattern);
        if (pattern === 1) await page.screenshot({ path: path.join(output, `v6-aim-${size.width}.png`) });
        await verifyRelease(pattern, candidate);
      }

      await restart(); await setFacing(1, 0); await down('1'); await down('w');
      const movingBefore = await inspect(); await ticks(2); await page.keyboard.press('Space'); await up('1');
      const cancelled = await ticks(2);
      c('Space cancels aiming while WASD continues moving', !cancelled.aim && !cancelled.preview && cancelled.snapshot.accepted === 0
        && cancelled.snapshot.rejected === 0 && cancelled.snapshot.actors[0].y < movingBefore.snapshot.actors[0].y,
      { before: movingBefore.snapshot.actors[0], after: cancelled.snapshot.actors[0] });
      await up('w');

      await restart(); await setFacing(1, 0); await down('1'); await down('2'); await up('1');
      const newer = await ticks(1);
      c('newest key owns aiming and old keyup cannot fire', newer.aim?.pattern === 1 && newer.snapshot.accepted === 0);
      await verifyRelease(1, newer);

      await restart();
      const skill = page.locator('.demo-skill').nth(1), box = await skill.boundingBox();
      await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2); await page.mouse.down(); mouseDown = true;
      const pressed = await ticks(1);
      c('HUD pointerdown selects without firing', pressed.aim?.pattern === 1 && pressed.snapshot.accepted === 0
        && await skill.evaluate(node => node.hasPointerCapture(1)));
      await setFacing(.6, -.8); const dragged = await ticks(1);
      c('dragging out of HUD retains captured pre-aim', dragged.aim?.pattern === 1 && dragged.preview?.valid === true
        && await skill.evaluate(node => node.hasPointerCapture(1)));
      await page.mouse.up(); mouseDown = false; const hudReleased = await ticks(1);
      c('HUD release outside its button fires exactly once', hudReleased.snapshot.accepted === 1 && hudReleased.snapshot.pattern === 1
        && hudReleased.snapshot.manualAim === true && !hudReleased.aim && compareRays(dragged.preview, hudReleased.snapshot).equal);
      await ticks(2); c('lost capture after HUD release cannot queue another shot', (await inspect()).snapshot.accepted === 1);

      await restart(); const cancelBox = await skill.boundingBox();
      await page.mouse.move(cancelBox.x + cancelBox.width / 2, cancelBox.y + cancelBox.height / 2);
      await page.mouse.down(); mouseDown = true; await ticks(1); await page.keyboard.press('Space');
      const pointerCancelled = await ticks(1);
      c('Space cancels held HUD aim and clears pointer capture', !pointerCancelled.aim && !pointerCancelled.preview
        && pointerCancelled.snapshot.accepted === 0 && !await skill.evaluate(node => node.hasPointerCapture(1)));
      await page.mouse.up(); mouseDown = false; await ticks(1);
      c('mouseup after Space cancellation cannot fire', (await inspect()).snapshot.accepted === 0 && (await inspect()).snapshot.rejected === 0);

      await restart(); const oldBox = await page.locator('.demo-skill').nth(0).boundingBox();
      await page.mouse.move(oldBox.x + oldBox.width / 2, oldBox.y + oldBox.height / 2); await page.mouse.down(); mouseDown = true;
      await down('2'); await ticks(1);
      report.syntheticEvents.push({ viewport: size.width, event: 'releasePointerCapture', purpose: 'Exercise actual Chrome lostpointercapture after a keyboard owner replaces a real held mouse button; UI capture only, no core mutation.' });
      await page.locator('.demo-skill').nth(0).evaluate(node => node.releasePointerCapture(1));
      await page.mouse.up(); mouseDown = false; const captureLost = await ticks(1);
      c('old pointer capture loss and mouseup preserve a newer keyboard aim', captureLost.aim?.pattern === 1 && captureLost.snapshot.accepted === 0);
      await page.keyboard.press('Space'); await up('2'); await ticks(1);

      await restart(); const rebaseBefore = await inspect(), rebaseBoss = rebaseBefore.snapshot.actors[0];
      await pointerAt(rebaseBoss.x + 90, rebaseBoss.y, rebaseBefore.snapshot); await down('w');
      const walked = await ticks(12), walkedBoss = walked.snapshot.actors[0];
      await down('1'); const rebased = await inspect();
      const dx = rebaseBoss.x + 90 - walkedBoss.x, dy = rebaseBoss.y - walkedBoss.y, length = Math.hypot(dx, dy);
      c('new hold rebases a stationary cursor from the latest moved Boss', walkedBoss.y < rebaseBoss.y
        && near(rebased.aim.dirX, dx / length, .00001) && near(rebased.aim.dirY, dy / length, .00001),
      { beforeBoss: rebaseBoss, afterBoss: walkedBoss, aim: rebased.aim, expected: { dirX: dx / length, dirY: dy / length } });
      const walkedHolding = await ticks(6);
      c('subsequent Boss movement keeps the rebased held direction fixed', walkedHolding.snapshot.actors[0].y < walkedBoss.y
        && sameDirection(walkedHolding.aim, rebased.aim));
      await up('w'); await page.keyboard.press('Space'); await up('1'); await ticks(1);

      await restart(); await setFacing(1, 0); const stationary = await inspect(), b = stationary.snapshot.actors[0];
      await pointerAt(b.x + 30, b.y, stationary.snapshot); await down('1'); const originalAim = (await inspect()).aim;
      await down('d'); const crossed = await ticks(18);
      c('Boss can cross a stationary pointer without aim rotating or flipping', crossed.snapshot.actors[0].x > b.x + 30
        && sameDirection(crossed.aim, originalAim), { before: b.x, after: crossed.snapshot.actors[0].x, aim: crossed.aim });
      const cb = crossed.snapshot.actors[0]; await pointerAt(cb.x, cb.y - 80, crossed.snapshot);
      const changed = await inspect();
      c('a real mouse move changes held aim using current Boss origin', near(changed.aim.dirX, 0, .00001) && near(changed.aim.dirY, -1, .00001), changed.aim);
      await up('d'); await pointerAt(cb.x, cb.y, crossed.snapshot); await ticks(1);
      const beforeResize = await inspect();
      await page.setViewportSize({ width: size.width + 120, height: size.height - 40 });
      const resized = await ticks(1);
      c('mid-round resize preserves held direction and the round field', sameDirection(resized.aim, beforeResize.aim)
        && resized.snapshot.fieldW === beforeResize.snapshot.fieldW && resized.snapshot.fieldH === beforeResize.snapshot.fieldH,
      { before: beforeResize.aim, after: resized.aim });
      await page.keyboard.press('Space'); await up('1'); await ticks(1);
      await page.setViewportSize(size); await restart();

      await setFacing(1, 0); await down('1'); await ticks(1); await page.keyboard.press('Escape');
      const paused = await inspect(); await up('1'); await page.clock.runFor(300);
      const frozen = await inspect();
      c('pause cancels aim and freezes ticks without delayed release', frozen.phase === 'paused' && !frozen.aim && !frozen.preview
        && frozen.snapshot.tick === paused.snapshot.tick && frozen.snapshot.accepted === 0);
      await page.locator('[data-overlay="action"]').click(); const resumed = await ticks(2);
      c('resume cannot fire a key released during pause', resumed.snapshot.accepted === 0 && resumed.snapshot.rejected === 0 && !resumed.aim);

      await restart(); await down('2'); await ticks(1);
      report.syntheticEvents.push({ viewport: size.width, event: 'blur', purpose: 'Headless loss-of-focus handler check; no game state mutation.' });
      await page.evaluate(() => window.dispatchEvent(new Event('blur'))); await up('2');
      const blurred = await inspect();
      c('blur clears held selection and automatically pauses', blurred.phase === 'paused' && !blurred.aim && !blurred.preview);
      await page.locator('[data-overlay="action"]').click(); const unblurred = await ticks(2);
      c('blur resume never submits a late keyup', unblurred.snapshot.accepted === 0 && unblurred.snapshot.rejected === 0);

      await restart(); await down('2'); await ticks(1); await page.keyboard.press('r'); await up('2');
      const restarted = await ticks(2);
      c('restart cancels held keyboard owner and prevents late keyup', restarted.snapshot.accepted === 0 && restarted.snapshot.rejected === 0 && !restarted.aim && !restarted.preview);
      const restartBox = await page.locator('.demo-skill').nth(0).boundingBox();
      await page.mouse.move(restartBox.x + restartBox.width / 2, restartBox.y + restartBox.height / 2);
      await page.mouse.down(); mouseDown = true; await ticks(1); await page.keyboard.press('r');
      await page.mouse.up(); mouseDown = false; const afterHeldRestart = await ticks(2);
      c('restart cancels HUD capture and prevents late mouseup', afterHeldRestart.snapshot.accepted === 0 && afterHeldRestart.snapshot.rejected === 0 && !afterHeldRestart.aim);
      await releaseAll();
    } finally { closing = true; await context.close(); }
  }
  check('offline browser makes no external HTTP requests', report.externalRequests.length === 0, report.externalRequests);
  check('all viewport runs have no runtime page errors', report.errors.length === 0, report.errors);
  report.passed = true;
} catch (error) {
  report.failure = error instanceof Error ? error.message : String(error);
  process.exitCode = 1;
} finally {
  report.elapsedSeconds = (performance.now() - started) / 1000;
  writeFileSync(path.join(output, 'v6-aim-browser-smoke.json'), `${JSON.stringify(report, null, 2)}\n`);
  console.log(JSON.stringify({ passed: report.passed, checks: report.checks.length, attacks: report.attacks.length,
    elapsedSeconds: report.elapsedSeconds, failure: report.failure, errors: report.errors,
    report: path.join(output, 'v6-aim-browser-smoke.json') }, null, 2));
  await browser?.close();
}
