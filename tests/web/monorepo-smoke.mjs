import { chromium } from '@playwright/test';
import { spawn, spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import net from 'node:net';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../../', import.meta.url));
const stamp = new Date().toISOString().replace(/[:.]/g, '-');
const packageOutput = path.resolve(process.argv[2] ?? path.join(root, 'build/monorepo-package', stamp));
const packageRoot = path.join(packageOutput, 'ustc-danmaku-endless-v6');
const output = path.join(root, 'build/monorepo-http-smoke.json');
const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
const report = {
  method: 'Actual root npm dev/preview, repository static server, and a freshly built downloadable package. Real browser navigation, keyboard and mouse input, C/Wasm snapshots and resource hashes. Controlled RAF clock is not a performance measurement. No core/world state or RNG injection.',
  configVersion: 6, abiVersion: 5, startedAt: new Date().toISOString(),
  checks: [], servers: [], startupLayouts: [], package: { root: packageRoot }, passed: false,
};
const children = new Set();
let browser;
function check(name, condition, detail) {
  report.checks.push({ name, passed: Boolean(condition), ...(detail === undefined ? {} : { detail }) });
  if (!condition) throw Error(name + ': ' + JSON.stringify(detail));
}
const near = (a, b, tolerance = .001) => Number.isFinite(a) && Number.isFinite(b) && Math.abs(a - b) <= tolerance;
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
async function withTimeout(promise, ms, message) {
  let timer;
  try {
    return await Promise.race([promise, new Promise((_, reject) => {
      timer = setTimeout(() => reject(Error(message)), ms);
    })]);
  } finally { clearTimeout(timer); }
}
function nodeTask(args, cwd = root) {
  const child = spawn(process.execPath, args, { cwd, windowsHide: true,
    detached: process.platform !== 'win32', stdio: ['ignore', 'pipe', 'pipe'] });
  const job = { child, log: '', error: null, closed: null };
  child.stdout.on('data', chunk => { job.log = (job.log + chunk).slice(-32768); });
  child.stderr.on('data', chunk => { job.log = (job.log + chunk).slice(-32768); });
  child.on('error', error => { job.error = error; });
  job.closed = new Promise(resolve => child.on('close', code => resolve(code)));
  children.add(job);
  return job;
}
async function stop(job) {
  if (!job || !children.has(job)) return;
  if (job.child.exitCode === null && job.child.pid) {
    if (process.platform === 'win32') {
      // Only terminate the process tree started by this test.
      const killed = spawnSync('taskkill', ['/PID', String(job.child.pid), '/T', '/F'],
        { windowsHide: true, stdio: 'ignore' });
      if (killed.status !== 0) job.child.kill();
    } else {
      try { process.kill(-job.child.pid, 'SIGTERM'); } catch { job.child.kill(); }
    }
    const ended = await withTimeout(job.closed.then(() => true), 5000, 'Process did not stop').catch(() => false);
    if (!ended) {
      try { process.kill(-job.child.pid, 'SIGKILL'); } catch { job.child.kill('SIGKILL'); }
    }
  }
  children.delete(job);
}
async function runTask(args, label, timeout = 120000) {
  const job = nodeTask(args);
  try {
    const result = await withTimeout(job.closed, timeout, label + ' timed out');
    if (job.error) throw job.error;
    if (result !== 0) throw Error(`${label} failed (${result}): ${job.log}`);
    return job.log;
  } finally { await stop(job); }
}
async function requireFreePort(port) {
  await new Promise((resolve, reject) => {
    const probe = net.createServer();
    probe.once('error', () => reject(Error(`Port ${port} is in use. Stop its owner before running this smoke test.`)));
    probe.listen(port, '127.0.0.1', () => probe.close(resolve));
  });
}
async function ready(job, url) {
  const deadline = Date.now() + 30000;
  while (Date.now() < deadline) {
    if (job.error) throw job.error;
    if (job.child.exitCode !== null) throw Error('HTTP server exited: ' + job.log);
    try {
      const response = await fetch(url, { signal: AbortSignal.timeout(1000) });
      if (response.ok) return;
    } catch { /* Wait for the owned server to begin listening. */ }
    await sleep(100);
  }
  throw Error('HTTP server did not listen at ' + url + ': ' + job.log);
}
function npmCli() {
  const candidates = [process.env.npm_execpath,
    path.join(path.dirname(process.execPath), 'node_modules/npm/bin/npm-cli.js'),
    path.resolve(path.dirname(process.execPath), '../lib/node_modules/npm/bin/npm-cli.js')];
  const cli = candidates.find(candidate => candidate && existsSync(candidate));
  if (!cli) throw Error('Cannot locate npm-cli.js. Run this test via the root npm test:monorepo entry.');
  return cli;
}
async function startupLayoutChecks() {
  const buildRoot = path.join(root, 'build');
  mkdirSync(buildRoot, { recursive: true });
  const fixtureRoot = mkdtempSync(path.join(buildRoot, 'startup-layout-'));
  const source = path.join(fixtureRoot, 'checkout'), download = path.join(fixtureRoot, 'download');
  const record = { root: fixtureRoot, retainedForInspection: true,
    method: 'Copied real static server and launcher run in fresh independent layouts. Plain HTTP markers identify selected directories. On Windows only, a node.cmd probe reports the batch-selected directory and prevents --open from launching a browser. This fixture does not exercise gameplay.',
    commands: [], launcher: process.platform === 'win32' ? 'cmd.exe with node.cmd directory probe' : 'not executed on this platform' };
  report.startupLayouts.push(record);
  function write(relative, data) {
    const filename = path.join(fixtureRoot, relative);
    mkdirSync(path.dirname(filename), { recursive: true });
    writeFileSync(filename, data);
  }
  for (const directory of [source, download]) {
    mkdirSync(path.join(directory, 'scripts'), { recursive: true });
    copyFileSync(path.join(root, 'scripts/serve-web.mjs'), path.join(directory, 'scripts/serve-web.mjs'));
    copyFileSync(path.join(root, 'Start-Web-Demo.bat'), path.join(directory, 'Start-Web-Demo.bat'));
    if (process.platform === 'win32') {
      writeFileSync(path.join(directory, 'node.cmd'), '@echo off\r\necho LAUNCHER_WEB_DIR=%~2\r\nexit /b 0\r\n');
    }
  }
  function launcher(directory, expectedDirectory, missing = false) {
    if (process.platform !== 'win32') return;
    const result = spawnSync('cmd.exe', ['/d', '/c', 'Start-Web-Demo.bat'], {
      cwd: directory, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'], encoding: 'utf8', timeout: 5000,
    });
    const text = (result.stdout ?? '') + (result.stderr ?? '');
    record.commands.push({ name: 'Windows launcher', cwd: directory, status: result.status, output: text });
    if (missing) {
      check('checkout launcher rejects a missing current build without selecting leftover web/dist', !result.error
        && result.status === 1 && text.includes('apps\\web\\dist\\index.html')
        && text.includes('npm run build') && !text.includes('LAUNCHER_WEB_DIR='), { status: result.status, output: text, error: result.error?.message });
    } else {
      check(`launcher selects ${expectedDirectory} for its layout`, !result.error && result.status === 0
        && text.includes('LAUNCHER_WEB_DIR=' + expectedDirectory), { status: result.status, output: text, error: result.error?.message });
    }
  }
  async function served(directory, args, expected, name) {
    await requireFreePort(4173);
    const job = nodeTask([path.join(directory, 'scripts/serve-web.mjs'), ...args], directory);
    try {
      await ready(job, 'http://127.0.0.1:4173/');
      const response = await fetch('http://127.0.0.1:4173/', { signal: AbortSignal.timeout(3000) });
      const text = await response.text();
      check(name, response.status === 200 && text === expected, { status: response.status, body: text });
    } finally {
      await stop(job);
      record.commands.push({ name, cwd: directory, args, output: job.log,
        stopped: job.child.exitCode !== null || job.child.signalCode !== null });
    }
  }
  const stale = 'STALE_PRE_MONOREPO_BUILD', current = 'CURRENT_MONOREPO_BUILD', packaged = 'CURRENT_DOWNLOAD_BUILD';
  write('checkout/apps/web/package.json', '{"name":"@ustc-danmaku/web","private":true}\n');
  write('checkout/web/dist/index.html', stale);
  await requireFreePort(4173);
  const rejected = nodeTask([path.join(source, 'scripts/serve-web.mjs')], source);
  try {
    const status = await withTimeout(rejected.closed, 5000, 'Default server served or hung on a missing monorepo build.');
    check('checkout default server rejects a missing current build instead of serving leftover web/dist', status === 1
      && rejected.log.includes('apps/web/dist/index.html') && rejected.log.includes('npm run build')
      && !rejected.log.includes('Demo:'), { status, output: rejected.log });
  } finally {
    await stop(rejected);
    record.commands.push({ name: 'missing checkout build', cwd: source, output: rejected.log,
      stopped: rejected.child.exitCode !== null || rejected.child.signalCode !== null });
  }
  await requireFreePort(4173);
  launcher(source, null, true);
  write('checkout/apps/web/dist/index.html', current);
  await served(source, [], current, 'checkout default server chooses current apps/web/dist with stale web/dist present');
  launcher(source, 'apps\\web\\dist');
  await served(source, ['web/dist'], stale, 'explicit directory remains a deliberate user choice');
  write('download/web/dist/index.html', packaged);
  await served(download, [], packaged, 'download default server chooses its web/dist layout');
  launcher(download, 'web\\dist');
}

async function exercise(server) {
  const record = { name: server.name, command: server.command, url: server.url, cwd: server.cwd ?? root,
    errors: [], failedRequests: [], httpErrors: [], resources: [], accepted: null };
  report.servers.push(record);
  await requireFreePort(server.port);
  const job = nodeTask(server.args, server.cwd ?? root);
  let context;
  try {
    await ready(job, server.url);
    record.output = job.log;
    check(`${server.name}: requested HTTP port is served`, true, { port: server.port });
    context = await browser.newContext({ viewport: { width: 1440, height: 900 } });
    const page = await context.newPage(), responses = [];
    page.on('pageerror', error => record.errors.push(error.message));
    page.on('requestfailed', request => record.failedRequests.push({ url: request.url(), error: request.failure()?.errorText }));
    page.on('response', response => {
      responses.push(response);
      record.resources.push({ url: response.url(), status: response.status(), contentType: response.headers()['content-type'] });
      if (response.status() >= 400) record.httpErrors.push({ url: response.url(), status: response.status() });
    });
    const now = new Date('2026-10-07T10:00:00+08:00');
    await page.clock.install({ time: now });
    await page.clock.pauseAt(new Date(now.getTime() + 1000));
    const url = new URL(server.url); url.searchParams.set('qa', '1');
    const navigation = await page.goto(url.href);
    await page.waitForFunction(() => window.__demo, null, { polling: 50, timeout: 30000 });
    const inspect = () => page.evaluate(() => window.__demo.inspect());
    const ticks = async count => {
      let state = await inspect(), frames = 0;
      const target = state.snapshot.tick + count;
      while (state.snapshot.tick < target) {
        if (state.phase !== 'playing' || frames++ > count * 3 + 6) throw Error('Logical clock did not advance while playing.');
        await page.clock.runFor(16); state = await inspect();
        if (state.snapshot.tick > target) throw Error('Controlled clock overshot a logical tick.');
      }
      return state;
    };
    const menu = await inspect();
    check(`${server.name}: menu boots cfg6 and the release-aware client`, navigation.status() === 200
      && menu.phase === 'menu' && menu.snapshot.configVersion === 6 && 'aim' in menu && 'preview' in menu);
    const wasmResponse = responses.find(response => new URL(response.url()).pathname.endsWith('/wasm/demo-core.wasm'));
    const moduleResponse = responses.find(response => new URL(response.url()).pathname.endsWith('/wasm/demo-core.mjs'));
    check(`${server.name}: separate Wasm and module load through HTTP`, wasmResponse?.status() === 200 && moduleResponse?.status() === 200);
    const wasm = await wasmResponse.body();
    record.wasmSha256 = sha256(wasm);
    check(`${server.name}: correct Wasm MIME and shared compiled bytes`, /application\/wasm/i.test(wasmResponse.headers()['content-type'] ?? '')
      && record.wasmSha256 === sha256(readFileSync(path.join(root, 'apps/web/public/wasm/demo-core.wasm'))));
    check(`${server.name}: all HTTP resources stay on the selected local origin`, record.resources.every(resource => new URL(resource.url).origin === url.origin));
    await page.locator('[data-overlay="action"]').click();
    const started = await ticks(1);
    check(`${server.name}: start enters the actual C world`, started.phase === 'playing' && started.snapshot.tick > 0 && started.snapshot.actors.length === 4);
    const boss = started.snapshot.actors[0];
    const point = await page.locator('#arena canvas').evaluate((canvas, data) => {
      const b = canvas.getBoundingClientRect(), width = Math.floor(b.width), height = Math.floor(b.height);
      const scale = Math.min(width / data.w, height / data.h);
      return { x: b.left + ((width - data.w * scale) / 2 + data.x * scale) * b.width / width,
        y: b.top + ((height - data.h * scale) / 2 + data.y * scale) * b.height / height };
    }, { x: boss.x + 54, y: boss.y - 72, w: started.snapshot.fieldW, h: started.snapshot.fieldH });
    await page.mouse.move(point.x, point.y);
    await page.keyboard.down('2'); await page.keyboard.down('w');
    const held = await ticks(6);
    await page.keyboard.up('w');
    check(`${server.name}: holding selects and draws C candidate rays`, held.aim?.pattern === 1 && held.preview?.valid === true && held.preview.rays.length > 0);
    check(`${server.name}: pre-aim costs no energy and submits no attack`, held.snapshot.accepted === 0 && held.snapshot.rejected === 0
      && held.snapshot.energy >= started.snapshot.energy && held.snapshot.attackState === 0);
    check(`${server.name}: movement preserves the mouse-selected angle`, Math.hypot(held.snapshot.actors[0].x - boss.x, held.snapshot.actors[0].y - boss.y) > 1
      && near(held.aim.dirX, .6) && near(held.aim.dirY, -.8)
      && near(held.aim.dirX, held.preview.dirX) && near(held.aim.dirY, held.preview.dirY));
    await page.keyboard.up('2');
    const released = await ticks(1), snapshot = released.snapshot;
    check(`${server.name}: keyup releases exactly once with its C cost and direction`, snapshot.accepted === 1 && snapshot.rejected === 0
      && snapshot.pattern === 1 && snapshot.manualAim && snapshot.attackState === 1
      && snapshot.energy >= held.snapshot.energy - snapshot.costs[1] && snapshot.energy <= held.snapshot.energy - snapshot.costs[1] + 1
      && near(snapshot.aimDirX, held.aim.dirX) && near(snapshot.aimDirY, held.aim.dirY));
    check(`${server.name}: released warning geometry matches preview`, snapshot.warnings.length === held.preview.rays.length
      && snapshot.warnings.every((ray, index) => ['x', 'y', 'vx', 'vy', 'radius'].every(key => near(ray[key], held.preview.rays[index][key]))));
    check(`${server.name}: release clears the selection and preview`, released.aim === null && released.preview === null);
    const firstSpawn = Math.min(...snapshot.warnings.map(ray => ray.spawnTick));
    const emitted = await ticks(firstSpawn + 1 - snapshot.tick);
    check(`${server.name}: first C wave emits and does not repeat the release`, emitted.snapshot.bossBullets > 0
      && emitted.snapshot.accepted === 1 && emitted.snapshot.bullets.some(bullet => bullet.faction === 1 && bullet.pattern === 1));
    record.accepted = { cost: snapshot.costs[1], tick: snapshot.startTick, dirX: snapshot.aimDirX, dirY: snapshot.aimDirY,
      warningRays: snapshot.warnings.length, firstEmissionTick: emitted.snapshot.tick, bulletsEmitted: emitted.snapshot.bossBullets };
    await page.keyboard.down('1'); await ticks(1); await page.keyboard.press('Space'); await page.keyboard.up('1');
    const cancelled = await ticks(1);
    check(`${server.name}: cancellation generates no extra release`, cancelled.aim === null && cancelled.preview === null
      && cancelled.snapshot.accepted === 1 && cancelled.snapshot.rejected === 0);
    check(`${server.name}: no runtime, resource or HTTP errors`, record.errors.length === 0 && record.failedRequests.length === 0 && record.httpErrors.length === 0,
      { errors: record.errors, failedRequests: record.failedRequests, httpErrors: record.httpErrors });
  } finally {
    if (context) await context.close();
    await stop(job);
    record.stopped = job.child.exitCode !== null || job.child.signalCode !== null;
  }
}

try {
  await startupLayoutChecks();
  check('package output is fresh and preserves previous deliveries', !existsSync(packageRoot) && !existsSync(packageRoot + '.zip'));
  const packageLog = await runTask([path.join(root, 'scripts/run-powershell.mjs'), 'package-web', '-OutputRoot', packageOutput], 'package:web');
  report.package.log = packageLog;
  const manifest = JSON.parse(readFileSync(path.join(packageRoot, 'manifest.json'), 'utf8').replace(/^\uFEFF/, ''));
  check('package metadata retains cfg6, ABI5 and all shared C sources', manifest.configVersion === 6 && manifest.abiVersion === 5 && manifest.coreSourceCount === 16);
  check('package manifest verifies every delivered file', manifest.files.length > 0 && manifest.files.every(file => {
    const absolute = path.resolve(packageRoot, file.path);
    if (!absolute.startsWith(packageRoot + path.sep)) return false;
    const bytes = readFileSync(absolute);
    return bytes.length === file.bytes && sha256(bytes).toUpperCase() === file.sha256;
  }));
  const standalone = readFileSync(path.join(packageRoot, 'ustc-danmaku.html'));
  const baseline = readFileSync(path.join(root, 'demo/ustc-danmaku.html'));
  check('packaged standalone preserves the verified game bytes', standalone.equals(baseline));
  const lock = JSON.parse(readFileSync(path.join(root, 'package-lock.json'), 'utf8'));
  const dependencies = Object.entries(lock.packages).filter(([name, info]) => name.split('/').includes('node_modules') && !info.dev && !info.link);
  const notices = readFileSync(path.join(packageRoot, 'THIRD-PARTY-NOTICES.txt'), 'utf8');
  check('all production dependency licenses are included without workspace links', dependencies.some(([name]) => name === 'node_modules/pixi.js')
    && dependencies.every(([name, info]) => notices.includes(`=== ${name} ${info.version} (${info.license}) ===`))
    && !notices.includes('=== node_modules/@ustc-danmaku/'));
  check('download preserves its web/dist layout and both startup branches', existsSync(path.join(packageRoot, 'web/dist/index.html'))
    && !existsSync(path.join(packageRoot, 'apps/web/dist/index.html'))
    && readFileSync(path.join(packageRoot, 'Start-Web-Demo.bat'), 'utf8').includes('apps\\web\\dist')
    && readFileSync(path.join(packageRoot, 'Start-Web-Demo.bat'), 'utf8').includes('web\\dist'));
  const zip = readFileSync(packageRoot + '.zip');
  report.package = { ...report.package, files: manifest.files.length, licenses: dependencies.map(([name, info]) => ({ name, version: info.version, license: info.license })),
    htmlSha256: sha256(standalone), htmlBytes: standalone.length, zip: packageRoot + '.zip', zipBytes: zip.length, zipSha256: sha256(zip) };
  browser = await chromium.launch({ channel: 'chrome', headless: true });
  report.browser = await browser.version();
  const cli = npmCli();
  await exercise({ name: 'root npm dev', command: 'npm run dev -- --port 5187 --strictPort',
    args: [cli, 'run', 'dev', '--', '--port', '5187', '--strictPort'], port: 5187, url: 'http://127.0.0.1:5187/' });
  await exercise({ name: 'root npm preview', command: 'npm run preview -- --port 5188 --strictPort',
    args: [cli, 'run', 'preview', '--', '--port', '5188', '--strictPort'], port: 5188, url: 'http://127.0.0.1:5188/' });
  await exercise({ name: 'repository default static server', command: 'node scripts/serve-web.mjs',
    args: [path.join(root, 'scripts/serve-web.mjs')], port: 4173, url: 'http://127.0.0.1:4173/' });
  await exercise({ name: 'download default static server', command: 'node scripts/serve-web.mjs (package cwd)', cwd: packageRoot,
    args: [path.join(packageRoot, 'scripts/serve-web.mjs')], port: 4173, url: 'http://127.0.0.1:4173/' });
  report.passed = true;
} catch (error) {
  report.failure = { message: error.message, stack: error.stack };
  process.exitCode = 1;
} finally {
  if (browser) await browser.close();
  for (const job of [...children]) await stop(job);
  report.finishedAt = new Date().toISOString();
  report.checksPassed = report.checks.filter(item => item.passed).length;
  mkdirSync(path.dirname(output), { recursive: true });
  writeFileSync(output, JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify({ passed: report.passed, checksPassed: report.checksPassed, checks: report.checks.length,
    servers: report.servers.map(server => ({ name: server.name, stopped: server.stopped, resources: server.resources.length, accepted: server.accepted })),
    package: { root: packageRoot, files: report.package.files, licenses: report.package.licenses?.length, zipBytes: report.package.zipBytes },
    report: output, failure: report.failure?.message }, null, 2));
}
