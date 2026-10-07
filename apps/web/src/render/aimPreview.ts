import { Container, Graphics, Text } from 'pixi.js';
import type { AimPreview, WarningRay } from '../types';
import { PATTERN_COLORS } from './warnings';

/** A faint display of C's candidate rays. It never constructs an attack plan. */
export class AimPreviewLayer extends Container {
  private readonly paths = new Graphics();
  private readonly direction = new Graphics();
  private readonly caption = new Text({ text: '', style: {
    fontFamily: 'Microsoft YaHei, Arial, sans-serif', fontSize: 11, fontWeight: '600', fill: 0xcce6ff,
  } });
  private previous: AimPreview | null = null;

  constructor() {
    super();
    this.eventMode = 'none';
    this.caption.anchor.set(0.5);
    this.addChild(this.paths, this.direction, this.caption);
    this.visible = false;
  }

  update(preview: AimPreview | null): void {
    if (!preview) { this.reset(); return; }
    this.visible = true;
    if (sameGeometry(this.previous, preview)) { this.previous = preview; return; }
    this.previous = preview;
    this.paths.clear(); this.direction.clear();
    const color = preview.valid ? PATTERN_COLORS[preview.pattern] ?? 0xa6d2ef
      : preview.reason === 0 || preview.reason === 5 ? 0xe8aa98 : 0x98a6b7;
    const firstTick = preview.rays.reduce((first, ray) => Math.min(first, ray.spawnTick), Infinity);
    const rays = preview.rays.filter(ray => ray.spawnTick === firstTick);
    // Only public first-wave paths are shown. Wide translucent strokes express
    // each ray's actual radius; gaps stay empty rather than filling a hull.
    for (const ray of rays) {
      const segment = clipRay(ray, preview.fieldW, preview.fieldH);
      if (!segment) continue;
      this.paths.moveTo(segment[0], segment[1]).lineTo(segment[2], segment[3])
        .stroke({ width: Math.max(2, ray.radius * 2), color, alpha: preview.valid ? .06 : .035 });
    }
    for (const ray of rays) {
      const segment = clipRay(ray, preview.fieldW, preview.fieldH);
      if (segment) this.paths.moveTo(segment[0], segment[1]).lineTo(segment[2], segment[3]);
    }
    this.paths.stroke({ width: 1, color, alpha: preview.valid ? .30 : .16 });
    const origins = new Set<string>();
    for (const ray of rays) {
      const key = `${ray.x}/${ray.y}`;
      if (origins.has(key) || ray.x < 0 || ray.x > preview.fieldW || ray.y < 0 || ray.y > preview.fieldH) continue;
      origins.add(key);
      this.paths.circle(ray.x, ray.y, Math.max(3, ray.radius + 1));
    }
    this.paths.stroke({ width: 1.1, color, alpha: .55 });

    const length = Math.hypot(preview.dirX, preview.dirY);
    if (length > 1e-8) {
      const dx = preview.dirX / length, dy = preview.dirY / length;
      const arrowRay = { x: preview.originX, y: preview.originY, vx: dx, vy: dy };
      const clipped = clipRay(arrowRay, preview.fieldW, preview.fieldH);
      const distance = clipped ? Math.hypot(clipped[2] - preview.originX, clipped[3] - preview.originY) : 0;
      const reach = Math.max(0, Math.min(150, distance - 6));
      const tail = Math.min(30, reach * .25);
      const tipX = preview.originX + dx * reach, tipY = preview.originY + dy * reach;
      const head = Math.min(13, reach * .45);
      this.direction.moveTo(preview.originX + dx * tail, preview.originY + dy * tail).lineTo(tipX, tipY)
        .stroke({ color, width: 3, alpha: .85 });
      this.direction.poly([tipX, tipY, tipX - dx * head - dy * head * .55, tipY - dy * head + dx * head * .55,
        tipX - dx * head + dy * head * .55, tipY - dy * head - dx * head * .55]).fill({ color, alpha: .9 });
    }
    this.direction.circle(preview.originX, preview.originY, 28).stroke({ color, width: 1.4, alpha: .55 });
    this.caption.text = preview.valid ? '预瞄 · 松开释放' : '预瞄 · 当前不可释放';
    this.caption.style.fill = color;
    const padding = this.caption.width / 2 + 8;
    this.caption.position.set(Math.max(padding, Math.min(preview.fieldW - padding, preview.originX + preview.dirX * 85)),
      Math.max(16, Math.min(preview.fieldH - 16, preview.originY + preview.dirY * 85 - 19)));
    this.caption.visible = true;
  }

  reset(): void {
    this.previous = null;
    this.paths.clear(); this.direction.clear(); this.caption.visible = false; this.visible = false;
  }
}

function sameGeometry(previous: AimPreview | null, next: AimPreview): boolean {
  if (!previous || previous.pattern !== next.pattern || previous.valid !== next.valid || previous.reason !== next.reason
    || previous.originX !== next.originX || previous.originY !== next.originY || previous.dirX !== next.dirX || previous.dirY !== next.dirY
    || previous.fieldW !== next.fieldW || previous.fieldH !== next.fieldH || previous.rays.length !== next.rays.length) return false;
  return previous.rays.every((ray, index) => {
    const other = next.rays[index];
    return ray.x === other.x && ray.y === other.y && ray.vx === other.vx && ray.vy === other.vy && ray.radius === other.radius
      && ray.wave === other.wave && ray.spawnTick - previous.tick === other.spawnTick - next.tick;
  });
}

/** Clip a supplied velocity ray to the displayed field; this is view math. */
function clipRay(ray: Pick<WarningRay, 'x' | 'y' | 'vx' | 'vy'>, width: number, height: number): [number, number, number, number] | null {
  let near = 0, far = Infinity;
  for (const [position, velocity, maximum] of [[ray.x, ray.vx, width], [ray.y, ray.vy, height]]) {
    if (Math.abs(velocity) < 1e-8) { if (position < 0 || position > maximum) return null; continue; }
    let start = -position / velocity, end = (maximum - position) / velocity;
    if (start > end) [start, end] = [end, start];
    near = Math.max(near, start); far = Math.min(far, end);
  }
  if (far < near || !Number.isFinite(far)) return null;
  return [ray.x + ray.vx * near, ray.y + ray.vy * near, ray.x + ray.vx * far, ray.y + ray.vy * far];
}
