import { chromium } from '@playwright/test';
import { createHash } from 'node:crypto';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { setTimeout as delay } from 'node:timers/promises';
import path from 'node:path';

const root = fileURLToPath(new URL('../../', import.meta.url));
const output = path.join(root, 'build/mobile');
mkdirSync(output, { recursive: true });
const htmlPath = path.resolve(process.argv[2] ?? path.join(root, 'build/release/ustc-danmaku-endless.html'));
const html = readFileSync(htmlPath);
const online = process.argv[3] !== undefined;
const url = online ? new URL(process.argv[3]) : pathToFileURL(htmlPath);
if (online && url.protocol !== 'https:') throw Error('The optional published page URL must use HTTPS.');
url.searchParams.set('qa', '1');
const report = {
  deliveryType: online ? 'https' : 'offline-file', url: url.href,
  method: `${online ? 'Published HTTPS final radial-skill-wheel page; each main document response must be HTTP 200 and match the local artifact SHA-256.' : 'Offline final radial-skill-wheel HTML.'} Actual Chromium DOM/C/Wasm, native CDP Input.dispatchTouchEvent multitouch and real keyboard/mouse events. Passive DOM pointermove observation only synchronizes native event delivery; no RAF/core tick is advanced to deliver a drag. Device profiles emulate UA, platform, viewport and touch capability; no core, energy, RNG, position or plan injection. Controlled RAF clock is not an FPS or human-hand-feel measurement. iPhone profiles run Chromium, not physical iOS Safari.`,
  htmlPath, htmlSha256: createHash('sha256').update(html).digest('hex'), configVersion: 6, abiVersion: 5,
  profiles: [], checks: [], errors: [], documentRequests: [], externalRequests: [], failedRequests: [], httpErrors: [], passed: false,
};
const phoneUA = 'Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Mobile/15E148 Safari/604.1';
const androidUA = 'Mozilla/5.0 (Linux; Android 15; Pixel 9) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Mobile Safari/537.36';
const ipadUA = 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Safari/605.1.15';
const profiles = [
  { name: 'iPhone portrait', width: 390, height: 844, dpr: 3, userAgent: phoneUA, platform: 'iPhone', touch: true, mode: 'touch', full: true },
  { name: 'iPhone landscape', width: 844, height: 390, dpr: 3, userAgent: phoneUA, platform: 'iPhone', touch: true, mode: 'touch', full: true },
  { name: 'Android portrait', width: 412, height: 915, dpr: 2.625, userAgent: androidUA, platform: 'Linux armv8l', touch: true, mode: 'touch', full: true },
  { name: 'iPhone compact', width: 320, height: 568, dpr: 2, userAgent: phoneUA, platform: 'iPhone', touch: true, mode: 'touch' },
  { name: 'iPad desktop UA', width: 1024, height: 768, dpr: 2, userAgent: ipadUA, platform: 'MacIntel', touch: true, mode: 'touch' },
  { name: 'desktop', width: 1440, height: 900, touch: false, mode: 'desktop' },
  { name: 'narrow desktop', width: 390, height: 844, touch: false, mode: 'desktop' },
  // Chromium touch emulation exposes a primary coarse pointer. The fine-pointer
  // Windows touchscreen laptop negative is separately covered by the pure probe.
  { name: 'Windows primary coarse touch', width: 1280, height: 800, platform: 'Win32', touch: true, mode: 'touch' },
];
const near = (a, b, tolerance = .001) => Number.isFinite(a) && Number.isFinite(b) && Math.abs(a - b) <= tolerance;
const directionSame = (a, b) => !!a && !!b && near(a.dirX, b.dirX, 1e-6) && near(a.dirY, b.dirY, 1e-6);
const distance = (a, b) => Math.hypot(a.x - b.x, a.y - b.y);
const deadline = performance.now() + 300000;
let browser;
function budget() { if (performance.now() > deadline) throw Error('Mobile browser smoke exceeded 300-second wall-time budget.'); }
function check(name, pass, detail) {
  report.checks.push({ name, pass: Boolean(pass), ...(detail === undefined ? {} : { detail }) });
  if (!pass) throw Error(`${name}: ${JSON.stringify(detail)}`);
}

try {
  check('single HTML has no external script or stylesheet', !/<script[^>]*\bsrc=|<link[^>]*\bhref=/i.test(html.toString('utf8')));
  const launch = { channel: 'chrome', headless: true };
  if (online && process.env.PAGES_QA_PROXY) launch.proxy = { server: process.env.PAGES_QA_PROXY };
  browser = await chromium.launch(launch);
  report.browser = await browser.version();
  for (const profile of profiles) {
    budget();
    const context = await browser.newContext({ viewport: { width: profile.width, height: profile.height },
      hasTouch: profile.touch, isMobile: profile.touch, deviceScaleFactor: profile.dpr ?? 1, offline: !online,
      ...(profile.userAgent ? { userAgent: profile.userAgent } : {}) });
    let closing = false;
    try {
      const page = await context.newPage();
      await page.addInitScript(() => {
        window.__mobileInputObservation = { moves: 0, last: null };
        addEventListener('pointermove', event => {
          if (event.pointerType !== 'touch') return;
          window.__mobileInputObservation.moves++;
          window.__mobileInputObservation.last = { x: event.clientX, y: event.clientY, id: event.pointerId };
        }, { capture: true, passive: true });
      });
      page.on('pageerror', error => report.errors.push({ profile: profile.name, error: error.message, stack: error.stack, closing }));
      page.on('request', request => {
        if (!/^https?:/.test(request.url())) return;
        const expectedDocument = online && request.url() === url.href && request.isNavigationRequest()
          && request.resourceType() === 'document' && request.frame() === page.mainFrame();
        const record = { profile: profile.name, url: request.url(), type: request.resourceType() };
        if (expectedDocument) report.documentRequests.push(record);
        else report.externalRequests.push(record);
      });
      page.on('requestfailed', request => report.failedRequests.push({ profile: profile.name, url: request.url(), error: request.failure()?.errorText }));
      page.on('response', response => { if (response.status() >= 400) report.httpErrors.push({ profile: profile.name, url: response.url(), status: response.status() }); });
      const cdp = await context.newCDPSession(page);
      if (profile.touch) await cdp.send('Emulation.setTouchEmulationEnabled', { enabled: true, maxTouchPoints: 5 });
      if (profile.platform) {
        const userAgent = profile.userAgent ?? await page.evaluate(() => navigator.userAgent);
        await cdp.send('Emulation.setUserAgentOverride', { userAgent, platform: profile.platform });
      }
      const clockTime = new Date('2026-10-08T10:00:00+08:00');
      await page.clock.install({ time: clockTime });
      await page.clock.pauseAt(new Date(clockTime.getTime() + 1000));
      const response = await page.goto(url.href, { waitUntil: 'load', timeout: 30000 });
      if (online) {
        check(`${profile.name}: published expected HTTPS document returns 200`, response?.status() === 200
          && response.url() === url.href && page.url() === url.href,
        { status: response?.status(), responseURL: response?.url(), actualURL: page.url() });
        const actualHash = createHash('sha256').update(await response.body()).digest('hex');
        check(`${profile.name}: published body matches local artifact byte for byte`, actualHash === report.htmlSha256,
        { expected: report.htmlSha256, actual: actualHash });
      }
      await page.waitForFunction(() => window.__demo, null, { timeout: 30000, polling: 50 });
      const inspect = () => page.evaluate(() => window.__demo.inspect());
      const c = (name, pass, detail) => check(`${profile.name}: ${name}`, pass, detail);
      const screenshotPath = kind => path.join(output, `${online ? 'online-' : ''}${profile.name.replaceAll(' ', '-').toLowerCase()}-${kind}.png`);
      const touchPoints = new Map();
      async function dispatchTouch(type, entries = [...touchPoints]) {
        budget();
        await cdp.send('Input.dispatchTouchEvent', { type,
          touchPoints: entries.map(([id, point]) => ({ id, x: point.x, y: point.y, radiusX: 7, radiusY: 7, force: 1 })) });
      }
      async function touchDown(id, point) { touchPoints.set(id, point); await dispatchTouch('touchStart'); }
      async function touchMove(id, point) {
        if (!touchPoints.has(id)) throw Error('Cannot move inactive touch ' + id);
        const previous = touchPoints.get(id);
        const observationBefore = await page.evaluate(() => window.__mobileInputObservation.moves);
        touchPoints.set(id, point); await dispatchTouch('touchMove');
        // Chromium may return the CDP response before a coalesced pointermove
        // reaches the DOM. Observe delivery with the JS game clock still paused;
        // otherwise an immediate drag selection would be sampled before input.
        if (previous.x !== point.x || previous.y !== point.y) {
          const until = performance.now() + 1000;
          while (await page.evaluate(() => window.__mobileInputObservation.moves) <= observationBefore) {
            if (performance.now() > until) throw Error('Native touchMove did not reach DOM observer');
            await delay(5);
          }
        }
      }
      async function touchUp(id) {
        const point = touchPoints.get(id);
        if (!point) throw Error('Cannot lift inactive touch ' + id);
        touchPoints.delete(id);
        // CDP touchEnd identifies the ended contacts, rather than the remaining
        // contacts. A remaining-left list would accidentally lift the left thumb.
        await dispatchTouch('touchEnd', [[id, point]]);
      }
      async function touchCancel() { touchPoints.clear(); await dispatchTouch('touchCancel'); }
      async function liftAll() { for (const id of [...touchPoints.keys()]) await touchUp(id); }
      async function center(selector) {
        const bounds = await page.locator(selector).boundingBox();
        if (!bounds) throw Error(`${profile.name}: missing visible control ${selector}`);
        return { x: bounds.x + bounds.width / 2, y: bounds.y + bounds.height / 2 };
      }
      async function tap(selector, id = 9) {
        await page.locator(selector).scrollIntoViewIfNeeded();
        const point = await center(selector);
        await touchDown(id, point); await touchUp(id);
      }
      async function ticks(count = 1) {
        const target = (await inspect()).snapshot.tick + count;
        let current = await inspect(), frames = 0;
        while (current.snapshot.tick < target) {
          budget();
          if (current.phase !== 'playing') throw Error(`${profile.name}: advance stopped in ${current.phase}: ${current.reason}`);
          if (frames++ > count * 3 + 6) throw Error(`${profile.name}: logical tick did not advance`);
          await page.clock.runFor(16); current = await inspect();
          if (current.snapshot.tick > target) throw Error(`${profile.name}: overshot logical boundary ${target}`);
        }
        return current;
      }
      async function restart() {
        await liftAll();
        if (profile.touch) await tap('[data-ui="restart"]'); else await page.keyboard.press('r');
        const state = await ticks(1);
        if (state.phase !== 'playing' || state.snapshot.accepted !== 0 || state.snapshot.rejected !== 0 || state.aim) {
          throw Error(`${profile.name}: restart did not clear real input state`);
        }
        return state;
      }
      async function axis() {
        return page.locator('[data-touch="joystick-base"]').evaluate(element => ({
          x: Number(element.dataset.axisX ?? 0), y: Number(element.dataset.axisY ?? 0), radius: Number(element.dataset.radius),
        }));
      }
      async function beginJoystick() {
        const point = await center('[data-touch="joystick-zone"]');
        await touchDown(1, point);
        const data = await axis();
        if (!Number.isFinite(data.radius) || data.radius < 28) throw Error('Missing joystick radius');
        return { ...point, radius: data.radius };
      }
      async function pushJoystick(anchor, dx, dy, strength = 1) {
        await touchMove(1, { x: anchor.x + dx * anchor.radius * strength, y: anchor.y + dy * anchor.radius * strength });
      }
      async function wheelState() {
        return page.locator('[data-touch="skill-wheel"]').evaluate(element => ({
          open: element.dataset.open === 'true', selection: Number(element.dataset.selection),
        }));
      }
      async function beginWheel(id = 2) {
        const origin = await center('[data-touch="wheel-trigger"]');
        await touchDown(id, origin);
        return origin;
      }
      async function chooseWheel(pattern, id = 2) {
        const point = await center(`[data-touch-skill="${pattern}"]`);
        await touchMove(id, point);
        return point;
      }
      async function holdSkill(pattern, id = 2) {
        const origin = await beginWheel(id), point = await chooseWheel(pattern, id);
        return { origin, point };
      }
      const signals = await page.evaluate(() => ({ userAgent: navigator.userAgent, platform: navigator.platform,
        maxTouchPoints: navigator.maxTouchPoints, coarsePointer: matchMedia('(pointer: coarse)').matches,
        noHover: matchMedia('(hover: none)').matches, userAgentMobile: navigator.userAgentData?.mobile,
        innerWidth, innerHeight, devicePixelRatio }));
      let state = await inspect();
      const profileRecord = { name: profile.name, emulation: profile, signals, controlMode: state.controlMode };
      report.profiles.push(profileRecord);
      c('automatic device/input mode matches exposed capability', state.controlMode?.mode === profile.mode
        && state.controlMode.detectedMode === profile.mode && state.controlMode.automatic === true
        && typeof state.controlMode.reason === 'string', profileRecord);
      c('browser exposes requested viewport and device pixel ratio', signals.innerWidth === profile.width && signals.innerHeight === profile.height
        && near(signals.devicePixelRatio, profile.dpr ?? 1, .00001), signals);
      c('current C game and four skill introductions load', state.snapshot.configVersion === 6
        && JSON.stringify(state.snapshot.costs) === '[25,50,20,100]'
        && await page.locator('.demo-skill-guide').count() === 4);
      c('pre-game overlay exposes an accessible mode switch', await page.locator('[data-overlay="controls-toggle"]').isVisible());
      if (profile.touch) await tap('[data-overlay="controls-toggle"]');
      else await page.locator('[data-overlay="controls-toggle"]').click();
      const menuSwitched = await inspect();
      c('menu mode switch overrides detection without starting a round', menuSwitched.phase === 'menu'
        && menuSwitched.snapshot.tick === 0 && menuSwitched.controlMode.mode !== profile.mode
        && menuSwitched.controlMode.detectedMode === profile.mode && menuSwitched.controlMode.automatic === false);
      if (profile.touch) await tap('[data-overlay="controls-toggle"]');
      else await page.locator('[data-overlay="controls-toggle"]').click();
      c('second menu activation returns to detected mode exactly once', (await inspect()).controlMode.mode === profile.mode
        && (await inspect()).phase === 'menu' && (await inspect()).snapshot.accepted === 0);
      if (!profile.touch) {
        await page.locator('[data-overlay="action"]').click(); await ticks(1);
        c('desktop including narrow window does not show touch controls', !await page.locator('[data-touch="controls"]').isVisible());
        await page.locator('[data-ui="controls-toggle"]').click();
        state = await inspect();
        c('real desktop click can manually enable touch mode', state.controlMode.mode === 'touch' && state.controlMode.automatic === false
          && await page.locator('[data-touch="controls"]').isVisible());
        await page.locator('[data-ui="controls-toggle"]').click();
        c('manual mode returns to desktop', (await inspect()).controlMode.mode === 'desktop'
          && !await page.locator('[data-touch="controls"]').isVisible());
        continue;
      }
      await page.screenshot({ path: screenshotPath('menu') });
      await tap('[data-overlay="action"]'); await ticks(1);
      c('native touch tap starts actual game and shows controls', (await inspect()).phase === 'playing'
        && await page.locator('[data-touch="controls"]').isVisible() && !await page.locator('[data-overlay="skills"]').isVisible());
      await beginWheel(); await ticks(1);
      c('wheel opens at central contact before layout sampling', (await wheelState()).open && !(await inspect()).aim);
      const layout = await page.evaluate(() => {
        const bounds = selector => { const b = document.querySelector(selector).getBoundingClientRect(); return { x: b.x, y: b.y,
          width: b.width, height: b.height, right: b.right, bottom: b.bottom }; };
        return { arena: bounds('#arena'), hud: bounds('#hud'), joystick: bounds('[data-touch="joystick-zone"]'),
          wheel: bounds('[data-touch="skill-wheel"]'), sectors: [0, 1, 2, 3].map(pattern => ({ pattern, ...bounds(`[data-touch-skill="${pattern}"]`) })),
          controls: ['[data-touch="wheel-trigger"]',
            '[data-touch="cancel"]', '[data-ui="pause"]', '[data-ui="restart"]', '[data-ui="controls-toggle"]'].map(selector => ({ selector, ...bounds(selector) })),
          width: innerWidth, height: innerHeight, scrollWidth: document.documentElement.scrollWidth };
      });
      c('battlefield fills below HUD without horizontal overflow', near(layout.arena.width, layout.width, 1)
        && near(layout.arena.y, layout.hud.bottom, 1) && near(layout.arena.bottom, layout.height, 1)
        && layout.scrollWidth <= layout.width + 1, layout);
      c('touch buttons are at least 44px and remain within viewport', layout.controls.every(b => b.width >= 44 && b.height >= 44
        && b.x >= 0 && b.y >= 0 && b.right <= layout.width + 1 && b.bottom <= layout.height + 1), layout.controls);
      const trigger = layout.controls.find(b => b.selector === '[data-touch="wheel-trigger"]');
      c('wheel central activation target is at least 64px', trigger.width >= 64 && trigger.height >= 64, trigger);
      c('left joystick and right wheel occupy separate safe touch areas', layout.wheel.x > layout.width / 2
        && layout.wheel.x >= layout.joystick.right && layout.joystick.x < layout.width / 2
        && layout.wheel.right <= layout.width + 1 && layout.wheel.bottom <= layout.height + 1
        && layout.wheel.y >= layout.arena.y, layout);
      c('four display sectors remain within the safe viewport without overlap', layout.sectors.every(a =>
        a.width > 0 && a.height > 0 && a.x >= 0 && a.y >= layout.arena.y && a.right <= layout.width + 1 && a.bottom <= layout.height + 1)
        && layout.sectors.every((a, i) => layout.sectors.slice(i + 1).every(b =>
          a.right <= b.x || b.right <= a.x || a.bottom <= b.y || b.bottom <= a.y)), layout.sectors);
      await page.screenshot({ path: screenshotPath('playing') });
      await touchUp(2); const layoutRelease = await ticks(1);
      c('layout-opening neutral release preserves zero attack counters', layoutRelease.snapshot.accepted === 0 && layoutRelease.snapshot.rejected === 0);
      if (!profile.full) continue;

      // A floating base may be visually clamped, but first finger contact must
      // remain neutral even at the touch zone's edges.
      const zone = await page.locator('[data-touch="joystick-zone"]').boundingBox();
      for (const [label, point] of [
        ['top-left', { x: zone.x + 5, y: zone.y + 5 }],
        ['bottom-left', { x: zone.x + 5, y: zone.y + zone.height - 5 }],
        ['top-right', { x: zone.x + zone.width - 5, y: zone.y + 5 }],
        ['bottom-right', { x: zone.x + zone.width - 5, y: zone.y + zone.height - 5 }],
      ]) {
        await restart(); const before = (await inspect()).snapshot.actors[0];
        await touchDown(1, point); const firstAxis = await axis(); const after = await ticks(3);
        c(`floating joystick ${label} contact stays neutral`, near(firstAxis.x, 0) && near(firstAxis.y, 0)
          && distance(before, after.snapshot.actors[0]) < .001, { point, firstAxis, before, after: after.snapshot.actors[0] });
        await touchUp(1);
      }
      await restart();
      let anchor = await beginJoystick();
      const beforeDeadZone = (await inspect()).snapshot.actors[0];
      await pushJoystick(anchor, 1, 0, .08); const deadAxis = await axis(); const deadState = await ticks(8);
      c('small deadzone motion stays still without jitter', near(deadAxis.x, 0) && near(deadAxis.y, 0)
        && distance(beforeDeadZone, deadState.snapshot.actors[0]) < .001, deadAxis);
      await touchUp(1);
      async function displacement(strength) {
        await restart(); const origin = (await inspect()).snapshot.actors[0];
        const hold = await beginJoystick(); await pushJoystick(hold, 1, 0, strength);
        const value = await axis(); const moved = await ticks(10); await touchUp(1);
        return { strength, value, delta: distance(origin, moved.snapshot.actors[0]), origin, actor: moved.snapshot.actors[0] };
      }
      const gentle = await displacement(.38), full = await displacement(1);
      profileRecord.movement = { gentle, full };
      c('partial stick offers slow control and full stick accelerates', gentle.delta > .1 && full.delta > gentle.delta * 2
        && gentle.value.x > 0 && gentle.value.x < full.value.x && near(full.value.x, 1) && near(full.value.y, 0), profileRecord.movement);
      const stopped = (await inspect()).snapshot.actors[0]; const stoppedState = await ticks(6);
      c('lifting left thumb immediately stops movement', distance(stopped, stoppedState.snapshot.actors[0]) < .001
        && near((await axis()).x, 0) && near((await axis()).y, 0));
      await restart(); anchor = await beginJoystick(); await pushJoystick(anchor, 1, 1, 1);
      const diagonal = await axis(); const diagonalOrigin = (await inspect()).snapshot.actors[0]; const diagonalState = await ticks(6);
      c('diagonal stick is normalized and does not move faster', near(Math.hypot(diagonal.x, diagonal.y), 1)
        && near(diagonal.x, Math.SQRT1_2) && near(diagonal.y, Math.SQRT1_2)
        && distance(diagonalOrigin, diagonalState.snapshot.actors[0]) <= full.delta / 10 * 6 + .01, diagonal);
      await liftAll();

      await restart();
      const beforeCenter = await inspect(), wheelOrigin = await beginWheel();
      const opened = await ticks(3);
      c('central contact opens wheel without selecting or pausing world', (await wheelState()).open
        && (await wheelState()).selection === -1 && !opened.aim && !opened.preview
        && opened.snapshot.tick > beforeCenter.snapshot.tick && opened.snapshot.accepted === 0
        && opened.snapshot.rejected === 0 && opened.snapshot.energy >= beforeCenter.snapshot.energy);
      await touchMove(2, { x: wheelOrigin.x + 14, y: wheelOrigin.y }); const neutral = await ticks(2);
      c('inner wheel deadzone permits thumb jitter without selecting', !neutral.aim && (await wheelState()).open
        && (await wheelState()).selection === -1 && neutral.snapshot.accepted === 0);
      await touchMove(2, { x: wheelOrigin.x + 19, y: wheelOrigin.y }); await ticks(1);
      c('18-to-20px neutral hysteresis does not invent a selection', !(await inspect()).aim && (await wheelState()).selection === -1);
      await touchMove(2, { x: wheelOrigin.x + 21, y: wheelOrigin.y }); const radiusSelected = await ticks(1);
      c('crossing wheel select threshold chooses right skill preview', radiusSelected.aim?.pattern === 1 && (await wheelState()).selection === 1
        && radiusSelected.snapshot.accepted === 0 && radiusSelected.snapshot.rejected === 0);
      await touchMove(2, { x: wheelOrigin.x + 19, y: wheelOrigin.y }); await ticks(1);
      c('radial hysteresis retains selected skill until inner deadzone', (await inspect()).aim?.pattern === 1 && (await wheelState()).selection === 1);
      await touchMove(2, { x: wheelOrigin.x + 17, y: wheelOrigin.y }); const backToCenter = await ticks(2);
      c('returning to center cancels selection but keeps wheel available', !backToCenter.aim && !backToCenter.preview
        && (await wheelState()).open && (await wheelState()).selection === -1);
      await touchUp(2); const neutralRelease = await ticks(2);
      c('neutral wheel release never sends an attack', neutralRelease.snapshot.accepted === 0 && neutralRelease.snapshot.rejected === 0);
      await beginWheel(); await touchUp(2); const tappedWheel = await ticks(1);
      c('ordinary central tap opens without firing an unintended skill', !tappedWheel.aim
        && tappedWheel.snapshot.accepted === 0 && tappedWheel.snapshot.rejected === 0);

      await restart(); anchor = await beginJoystick(); await pushJoystick(anchor, 1, 0, .38);
      const selectionOrigin = await beginWheel();
      for (const pattern of [0, 1, 2, 3, 0]) {
        await chooseWheel(pattern); const switched = await ticks(1);
        c(`wheel drag to sector ${pattern + 1} changes preview without submitting`, switched.aim?.pattern === pattern
          && switched.preview?.pattern === pattern && switched.snapshot.accepted === 0 && switched.snapshot.rejected === 0
          && near(switched.aim?.dirX, 1) && near(switched.aim?.dirY, 0) && (await axis()).x > 0);
      }
      // Probe both sides of a quadrant boundary. Eight-degree angular hysteresis
      // should retain a sector near 45 degrees and switch after a clear crossing.
      const selectRadius = Math.hypot((await center('[data-touch-skill="0"]')).x - selectionOrigin.x,
        (await center('[data-touch-skill="0"]')).y - selectionOrigin.y);
      async function wheelAngle(degrees) {
        const angle = degrees * Math.PI / 180;
        await touchMove(2, { x: selectionOrigin.x + Math.sin(angle) * selectRadius,
          y: selectionOrigin.y - Math.cos(angle) * selectRadius });
        return ticks(1);
      }
      c('angular hysteresis keeps top selection near quadrant boundary', (await wheelAngle(50)).aim?.pattern === 0);
      c('clear angular crossing selects right sector', (await wheelAngle(60)).aim?.pattern === 1);
      c('small boundary reversal retains right sector without chatter', (await wheelAngle(45)).aim?.pattern === 1);
      c('clear boundary reversal returns to top sector', (await wheelAngle(30)).aim?.pattern === 0);
      await touchMove(2, selectionOrigin); const wheelRecentred = await ticks(1);
      c('center can cancel then accept another sector in the same gesture', !wheelRecentred.aim && (await wheelState()).open);
      await chooseWheel(0); const finalSector = await ticks(1); await touchUp(2); const switchedRelease = await ticks(1);
      c('multi-sector gesture releases only final skill and charges once', switchedRelease.snapshot.accepted === 1
        && switchedRelease.snapshot.pattern === 0 && switchedRelease.snapshot.manualAim
        && switchedRelease.snapshot.energy >= finalSector.snapshot.energy - 25
        && switchedRelease.snapshot.energy <= finalSector.snapshot.energy - 25 + 1);
      await touchUp(1);

      // No RAF runs between cancellation/owner replacement and the stale
      // physical gesture. Rendering cannot be relied on to clear ownership.
      await restart(); anchor = await beginJoystick(); await pushJoystick(anchor, 1, 0, .38);
      await holdSkill(0); const beforeZeroRafSpace = await inspect();
      await page.keyboard.press('Space'); await chooseWheel(1); await touchUp(2);
      const zeroRafSpace = await inspect();
      c('zero-RAF Space then stale sector move/up cannot recreate cancelled skill', !zeroRafSpace.aim
        && zeroRafSpace.snapshot.tick === beforeZeroRafSpace.snapshot.tick
        && zeroRafSpace.snapshot.energy === beforeZeroRafSpace.snapshot.energy
        && zeroRafSpace.snapshot.accepted === 0 && zeroRafSpace.snapshot.rejected === 0 && (await axis()).x > 0,
      { tickBefore: beforeZeroRafSpace.snapshot.tick, tickAfter: zeroRafSpace.snapshot.tick, aim: zeroRafSpace.aim, axis: await axis() });
      const afterZeroRafSpace = await ticks(3);
      c('zero-RAF wheel cancellation preserves movement and never submits later', afterZeroRafSpace.snapshot.accepted === 0
        && afterZeroRafSpace.snapshot.rejected === 0 && afterZeroRafSpace.snapshot.actors[0].x > zeroRafSpace.snapshot.actors[0].x);
      await touchUp(1);
      await restart(); anchor = await beginJoystick(); await pushJoystick(anchor, 1, 0, .38);
      await holdSkill(0); const beforeZeroRafKey = await inspect();
      await page.keyboard.down('2'); const keyReplacedWheel = await inspect();
      c('zero-RAF keyboard hold immediately replaces wheel owner', keyReplacedWheel.aim?.pattern === 1
        && keyReplacedWheel.snapshot.tick === beforeZeroRafKey.snapshot.tick && keyReplacedWheel.snapshot.accepted === 0);
      await chooseWheel(2); await touchUp(2); const staleWheelAfterKey = await inspect();
      c('zero-RAF old wheel move/up cannot seize or release newer keyboard owner', staleWheelAfterKey.aim?.pattern === 1
        && staleWheelAfterKey.snapshot.tick === beforeZeroRafKey.snapshot.tick
        && staleWheelAfterKey.snapshot.accepted === 0 && staleWheelAfterKey.snapshot.rejected === 0 && (await axis()).x > 0,
      { tickBefore: beforeZeroRafKey.snapshot.tick, tickAfter: staleWheelAfterKey.snapshot.tick, aim: staleWheelAfterKey.aim });
      await page.keyboard.up('2'); const keyReleasePacket = await inspect();
      c('new key owner release remains a zero-tick packet until consumed', !keyReleasePacket.aim
        && keyReleasePacket.snapshot.tick === beforeZeroRafKey.snapshot.tick && keyReleasePacket.snapshot.accepted === 0
        && keyReleasePacket.snapshot.energy === staleWheelAfterKey.snapshot.energy);
      const acceptedKeyOwner = await ticks(1);
      c('only newer key owner releases once after stale wheel gesture', acceptedKeyOwner.snapshot.accepted === 1
        && acceptedKeyOwner.snapshot.rejected === 0 && acceptedKeyOwner.snapshot.pattern === 1
        && acceptedKeyOwner.snapshot.energy >= keyReleasePacket.snapshot.energy - 50
        && acceptedKeyOwner.snapshot.energy <= keyReleasePacket.snapshot.energy - 50 + 1);
      await touchUp(1);

      // All four radial sectors use the same hold/release contract, including
      // a visible resource refusal for the 100-energy skill at round start.
      for (let pattern = 0; pattern < 4; pattern++) {
        await restart(); anchor = await beginJoystick(); await pushJoystick(anchor, 0, -1, .38);
        const before = await inspect(); await holdSkill(pattern);
        const immediate = await inspect(); const heldState = await ticks(5);
        c(`skill ${pattern + 1} touch hold immediately selects without charging`, immediate.aim?.pattern === pattern
          && immediate.snapshot.energy === before.snapshot.energy && immediate.snapshot.accepted === 0
          && heldState.snapshot.accepted === 0 && heldState.snapshot.rejected === 0 && heldState.snapshot.energy >= before.snapshot.energy
          && heldState.preview?.pattern === pattern && near(heldState.aim?.dirX, 0) && near(heldState.aim?.dirY, -1),
        { immediateAim: immediate.aim, aim: heldState.aim, valid: heldState.preview?.valid, reason: heldState.preview?.reason,
          beforeEnergy: before.snapshot.energy, immediateEnergy: immediate.snapshot.energy, heldEnergy: heldState.snapshot.energy,
          immediateAccepted: immediate.snapshot.accepted, accepted: heldState.snapshot.accepted, rejected: heldState.snapshot.rejected,
          wheel: await wheelState(), axis: await axis() });
        if (pattern === 3) c('100-energy skill visibly reports resource refusal during hold', heldState.snapshot.energy < 100
          && heldState.preview?.valid === false && heldState.preview.reason === 1
          && await page.locator('[data-touch-skill="3"]').getAttribute('data-cost') === '100'
          && await page.locator('[data-touch-skill="3"]').getAttribute('data-aim-ready') === 'false');
        const point = await center(`[data-touch-skill="${pattern}"]`);
        await touchMove(2, { x: point.x + 8, y: point.y + 8 });
        const rightMoved = await ticks(3);
        c(`skill ${pattern + 1} right-thumb drift cannot rotate or stop left stick`, directionSame(heldState.aim, rightMoved.aim)
          && rightMoved.snapshot.actors[0].y < heldState.snapshot.actors[0].y && near((await axis()).x, 0) && (await axis()).y < 0);
        await page.keyboard.press('Space'); await touchUp(2); await ticks(2);
        const cancelled = await inspect();
        c(`skill ${pattern + 1} Space cancels release but preserves stick`, !cancelled.aim && !cancelled.preview
          && cancelled.snapshot.accepted === 0 && cancelled.snapshot.rejected === 0 && (await axis()).y < 0,
        { aim: cancelled.aim, preview: cancelled.preview?.pattern, accepted: cancelled.snapshot.accepted,
          rejected: cancelled.snapshot.rejected, axis: await axis(), phase: cancelled.phase });
        await touchUp(1);
      }

      await restart(); anchor = await beginJoystick(); await pushJoystick(anchor, 1, 0, .38);
      await holdSkill(0);
      const heldAim = await ticks(8);
      c('left stick drives stable rightward pre-aim while Boss moves', near(heldAim.aim?.dirX, 1) && near(heldAim.aim?.dirY, 0)
        && near(Math.hypot(heldAim.aim.dirX, heldAim.aim.dirY), 1) && heldAim.preview?.valid === true);
      await ticks(8); const stableAim = await inspect();
      c('unchanged stick direction does not drift from Boss motion', directionSame(heldAim.aim, stableAim.aim));
      await pushJoystick(anchor, 0, -1, .38); const turned = await ticks(2);
      c('actual left-thumb change rotates held skill to movement direction', near(turned.aim?.dirX, 0) && near(turned.aim?.dirY, -1)
        && near(turned.preview?.dirX, turned.aim.dirX) && near(turned.preview?.dirY, turned.aim.dirY));
      await pushJoystick(anchor, 0, 0, 0); const recentred = await ticks(2);
      c('recentering stops motion and keeps chosen skill direction', directionSame(turned.aim, recentred.aim)
        && near((await axis()).x, 0) && near((await axis()).y, 0));
      const candidate = await ticks(1), lockedActor = candidate.snapshot.actors[0];
      await page.screenshot({ path: screenshotPath('preaim') });
      await touchUp(2); const release = await inspect();
      c('right release queues once without deducting at zero tick', !release.aim && release.snapshot.accepted === 0
        && release.snapshot.energy === candidate.snapshot.energy);
      const accepted = await ticks(1);
      c('right release accepts exactly once and spends only skill cost', accepted.snapshot.accepted === 1
        && accepted.snapshot.pattern === 0 && accepted.snapshot.manualAim && accepted.snapshot.attackState === 1
        && near(accepted.snapshot.aimDirX, candidate.aim.dirX) && near(accepted.snapshot.aimDirY, candidate.aim.dirY)
        && accepted.snapshot.energy >= candidate.snapshot.energy - 25 && accepted.snapshot.energy <= candidate.snapshot.energy - 25 + 1,
      { energyBefore: candidate.snapshot.energy, energyAfter: accepted.snapshot.energy, direction: [accepted.snapshot.aimDirX, accepted.snapshot.aimDirY] });
      const shift = accepted.snapshot.startTick - candidate.preview.tick;
      c('accepted C warnings preserve preview geometry and direction', accepted.snapshot.warnings.length === candidate.preview.rays.length
        && candidate.preview.rays.every((ray, i) => {
          const warning = accepted.snapshot.warnings[i];
          return ['x', 'y', 'vx', 'vy', 'radius'].every(field => near(ray[field], warning[field]))
            && warning.spawnTick === ray.spawnTick + shift;
        }), { origin: lockedActor, warningCount: accepted.snapshot.warnings.length });
      await ticks(3); c('holding or releasing other thumb never duplicates attack', (await inspect()).snapshot.accepted === 1);
      await holdSkill(1); const busyWheel = await ticks(1);
      c('wheel can preview busy skill with shared-CD refusal visible', busyWheel.aim?.pattern === 1 && busyWheel.preview?.valid === false
        && busyWheel.preview.reason === 2 && await page.locator('[data-touch-skill="1"]').getAttribute('data-aim-ready') === 'false');
      await touchUp(2); const busyRelease = await ticks(1);
      c('busy wheel release rejects once without energy charge or queued attack', busyRelease.snapshot.accepted === 1
        && busyRelease.snapshot.rejected === 1 && busyRelease.snapshot.energy >= busyWheel.snapshot.energy);
      await touchUp(1);

      await restart(); anchor = await beginJoystick(); await pushJoystick(anchor, 1, 0, .38);
      await holdSkill(0); await ticks(1);
      await touchMove(2, await center('[data-touch="cancel-zone"]'));
      const slid = await ticks(1);
      c('native right-thumb upward slide cancels while left thumb moves', !slid.aim && !slid.preview && slid.snapshot.accepted === 0
        && slid.snapshot.rejected === 0 && (await axis()).x > 0);
      await touchMove(2, await center('[data-touch-skill="0"]')); await touchUp(2); const slideRelease = await ticks(2);
      c('returning cancelled finger to sector then lifting cannot fire', slideRelease.snapshot.accepted === 0 && slideRelease.snapshot.rejected === 0);
      await touchUp(1);
      await restart(); await holdSkill(0); await ticks(1);
      await tap('[data-touch="cancel"]', 3); await touchUp(2); const buttonCancelled = await ticks(2);
      c('native explicit cancel button suppresses later skill release', !buttonCancelled.aim && buttonCancelled.snapshot.accepted === 0
        && buttonCancelled.snapshot.rejected === 0);
      await restart(); await holdSkill(0); await ticks(1);
      await holdSkill(1, 3); const replacement = await ticks(1);
      c('second right finger replaces selection with latest skill', replacement.aim?.pattern === 1 && replacement.snapshot.accepted === 0);
      await touchUp(2); const oldLift = await ticks(1);
      c('replaced skill finger cannot release newer owner', oldLift.aim?.pattern === 1 && oldLift.snapshot.accepted === 0);
      await touchUp(3); const newLift = await ticks(1);
      c('new skill owner releases its own selection once', newLift.snapshot.accepted === 1 && newLift.snapshot.pattern === 1
        && newLift.snapshot.energy >= oldLift.snapshot.energy - 50 && newLift.snapshot.energy <= oldLift.snapshot.energy - 50 + 1);
      await restart(); anchor = await beginJoystick(); await pushJoystick(anchor, 1, 0, .38);
      await holdSkill(0); const preCancel = await ticks(2);
      await touchCancel(); const cancelledTouch = await ticks(3);
      c('native touchCancel clears both owners without delayed firing', !cancelledTouch.aim && !cancelledTouch.preview
        && cancelledTouch.snapshot.accepted === 0 && cancelledTouch.snapshot.rejected === 0
        && distance(preCancel.snapshot.actors[0], cancelledTouch.snapshot.actors[0]) < .001
        && near((await axis()).x, 0) && near((await axis()).y, 0));

      await restart(); anchor = await beginJoystick(); await pushJoystick(anchor, 1, 0, .38);
      await holdSkill(0); await ticks(2);
      await tap('[data-ui="pause"]', 3);
      const paused = await inspect(); await page.clock.runFor(500); const frozen = await inspect();
      c('touch pause clears aim and freezes actual core', paused.phase === 'paused' && !paused.aim && !paused.preview
        && frozen.snapshot.tick === paused.snapshot.tick && frozen.snapshot.energy === paused.snapshot.energy
        && !await page.locator('[data-touch="controls"]').isVisible(),
      { phase: paused.phase, aim: paused.aim, preview: paused.preview?.pattern, tick: paused.snapshot.tick,
        frozenTick: frozen.snapshot.tick, energy: paused.snapshot.energy, frozenEnergy: frozen.snapshot.energy });
      // Physical contacts may still be down after phase capture is cleared.
      // The overlay action must also accept a non-primary third-finger tap.
      await tap('[data-overlay="action"]', 3); const resumed = await ticks(3);
      c('non-primary touch resume cannot catch up or resume stale thumb state', resumed.phase === 'playing'
        && distance(paused.snapshot.actors[0], resumed.snapshot.actors[0]) < .001 && resumed.snapshot.accepted === 0);
      await liftAll(); const releasedAfterPause = await ticks(2);
      c('physical releases after pause/resume cannot deliver cancelled skill', releasedAfterPause.snapshot.accepted === 0
        && distance(resumed.snapshot.actors[0], releasedAfterPause.snapshot.actors[0]) < .001);
      await tap('[data-ui="pause"]');
      c('single-finger pause does not double-toggle through compatibility click', (await inspect()).phase === 'paused');
      await tap('[data-overlay="action"]');
      c('single-finger resume advances only one phase transition', (await inspect()).phase === 'playing');
      anchor = await beginJoystick(); await pushJoystick(anchor, 1, 0, .38);
      await holdSkill(0); await ticks(1);
      await tap('[data-ui="restart"]', 3); const reset = await ticks(1); await liftAll(); const resetLift = await ticks(2);
      c('touch restart clears captured thumb input before stale releases', reset.snapshot.wave === 1 && reset.snapshot.kills === 0
        && reset.snapshot.accepted === 0 && !reset.aim && !reset.preview && resetLift.snapshot.accepted === 0
        && distance(reset.snapshot.actors[0], resetLift.snapshot.actors[0]) < .001);

      await tap('[data-ui="controls-toggle"]'); const manualDesktop = await inspect();
      c('real phone tap manually switches to desktop controls', manualDesktop.controlMode.mode === 'desktop'
        && manualDesktop.controlMode.detectedMode === 'touch' && manualDesktop.controlMode.automatic === false
        && !await page.locator('[data-touch="controls"]').isVisible());
      await tap('[data-ui="controls-toggle"]');
      c('another phone tap restores touch controls', (await inspect()).controlMode.mode === 'touch'
        && await page.locator('[data-touch="controls"]').isVisible());

      await restart(); anchor = await beginJoystick(); await pushJoystick(anchor, 1, 0, .38);
      await holdSkill(0); const beforeResize = await ticks(1);
      const rotated = { width: profile.height, height: profile.width };
      await page.setViewportSize(rotated); await page.clock.runFor(16); const afterResize = await inspect();
      c('orientation change preserves round physics and clears active thumbs', near(afterResize.snapshot.fieldW, beforeResize.snapshot.fieldW)
        && near(afterResize.snapshot.fieldH, beforeResize.snapshot.fieldH) && !afterResize.aim && !afterResize.preview
        && near((await axis()).x, 0) && near((await axis()).y, 0));
      const canvas = await page.locator('#arena canvas').boundingBox();
      const resizedArena = await page.locator('#arena').boundingBox();
      c('rotated canvas fills new arena while original round aspect remains', near(canvas.width, resizedArena.width, 1)
        && near(canvas.height, resizedArena.height, 1) && near(afterResize.snapshot.fieldW / afterResize.snapshot.fieldH,
          beforeResize.snapshot.fieldW / beforeResize.snapshot.fieldH, .00001),
      { canvas, arena: resizedArena, field: [afterResize.snapshot.fieldW, afterResize.snapshot.fieldH] });
      await liftAll(); const refit = await restart();
      const arenaBounds = await page.locator('#arena').boundingBox();
      c('restart after orientation change refits field to new arena', near(refit.snapshot.fieldW / refit.snapshot.fieldH,
        arenaBounds.width / arenaBounds.height, .001) && near(refit.snapshot.fieldW * refit.snapshot.fieldH, 960 * 720, 1));
      await page.screenshot({ path: screenshotPath('rotated') });
    } finally { closing = true; await context.close(); }
  }
  if (online) check('each profile requested only its expected main document', report.documentRequests.length === profiles.length,
  report.documentRequests);
  check('all profiles have zero runtime errors and external/failed/error-status requests', report.errors.length === 0
    && report.externalRequests.length === 0 && report.failedRequests.length === 0 && report.httpErrors.length === 0,
  { errors: report.errors, externalRequests: report.externalRequests, failedRequests: report.failedRequests, httpErrors: report.httpErrors });
  report.passed = true;
} catch (error) { report.failure = error instanceof Error ? error.message : String(error); process.exitCode = 1; }
finally {
  await browser?.close();
  writeFileSync(path.join(output, online ? 'mobile-pages-smoke.json' : 'mobile-browser-smoke.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify({ deliveryType: report.deliveryType, checks: report.checks.length, passed: report.passed, profiles: report.profiles.length,
    errors: report.errors.length, externalRequests: report.externalRequests.length, failedRequests: report.failedRequests.length,
    httpErrors: report.httpErrors.length,
    ...(report.failure ? { failure: report.failure } : {}) }, null, 2));
}
