import type { Actor, AimPreview, Bullet, CoreClient, GameEvent, Snapshot, TickInput, WarningRay } from '../types';

type WasmModule = {
  HEAPU8: Uint8Array;
  _demo_reset(lo: number, hi: number, students: number): number;
  _demo_reset_sized(lo: number, hi: number, students: number, width: number, height: number): number;
  _demo_step(mx: number, my: number, pointerValid: number, px: number, py: number, mask: number): number;
  _demo_step_aim(mx: number, my: number, pointerValid: number, px: number, py: number, mask: number,
    dirX: number, dirY: number): number;
  _demo_preview(pattern: number, dirX: number, dirY: number): number;
  _demo_preview_size(): number;
  _demo_snapshot(): number;
  _demo_snapshot_size(): number;
  _demo_dispose(): void;
};
type ModuleFactory = (options: {
  locateFile?(path: string): string;
  instantiateWasm?(imports: WebAssembly.Imports, receive: (instance: WebAssembly.Instance) => void): WebAssembly.Exports;
}) => Promise<WasmModule>;
type EmbeddedCore = { factory: ModuleFactory; bytesBase64: string };
const HEADER_BYTES = 64 * 4;
const MAX_BYTES = HEADER_BYTES + 9 * 40 + 800 * 40 + 8192 * 32 + 256 * 36 + 8 * 8;
const PREVIEW_HEADER_BYTES = 16 * 4;
const PREVIEW_MAX_BYTES = PREVIEW_HEADER_BYTES + 8192 * 32;

function requireValue(condition: boolean, message: string): asserts condition {
  if (!condition) throw new Error(`C/Wasm ABI v5: ${message}`);
}

function unsigned(value: number, label: string): number {
  requireValue(Number.isInteger(value) && value >= 0 && value <= 0xffff_ffff, `${label} 必须是 uint32`);
  return value;
}

/** Accepts an owned copy only. No returned value references Wasm memory. */
export function decodeSnapshot(bytes: Uint8Array): Snapshot {
  requireValue(bytes.byteLength >= HEADER_BYTES && bytes.byteLength <= MAX_BYTES && bytes.byteLength % 4 === 0,
    '快照大小非法');
  const data = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const u = (word: number): number => data.getUint32(word * 4, true);
  const i = (word: number): number => data.getInt32(word * 4, true);
  const f = (word: number): number => {
    const value = data.getFloat32(word * 4, true);
    requireValue(Number.isFinite(value), `word ${word} 不是有限浮点数`);
    return value;
  };
  const flag = (word: number): boolean => {
    const value = u(word);
    requireValue(value === 0 || value === 1, `word ${word} 不是布尔值`);
    return value === 1;
  };
  const identity = (word: number): string => ((BigInt(u(word + 1)) << 32n) | BigInt(u(word))).toString();
  requireValue(u(0) === 0x55444331 && u(1) === 5, 'magic 或版本不匹配');
  requireValue(u(2) === bytes.byteLength, '头部字节数与缓冲不匹配');
  requireValue(i(3) >= 0 && u(4) <= 3 && u(5) > 0, 'tick、状态或配置版本非法');
  const students = u(6), bulletCount = u(7), warningCount = u(8), eventCount = u(9);
  requireValue(students >= 1 && students <= 8 && bulletCount <= 800 && warningCount <= 8192 && eventCount <= 256,
    '记录数超过冻结容量');
  const actorOffset = HEADER_BYTES;
  const bulletOffset = actorOffset + (students + 1) * 40;
  const warningOffset = bulletOffset + bulletCount * 40;
  const eventOffset = warningOffset + warningCount * 32;
  requireValue(u(40) === actorOffset && u(41) === bulletOffset && u(42) === warningOffset && u(43) === eventOffset,
    '记录偏移不是规范连续布局');
  const previewCount = u(53), previewOffset = eventOffset + eventCount * 36;
  requireValue(previewCount <= 8 && u(54) === previewOffset && previewOffset + previewCount * 8 === bytes.byteLength,
    '出生预告偏移或记录长度非法');
  requireValue(u(45) >= 1 && u(47) <= 1 && u(48) <= 8 && i(49) >= 0 &&
    i(50) >= 0 && u(55) > 0 && i(56) === 430 && i(50) < i(56) && u(52) >= u(51), '波次或 GPA 非法');
  requireValue((u(47) === 0 && previewCount === 0) || (u(47) === 1 && previewCount === u(48)), '出生预告与波状态不一致');
  const aimDirX = f(57), aimDirY = f(58), manualAim = flag(59);
  for (let word = 60; word < 64; ++word) requireValue(u(word) === 0, '保留字必须为零');
  requireValue(manualAim ? Math.abs(Math.hypot(aimDirX, aimDirY) - 1) < .0001 && u(12) !== 0 && u(14) === 0 :
    u(57) === 0 && u(58) === 0, '手动锁定方向或状态非法');
  requireValue(i(10) >= 0 && i(11) > 0 && i(10) <= i(11), '能量范围非法');
  requireValue(u(12) <= 2 && u(13) < 4 && i(15) >= 0 && i(16) >= 0 && i(17) >= 0,
    '攻击状态或时刻非法');
  const fieldW = f(29), fieldH = f(30);
  requireValue(fieldW > 0 && fieldH > 0, '场地大小非法');
  const costs = Array.from({ length: 4 }, (_, p) => {
    const cost = i(32 + p);
    requireValue(cost > 0 && cost <= i(11), '技能消耗非法');
    return cost;
  });
  const available = Array.from({ length: 4 }, (_, p) => flag(36 + p));
  const actors: Actor[] = [];
  const actorIds = new Set<number>();
  for (let index = 0; index <= students; ++index) {
    const word = actorOffset / 4 + index * 10;
    const actor: Actor = { id: u(word), alive: flag(word + 1), x: f(word + 2), y: f(word + 3),
      radius: f(word + 4), hp: i(word + 5), hpMax: i(word + 6), invuln: i(word + 7), faction: u(word + 8) };
    requireValue(actor.id > 0 && !actorIds.has(actor.id) && actor.radius > 0 && actor.hp >= 0 &&
      actor.hpMax > 0 && actor.hp <= actor.hpMax && actor.invuln >= 0 &&
      actor.faction === (index === 0 ? 1 : 2) && u(word + 9) === 0, '角色记录非法');
    actorIds.add(actor.id); actors.push(actor);
  }
  const bullets: Bullet[] = [];
  const bulletIds = new Set<string>();
  for (let index = 0; index < bulletCount; ++index) {
    const word = bulletOffset / 4 + index * 10;
    const bullet: Bullet = { id: identity(word), faction: u(word + 2), pattern: u(word + 3),
      x: f(word + 4), y: f(word + 5), vx: f(word + 6), vy: f(word + 7), radius: f(word + 8), source: u(word + 9) };
    requireValue(!bulletIds.has(bullet.id) && (bullet.faction === 1 || bullet.faction === 2) &&
      bullet.pattern < 4 && bullet.radius > 0 && actorIds.has(bullet.source), '弹幕记录非法');
    bulletIds.add(bullet.id); bullets.push(bullet);
  }
  const warnings: WarningRay[] = [];
  for (let index = 0; index < warningCount; ++index) {
    const word = warningOffset / 4 + index * 8;
    const warning: WarningRay = { x: f(word), y: f(word + 1), vx: f(word + 2), vy: f(word + 3),
      radius: f(word + 4), spawnTick: i(word + 5), wave: u(word + 6), pattern: u(word + 7) };
    requireValue(warning.radius > 0 && warning.spawnTick >= 0 && warning.wave < 32 && warning.pattern < 4,
      '预警记录非法');
    warnings.push(warning);
  }
  const events: GameEvent[] = [];
  for (let index = 0; index < eventCount; ++index) {
    const word = eventOffset / 4 + index * 9;
    const event: GameEvent = { type: u(word), tick: i(word + 1), source: u(word + 2), target: u(word + 3),
      pattern: u(word + 4), reject: u(word + 5), amount: i(word + 6), x: f(word + 7), y: f(word + 8) };
    requireValue(event.type <= 15 && event.tick >= 0 && event.tick <= i(3) && event.pattern < 4 && event.reject < 5,
      '事件记录非法');
    events.push(event);
  }
  const spawnPreview = Array.from({ length: previewCount }, (_, index) => {
    const word = previewOffset / 4 + index * 2;
    const point = { x: f(word), y: f(word + 1) };
    requireValue(point.x >= 0 && point.x <= fieldW && point.y >= 0 && point.y <= fieldH, '出生预告越界');
    return point;
  });
  return { tick: i(3), status: u(4), configVersion: u(5), energy: i(10), energyMax: i(11),
    wave: u(45), wavesCleared: u(46), wavePhase: u(47) as 0 | 1, nextWaveStudents: u(48), waveSpawnTick: i(49),
    gpaHundredths: i(50), kills: u(51), studentsDeployed: u(52),
    gpaHalfSaturationKills: u(55), gpaMaxHundredths: i(56), spawnPreview,
    attackState: u(12), pattern: u(13), target: u(14), startTick: i(15), windup: i(16), active: i(17),
    aimDirX, aimDirY, manualAim,
    planId: identity(18), seed: identity(20), accepted: u(22), rejected: u(23), bossHits: u(24), studentHits: u(25),
    bossBullets: u(26), studentBullets: u(27), overflow: u(28), fieldW, fieldH, markedTarget: u(31),
    costs, available, eventDropped: u(44), actors, bullets, warnings, events };
}

/** Owned copy of the independent pure C preview. This never decodes a world
 * snapshot or constructs ray geometry in JavaScript. */
export function decodePreview(bytes: Uint8Array): AimPreview {
  requireValue(bytes.byteLength >= PREVIEW_HEADER_BYTES && bytes.byteLength <= PREVIEW_MAX_BYTES &&
    bytes.byteLength % 4 === 0, '预瞄缓冲大小非法');
  const data = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const u = (word: number): number => data.getUint32(word * 4, true);
  const i = (word: number): number => data.getInt32(word * 4, true);
  const f = (word: number): number => {
    const value = data.getFloat32(word * 4, true);
    requireValue(Number.isFinite(value), `预瞄 word ${word} 不是有限浮点数`);
    return value;
  };
  requireValue(u(0) === 0x55445031 && u(1) === 1 && u(2) === bytes.byteLength, '预瞄 magic、版本或长度非法');
  const pattern = u(3), readyWord = u(4), reason = u(5), count = u(10), tick = i(12);
  requireValue(pattern < 4 && readyWord <= 1 && reason < 5 && count <= 8192 && tick >= 0 && u(15) === 0,
    '预瞄技能、就绪、原因、数量或保留字非法');
  requireValue(u(11) === PREVIEW_HEADER_BYTES && PREVIEW_HEADER_BYTES + count * 32 === bytes.byteLength,
    '预瞄弹道不是规范连续布局');
  const originX = f(6), originY = f(7), dirX = f(8), dirY = f(9), fieldW = f(13), fieldH = f(14);
  requireValue(fieldW > 0 && fieldH > 0 && originX >= 0 && originX <= fieldW && originY >= 0 && originY <= fieldH &&
    Math.abs(Math.hypot(dirX, dirY) - 1) < .0001, '预瞄原点、场地或单位方向非法');
  const valid = readyWord === 1;
  requireValue(valid ? reason === 0 && count > 0 : reason !== 0 || count === 0, '预瞄就绪与拒绝原因不一致');
  const rays: WarningRay[] = [];
  for (let index = 0; index < count; ++index) {
    const word = PREVIEW_HEADER_BYTES / 4 + index * 8;
    const ray: WarningRay = { x: f(word), y: f(word + 1), vx: f(word + 2), vy: f(word + 3),
      radius: f(word + 4), spawnTick: i(word + 5), wave: u(word + 6), pattern: u(word + 7) };
    requireValue(ray.radius > 0 && ray.spawnTick >= tick && ray.wave < 32 && ray.pattern === pattern &&
      ray.x >= -64 && ray.x <= fieldW + 64 && ray.y >= -64 && ray.y <= fieldH + 64 &&
      ray.vx * dirX + ray.vy * dirY > 0, '预瞄弹道、出生边界或方向非法');
    rays.push(ray);
  }
  return { pattern, valid, reason, originX, originY, dirX, dirY, tick, fieldW, fieldH, rays };
}

export async function loadCore(): Promise<CoreClient> {
  const embedded = (window as Window & { __ustcEmbeddedCore?: EmbeddedCore }).__ustcEmbeddedCore;
  let module: WasmModule;
  if (embedded) {
    requireValue(typeof embedded.factory === 'function', '内联核心缺少初始化工厂');
    const bytes = Uint8Array.from(atob(embedded.bytesBase64), character => character.charCodeAt(0));
    const compiled = await WebAssembly.compile(bytes);
    module = await embedded.factory({ instantiateWasm: (imports, receive) => {
      const instance = new WebAssembly.Instance(compiled, imports);
      receive(instance);
      return instance.exports;
    } });
  } else {
    const moduleUrl = new URL(`${import.meta.env.BASE_URL}wasm/demo-core.mjs`, window.location.href);
    const imported = await import(/* @vite-ignore */ moduleUrl.href) as { default: ModuleFactory };
    requireValue(typeof imported.default === 'function', 'Wasm 模块未导出初始化工厂');
    module = await imported.default({ locateFile: path => new URL(path, moduleUrl).href });
  }
  for (const name of ['_demo_reset', '_demo_step', '_demo_step_aim', '_demo_preview', '_demo_preview_size',
    '_demo_snapshot', '_demo_snapshot_size', '_demo_dispose'] as const)
    requireValue(typeof module[name] === 'function', `缺少导出 ${name}`);
  requireValue(typeof module._demo_reset_sized === 'function', '缺少可变战场重置导出');
  let disposed = false;
  const assertAlive = (): void => requireValue(!disposed, '客户端已释放');
  const snapshot = (): Snapshot => {
    assertAlive();
    const pointer = module._demo_snapshot() >>> 0;
    const size = module._demo_snapshot_size() >>> 0;
    // Fetch HEAPU8 after C calls: memory.grow replaces the canonical view.
    const heap = module.HEAPU8;
    requireValue(heap instanceof Uint8Array && pointer !== 0 && size >= HEADER_BYTES && size <= MAX_BYTES &&
      pointer <= heap.byteLength && size <= heap.byteLength - pointer, '快照失败、超容量或内存范围非法');
    return decodeSnapshot(heap.slice(pointer, pointer + size));
  };
  const aimDirection = (x: number, y: number): void => {
    requireValue(Number.isFinite(x) && Number.isFinite(y) && Number.isFinite(Math.fround(x)) &&
      Number.isFinite(Math.fround(y)) && Math.hypot(x, y) >= 1e-6, '瞄准方向必须是有限非零向量');
  };
  return {
    reset(seedLo = 12345, seedHi = 0, students = 3, fieldW = 960, fieldH = 720): Snapshot {
      assertAlive(); unsigned(seedLo, 'seedLo'); unsigned(seedHi, 'seedHi');
      requireValue(Number.isInteger(students) && students >= 1 && students <= 8, '学生数量必须为 1..8');
      requireValue(Number.isFinite(fieldW) && Number.isFinite(fieldH) && fieldW > 0 && fieldH > 0, '战场尺寸非法');
      requireValue(module._demo_reset_sized(seedLo, seedHi, students, fieldW, fieldH) === 1, '核心重置失败');
      return snapshot();
    },
    step(input: TickInput): void {
      assertAlive();
      requireValue([input.moveX, input.moveY, input.pointerX, input.pointerY].every(Number.isFinite), '输入不是有限数');
      requireValue(typeof input.pointerValid === 'boolean', 'pointerValid 必须为布尔值');
      requireValue(input.aimValid === undefined || typeof input.aimValid === 'boolean', 'aimValid 必须为布尔值');
      unsigned(input.attacks, 'attacks'); requireValue(input.attacks <= 15, '出招位掩码只能使用低四位');
      let advanced: number;
      if (input.aimValid) {
        requireValue(typeof input.aimX === 'number' && typeof input.aimY === 'number', '手动出招缺少方向');
        aimDirection(input.aimX, input.aimY);
        advanced = module._demo_step_aim(input.moveX, input.moveY, input.pointerValid ? 1 : 0,
          input.pointerX, input.pointerY, input.attacks, input.aimX, input.aimY);
      } else advanced = module._demo_step(input.moveX, input.moveY, input.pointerValid ? 1 : 0,
        input.pointerX, input.pointerY, input.attacks);
      requireValue(advanced === 1 || advanced === 0, 'step 返回值非法');
      if (advanced === 0) {
        const current = snapshot();
        requireValue(current.status !== 0, '运行中的核心拒绝 step（非法输入或实验截断）');
      }
    },
    preview(pattern: number, dirX: number, dirY: number): AimPreview {
      assertAlive(); unsigned(pattern, 'pattern'); requireValue(pattern < 4, '预瞄技能必须为 0..3');
      aimDirection(dirX, dirY);
      const pointer = module._demo_preview(pattern, dirX, dirY) >>> 0;
      const size = module._demo_preview_size() >>> 0;
      const heap = module.HEAPU8;
      requireValue(heap instanceof Uint8Array && pointer !== 0 && size >= PREVIEW_HEADER_BYTES && size <= PREVIEW_MAX_BYTES &&
        pointer <= heap.byteLength && size <= heap.byteLength - pointer, '预瞄失败、超容量或内存范围非法');
      return decodePreview(heap.slice(pointer, pointer + size));
    },
    snapshot,
    dispose(): void { if (!disposed) { module._demo_dispose(); disposed = true; } },
  };
}
