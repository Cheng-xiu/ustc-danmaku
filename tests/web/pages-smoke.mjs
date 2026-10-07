import { chromium } from '@playwright/test';
import { readFileSync, mkdirSync, writeFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const root = fileURLToPath(new URL('../../', import.meta.url));
const output = path.join(root, 'build/pages-v6');
mkdirSync(output, { recursive: true });
const url = new URL(process.argv[2] ?? 'https://cheng-xiu.github.io/ustc-danmaku/');
url.searchParams.set('qa', '1');
const html = readFileSync(path.resolve(process.argv[3] ?? path.join(root, 'demo/ustc-danmaku.html')));
const expectedHash = createHash('sha256').update(html).digest('hex');
const options = { channel: 'chrome', headless: true };
if (process.env.PAGES_QA_PROXY) options.proxy = { server: process.env.PAGES_QA_PROXY };
const report = { url: url.href, htmlSha256: expectedHash, configVersion: 6, abiVersion: 5,
  abiEvidence: 'The bundled client requires ABI5; the online HTML must match the tested local artifact byte for byte and expose manual-aim snapshots and C previews.',
  method: 'Actual HTTPS page and real mouse/keyboard inputs; read-only QA snapshots; clock installed and paused before navigation. No OS blur is simulated. Controlled time is not an FPS measurement.',
  checks: [], errors: [], failedRequests: [], httpErrors: [], passed: false };
const startTime = performance.now(), deadline = startTime + 120000;
let browser;
function check(name, condition, detail) {
  report.checks.push({ name, pass: Boolean(condition), ...(detail === undefined ? {} : { detail }) });
  if (!condition) throw Error(name + ': ' + JSON.stringify(detail));
}
function budget() { if (performance.now() > deadline) throw Error('Pages v6 validation exceeded 120-second wall-time budget.'); }
const near = (a, b, tolerance = .001) => Number.isFinite(a) && Number.isFinite(b) && Math.abs(a - b) < tolerance;
try {
  browser = await chromium.launch(options);
  report.browser = await browser.version();
  for (const viewport of [{ width: 1440, height: 900 }, { width: 1024, height: 768 }, { width: 390, height: 844 }]) {
    budget();
    const context = await browser.newContext({ viewport });
    try {
      const page = await context.newPage();
      page.on('pageerror', e => report.errors.push({ viewport: viewport.width, error: e.message, stack: e.stack }));
      page.on('requestfailed', r => report.failedRequests.push({ url: r.url(), error: r.failure()?.errorText }));
      page.on('response', r => { if (r.status() >= 400) report.httpErrors.push({ url: r.url(), status: r.status() }); });
      const time = new Date('2026-10-07T10:00:00+08:00');
      await page.clock.install({ time });
      await page.clock.pauseAt(new Date(time.getTime() + 1000));
      const response = await page.goto(url.href, { waitUntil: 'load', timeout: 30000 });
      const prefix = `${viewport.width}x${viewport.height}`;
      check(`${prefix}: HTTPS200`, response?.status() === 200 && page.url().startsWith('https:'));
      check(`${prefix}: online HTML matches tested artifact`, createHash('sha256').update(await response.body()).digest('hex') === expectedHash);
      await page.waitForFunction(() => window.__demo, null, { timeout: 30000, polling: 50 });
      const inspect = () => page.evaluate(() => window.__demo.inspect());
      async function ticks(count = 1) {
        const target = (await inspect()).snapshot.tick + count;
        let current = await inspect(), frames = 0;
        while (current.snapshot.tick < target) {
          budget();
          if (current.phase !== 'playing') throw Error(`${prefix}: logical advance stopped in ${current.phase}: ${current.reason}`);
          if (frames++ > count * 3 + 6) throw Error(`${prefix}: logical tick did not advance under controlled RAF time`);
          // The first RAF after a phase change establishes its baseline. Read
          // actual ticks rather than assuming that every clock call advances.
          await page.clock.runFor(16); current = await inspect();
          if (current.snapshot.tick > target) throw Error(`${prefix}: overshot logical boundary ${target}`);
        }
        return current;
      }
      const menu = await inspect();
      check(`${prefix}: current core and four pre-game introductions`, menu.snapshot.configVersion === 6
        && JSON.stringify(menu.snapshot.costs) === '[25,50,20,100]'
        && 'aim' in menu && 'preview' in menu
        && await page.locator('.demo-skill-guide').count() === 4
        && await page.locator('[data-overlay="skills"]').isVisible());
      if (viewport.width === 1440) await page.screenshot({ path: path.join(output, 'menu.png') });
      await page.locator('[data-overlay="action"]').click();
      await page.clock.runFor(100);
      const started = await inspect();
      check(`${prefix}: real game starts and hides skill guide`, started.phase === 'playing' && started.snapshot.tick > 0
        && started.snapshot.actors.length === 4 && !await page.locator('[data-overlay="skills"]').isVisible());
      const layout = await page.evaluate(() => {
        const arena = document.querySelector('#arena').getBoundingClientRect();
        const hud = document.querySelector('#hud').getBoundingClientRect();
        const canvas = document.querySelector('#arena canvas').getBoundingClientRect();
        return { width: arena.width, height: arena.height, top: arena.top, bottom: arena.bottom,
          hudBottom: hud.bottom, canvasWidth: canvas.width, canvasHeight: canvas.height, viewport: innerHeight };
      });
      check(`${prefix}: top bars and remaining window form the field`, Math.abs(layout.width - viewport.width) < 1
        && Math.abs(layout.top - layout.hudBottom) < 1 && Math.abs(layout.bottom - layout.viewport) < 1
        && Math.abs(started.snapshot.fieldW / started.snapshot.fieldH - layout.width / layout.height) < 0.001
        && await page.locator('[data-ui="energy-track"]').isVisible() && await page.locator('[data-ui="cd-track"]').isVisible(), layout);
      await page.mouse.move(viewport.width / 2, 30);
      const beforeMouse = await inspect(); await page.clock.runFor(200);
      const afterMouse = await inspect();
      check(`${prefix}: actual mouse in HUD still steers upward`, afterMouse.snapshot.actors[0].y < beforeMouse.snapshot.actors[0].y - 10);
      await page.keyboard.down('ArrowRight'); await page.clock.runFor(100); await page.keyboard.up('ArrowRight');
      const keyboard = await inspect();
      check(`${prefix}: keyboard overrides HUD pointer`, keyboard.snapshot.actors[0].x > afterMouse.snapshot.actors[0].x + 10
        && Math.abs(keyboard.snapshot.actors[0].y - afterMouse.snapshot.actors[0].y) < 0.1);

      await page.keyboard.down('1');
      const immediate = await inspect();
      check(`${prefix}: pressing ring immediately selects pre-aim without charging`, immediate.aim?.pattern === 0
        && immediate.snapshot.accepted === 0 && immediate.snapshot.energy === keyboard.snapshot.energy);
      const holding = await ticks(12);
      check(`${prefix}: held ring exposes C preview without accepting or charging`, holding.aim?.pattern === 0
        && holding.preview?.pattern === 0 && holding.preview.valid && holding.preview.rays.length > 0
        && near(holding.preview.dirX, holding.aim.dirX) && near(holding.preview.dirY, holding.aim.dirY)
        && near(holding.preview.originX, holding.snapshot.actors[0].x) && near(holding.preview.originY, holding.snapshot.actors[0].y)
        && holding.snapshot.accepted === 0 && holding.snapshot.rejected === 0 && holding.snapshot.attackState === 0
        && holding.snapshot.energy >= keyboard.snapshot.energy && holding.snapshot.warnings.length === 0,
      { energy: holding.snapshot.energy, rayCount: holding.preview?.rays.length, aim: holding.aim });
      if (viewport.width === 1440) await page.screenshot({ path: path.join(output, 'preaim.png') });
      await page.keyboard.press('Space'); await page.keyboard.up('1');
      const cancelled = await ticks(2);
      check(`${prefix}: Space then keyup cancels without submitting an attack`, !cancelled.aim && !cancelled.preview
        && cancelled.snapshot.accepted === 0 && cancelled.snapshot.rejected === 0 && cancelled.snapshot.attackState === 0);

      await page.keyboard.down('1'); const candidate = await ticks(1);
      check(`${prefix}: a new hold has a ready C preview`, candidate.aim?.pattern === 0 && candidate.preview?.valid === true
        && candidate.preview.rays.length > 0);
      await page.keyboard.up('1'); const released = await inspect();
      check(`${prefix}: release edge cannot deduct before a logical tick`, !released.aim && released.snapshot.accepted === 0
        && released.snapshot.energy === candidate.snapshot.energy);
      await ticks(1);
      const accepted = await inspect();
      check(`${prefix}: manual ring release charges once and matches the preview direction`, accepted.snapshot.accepted === 1
        && accepted.snapshot.pattern === 0 && accepted.snapshot.manualAim === true && accepted.snapshot.attackState === 1
        && near(accepted.snapshot.aimDirX, candidate.preview.dirX) && near(accepted.snapshot.aimDirY, candidate.preview.dirY)
        && accepted.snapshot.energy >= candidate.snapshot.energy - candidate.snapshot.costs[0]
        && accepted.snapshot.energy <= candidate.snapshot.energy - candidate.snapshot.costs[0] + 1
        && !accepted.aim && !accepted.preview
        && await page.locator('[data-ui="cd-text"]').textContent().then(t => t.includes('s')),
      { energyBefore: candidate.snapshot.energy, energyAfter: accepted.snapshot.energy, direction: [accepted.snapshot.aimDirX, accepted.snapshot.aimDirY] });
      const warnings = accepted.snapshot.warnings, rays = candidate.preview.rays;
      const tickShift = accepted.snapshot.startTick - candidate.preview.tick;
      check(`${prefix}: accepted warnings preserve all C candidate rays`, warnings.length === rays.length && rays.every((ray, index) => {
        const warning = warnings[index];
        return ['x', 'y', 'vx', 'vy', 'radius'].every(field => near(ray[field], warning[field]))
          && warning.pattern === ray.pattern && warning.wave === ray.wave && warning.spawnTick === ray.spawnTick + tickShift;
      }), { candidateRays: rays.length, warningRays: warnings.length, tickShift });
      const firstSpawn = Math.min(...warnings.map(ray => ray.spawnTick));
      await ticks(firstSpawn + 1 - accepted.snapshot.tick);
      const firing = await inspect();
      const firstRays = warnings.filter(ray => ray.spawnTick === firstSpawn);
      const matchingBullet = firing.snapshot.bullets.some(bullet => bullet.faction === 1 && bullet.pattern === 0
        && firstRays.some(ray => near(bullet.vx, ray.vx) && near(bullet.vy, ray.vy) && near(bullet.radius, ray.radius)
          && near(bullet.x, ray.x + ray.vx / 60, .01) && near(bullet.y, ray.y + ray.vy / 60, .01)));
      check(`${prefix}: manual ring emits actual bullets along its locked rays`, firing.snapshot.bossBullets >= 24
        && firing.snapshot.attackState === 2 && firing.snapshot.accepted === 1 && matchingBullet,
      { tick: firing.snapshot.tick, firstSpawn, emitted: firing.snapshot.bossBullets });
      if (viewport.width === 1440) await page.screenshot({ path: path.join(output, 'playing.png') });
      await page.keyboard.press('Escape'); const paused = await inspect(); await page.clock.runFor(1000);
      const frozen = await inspect();
      check(`${prefix}: pause freezes core and score`, frozen.phase === 'paused' && frozen.snapshot.tick === paused.snapshot.tick
        && frozen.snapshot.energy === paused.snapshot.energy && frozen.snapshot.gpaHundredths === paused.snapshot.gpaHundredths);
      await page.keyboard.press('r');
      const reset = await inspect();
      check(`${prefix}: restart resets round and matches window`, reset.phase === 'playing' && reset.snapshot.tick === 0
        && reset.snapshot.wave === 1 && reset.snapshot.kills === 0 && reset.snapshot.gpaHundredths === 0
        && reset.snapshot.accepted === 0 && reset.snapshot.rejected === 0 && !reset.aim && !reset.preview);
    } finally { await context.close(); }
  }
  check('no runtime errors or failed resources', report.errors.length === 0 && report.failedRequests.length === 0 && report.httpErrors.length === 0);
  report.passed = true;
} catch (error) {
  report.failure = error instanceof Error ? error.message : String(error);
  process.exitCode = 1;
} finally {
  report.elapsedSeconds = (performance.now() - startTime) / 1000;
  writeFileSync(path.join(output, 'browser-smoke.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify({ passed: report.passed, checks: report.checks.length, failure: report.failure,
    errors: report.errors, failedRequests: report.failedRequests, httpErrors: report.httpErrors }, null, 2));
  await browser?.close();
}
