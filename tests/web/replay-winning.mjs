import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath, pathToFileURL } from 'node:url';
const root = fileURLToPath(new URL('../../', import.meta.url));
const replay = JSON.parse(readFileSync(root + 'build/web-validation/winning-inputs.json', 'utf8'));
const factory = (await import(pathToFileURL(root + 'apps/web/public/wasm/demo-core.mjs').href)).default;
const core = await factory();
if (!core._demo_reset(replay.seed_lo, replay.seed_hi, replay.students)) throw Error('Reset failed');
for (const [tick, input] of replay.inputs.entries()) {
  if (tick !== input.tick || Math.hypot(input.move_x, input.move_y) > 1.00001 || input.attack_mask > 15) throw Error('Illegal input');
  core._demo_step(input.move_x, input.move_y, input.pointer_valid, input.pointer_x, input.pointer_y, input.attack_mask);
}
const pointer = core._demo_snapshot();
const view = new DataView(core.HEAPU8.buffer, pointer, core._demo_snapshot_size());
const actorOffset = view.getUint32(40 * 4, true);
const actual = { status: view.getUint32(4 * 4, true), tick: view.getUint32(3 * 4, true), boss_hp: view.getInt32(actorOffset + 5 * 4, true), boss_hits: view.getUint32(24 * 4, true), student_hits: view.getUint32(25 * 4, true) };
for (const [key, expected] of Object.entries(replay.expected)) if (actual[key] !== expected) throw Error(`${key}: ${actual[key]} != ${expected}`);
const result = { configVersion: view.getUint32(5 * 4, true), seedLo: replay.seed_lo, seedHi: replay.seed_hi, students: replay.students, inputs: replay.inputs.length, actual, passed: true };
writeFileSync(root + 'build/web-validation/winning-wasm-result.json', JSON.stringify(result, null, 2));
console.log(JSON.stringify(result, null, 2));
core._demo_dispose();
