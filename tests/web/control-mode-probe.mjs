import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import vm from 'node:vm';
import ts from 'typescript';

const root = fileURLToPath(new URL('../../', import.meta.url));
const source = readFileSync(path.join(root, 'apps/web/src/input/controlMode.ts'), 'utf8');
const exports = {};
vm.runInNewContext(ts.transpileModule(source, {
  compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.CommonJS },
}).outputText, { exports, Number, Math });
const baseline = { userAgent: 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)', platform: 'Win32',
  maxTouchPoints: 0, coarsePointer: false, noHover: false };
const cases = [
  ['iPhone with actual touch', { userAgent: 'Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X)',
    platform: 'iPhone', maxTouchPoints: 5, coarsePointer: true, noHover: true }, 'touch'],
  ['iPhone with paired fine pointer', { userAgent: 'iPhone', maxTouchPoints: 5 }, 'touch'],
  ['Android phone', { userAgent: 'Mozilla/5.0 (Linux; Android 15) Mobile', maxTouchPoints: 5,
    coarsePointer: true, noHover: true }, 'touch'],
  ['iPad desktop Safari UA', { userAgent: 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15) Version/18.0 Safari/605.1.15',
    platform: 'MacIntel', maxTouchPoints: 5, coarsePointer: true, noHover: true }, 'touch'],
  ['iPad desktop UA with paired pointer', { platform: 'MacIntel', maxTouchPoints: 5 }, 'touch'],
  ['iPad native UA', { userAgent: 'Mozilla/5.0 (iPad; CPU OS 18_0)', platform: 'iPad', maxTouchPoints: 5 }, 'touch'],
  ['Android tablet', { userAgent: 'Mozilla/5.0 (Linux; Android 15) Safari', maxTouchPoints: 5,
    coarsePointer: true, noHover: true }, 'touch'],
  ['UA-CH mobile with actual touch', { userAgentMobile: true, maxTouchPoints: 5 }, 'touch'],
  ['desktop', {}, 'desktop'],
  ['narrow desktop still uses input capabilities', {}, 'desktop'],
  ['Windows touchscreen laptop with primary fine pointer', { maxTouchPoints: 10 }, 'desktop'],
  ['Chromebook touchscreen with fine pointer', { userAgent: 'CrOS x86_64', platform: 'Linux x86_64', maxTouchPoints: 10 }, 'desktop'],
  ['phone UA without touchscreen', { userAgent: 'iPhone', coarsePointer: true, noHover: true }, 'desktop'],
  ['UA-CH mobile without touchscreen', { userAgentMobile: true, coarsePointer: true, noHover: true }, 'desktop'],
  ['Mac desktop', { platform: 'MacIntel' }, 'desktop'],
  ['Windows tablet with primary coarse non-hover pointer', { maxTouchPoints: 5, coarsePointer: true, noHover: true }, 'touch'],
  ['coarse hover-capable desktop device does not force touch', { maxTouchPoints: 5, coarsePointer: true }, 'desktop'],
  ['non-finite touch points do not enable touch', { userAgentMobile: true, maxTouchPoints: NaN }, 'desktop'],
];
const checks = [];
for (const [name, patch, expected] of cases) {
  const signals = { ...baseline, ...patch };
  const actual = exports.detectControlModeFromSignals(signals);
  try {
    assert.equal(actual.mode, expected);
    assert.ok(typeof actual.reason === 'string' && actual.reason.length > 0);
    checks.push({ name, signals, expected, actual, pass: true });
  } catch (error) { checks.push({ name, signals, expected, actual, pass: false, error: String(error) }); }
}
const report = { method: 'Pure public input-capability signal fixtures; no claimed physical-device or browser execution.',
  sourceSha256: createHash('sha256').update(source).digest('hex'), checks, passed: checks.every(check => check.pass) };
const output = path.join(root, 'build/mobile');
mkdirSync(output, { recursive: true });
writeFileSync(path.join(output, 'control-mode-probe.json'), JSON.stringify(report, null, 2));
console.log(JSON.stringify({ checks: checks.length, passed: report.passed }));
if (!report.passed) process.exitCode = 1;
