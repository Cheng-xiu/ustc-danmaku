import { chromium } from '@playwright/test';
import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const directory = fileURLToPath(new URL('../../build/web-validation/', import.meta.url));
const fixture = JSON.parse(readFileSync(directory + 'winning-inputs.json', 'utf8'));
const started = performance.now();
const deadline = started + 170_000;
const report = {
  method: 'Real browser input handlers: WASD and digit keys; controlled RAF; QA reads only.',
  configVersion: fixture.config_version, seedLo: fixture.seed_lo, seedHi: fixture.seed_hi,
  students: fixture.students, expected: fixture.expected, success: false, errors: [],
  inputsReplayed: 0, rafAdvances: 0, zeroTickFrames: 0,
};
const browser = await chromium.launch({ channel: 'chrome', headless: true });
try {
  report.browser = await browser.version();
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
  page.on('pageerror', (error) => report.errors.push(error.message));
  const inspect = () => page.evaluate(() => window.__demo.inspect());
  const requireCondition = (condition, message) => { if (!condition) throw new Error(message); };
  await page.clock.install({ time: new Date('2026-10-07T10:00:00+08:00') });
  await page.goto((process.env.DEMO_URL ?? 'http://127.0.0.1:4173/') + '?qa=1');
  await page.waitForFunction(() => window.__demo, null, { timeout: 15_000 });
  await page.clock.pauseAt(await page.evaluate(() => Date.now() + 1000));
  await page.locator('[data-overlay="action"]').click({ force: true });
  // Removing the menu exposes the canvas beneath the click. Leave it before
  // the first logical tick, so mouse steering cannot override the WASD fixture.
  await page.mouse.move(1439, 999);
  let state = await inspect();
  requireCondition(state.phase === 'playing' && state.snapshot.tick === 0,
    `Start must remain at tick 0; got ${state.phase}/${state.snapshot.tick}`);
  requireCondition(state.snapshot.configVersion === fixture.config_version, 'Config version differs from winning fixture.');
  requireCondition(state.snapshot.seed === String(fixture.seed_lo), 'Seed differs from winning fixture.');
  requireCondition(state.snapshot.actors.length - 1 === fixture.students, 'Student count differs from winning fixture.');

  let held = new Set();
  for (const row of fixture.inputs) {
    requireCondition(performance.now() < deadline, 'Browser replay reached the 170-second time budget.');
    requireCondition(state.phase === 'playing' && state.snapshot.tick === row.tick,
      `Input tick ${row.tick} has state ${state.phase}/${state.snapshot.tick}.`);
    requireCondition(!row.pointer_valid, 'Fixture requires pointer input; keyboard-only replay cannot reproduce it.');
    const desired = new Set();
    if (row.move_x < -0.1) desired.add('a');
    if (row.move_x > 0.1) desired.add('d');
    if (row.move_y < -0.1) desired.add('w');
    if (row.move_y > 0.1) desired.add('s');
    const divisor = desired.size > 1 ? Math.SQRT2 : 1;
    const actualX = (Number(desired.has('d')) - Number(desired.has('a'))) / divisor;
    const actualY = (Number(desired.has('s')) - Number(desired.has('w'))) / divisor;
    requireCondition(Math.abs(actualX - row.move_x) < 1e-6 && Math.abs(actualY - row.move_y) < 1e-6,
      `Input tick ${row.tick} is not reproducible using normalized WASD: ${row.move_x}/${row.move_y}.`);
    for (const key of held) if (!desired.has(key)) await page.keyboard.up(key);
    for (const key of desired) if (!held.has(key)) await page.keyboard.down(key);
    held = desired;
    for (let pattern = 0; pattern < 4; pattern++) {
      if (row.attack_mask & (1 << pattern)) await page.keyboard.press(String(pattern + 1));
    }
    for (let attempts = 0; ; attempts++) {
      requireCondition(attempts < 5, `Input tick ${row.tick} did not advance after five RAF intervals.`);
      await page.clock.runFor(16);
      report.rafAdvances++;
      state = await inspect();
      if (state.snapshot.tick === row.tick) {
        report.zeroTickFrames++;
        requireCondition(state.phase === 'playing', `Replay paused before consuming tick ${row.tick}: ${state.reason}`);
        continue;
      }
      requireCondition(state.snapshot.tick === row.tick + 1,
        `Input ${row.tick} advanced to ${state.snapshot.tick}; more than one tick consumed.`);
      break;
    }
    report.inputsReplayed++;
  }
  report.actual = {
    phase: state.phase, status: state.snapshot.status, tick: state.snapshot.tick,
    boss_hp: state.snapshot.actors[0].hp, boss_hits: state.snapshot.bossHits,
    student_hits: state.snapshot.studentHits, accepted: state.snapshot.accepted,
    rejected: state.snapshot.rejected, studentsAlive: state.snapshot.actors.slice(1).filter((actor) => actor.alive).length,
    hudTitle: await page.locator('[data-overlay="title"]').textContent(),
  };
  for (const [key, expected] of Object.entries(fixture.expected)) {
    requireCondition(report.actual[key] === expected, `${key}: expected ${expected}, actual ${report.actual[key]}.`);
  }
  requireCondition(state.phase === 'over' && report.actual.studentsAlive === 0, 'Victory must be displayed with all students down.');
  requireCondition(report.errors.length === 0, 'Browser reported uncaught errors.');
  await page.screenshot({ path: directory + 'victory.png' });
  report.success = true;
} catch (error) {
  report.failure = error instanceof Error ? error.message : String(error);
  process.exitCode = 1;
} finally {
  report.elapsedSeconds = (performance.now() - started) / 1000;
  writeFileSync(directory + 'winning-browser-result.json', JSON.stringify(report, null, 2));
  console.log(JSON.stringify(report, null, 2));
  await browser.close();
}
