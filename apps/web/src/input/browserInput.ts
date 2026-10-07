import type { AimSelection, TickInput } from '../types';

export type InputCallbacks = { pause(): void; restart(): void; isPlaying?(): boolean; getBoss?(): { x: number; y: number } | undefined };

/** Collects edges until a logical tick consumes them. It never advances the core. */
export class BrowserInput {
  private readonly keys = new Set<string>();
  private pendingAttacks = 0;
  private selection: (AimSelection & { owner: string }) | null = null;
  private pendingRelease: (AimSelection & { owner: string }) | null = null;
  private pointerFacing: { x: number; y: number } | null = null;
  private keyboardFacing: { x: number; y: number } | null = null;
  private lastBoss: { x: number; y: number } | undefined;
  private pointerValid = false;
  private pointerPositionKnown = false;
  private pointerClientX = 0;
  private pointerClientY = 0;
  private pointerX = 0;
  private pointerY = 0;
  private readonly disposers: Array<() => void> = [];

  constructor(private readonly canvas: HTMLCanvasElement, private readonly callbacks: InputCallbacks,
    private readonly project?: (clientX: number, clientY: number) => { x: number; y: number }) {
    canvas.tabIndex = 0;
    canvas.setAttribute('aria-label', '弹幕战场，鼠标移动 Boss，按住数字 1 到 4 预瞄，松开发射，空格取消');
    this.listen(window, 'pointermove', (event) => this.updatePointer(event as PointerEvent));
    // Leaving the playfield or page retains the last known pointer direction.
    // The browser cannot observe positions outside its window; blur still clears input.
    this.listen(window, 'pointerout', (event) => {
      const pointer = event as PointerEvent;
      if (pointer.relatedTarget === null) this.updatePointer(pointer);
    });
    this.listen(canvas, 'pointercancel', (event) => {
      this.pointerValid = false;
      this.cancelSelection(`pointer:${(event as PointerEvent).pointerId}`);
    });
    this.listen(canvas, 'pointerdown', (event) => {
      const pointer = event as PointerEvent;
      if (pointer.button === 0) {
        canvas.focus({ preventScroll: true });
        this.updatePointer(pointer);
      }
    });
    this.listen(canvas, 'contextmenu', (event) => {
      event.preventDefault();
      this.clear();
      this.callbacks.pause();
    });
    this.listen(window, 'keydown', (event) => this.keyDown(event as KeyboardEvent));
    this.listen(window, 'keyup', (event) => this.keyUp(event as KeyboardEvent));
    this.listen(window, 'blur', () => this.clear());
  }

  private listen(target: EventTarget, type: string, listener: EventListener): void {
    target.addEventListener(type, listener);
    this.disposers.push(() => target.removeEventListener(type, listener));
  }

  private updatePointer(event: PointerEvent): void {
    const moved = !this.pointerPositionKnown || event.clientX !== this.pointerClientX || event.clientY !== this.pointerClientY;
    this.pointerClientX = event.clientX;
    this.pointerClientY = event.clientY;
    this.pointerPositionKnown = true;
    this.projectPointer();
    const boss = this.callbacks.getBoss?.() ?? this.lastBoss;
    // Movement uses the live pointer target, but aiming changes only when the
    // screen pointer itself moves. Boss motion and resize never turn a held aim.
    if (!moved || !this.pointerValid || !boss) return;
    const dx = this.pointerX - boss.x, dy = this.pointerY - boss.y;
    const distance = Math.hypot(dx, dy);
    if (!Number.isFinite(distance) || distance <= 12) return;
    this.pointerFacing = { x: dx / distance, y: dy / distance };
    if (this.selection) {
      this.selection.dirX = this.pointerFacing.x;
      this.selection.dirY = this.pointerFacing.y;
    }
  }

  private projectPointer(): void {
    const bounds = this.canvas.getBoundingClientRect();
    if (bounds.width <= 0 || bounds.height <= 0) {
      this.pointerValid = false;
      return;
    }
    // Use the scene's current transform; the fallback supports legacy 960 × 720 callers.
    const point = this.project?.(this.pointerClientX, this.pointerClientY);
    this.pointerX = point?.x ?? (this.pointerClientX - bounds.left) * 960 / bounds.width;
    this.pointerY = point?.y ?? (this.pointerClientY - bounds.top) * 720 / bounds.height;
    this.pointerValid = Number.isFinite(this.pointerX) && Number.isFinite(this.pointerY);
  }

  private keyDown(event: KeyboardEvent): void {
    const target = event.target;
    if (target instanceof HTMLElement && (target.isContentEditable
      || /^(INPUT|TEXTAREA|SELECT)$/.test(target.tagName))) return;
    const code = event.code;
    const movement = ['KeyW', 'KeyA', 'KeyS', 'KeyD', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight'].includes(code);
    const attack = /^(Digit|Numpad)[1-4]$/.test(code);
    if (!movement && !attack && code !== 'Escape' && code !== 'KeyR' && code !== 'Space') return;
    event.preventDefault();
    if (event.repeat || this.keys.has(code)) return;
    this.keys.add(code);
    if (attack) this.beginSelection(Number(code.slice(-1)) - 1, `key:${code}`);
    else if (code === 'Space') this.cancelSelection();
    else if (code === 'Escape') { this.clear(); this.callbacks.pause(); }
    else if (code === 'KeyR') { this.clear(); this.callbacks.restart(); }
    else if (movement) this.rememberKeyboardFacing();
  }

  private keyUp(event: KeyboardEvent): void {
    const code = event.code;
    this.keys.delete(code);
    if (/^(Digit|Numpad)[1-4]$/.test(code)) this.endSelection(`key:${code}`);
    this.rememberKeyboardFacing();
  }

  private keyboardMovement(): { moveX: number; moveY: number; length: number } {
    const held = (...codes: string[]) => codes.some((code) => this.keys.has(code));
    let moveX = Number(held('KeyD', 'ArrowRight')) - Number(held('KeyA', 'ArrowLeft'));
    let moveY = Number(held('KeyS', 'ArrowDown')) - Number(held('KeyW', 'ArrowUp'));
    const length = Math.hypot(moveX, moveY);
    if (length > 1) { moveX /= length; moveY /= length; }
    return { moveX, moveY, length };
  }

  private rememberKeyboardFacing(): void {
    const { moveX, moveY, length } = this.keyboardMovement();
    if (length > 0) this.keyboardFacing = { x: moveX, y: moveY };
  }

  beginSelection(pattern: number, owner: string): void {
    if (!Number.isInteger(pattern) || pattern < 0 || pattern >= 4 || typeof owner !== 'string' || !owner) return;
    if (this.callbacks.isPlaying?.() === false) { this.cancelSelection(); return; }
    this.rememberKeyboardFacing();
    // A new hold starts from the current Boss-to-mouse direction. Only this
    // boundary may rebase a stationary cursor; Boss motion during a hold cannot.
    if (this.pointerPositionKnown && this.pointerValid) {
      this.projectPointer();
      const boss = this.callbacks.getBoss?.() ?? this.lastBoss;
      if (boss && this.pointerValid) {
        const dx = this.pointerX - boss.x, dy = this.pointerY - boss.y;
        const distance = Math.hypot(dx, dy);
        if (Number.isFinite(distance) && distance > 12) this.pointerFacing = { x: dx / distance, y: dy / distance };
      }
    }
    const facing = this.pointerValid && this.pointerFacing ? this.pointerFacing : this.keyboardFacing ?? { x: 0, y: -1 };
    // Latest press wins; a release from the replaced owner cannot fire this selection.
    this.selection = { pattern, owner, dirX: facing.x, dirY: facing.y };
  }

  endSelection(owner: string): void {
    if (!this.selection || this.selection.owner !== owner) return;
    if (this.callbacks.isPlaying?.() === false) { this.cancelSelection(); return; }
    const selection = this.selection;
    this.selection = null;
    // Keep the first unconsumed release, never queue shots for a later cooldown.
    if (!this.pendingRelease) this.pendingRelease = { ...selection };
  }

  cancelSelection(owner?: string): void {
    // Capture loss from a replaced pointer must not cancel a newer keyboard aim.
    if (owner === undefined || this.selection?.owner === owner) this.selection = null;
    if (owner === undefined || this.pendingRelease?.owner === owner) this.pendingRelease = null;
    if (owner === undefined) this.pendingAttacks = 0;
  }

  getSelection(): AimSelection | null {
    if (!this.selection || this.callbacks.isPlaying?.() === false) return null;
    const { pattern, dirX, dirY } = this.selection;
    return { pattern, dirX, dirY };
  }

  /** Compatibility edge for old callers; product controls use press/release selection. */
  requestAttack(pattern: number): void {
    if (Number.isInteger(pattern) && pattern >= 0 && pattern < 4) this.pendingAttacks |= 1 << pattern;
  }

  consume(boss?: { x: number; y: number }): TickInput {
    if (boss && Number.isFinite(boss.x) && Number.isFinite(boss.y)) this.lastBoss = { x: boss.x, y: boss.y };
    // Resize can change the world projection without any new pointer event.
    // A cleared/cancelled pointer stays invalid until another pointer event.
    if (this.pointerValid) this.projectPointer();
    const { moveX, moveY, length } = this.keyboardMovement();
    this.rememberKeyboardFacing();
    const release = this.pendingRelease;
    const input: TickInput = {
      moveX, moveY, pointerValid: length === 0 && this.pointerValid && (!boss || Math.hypot(this.pointerX - boss.x, this.pointerY - boss.y) > 12),
      pointerX: this.pointerX, pointerY: this.pointerY, attacks: release ? 1 << release.pattern : this.pendingAttacks,
      aimValid: release !== null, aimX: release?.dirX ?? 0, aimY: release?.dirY ?? 0,
    };
    this.pendingRelease = null;
    this.pendingAttacks = 0;
    return input;
  }

  clear(): void {
    this.keys.clear();
    this.cancelSelection();
    this.pointerValid = false;
    this.pointerPositionKnown = false;
    this.pointerFacing = null;
    this.keyboardFacing = null;
    this.lastBoss = undefined;
  }

  dispose(): void {
    this.clear();
    for (const dispose of this.disposers) dispose();
    this.disposers.length = 0;
  }
}
