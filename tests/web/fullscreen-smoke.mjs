import { chromium } from '@playwright/test';
import { mkdirSync, writeFileSync, readFileSync } from 'node:fs';
import { fileURLToPath, pathToFileURL } from 'node:url';
import path from 'node:path';
import { createHash } from 'node:crypto';

const root = fileURLToPath(new URL('../../', import.meta.url));
const output = path.join(root, 'build/web-validation');
mkdirSync(output, { recursive: true });
const htmlPath = path.resolve(process.argv[2] ?? path.join(root, 'demo/ustc-danmaku.html'));
const url = pathToFileURL(htmlPath);
url.searchParams.set('qa', '1');
const startTime = performance.now();
const deadline = startTime + 120000;
const report = {
  method: 'Offline single HTML; real Playwright mouse and keyboard input with a clock paused before navigation. window.__demo is read only. Synthetic DOM lifecycle/pointerout checks are identified individually.',
  configVersion: 5,
  abiVersion: 4,
  abiEvidence: 'The bundled client rejects any snapshot ABI other than v4; the browser successfully loads and advances that client and core.',
  htmlPath, url: url.href, checks: [], attacks: [], viewports: [],
  htmlSha256: createHash('sha256').update(readFileSync(htmlPath)).digest('hex'),
  errors: [], externalRequests: [], syntheticEvents: [], passed: false,
};
let browser;
function check(name, condition, detail) {
  report.checks.push({ name, pass: Boolean(condition), ...(detail === undefined ? {} : { detail }) });
  if (!condition) throw Error(`${name}: ${JSON.stringify(detail)}`);
}
function budget() {
  if (performance.now() >= deadline) throw Error('Fullscreen browser validation exceeded its 120-second wall-time budget.');
}
function assertPlaying(state, action) {
  if (state.phase !== 'playing') throw Error(`${action}: game is ${state.phase}, tick ${state.snapshot.tick}, HP ${state.snapshot.actors[0].hp}, ${state.reason}`);
}

try {
  check('single HTML embeds scripts and styles without external file references',
    !/<script[^>]*\bsrc=|<link[^>]*\bhref=/i.test(readFileSync(htmlPath, 'utf8')));
  browser = await chromium.launch({ channel: 'chrome', headless: true });
  report.browser = await browser.version();
  for (const size of [{ width: 1440, height: 900 }, { width: 1024, height: 768 }, { width: 390, height: 844 }]) {
    budget();
    const context = await browser.newContext({ viewport: size, offline: true });
    try {
      const page = await context.newPage();
      page.on('pageerror', error => report.errors.push({ viewport: size.width, error: error.message }));
      page.on('request', request => {
        if (/^https?:/.test(request.url())) report.externalRequests.push(request.url());
      });
      const clockTime = new Date('2026-10-07T10:00:00+08:00');
      await page.clock.install({ time: clockTime });
      await page.clock.pauseAt(new Date(clockTime.getTime() + 1000));
      await page.goto(url.href);
      await page.waitForFunction(() => window.__demo, null, { timeout: 30000, polling: 50 });
      const inspect = () => page.evaluate(() => window.__demo.inspect());
      const prefix = `${size.width}x${size.height}`;
      const menu = await inspect();
      check(`${prefix}: real cfg5 core and current costs load offline`, menu.snapshot.configVersion === 5
        && JSON.stringify(menu.snapshot.costs) === '[25,50,20,100]', menu.snapshot.costs);
      check(`${prefix}: four skill introductions appear before start`, await page.locator('.demo-skill-guide').count() === 4
        && await page.locator('[data-overlay="skills"]').isVisible());
      const guideCosts = await page.locator('[data-guide-cost]').allTextContents();
      check(`${prefix}: menu costs read actual snapshot`, JSON.stringify(guideCosts) === JSON.stringify(['25 能量', '50 能量', '20 能量', '100 能量']), guideCosts);
      await page.screenshot({ path: path.join(output, `v5-menu-${size.width}.png`) });
      await page.locator('[data-overlay="action"]').click();
      let state = await inspect();
      check(`${prefix}: start is live with three students and zero score`, state.phase === 'playing'
        && state.snapshot.actors.length === 4 && state.snapshot.kills === 0 && state.snapshot.gpaHundredths === 0);
      await page.clock.runFor(34);

      let initialArena;
      async function layoutCheck() {
        const layout = await page.evaluate(() => {
          const bounds = selector => {
            const node = document.querySelector(selector); const b = node.getBoundingClientRect();
            return { x: b.x, y: b.y, width: b.width, height: b.height, right: b.right, bottom: b.bottom };
          };
          return {
            arena: bounds('#arena'), canvas: bounds('#arena canvas'), hud: bounds('#hud'), shell: bounds('#game-shell'),
            energy: bounds('[data-ui="energy-track"]'), cd: bounds('[data-ui="cd-track"]'),
            cdText: bounds('[data-ui="cd-text"]'), energyText: bounds('[data-ui="energy-text"]'),
            bodyWidth: document.body.scrollWidth, bodyHeight: document.body.scrollHeight,
            sidebarCount: document.querySelectorAll('.page-header, main > footer, aside#hud').length,
            buttons: [...document.querySelectorAll('.demo-hud button')].map(node => {
              const b = node.getBoundingClientRect(); return { x: b.x, right: b.right, y: b.y, bottom: b.bottom };
            }),
          };
        });
        const s = (await inspect()).snapshot;
        check(`${prefix}: canvas fills all space below top HUD`, layout.shell.width === size.width
          && layout.shell.height === size.height && layout.hud.x === 0 && layout.hud.y === 0
          && layout.hud.width === size.width && Math.abs(layout.arena.y - layout.hud.bottom) < .05
          && Math.abs(layout.arena.bottom - size.height) < .05 && layout.arena.width === size.width
          && Math.abs(layout.canvas.x - layout.arena.x) < .05 && Math.abs(layout.canvas.y - layout.arena.y) < .05
          // Pixi backs its canvas with whole device pixels. Fractional CSS
          // layout may leave less than one pixel of rounding at the edge.
          && Math.abs(layout.canvas.width - layout.arena.width) < 1 && Math.abs(layout.canvas.height - layout.arena.height) < 1, layout);
        check(`${prefix}: no sidebar or page scrolling`, layout.sidebarCount === 0 && layout.bodyWidth === size.width
          && layout.bodyHeight === size.height, layout);
        check(`${prefix}: field uses this window aspect with fixed playable area`,
          Math.abs(s.fieldW / s.fieldH - layout.arena.width / layout.arena.height) < .002
          && Math.abs(s.fieldW * s.fieldH - 960 * 720) < 1, { fieldW: s.fieldW, fieldH: s.fieldH, area: s.fieldW * s.fieldH, arena: layout.arena });
        check(`${prefix}: energy and CD bars are prominent at the top`, layout.energy.width >= 150
          && layout.cd.width >= 150 && layout.energy.height >= 10 && layout.cd.height >= 10
          && layout.cd.bottom < layout.arena.y && layout.energy.bottom < layout.arena.y
          && layout.cdText.height >= 19 && layout.energyText.height >= 19, layout);
        check(`${prefix}: all six battle buttons remain visible`, layout.buttons.length === 6
          && layout.buttons.every(b => b.x >= -.05 && b.right <= size.width + .05 && b.y >= 0 && b.bottom <= layout.hud.bottom + .05), layout.buttons);
        report.viewports.push({ ...size, layout, fieldW: s.fieldW, fieldH: s.fieldH });
        initialArena = layout.arena;
      }
      async function stableArena(label) {
        const current = await page.locator('#arena').evaluate(node => {
          const b = node.getBoundingClientRect(); return { x: b.x, y: b.y, width: b.width, height: b.height };
        });
        check(`${prefix}: ${label} preserves battlefield geometry`, ['x', 'y', 'width', 'height']
          .every(key => Math.abs(current[key] - initialArena[key]) < .05), { initial: initialArena, current });
        const notice = await page.locator('[data-ui="notice"]').evaluate(node => ({ text: node.textContent, title: node.title }));
        check(`${prefix}: ${label} preserves full notice in tooltip`, notice.text === notice.title);
      }
      await layoutCheck();
      check(`${prefix}: introductions are hidden after start`, await page.locator('[data-overlay="skills"]').isHidden()
        && await page.locator('.demo-status-overlay').isHidden());
      await page.screenshot({ path: path.join(output, `v5-playing-${size.width}.png`) });

      // The pointer is genuinely moved to the HUD, outside the canvas. The
      // expected direction uses only the public world field and DOM rectangle.
      const beforePointer = await inspect();
      const arena = report.viewports.at(-1).layout.arena;
      const pointer = { x: size.width - 8, y: 12 };
      const boss = beforePointer.snapshot.actors[0];
      const worldPointer = { x: pointer.x * beforePointer.snapshot.fieldW / arena.width,
        y: (pointer.y - arena.y) * beforePointer.snapshot.fieldH / arena.height };
      const vector = { x: worldPointer.x - boss.x, y: worldPointer.y - boss.y };
      await page.mouse.move(pointer.x, pointer.y);
      await page.clock.runFor(100);
      const afterPointer = await inspect();
      const movement = { x: afterPointer.snapshot.actors[0].x - boss.x, y: afterPointer.snapshot.actors[0].y - boss.y };
      const directionError = Math.abs(movement.x * vector.y - movement.y * vector.x)
        / Math.max(1, Math.hypot(movement.x, movement.y) * Math.hypot(vector.x, vector.y));
      check(`${prefix}: real pointer movement in top HUD still steers with correct world projection`,
        afterPointer.phase === 'playing' && movement.x > 0 && movement.y < 0 && directionError < .002,
        { movement, vector, directionError });
      const keyboardBefore = afterPointer.snapshot.actors[0];
      await page.keyboard.down('a'); await page.clock.runFor(100); await page.keyboard.up('a');
      const keyboardAfter = (await inspect()).snapshot.actors[0];
      check(`${prefix}: WASD overrides the opposite mouse direction`, keyboardAfter.x < keyboardBefore.x
        && Math.abs(keyboardAfter.y - keyboardBefore.y) < .002, { before: keyboardBefore, after: keyboardAfter });
      const arrowBefore = keyboardAfter;
      await page.keyboard.down('ArrowDown'); await page.clock.runFor(100); await page.keyboard.up('ArrowDown');
      const arrowAfter = (await inspect()).snapshot.actors[0];
      check(`${prefix}: arrow key also overrides mouse direction`, arrowAfter.y > arrowBefore.y
        && Math.abs(arrowAfter.x - arrowBefore.x) < .002, { before: arrowBefore, after: arrowAfter });
      report.syntheticEvents.push({ viewport: size.width, event: 'pointerout', purpose: 'Verify page-exit handler preserves the latest visible pointer direction. Actual OS cursor positions outside the browser cannot be observed by a web page.' });
      await page.evaluate(p => window.dispatchEvent(new PointerEvent('pointerout', {
        clientX: p.x, clientY: p.y, pointerType: 'mouse', relatedTarget: null, bubbles: true,
      })), pointer);
      const outBefore = (await inspect()).snapshot.actors[0];
      await page.clock.runFor(100); const outAfter = (await inspect()).snapshot.actors[0];
      check(`${prefix}: synthetic page-exit pointerout retains movement`, outAfter.x > outBefore.x && outAfter.y < outBefore.y);

      let held = new Set(); let leg = 0;
      async function releaseMovement() {
        for (const key of held) await page.keyboard.up(key);
        held = new Set();
      }
      async function restart() {
        await releaseMovement(); await page.keyboard.press('r'); await page.clock.runFor(34); leg = 0;
        const s = await inspect(); assertPlaying(s, 'restart');
        return s;
      }
      async function moving(ms) {
        const steps = Math.ceil(ms / 150);
        for (let index = 0; index < steps; index++) {
          budget(); const current = await inspect(); assertPlaying(current, 'legal survival movement');
          const s = current.snapshot, b = s.actors[0];
          const route = [[.80, .78], [.80, .18], [.20, .18], [.20, .78]];
          let target = { x: route[leg][0] * s.fieldW, y: route[leg][1] * s.fieldH };
          if (Math.hypot(target.x - b.x, target.y - b.y) < 36) {
            leg = (leg + 1) % route.length; target = { x: route[leg][0] * s.fieldW, y: route[leg][1] * s.fieldH };
          }
          const desired = new Set();
          if (target.x - b.x > 18) desired.add('d'); if (target.x - b.x < -18) desired.add('a');
          if (target.y - b.y > 18) desired.add('s'); if (target.y - b.y < -18) desired.add('w');
          for (const key of held) if (!desired.has(key)) await page.keyboard.up(key);
          for (const key of desired) if (!held.has(key)) await page.keyboard.down(key);
          held = desired;
          await page.clock.runFor(Math.min(150, ms - index * 150));
        }
      }
      function checkCD(state, label) {
        const s = state.snapshot;
        return page.locator('[data-ui="cd-text"]').textContent().then(async text => {
          if (s.attackState === 0) check(`${prefix}: ${label} idle is ready`, text === '就绪', { text, tick: s.tick });
          else {
            const ticks = Math.max(1, s.startTick + s.windup + s.active + 1 - s.tick);
            const expected = `${(Math.ceil(ticks / 6) / 10).toFixed(1)}s`;
            check(`${prefix}: ${label} busy CD is positive and matches C plan`, text === expected && parseFloat(text) > 0,
              { text, expected, tick: s.tick, attackState: s.attackState });
          }
          await stableArena(label);
        });
      }

      for (let pattern = 0; pattern < 4; pattern++) {
        budget(); state = await restart();
        if (pattern === 3) {
          await page.keyboard.press('4');
          const unconsumed = await inspect();
          check(`${prefix}: fourth request cannot charge before a logical tick`, unconsumed.snapshot.energy === state.snapshot.energy && unconsumed.snapshot.accepted === 0);
          await page.clock.runFor(34); const rejected = await inspect();
          check(`${prefix}: fourth refuses starting 60 without charging`, rejected.snapshot.rejected === 1
            && rejected.snapshot.accepted === 0 && rejected.snapshot.energy >= 60 && rejected.snapshot.energy <= 61, rejected.snapshot);
          await moving(4100); state = await inspect();
          check(`${prefix}: fourth naturally reaches 100 by movement and time`, state.phase === 'playing' && state.snapshot.energy === 100, { tick: state.snapshot.tick, energy: state.snapshot.energy, hp: state.snapshot.actors[0].hp });
        }
        // Geometry availability is public. Move legally if the mine would be
        // unsafe; do not edit positions, energy, RNG, core state or the plan.
        for (let attempts = 0; !state.snapshot.available[pattern] && attempts < 20; attempts++) {
          await moving(150); state = await inspect();
        }
        check(`${prefix}: skill ${pattern + 1} has a legal public start`, state.snapshot.available[pattern],
          { tick: state.snapshot.tick, energy: state.snapshot.energy, actors: state.snapshot.actors });
        const before = state.snapshot;
        await page.keyboard.down(String(pattern + 1));
        const pending = await inspect();
        check(`${prefix}: skill ${pattern + 1} does not deduct before consumption`, pending.snapshot.energy === before.energy && pending.snapshot.accepted === before.accepted);
        await page.clock.runFor(34); state = await inspect();
        assertPlaying(state, `skill ${pattern + 1} accept`);
        check(`${prefix}: skill ${pattern + 1} is accepted once with exact shared cost`, state.snapshot.pattern === pattern
          && state.snapshot.accepted === before.accepted + 1 && state.snapshot.energy >= before.energy - before.costs[pattern]
          && state.snapshot.energy <= before.energy - before.costs[pattern] + 1, { beforeEnergy: before.energy, afterEnergy: state.snapshot.energy, cost: before.costs[pattern], accepted: state.snapshot.accepted });
        check(`${prefix}: skill ${pattern + 1} exposes warnings before any Boss bullet`, state.snapshot.attackState === 1
          && state.snapshot.warnings.length > 0 && state.snapshot.bossBullets === 0, { warnings: state.snapshot.warnings.length, tick: state.snapshot.tick });
        const accepted = state.snapshot;
        await moving(100); state = await inspect();
        check(`${prefix}: held skill ${pattern + 1} produces a single input edge`, state.snapshot.accepted === accepted.accepted);
        await page.keyboard.up(String(pattern + 1));
        await checkCD(state, `skill ${pattern + 1} windup`);
        if (pattern === 0) {
          const beforeBusy = state.snapshot;
          await page.locator('.demo-skill').nth(1).click(); await page.clock.runFor(34);
          const busy = await inspect();
          check(`${prefix}: busy request is rejected without queuing or charging`, busy.snapshot.accepted === beforeBusy.accepted
            && busy.snapshot.rejected === beforeBusy.rejected + 1 && busy.snapshot.energy >= beforeBusy.energy
            && busy.reason.includes('尚未结束'), { before: beforeBusy.energy, after: busy.snapshot.energy, reason: busy.reason });
          await stableArena('busy rejection notice');
          await page.keyboard.press('Escape'); const paused = await inspect();
          const cdPaused = await page.locator('[data-ui="cd-text"]').textContent();
          await page.clock.runFor(2000); const frozen = await inspect();
          check(`${prefix}: pause freezes shared CD energy score and time`, frozen.phase === 'paused'
            && frozen.snapshot.tick === paused.snapshot.tick && frozen.snapshot.energy === paused.snapshot.energy
            && frozen.snapshot.gpaHundredths === paused.snapshot.gpaHundredths
            && await page.locator('[data-ui="cd-text"]').textContent() === cdPaused);
          await page.locator('[data-overlay="action"]').click(); await releaseMovement();
        }
        // Advance just beyond this plan's public first emission, with ordinary
        // movement while waiting for its windup rather than standing in fire.
        state = await inspect();
        const untilFirst = Math.max(50, (accepted.startTick + accepted.windup + 3 - state.snapshot.tick) * 1000 / 60);
        await moving(untilFirst); state = await inspect();
        check(`${prefix}: skill ${pattern + 1} emits real C/Wasm Boss bullets`, state.snapshot.bossBullets > 0
          && state.snapshot.bullets.some(bullet => bullet.faction === 1 && bullet.pattern === pattern),
          { tick: state.snapshot.tick, emitted: state.snapshot.bossBullets, live: state.snapshot.bullets.filter(bullet => bullet.faction === 1).length });
        if (size.width === 1440) await page.screenshot({ path: path.join(output, `v5-skill-${pattern + 1}-${size.width}.png`) });
        report.attacks.push({ viewport: size.width, pattern, acceptedTick: accepted.startTick,
          windup: accepted.windup, active: accepted.active, emitted: state.snapshot.bossBullets,
          energyAfterAccept: accepted.energy, warningCount: accepted.warnings.length });
        // Poll the shared lock through completion. A clear wave may explicitly
        // cancel a plan early; no extra attack is sent while it is locked.
        let iterations = 0;
        while (state.snapshot.attackState !== 0 && iterations++ < 150) {
          await checkCD(state, `skill ${pattern + 1} progress ${iterations}`);
          const ticksLeft = accepted.startTick + accepted.windup + accepted.active + 1 - state.snapshot.tick;
          await moving(ticksLeft < 12 ? 16 : 150);
          state = await inspect();
        }
        assertPlaying(state, `skill ${pattern + 1} completion`);
        check(`${prefix}: skill ${pattern + 1} releases shared lock`, state.snapshot.attackState === 0);
        await checkCD(state, `skill ${pattern + 1} completion`);
        if (pattern === 0) check(`${prefix}: rejected busy input never fires after CD`, state.snapshot.accepted === accepted.accepted);
      }
      await releaseMovement();
      state = await inspect();
      check(`${prefix}: public HUD GPA equals current core GPA`, await page.locator('[data-ui="gpa"]').textContent()
        === (state.snapshot.gpaHundredths / 100).toFixed(2));
      const previousScore = { kills: state.snapshot.kills, gpaHundredths: state.snapshot.gpaHundredths };
      state = await restart();
      check(`${prefix}: R resets GPA kills wave actors and attack counters`, state.snapshot.kills === 0
        && state.snapshot.gpaHundredths === 0 && state.snapshot.wave === 1 && state.snapshot.wavesCleared === 0
        && state.snapshot.actors.length === 4 && state.snapshot.accepted === 0 && state.snapshot.rejected === 0, previousScore);

      // A window resize is an actual browser operation. It must scale this
      // round uniformly while keeping the already accepted public plan intact.
      await page.keyboard.press('1'); await page.clock.runFor(34);
      const beforeResize = await inspect();
      check(`${prefix}: resize fixture accepts a real ring plan`, beforeResize.snapshot.attackState === 1 && beforeResize.snapshot.pattern === 0);
      const resized = size.width < 700 ? { width: 700, height: 390 } : { width: 900, height: 700 };
      await page.setViewportSize(resized); await page.clock.runFor(34);
      const afterResize = await inspect();
      check(`${prefix}: resizing preserves current field and accepted warning plan`, afterResize.phase === 'playing'
        && afterResize.snapshot.fieldW === beforeResize.snapshot.fieldW && afterResize.snapshot.fieldH === beforeResize.snapshot.fieldH
        && afterResize.snapshot.planId === beforeResize.snapshot.planId
        && afterResize.snapshot.startTick === beforeResize.snapshot.startTick
        && JSON.stringify(afterResize.snapshot.warnings) === JSON.stringify(beforeResize.snapshot.warnings));
      state = await restart();
      const resizedArena = await page.locator('#arena').boundingBox();
      check(`${prefix}: restarting after resize adopts the new window aspect`,
        Math.abs(state.snapshot.fieldW / state.snapshot.fieldH - resizedArena.width / resizedArena.height) < .002
        && Math.abs(state.snapshot.fieldW * state.snapshot.fieldH - 960 * 720) < 1
        && (state.snapshot.fieldW !== beforeResize.snapshot.fieldW || state.snapshot.fieldH !== beforeResize.snapshot.fieldH),
        { size: resized, arena: resizedArena, fieldW: state.snapshot.fieldW, fieldH: state.snapshot.fieldH });
      await page.setViewportSize(size); state = await restart();

      // These are synthetic handler checks: no hidden property or game state
      // is changed to make a controller survive or to fabricate gameplay.
      report.syntheticEvents.push({ viewport: size.width, event: 'blur', purpose: 'Verify loss-of-focus pause handler and input clearing.' });
      await page.evaluate(() => window.dispatchEvent(new Event('blur')));
      const blurred = await inspect();
      check(`${prefix}: synthetic window blur automatically pauses`, blurred.phase === 'paused' && blurred.reason.includes('失去焦点'));
      await page.clock.runFor(500);
      check(`${prefix}: blur pause does not advance logical time`, (await inspect()).snapshot.tick === blurred.snapshot.tick);
      await page.locator('[data-overlay="action"]').click();
      const otherPage = await context.newPage(); await otherPage.bringToFront();
      const hidden = await page.evaluate(() => document.hidden);
      if (hidden) {
        check(`${prefix}: actual browser background tab automatically pauses`, (await inspect()).phase === 'paused');
      } else {
        report.syntheticEvents.push({ viewport: size.width, event: 'visibilitychange', purpose: 'Headless Chrome keeps tabs visible; temporarily expose document.hidden solely to trigger the lifecycle handler. No logical game state is written.' });
        await page.evaluate(() => {
          Object.defineProperty(document, 'hidden', { configurable: true, get: () => true });
          document.dispatchEvent(new Event('visibilitychange'));
          delete document.hidden;
        });
        check(`${prefix}: synthetic hidden-page event automatically pauses`, (await inspect()).phase === 'paused');
      }
      await otherPage.close(); await page.bringToFront();
      await page.locator('[data-overlay="action"]').click(); await page.clock.runFor(34);
      check(`${prefix}: resume resets accumulated time and continues`, (await inspect()).phase === 'playing');
    } finally {
      await context.close();
    }
  }
  check('offline HTML has zero external HTTP requests', report.externalRequests.length === 0, report.externalRequests);
  check('all three browser runs report no runtime errors', report.errors.length === 0, report.errors);
  report.passed = true;
} catch (error) {
  report.failure = error instanceof Error ? error.message : String(error);
  process.exitCode = 1;
} finally {
  report.elapsedSeconds = (performance.now() - startTime) / 1000;
  writeFileSync(path.join(output, 'v5-browser-fullscreen.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify({ passed: report.passed, checks: report.checks.length, attacks: report.attacks.length,
    elapsedSeconds: report.elapsedSeconds, failure: report.failure, errors: report.errors, externalRequests: report.externalRequests,
    report: path.join(output, 'v5-browser-fullscreen.json') }, null, 2));
  await browser?.close();
}
