import type { TickInput } from '../types';

export type InputCallbacks = { pause(): void; restart(): void };

/** Collects edges until a logical tick consumes them. It never advances the core. */
export class BrowserInput {
  private readonly keys = new Set<string>();
  private pendingAttacks = 0;
  private pointerValid = false;
  private pointerClientX = 0;
  private pointerClientY = 0;
  private pointerX = 0;
  private pointerY = 0;
  private readonly disposers: Array<() => void> = [];

  constructor(private readonly canvas: HTMLCanvasElement, private readonly callbacks: InputCallbacks,
    private readonly project?: (clientX: number, clientY: number) => { x: number; y: number }) {
    canvas.tabIndex = 0;
    canvas.setAttribute('aria-label', '弹幕战场，鼠标移动 Boss，数字 1 到 4 出招');
    this.listen(window, 'pointermove', (event) => this.updatePointer(event as PointerEvent));
    // Leaving the playfield or page retains the last known pointer direction.
    // The browser cannot observe positions outside its window; blur still clears input.
    this.listen(window, 'pointerout', (event) => {
      const pointer = event as PointerEvent;
      if (pointer.relatedTarget === null) this.updatePointer(pointer);
    });
    this.listen(canvas, 'pointercancel', () => { this.pointerValid = false; });
    this.listen(canvas, 'pointerdown', (event) => {
      const pointer = event as PointerEvent;
      if (pointer.button === 0) {
        canvas.focus({ preventScroll: true });
        this.updatePointer(pointer);
      }
    });
    this.listen(canvas, 'contextmenu', (event) => {
      event.preventDefault();
      this.callbacks.pause();
    });
    this.listen(window, 'keydown', (event) => this.keyDown(event as KeyboardEvent));
    this.listen(window, 'keyup', (event) => { this.keys.delete((event as KeyboardEvent).code); });
    this.listen(window, 'blur', () => this.clear());
  }

  private listen(target: EventTarget, type: string, listener: EventListener): void {
    target.addEventListener(type, listener);
    this.disposers.push(() => target.removeEventListener(type, listener));
  }

  private updatePointer(event: PointerEvent): void {
    this.pointerClientX = event.clientX;
    this.pointerClientY = event.clientY;
    this.projectPointer();
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
    if (!movement && !attack && code !== 'Escape' && code !== 'KeyR') return;
    event.preventDefault();
    if (event.repeat || this.keys.has(code)) return;
    this.keys.add(code);
    if (attack) this.requestAttack(Number(code.slice(-1)) - 1);
    else if (code === 'Escape') this.callbacks.pause();
    else if (code === 'KeyR') this.callbacks.restart();
  }

  requestAttack(pattern: number): void {
    if (Number.isInteger(pattern) && pattern >= 0 && pattern < 4) this.pendingAttacks |= 1 << pattern;
  }

  consume(boss?: { x: number; y: number }): TickInput {
    // Resize can change the world projection without any new pointer event.
    // A cleared/cancelled pointer stays invalid until another pointer event.
    if (this.pointerValid) this.projectPointer();
    const held = (...codes: string[]) => codes.some((code) => this.keys.has(code));
    let moveX = Number(held('KeyD', 'ArrowRight')) - Number(held('KeyA', 'ArrowLeft'));
    let moveY = Number(held('KeyS', 'ArrowDown')) - Number(held('KeyW', 'ArrowUp'));
    const length = Math.hypot(moveX, moveY);
    if (length > 1) { moveX /= length; moveY /= length; }
    const input: TickInput = {
      moveX, moveY, pointerValid: length === 0 && this.pointerValid && (!boss || Math.hypot(this.pointerX - boss.x, this.pointerY - boss.y) > 12),
      pointerX: this.pointerX, pointerY: this.pointerY, attacks: this.pendingAttacks,
    };
    this.pendingAttacks = 0;
    return input;
  }

  clear(): void {
    this.keys.clear();
    this.pendingAttacks = 0;
    this.pointerValid = false;
  }

  dispose(): void {
    this.clear();
    for (const dispose of this.disposers) dispose();
    this.disposers.length = 0;
  }
}
