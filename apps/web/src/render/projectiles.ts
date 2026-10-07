import { Container, Sprite, type Texture } from 'pixi.js';
import type { Bullet } from '../types';

/** Stable bullet identities select pooled sprites; shared textures keep batching cheap. */
export class ProjectileLayer extends Container {
  private readonly active = new Map<string, Sprite>();
  private readonly free: Sprite[] = [];
  private readonly seen = new Set<string>();

  constructor(private readonly bossTextures: Texture[], private readonly studentTexture: Texture) { super(); }

  update(bullets: Bullet[]): void {
    this.seen.clear();
    for (const bullet of bullets) this.seen.add(bullet.id);
    // Reclaim first: replacing all 800 entities does not transiently allocate 1600 sprites.
    for (const [id, sprite] of this.active) {
      if (this.seen.has(id)) continue;
      sprite.visible = false;
      this.active.delete(id);
      this.free.push(sprite);
    }
    for (const bullet of bullets) {
      let sprite = this.active.get(bullet.id);
      if (!sprite) {
        sprite = this.free.pop() ?? new Sprite(this.studentTexture);
        sprite.anchor.set(0.5);
        this.active.set(bullet.id, sprite);
        if (sprite.parent !== this) this.addChild(sprite);
      }
      sprite.texture = bullet.faction === 2 ? this.studentTexture : this.bossTextures[bullet.pattern] ?? this.bossTextures[0];
      sprite.position.set(bullet.x, bullet.y);
      // The shared 32px texture has a 12px physical silhouette radius.
      sprite.width = sprite.height = bullet.radius * 32 / 12;
      sprite.rotation = bullet.faction === 2 || bullet.pattern === 3 ? Math.atan2(bullet.vy, bullet.vx) + Math.PI / 2 : 0;
      sprite.visible = true;
    }
  }
}
