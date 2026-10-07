import { Container, Graphics } from 'pixi.js';
import type { Snapshot, WarningRay } from '../types';

export const PATTERN_COLORS = [0x94e275, 0xbb9bff, 0xffcf63, 0x80c7ff];

type Wave = { graphics: Graphics; spawnTick: number };

/** A display projection of C's published rays; no pattern is regenerated here. */
export class WarningLayer extends Container {
  private planId = '';
  private waves: Wave[] = [];

  update(snapshot: Snapshot): void {
    this.visible = snapshot.attackState !== 0;
    if (!this.visible) {
      this.planId = '';
      return;
    }
    if (this.planId !== snapshot.planId) {
      this.planId = snapshot.planId;
      for (const wave of this.waves) wave.graphics.destroy();
      this.waves = [];
      const grouped = new Map<number, WarningRay[]>();
      for (const ray of snapshot.warnings) {
        const group = grouped.get(ray.spawnTick) ?? [];
        group.push(ray);
        grouped.set(ray.spawnTick, group);
      }
      for (const [spawnTick, rays] of grouped) {
        const graphics = new Graphics();
        const color = PATTERN_COLORS[rays[0]?.pattern] ?? PATTERN_COLORS[0];
        for (const ray of rays) {
          const segment = clipRay(ray, snapshot.fieldW, snapshot.fieldH);
          if (!segment) continue;
          graphics.moveTo(segment[0], segment[1]).lineTo(segment[2], segment[3]);
        }
        graphics.stroke({ width: 1.2, color, alpha: 0.48 });
        const origins = new Set<string>();
        for (const ray of rays) {
          const segment = clipRay(ray, snapshot.fieldW, snapshot.fieldH);
          if (!segment) continue;
          // Short direction marks distinguish warning lines from solid live bullets.
          const speed = Math.hypot(ray.vx, ray.vy);
          if (speed > 0) {
            graphics.moveTo(segment[0], segment[1]).lineTo(segment[0] + ray.vx / speed * 18, segment[1] + ray.vy / speed * 18);
          }
          const key = `${ray.x.toFixed(2)}/${ray.y.toFixed(2)}`;
          if (!origins.has(key) && ray.x >= 0 && ray.x <= snapshot.fieldW && ray.y >= 0 && ray.y <= snapshot.fieldH) {
            graphics.circle(ray.x, ray.y, Math.max(4, ray.radius + 2));
            origins.add(key);
          }
        }
        graphics.stroke({ width: 1.5, color, alpha: 0.85 });
        this.addChild(graphics);
        this.waves.push({ graphics, spawnTick });
      }
    }
    for (const wave of this.waves) {
      // Already emitted waves are live bullets. Only future waves retain a warning.
      wave.graphics.visible = snapshot.attackState === 1 || wave.spawnTick >= snapshot.tick;
      wave.graphics.alpha = snapshot.attackState === 1 ? 0.48 : wave.spawnTick - snapshot.tick < 24 ? 0.35 : 0.16;
    }
  }

  reset(): void {
    this.planId = '';
    for (const wave of this.waves) wave.graphics.destroy();
    this.waves = [];
  }
}

/** Clip one public ray against the display rectangle, using its given velocity. */
function clipRay(ray: WarningRay, width: number, height: number): [number, number, number, number] | null {
  let near = 0;
  let far = Infinity;
  for (const [position, velocity, maximum] of [[ray.x, ray.vx, width], [ray.y, ray.vy, height]]) {
    if (Math.abs(velocity) < 1e-8) {
      if (position < 0 || position > maximum) return null;
      continue;
    }
    let start = -position / velocity;
    let end = (maximum - position) / velocity;
    if (start > end) [start, end] = [end, start];
    near = Math.max(near, start);
    far = Math.min(far, end);
  }
  if (far < near || !Number.isFinite(far)) return null;
  return [ray.x + ray.vx * near, ray.y + ray.vy * near, ray.x + ray.vx * far, ray.y + ray.vy * far];
}
