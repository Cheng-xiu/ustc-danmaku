import { Application, Assets, Container, Graphics, Rectangle, Sprite, Text, type Texture } from 'pixi.js';
import type { Actor, AimPreview, Snapshot } from '../types';
import { ProjectileLayer } from './projectiles';
import { PATTERN_COLORS, WarningLayer } from './warnings';
import { AimPreviewLayer } from './aimPreview';
import emblemUrl from '../../assets/ustc-emblem.jpg?inline';

type ActorDisplay = { container: Container; sprite: Sprite; mask: Graphics | null; label: Text; health: Graphics; outline: Graphics; hpKey: string };
const names = ['绿色圆圈好辣', '课表华容道', '绩点淘金', '绩点淋浴'];
// The official 638 × 656 JPG includes uneven white margins around this disk.
// Align the disk itself with the hit circle before masking the rectangular image.
const emblemDisk = { x: 326, y: 344, radius: 312 };

export class GameScene {
  readonly canvas: HTMLCanvasElement;
  private readonly textures: Texture[] = [];
  private readonly actorLayer = new Container();
  private readonly background = new Container();
  private fieldW = 960;
  private fieldH = 720;
  private readonly actorDisplays = new Map<number, ActorDisplay>();
  private readonly warnings = new WarningLayer();
  private readonly aimPreview = new AimPreviewLayer();
  private readonly targets = new Graphics();
  private readonly spawnMarkers = new Graphics();
  private readonly spawnLabel = new Text({ text: '', style: { fontFamily: 'Microsoft YaHei, Arial, sans-serif', fontSize: 14, fill: 0xb7deef, fontWeight: '600' } });
  private readonly hitEffects = new Graphics();
  private readonly attackLabel = new Text({ text: '', style: { fontFamily: 'Microsoft YaHei, Arial, sans-serif', fontSize: 12, fill: 0xc2d4e4, fontWeight: '500' } });
  private readonly targetLabel = new Text({ text: '', style: { fontFamily: 'Microsoft YaHei, Arial, sans-serif', fontSize: 9, fill: 0xd9ae75 } });
  private readonly projectiles: ProjectileLayer;
  private readonly bossTexture: Texture;
  private readonly studentTexture: Texture;
  private hits: { key: string; tick: number; x: number; y: number }[] = [];
  private lastTick = -1;

  static async create(host: HTMLElement): Promise<GameScene> {
    const app = new Application();
    await app.init({
      width: 960, height: 720, backgroundColor: 0x0b1828, preference: 'webgl',
      autoStart: false, sharedTicker: false, autoDensity: true,
      resolution: Math.min(window.devicePixelRatio || 1, 2), antialias: true,
    });
    app.stop();
    const emblem = await Assets.load<Texture>(emblemUrl);
    const scene = new GameScene(app, emblem);
    host.append(scene.canvas);
    return scene;
  }

  private constructor(private readonly app: Application, emblem: Texture) {
    this.canvas = app.canvas as HTMLCanvasElement;
    this.bossTexture = emblem;
    this.studentTexture = this.makeStudentTexture();
    const bulletTextures = PATTERN_COLORS.map((color, pattern) => this.makeBulletTexture(color, pattern));
    const studentBullet = this.makeBulletTexture(0xff7698, 4);
    this.projectiles = new ProjectileLayer(bulletTextures, studentBullet);
    this.makeBackground();
    app.stage.addChild(this.aimPreview, this.warnings, this.spawnMarkers, this.projectiles, this.actorLayer, this.targets, this.hitEffects);
    this.attackLabel.anchor.set(0.5, 0);
    this.attackLabel.position.set(480, 34);
    this.targetLabel.anchor.set(0.5, 0);
    this.spawnLabel.anchor.set(0.5, 0);
    this.spawnLabel.position.set(480, 58);
    app.stage.addChild(this.attackLabel, this.targetLabel, this.spawnLabel);
  }

  private texture(graphics: Graphics, size: number): Texture {
    const texture = this.app.renderer.generateTexture({ target: graphics, frame: new Rectangle(0, 0, size, size), resolution: 2, antialias: true });
    graphics.destroy();
    this.textures.push(texture);
    return texture;
  }

  private makeStudentTexture(): Texture {
    const graphics = new Graphics();
    graphics.circle(32, 32, 22).fill(0x173a42).stroke({ color: 0x91d4d2, width: 1.8 });
    graphics.circle(32, 32, 14).fill(0xb9e4de);
    graphics.circle(32, 32, 5).fill(0x436d72);
    graphics.circle(32, 32, 2).fill(0xeffff8);
    return this.texture(graphics, 64);
  }

  private makeBulletTexture(color: number, pattern: number): Texture {
    const graphics = new Graphics();
    if (pattern === 0) graphics.circle(16, 16, 12).fill({ color, alpha: 0.95 }).circle(16, 16, 6).fill(0xd7ffbf);
    else if (pattern === 1) graphics.poly([16, 4, 28, 16, 16, 28, 4, 16]).fill(color).poly([16, 9, 23, 16, 16, 23, 9, 16]).fill(0xece4ff);
    else if (pattern === 2) {
      const vertices: number[] = [];
      for (let i = 0; i < 8; i++) { const radius = i % 2 ? 5 : 12; vertices.push(16 + Math.cos(i * Math.PI / 4) * radius, 16 + Math.sin(i * Math.PI / 4) * radius); }
      graphics.poly(vertices).fill(color).circle(16, 16, 3).fill(0xfff0b8);
    } else if (pattern === 3) graphics.poly([16, 4, 26, 20, 22, 26, 16, 28, 10, 26, 6, 20]).fill(color).circle(16, 20, 4).fill(0xd8eeff);
    else graphics.circle(16, 16, 12).fill(color).circle(16, 16, 6).fill(0xffd5dd);
    return this.texture(graphics, 32);
  }

  private makeBackground(): void {
    for (const child of this.background.removeChildren()) child.destroy({ children: true });
    const w = this.fieldW, h = this.fieldH;
    const sx = w / 960, sy = h / 720;
    const base = new Graphics().rect(0, 0, w, h).fill(0x0b1828);
    // Campus floor markings are decorative; the entire field remains walkable.
    base.roundRect(90 * sx, 82 * sy, 190 * sx, 118 * sy, 12).fill({ color: 0x1b3445, alpha: 0.2 }).stroke({ width: 1, color: 0x3c566a, alpha: 0.2 });
    base.roundRect(680 * sx, 82 * sy, 190 * sx, 118 * sy, 12).fill({ color: 0x1b3445, alpha: 0.2 }).stroke({ width: 1, color: 0x3c566a, alpha: 0.2 });
    base.roundRect(372 * sx, 94 * sy, 216 * sx, 66 * sy, 8).fill({ color: 0x1b3445, alpha: 0.18 }).stroke({ width: 1, color: 0x3c566a, alpha: 0.16 });
    for (let x = 0; x <= w; x += 48) base.moveTo(x, 0).lineTo(x, h);
    for (let y = 0; y <= h; y += 48) base.moveTo(0, y).lineTo(w, y);
    base.stroke({ color: 0x294359, width: 1, alpha: 0.24 });
    base.rect(22, 22, w - 44, h - 44).stroke({ color: 0x416478, width: 1, alpha: 0.35 });
    const corner = new Graphics();
    for (const [x, y, sx, sy] of [[22, 22, 1, 1], [w - 22, 22, -1, 1], [22, h - 22, 1, -1], [w - 22, h - 22, -1, -1]]) corner.moveTo(x, y + 13 * sy).lineTo(x, y).lineTo(x + 13 * sx, y);
    corner.stroke({ color: 0x6a8a9b, width: 1.5, alpha: 0.6 });
    this.background.addChild(base, corner);
    if (!this.background.parent) this.app.stage.addChild(this.background);
    const caption = (text: string, x: number, y: number, size: number, color: number, alpha = 1) => {
      const label = new Text({ text, style: { fontFamily: 'Arial, sans-serif', fontSize: size, fill: color, letterSpacing: size < 12 ? 1.3 : 2.5 } });
      label.position.set(x, y); label.alpha = alpha; this.background.addChild(label);
    };
    caption('CAMPUS / BOSS FIELD', 40, 38, 10, 0x7897ab, 0.7);
    caption('TAOLI GARDEN', 115 * sx, 103 * sy, 9, 0x658373, 0.38);
    caption('TEACHING BUILDING', 699 * sx, 103 * sy, 9, 0x7797ac, 0.38);
    caption('USTC', 427 * sx, 119 * sy, 24, 0x7193a6, 0.12);
    caption('MOVE • AIM • RELEASE', 40, h - 49, 9, 0x547086, 0.65);
    caption('HOLD · AIM · RELEASE', Math.max(40, w - 161), 38, 9, 0xa7b292, 0.62);
  }

  /** Resize the display only. The accepted plan and this round's field stay frozen. */
  resize(width: number, height: number): void {
    this.app.renderer.resize(Math.max(1, Math.floor(width)), Math.max(1, Math.floor(height)));
    const scale = Math.min(this.app.screen.width / this.fieldW, this.app.screen.height / this.fieldH);
    this.app.stage.scale.set(scale);
    this.app.stage.position.set((this.app.screen.width - this.fieldW * scale) / 2,
      (this.app.screen.height - this.fieldH * scale) / 2);
  }

  screenToWorld(clientX: number, clientY: number): { x: number; y: number } {
    const bounds = this.canvas.getBoundingClientRect();
    return {
      x: ((clientX - bounds.left) * this.app.screen.width / bounds.width - this.app.stage.x) / this.app.stage.scale.x,
      y: ((clientY - bounds.top) * this.app.screen.height / bounds.height - this.app.stage.y) / this.app.stage.scale.y,
    };
  }

  draw(snapshot: Snapshot): void {
    if (snapshot.fieldW !== this.fieldW || snapshot.fieldH !== this.fieldH) {
      this.fieldW = snapshot.fieldW; this.fieldH = snapshot.fieldH;
      this.makeBackground();
      this.attackLabel.position.set(this.fieldW / 2, 34);
      this.spawnLabel.position.set(this.fieldW / 2, 58);
      this.resize(this.app.screen.width, this.app.screen.height);
    }
    if (snapshot.tick < this.lastTick) {
      this.hits = [];
      this.warnings.reset();
    }
    this.lastTick = snapshot.tick;
    this.warnings.update(snapshot);
    this.projectiles.update(snapshot.bullets);
    for (const actor of snapshot.actors) this.drawActor(actor, snapshot.tick);
    const currentIds = new Set(snapshot.actors.map((actor) => actor.id));
    for (const [id, display] of this.actorDisplays) {
      if (currentIds.has(id)) continue;
      display.container.destroy({ children: true });
      this.actorDisplays.delete(id);
    }
    this.drawSpawns(snapshot);
    this.drawTargets(snapshot);
    for (const event of snapshot.events) {
      if (event.type !== 7) continue;
      const key = `${event.tick}/${event.source}/${event.target}/${event.x}/${event.y}`;
      if (!this.hits.some((hit) => hit.key === key)) this.hits.push({ key, tick: event.tick, x: event.x, y: event.y });
    }
    this.hits = this.hits.filter((hit) => snapshot.tick - hit.tick < 18 && snapshot.tick >= hit.tick);
    this.hitEffects.clear();
    for (const hit of this.hits) {
      const age = snapshot.tick - hit.tick;
      this.hitEffects.circle(hit.x, hit.y, 8 + age * 1.5).stroke({ width: 2, color: 0xffe0b4, alpha: 0.8 * (1 - age / 18) });
    }
    const text = snapshot.attackState === 0 ? '' : `${snapshot.attackState === 1 ? '预警' : '攻击'}  ·  ${names[snapshot.pattern] ?? '招式'}  ·  ${snapshot.attackState === 1 ? '位置已锁定' : '躲开红色反击弹'}`;
    if (this.attackLabel.text !== text) this.attackLabel.text = text;
    this.attackLabel.style.fill = PATTERN_COLORS[snapshot.pattern] ?? 0xc2d4e4;
  }

  /** C provides candidate geometry; this layer only displays it before release. */
  drawAim(preview: AimPreview | null): void { this.aimPreview.update(preview); }

  private drawActor(actor: Actor, tick: number): void {
    let display = this.actorDisplays.get(actor.id);
    const boss = actor.faction === 1;
    if (!display) {
      const container = new Container();
      const sprite = new Sprite(boss ? this.bossTexture : this.studentTexture);
      sprite.anchor.set(0.5);
      const label = new Text({ text: boss ? 'YOU / BOSS' : `学生 ${actor.id}`, style: { fontFamily: 'Microsoft YaHei, Arial, sans-serif', fontSize: boss ? 10 : 9, fill: boss ? 0xe9bf8f : 0x8ec4c4, letterSpacing: boss ? 1 : 0 } });
      label.anchor.set(0.5, 1);
      const health = new Graphics();
      const outline = new Graphics();
      const mask = boss ? new Graphics() : null;
      container.addChild(sprite);
      if (mask) {
        mask.circle(0, 0, actor.radius).fill(0xffffff);
        container.addChild(mask);
        sprite.mask = mask;
      }
      container.addChild(outline, label, health);
      this.actorLayer.addChild(container);
      display = { container, sprite, mask, label, health, outline, hpKey: '' };
      this.actorDisplays.set(actor.id, display);
    }
    display.container.position.set(actor.x, actor.y);
    if (boss) {
      const scale = actor.radius / emblemDisk.radius;
      display.sprite.scale.set(scale);
      display.sprite.position.set((this.bossTexture.width / 2 - emblemDisk.x) * scale, (this.bossTexture.height / 2 - emblemDisk.y) * scale);
    } else display.sprite.width = display.sprite.height = actor.radius * 64 / 22;
    display.label.y = -actor.radius - 8;
    display.container.alpha = !actor.alive ? 0.16 : actor.invuln > 0 && Math.floor(tick / 3) % 2 === 0 ? 0.5 : 1;
    const hpKey = `${actor.hp}/${actor.hpMax}/${actor.radius}`;
    if (display.hpKey !== hpKey) {
      display.hpKey = hpKey;
      const width = actor.radius * 1.6;
      const gap = 3;
      const segment = (width - gap * (actor.hpMax - 1)) / Math.max(1, actor.hpMax);
      display.health.clear();
      display.outline.clear();
      if (display.mask) display.mask.clear().circle(0, 0, actor.radius).fill(0xffffff);
      if (boss) display.outline.circle(0, 0, actor.radius).stroke({ color: 0xffc58a, width: 1.5, alpha: 0.9 });
      for (let hp = 0; hp < actor.hpMax; hp++) display.health.rect(-width / 2 + hp * (segment + gap), actor.radius + 5, segment, 3).fill(hp < actor.hp ? boss ? 0xffb877 : 0x80cac9 : 0x2e4557);
    }
  }

  private drawSpawns(snapshot: Snapshot): void {
    this.spawnMarkers.clear();
    this.spawnLabel.visible = snapshot.wavePhase === 1;
    if (snapshot.wavePhase !== 1) return;
    const remaining = Math.max(0, snapshot.waveSpawnTick - snapshot.tick) / 60;
    this.spawnLabel.text = `第 ${snapshot.wave + 1} 波 · ${snapshot.nextWaveStudents} 名学生 · ${remaining.toFixed(1)} 秒后入场`;
    for (const point of snapshot.spawnPreview) {
      this.spawnMarkers.circle(point.x, point.y, 24).fill({ color: 0x76b8d8, alpha: 0.08 }).stroke({ color: 0x9bd7ef, width: 1.5, alpha: 0.7 });
      this.spawnMarkers.moveTo(point.x - 7, point.y).lineTo(point.x + 7, point.y);
      this.spawnMarkers.moveTo(point.x, point.y - 7).lineTo(point.x, point.y + 7);
      this.spawnMarkers.stroke({ color: 0x9bd7ef, width: 1.2, alpha: 0.7 });
    }
  }

  private drawTargets(snapshot: Snapshot): void {
    this.targets.clear();
    const marked = snapshot.actors.find((actor) => actor.id === snapshot.markedTarget && actor.alive);
    if (marked) {
      const radius = marked.radius + 8;
      for (let i = 0; i < 4; i++) {
        const start = i * Math.PI / 2 + 0.2;
        this.targets.moveTo(marked.x + Math.cos(start) * radius, marked.y + Math.sin(start) * radius);
        this.targets.arc(marked.x, marked.y, radius, start, i * Math.PI / 2 + 0.85);
      }
      this.targets.stroke({ color: 0xb1c297, alpha: 0.7, width: 1.5 });
    }
    const locked = snapshot.attackState !== 0 ? snapshot.actors.find((actor) => actor.id === snapshot.target) : undefined;
    this.targetLabel.visible = !!locked;
    if (locked) {
      const r = locked.radius + 14;
      this.targets.poly([locked.x, locked.y - r, locked.x + r, locked.y, locked.x, locked.y + r, locked.x - r, locked.y]).stroke({ color: 0xf3bd77, width: 1.3, alpha: 0.65 });
      this.targetLabel.position.set(locked.x, locked.y + r + 5);
      const text = `锁定 #${locked.id}`;
      if (this.targetLabel.text !== text) this.targetLabel.text = text;
    }
  }

  /** Explicit generation boundary, including restarts that reach the same tick/plan ID. */
  reset(): void {
    this.hits = [];
    this.lastTick = -1;
    this.warnings.reset();
    this.aimPreview.reset();
    this.warnings.visible = false;
    this.projectiles.update([]);
    this.targets.clear();
    this.spawnMarkers.clear();
    this.spawnLabel.visible = false;
    this.hitEffects.clear();
    this.attackLabel.text = '';
    this.targetLabel.visible = false;
  }

  render(): void { this.app.render(); }

  destroy(): void {
    this.aimPreview.reset();
    this.app.destroy({ removeView: true }, { children: true, texture: false, textureSource: false });
    for (const texture of this.textures) texture.destroy(true);
    this.textures.length = 0;
  }
}
