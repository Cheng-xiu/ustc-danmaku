/** Web ABI v5. Scalar words are explicitly written by C, never raw C structs. */
export type Actor = { id: number; alive: boolean; x: number; y: number; radius: number; hp: number; hpMax: number; invuln: number; faction: number };
export type Bullet = { id: string; faction: number; pattern: number; x: number; y: number; vx: number; vy: number; radius: number; source: number };
export type WarningRay = { x: number; y: number; vx: number; vy: number; radius: number; spawnTick: number; wave: number; pattern: number };
export type GameEvent = { type: number; tick: number; source: number; target: number; pattern: number; reject: number; amount: number; x: number; y: number };
export type Snapshot = {
  tick: number; status: number; configVersion: number; energy: number; energyMax: number;
  attackState: number; pattern: number; target: number; startTick: number; windup: number; active: number;
  manualAim: boolean; aimDirX: number; aimDirY: number;
  planId: string; seed: string; accepted: number; rejected: number; bossHits: number; studentHits: number;
  bossBullets: number; studentBullets: number; overflow: number; fieldW: number; fieldH: number;
  costs: number[]; available: boolean[]; markedTarget: number; eventDropped: number;
  wave: number; wavesCleared: number; wavePhase: 0 | 1; nextWaveStudents: number; waveSpawnTick: number;
  gpaHundredths: number; kills: number; studentsDeployed: number;
  gpaHalfSaturationKills: number; gpaMaxHundredths: number; spawnPreview: { x: number; y: number }[];
  actors: Actor[]; bullets: Bullet[]; warnings: WarningRay[]; events: GameEvent[];
};
export type AimSelection = { pattern: number; dirX: number; dirY: number };
export type AimPreview = { pattern: number; valid: boolean; reason: number; originX: number; originY: number;
  dirX: number; dirY: number; tick: number; fieldW: number; fieldH: number; rays: WarningRay[] };
export type TickInput = { moveX: number; moveY: number; pointerValid: boolean; pointerX: number; pointerY: number; attacks: number;
  aimValid?: boolean; aimX?: number; aimY?: number };
export type CoreClient = { reset(seedLo?: number, seedHi?: number, students?: number, fieldW?: number, fieldH?: number): Snapshot;
  step(input: TickInput): void; preview(pattern: number, dirX: number, dirY: number): AimPreview; snapshot(): Snapshot; dispose(): void };
export type Phase = 'menu' | 'playing' | 'paused' | 'over' | 'error';
