import { chromium } from '../../web/node_modules/@playwright/test/index.mjs';
import { readFileSync, mkdirSync, writeFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const root = fileURLToPath(new URL('../../', import.meta.url));
const output = path.join(root, 'build/pages-v5');
mkdirSync(output, { recursive: true });
const url = new URL(process.argv[2] ?? 'https://cheng-xiu.github.io/ustc-danmaku/');
url.searchParams.set('qa', '1');
const html = readFileSync(path.resolve(process.argv[3] ?? path.join(root, 'demo/ustc-danmaku.html')));
const expectedHash = createHash('sha256').update(html).digest('hex');
const options = { channel: 'chrome', headless: true };
if (process.env.PAGES_QA_PROXY) options.proxy = { server: process.env.PAGES_QA_PROXY };
const report = { url: url.href, htmlSha256: expectedHash, configVersion: 5, abiVersion: 4,
  method: 'Actual HTTPS page and real mouse/keyboard inputs; read-only QA; controlled time is not an FPS measurement.',
  checks: [], errors: [], failedRequests: [], httpErrors: [], passed: false };
let browser;
function check(name, condition, detail) {
  report.checks.push({ name, pass: Boolean(condition), ...(detail === undefined ? {} : { detail }) });
  if (!condition) throw Error(name + ': ' + JSON.stringify(detail));
}
try {
  browser = await chromium.launch(options);
  report.browser = await browser.version();
  for (const viewport of [{ width: 1440, height: 900 }, { width: 1024, height: 768 }, { width: 390, height: 844 }]) {
    const context = await browser.newContext({ viewport });
    try {
      const page = await context.newPage();
      page.on('pageerror', e => report.errors.push(e.message));
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
      const menu = await inspect();
      check(`${prefix}: current core and four pre-game introductions`, menu.snapshot.configVersion === 5
        && JSON.stringify(menu.snapshot.costs) === '[25,50,20,100]'
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
      await page.keyboard.press('1'); await page.clock.runFor(34);
      const accepted = await inspect();
      check(`${prefix}: ring consumes energy and starts shared CD`, accepted.snapshot.accepted === 1
        && accepted.snapshot.pattern === 0 && accepted.snapshot.energy < keyboard.snapshot.energy - 20
        && await page.locator('[data-ui="cd-text"]').textContent().then(t => t.includes('s')), accepted.snapshot.energy);
      await page.clock.runFor(500);
      const firing = await inspect();
      check(`${prefix}: current ring produces actual bullets`, firing.snapshot.bossBullets >= 24 && firing.snapshot.attackState === 2);
      if (viewport.width === 1440) await page.screenshot({ path: path.join(output, 'playing.png') });
      await page.keyboard.press('Escape'); const paused = await inspect(); await page.clock.runFor(1000);
      const frozen = await inspect();
      check(`${prefix}: pause freezes core and score`, frozen.phase === 'paused' && frozen.snapshot.tick === paused.snapshot.tick
        && frozen.snapshot.energy === paused.snapshot.energy && frozen.snapshot.gpaHundredths === paused.snapshot.gpaHundredths);
      await page.keyboard.press('r');
      const reset = await inspect();
      check(`${prefix}: restart resets round and matches window`, reset.phase === 'playing' && reset.snapshot.tick === 0
        && reset.snapshot.wave === 1 && reset.snapshot.kills === 0 && reset.snapshot.gpaHundredths === 0);
    } finally { await context.close(); }
  }
  check('no runtime errors or failed resources', report.errors.length === 0 && report.failedRequests.length === 0 && report.httpErrors.length === 0);
  report.passed = true;
} catch (error) {
  report.failure = error instanceof Error ? error.message : String(error);
  process.exitCode = 1;
} finally {
  writeFileSync(path.join(output, 'browser-smoke.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify({ passed: report.passed, checks: report.checks.length, failure: report.failure,
    errors: report.errors, failedRequests: report.failedRequests, httpErrors: report.httpErrors }, null, 2));
  await browser?.close();
}
