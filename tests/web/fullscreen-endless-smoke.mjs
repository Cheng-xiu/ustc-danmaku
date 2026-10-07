import { chromium } from '@playwright/test';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { fileURLToPath, pathToFileURL } from 'node:url';
import path from 'node:path';

const root = fileURLToPath(new URL('../../', import.meta.url));
const output = path.join(root, 'build/web-validation');
mkdirSync(output, { recursive: true });
const htmlPath = path.resolve(process.argv[2] ?? path.join(root, '../../outputs/ustc-danmaku-endless-v5.html'));
const started = performance.now(), deadline = started + 120000;
const hash = () => createHash('sha256').update(readFileSync(htmlPath)).digest('hex');
const report = {
  method: 'Actual offline cfg5/ABI4 single HTML, real WASD and Digit2 browser input. One logical tick observed at a time; movement reconsidered every 6 ticks using only current public actors and student bullets. Clock installed and paused before navigation. No state injection, RNG, planned rays, copied-world rollout or native-input fixture.',
  controller: 'chase current nearest student at 150px; eight legal directions plus stop; 0.45s visible-bullet risk, weight4; current-body boundary penalty; course-only requests every12ticks when C available[] permits.',
  htmlPath, htmlSha256: hash(), configVersion: 5, abiVersion: 4,
  viewport: { width: 1440, height: 900 }, checks: [], controls: [],
  errors: [], externalRequests: [], rafAdvances: 0, zeroTickFrames: 0,
  logicalTicksObserved: 0, previewSnapshotsObserved: 0, passed: false,
};
let browser, context, state, held = new Set();
function check(name, condition, detail) {
  report.checks.push({ name, pass: Boolean(condition), ...(detail === undefined ? {} : { detail }) });
  if (!condition) throw Error(`${name}: ${JSON.stringify(detail)}`);
}
function requireCondition(condition, message) { if (!condition) throw Error(message); }
function budget() { requireCondition(performance.now() < deadline, '120-second wall-time budget exhausted.'); }
const gpa = kills => Math.floor(430 * kills / (kills + 20));
const dirs = [[0, 0], [1, 0], [Math.SQRT1_2, Math.SQRT1_2], [0, 1], [-Math.SQRT1_2, Math.SQRT1_2],
  [-1, 0], [-Math.SQRT1_2, -Math.SQRT1_2], [0, -1], [Math.SQRT1_2, -Math.SQRT1_2]];
function direction(snapshot) {
  const boss = snapshot.actors[0];
  let nearest = null, distance = Infinity;
  for (const student of snapshot.actors.slice(1)) {
    if (!student.alive) continue;
    const d = Math.hypot(student.x - boss.x, student.y - boss.y);
    if (d < distance) { nearest = student; distance = d; }
  }
  if (!nearest) return [0, 0];
  const sign = distance > 158 ? 1 : distance < 142 ? -1 : 0;
  const wanted = distance > 0 ? [sign * (nearest.x - boss.x) / distance,
    sign * (nearest.y - boss.y) / distance] : [0, 0];
  let best = Infinity, chosen = dirs[0];
  for (const movement of dirs) {
    const [mx, my] = movement;
    let risk = 0;
    for (const bullet of snapshot.bullets) {
      if (bullet.faction !== 2 || bullet.x < 0 || bullet.x > snapshot.fieldW
        || bullet.y < 0 || bullet.y > snapshot.fieldH) continue;
      const rx = bullet.x - boss.x, ry = bullet.y - boss.y;
      const vx = bullet.vx - mx * 270, vy = bullet.vy - my * 270;
      const speedSquared = vx * vx + vy * vy;
      const t = Math.max(0, Math.min(.45, speedSquared > 0 ? -(rx * vx + ry * vy) / speedSquared : 0));
      const closest = Math.hypot(rx + vx * t, ry + vy * t), safe = boss.radius + bullet.radius + 10;
      if (closest < safe) risk += 1 + (safe - closest) / safe;
    }
    let score = (mx - wanted[0]) ** 2 + (my - wanted[1]) ** 2 + 4 * risk;
    const x = boss.x + mx * 270 * .35, y = boss.y + my * 270 * .35;
    if (x < boss.radius || x > snapshot.fieldW - boss.radius || y < boss.radius || y > snapshot.fieldH - boss.radius) score += 8;
    if (score < best) { best = score; chosen = movement; }
  }
  return chosen;
}
function summarize(snapshot) {
  return { tick: snapshot.tick, status: snapshot.status, wave: snapshot.wave, wavePhase: snapshot.wavePhase,
    wavesCleared: snapshot.wavesCleared, kills: snapshot.kills, gpaHundredths: snapshot.gpaHundredths,
    hp: snapshot.actors[0].hp, energy: snapshot.energy, accepted: snapshot.accepted,
    rejected: snapshot.rejected, studentHits: snapshot.studentHits, fieldW: snapshot.fieldW, fieldH: snapshot.fieldH };
}

try {
  browser = await chromium.launch({ channel: 'chrome', headless: true });
  report.browser = await browser.version();
  context = await browser.newContext({ viewport: report.viewport, offline: true });
  const page = await context.newPage();
  page.on('pageerror', error => report.errors.push(error.message));
  page.on('request', request => { if (/^https?:/.test(request.url())) report.externalRequests.push(request.url()); });
  const clockTime = new Date('2026-10-07T10:00:00+08:00');
  await page.clock.install({ time: clockTime });
  await page.clock.pauseAt(new Date(clockTime.getTime() + 1000));
  const url = pathToFileURL(htmlPath); url.searchParams.set('qa', '1'); report.url = url.href;
  await page.goto(url.href);
  await page.waitForFunction(() => window.__demo, null, { timeout: 30000, polling: 50 });
  // Copy only the current public display fields; timings and future warning
  // geometry are not inputs to this controller.
  const inspect = () => page.evaluate(() => {
    const view = window.__demo.inspect(), s = view.snapshot;
    return { phase: view.phase, reason: view.reason, snapshot: {
      tick: s.tick, status: s.status, configVersion: s.configVersion, fieldW: s.fieldW, fieldH: s.fieldH,
      wave: s.wave, wavesCleared: s.wavesCleared, wavePhase: s.wavePhase,
      nextWaveStudents: s.nextWaveStudents, waveSpawnTick: s.waveSpawnTick, spawnPreview: s.spawnPreview,
      kills: s.kills, gpaHundredths: s.gpaHundredths, energy: s.energy, available: s.available,
      accepted: s.accepted, rejected: s.rejected, studentHits: s.studentHits,
      actors: s.actors, bullets: s.bullets.filter(bullet => bullet.faction === 2),
    } };
  });
  async function applyMovement(movement) {
    const desired = new Set();
    if (movement[0] > .1) desired.add('d'); if (movement[0] < -.1) desired.add('a');
    if (movement[1] > .1) desired.add('s'); if (movement[1] < -.1) desired.add('w');
    for (const key of held) if (!desired.has(key)) await page.keyboard.up(key);
    for (const key of desired) if (!held.has(key)) await page.keyboard.down(key);
    held = desired;
  }
  async function oneTick() {
    const before = state.snapshot.tick;
    for (let attempts = 0; attempts < 5; ++attempts) {
      budget(); await page.clock.runFor(16); report.rafAdvances++;
      state = await inspect();
      requireCondition(state.phase === 'playing', `Stopped at tick ${state.snapshot.tick}: ${state.phase}, HP ${state.snapshot.actors[0].hp}, ${state.reason}`);
      if (state.snapshot.tick === before) { report.zeroTickFrames++; continue; }
      requireCondition(state.snapshot.tick === before + 1, `Expected one tick after ${before}, got ${state.snapshot.tick}.`);
      report.logicalTicksObserved++;
      requireCondition(state.snapshot.gpaHundredths === gpa(state.snapshot.kills), `GPA differs from kill-only formula at tick ${state.snapshot.tick}.`);
      return;
    }
    throw Error(`Tick ${before} did not advance after five RAF intervals.`);
  }
  state = await inspect();
  check('offline actual cfg5 core loads through strict ABI4 client', state.snapshot.configVersion === 5);
  await page.locator('[data-overlay="action"]').click();
  state = await inspect();
  check('real start is playing tick0 with three students and zero GPA', state.phase === 'playing'
    && state.snapshot.tick === 0 && state.snapshot.actors.length === 4 && state.snapshot.kills === 0 && state.snapshot.gpaHundredths === 0);
  const arena = await page.locator('#arena').boundingBox();
  check('current round matches window battlefield aspect and fixed area',
    Math.abs(state.snapshot.fieldW / state.snapshot.fieldH - arena.width / arena.height) < .002
    && Math.abs(state.snapshot.fieldW * state.snapshot.fieldH - 960 * 720) < 1,
    { arena, fieldW: state.snapshot.fieldW, fieldH: state.snapshot.fieldH });
  report.initial = summarize(state.snapshot);
  let lastRequest = -12, lastMovementDecision = -6;
  while (state.snapshot.wavePhase === 0 && state.snapshot.tick < 3600) {
    budget();
    if (state.snapshot.tick - lastMovementDecision >= 6) {
      const movement = direction(state.snapshot); await applyMovement(movement);
      report.controls.push({ tick: state.snapshot.tick, moveX: movement[0], moveY: movement[1], attack: null });
      lastMovementDecision = state.snapshot.tick;
    }
    if (state.snapshot.tick - lastRequest >= 12 && state.snapshot.available[1]) {
      await page.keyboard.press('2'); lastRequest = state.snapshot.tick;
      report.controls.push({ tick: state.snapshot.tick, attack: 2 });
    }
    await oneTick();
  }
  check('first wave truly clears and remains playing without victory', state.phase === 'playing'
    && state.snapshot.status === 0 && state.snapshot.wavePhase === 1 && state.snapshot.wave === 1
    && state.snapshot.wavesCleared === 1 && state.snapshot.kills === 3, summarize(state.snapshot));
  report.clear = summarize(state.snapshot);
  check('clear GPA is floor(430*3/23)=56', state.snapshot.gpaHundredths === 56);
  const clearTick = state.snapshot.tick, due = state.snapshot.waveSpawnTick;
  const markers = state.snapshot.spawnPreview.map(point => ({ ...point }));
  check('clear publishes four fixed markers with exact120tick due time',
    state.snapshot.nextWaveStudents === 4 && markers.length === 4 && due - clearTick === 120,
    { clearTick, due, markers });
  check('clear does not show death or victory overlay', await page.locator('.demo-status-overlay').isHidden());
  await applyMovement([0, 0]);
  await page.screenshot({ path: path.join(output, 'v5-endless-preview.png') });
  report.previewSnapshotsObserved = 1;
  while (state.snapshot.tick < due - 1) {
    await oneTick();
    requireCondition(state.snapshot.wavePhase === 1 && state.snapshot.wave === 1
      && state.snapshot.nextWaveStudents === 4 && state.snapshot.waveSpawnTick === due
      && JSON.stringify(state.snapshot.spawnPreview) === JSON.stringify(markers),
    `Public preview changed before birth at tick ${state.snapshot.tick}.`);
    report.previewSnapshotsObserved++;
  }
  check('all120preview snapshots retain exact markers through one tick before birth',
    report.previewSnapshotsObserved === 120 && state.snapshot.tick === due - 1 && state.snapshot.wavePhase === 1,
    { observed: report.previewSnapshotsObserved, tick: state.snapshot.tick, due });
  await oneTick();
  const students = state.snapshot.actors.slice(1);
  check('four students spawn on precisely tick clear+120', state.snapshot.tick === due
    && state.snapshot.wave === 2 && state.snapshot.wavePhase === 0 && students.length === 4 && students.every(student => student.alive));
  check('real student birth origins equal all four public markers',
    JSON.stringify(students.map(({ x, y }) => ({ x, y }))) === JSON.stringify(markers), { students, markers });
  check('birth retains cumulative kills and kill-only GPA', state.snapshot.kills === 3 && state.snapshot.gpaHundredths === 56);
  report.birth = summarize(state.snapshot);
  for (let i = 0; i < 30; ++i) await oneTick();
  check('second wave continues playing for30ticks', state.phase === 'playing' && state.snapshot.status === 0
    && state.snapshot.wave === 2 && state.snapshot.tick === due + 30);
  await page.screenshot({ path: path.join(output, 'v5-endless-wave2.png') });
  check('HUD shows current kill-only GPA', await page.locator('[data-ui="gpa"]').textContent() === '0.56');
  report.final = summarize(state.snapshot);
  await page.keyboard.press('r'); state = await inspect();
  check('actual R restart clears score kills and waves', state.phase === 'playing' && state.snapshot.tick === 0
    && state.snapshot.wave === 1 && state.snapshot.wavesCleared === 0 && state.snapshot.kills === 0
    && state.snapshot.gpaHundredths === 0 && state.snapshot.actors.length === 4);
  check('HTML did not change during the validation', hash() === report.htmlSha256);
  check('offline HTML makes zero external network requests', report.externalRequests.length === 0, report.externalRequests);
  check('browser reports zero runtime errors', report.errors.length === 0, report.errors);
  report.passed = true;
} catch (error) {
  report.failure = error instanceof Error ? error.message : String(error);
  if (state) report.failedState = summarize(state.snapshot);
  process.exitCode = 1;
} finally {
  await context?.close();
  await browser?.close();
  report.elapsedSeconds = (performance.now() - started) / 1000;
  writeFileSync(path.join(output, 'v5-browser-endless.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify({ ...report, controls: `${report.controls.length} decisions saved in v5-browser-endless.json` }, null, 2));
}
