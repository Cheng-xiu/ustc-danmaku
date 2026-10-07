/** Fixed logic time; the application owns the only requestAnimationFrame loop. */
export class FixedStepClock {
  private accumulatorMs = 0;
  private slow = false;
  readonly tickMs = 1000 / 60;

  get tooSlow(): boolean { return this.slow; }

  advance(deltaMs: number): number {
    this.slow = false;
    if (!Number.isFinite(deltaMs) || deltaMs < 0 || deltaMs > 250) {
      this.accumulatorMs = 0;
      this.slow = true;
      return 0;
    }
    this.accumulatorMs += deltaMs;
    const ticks = Math.floor((this.accumulatorMs + 1e-7) / this.tickMs);
    if (ticks > 8) {
      this.accumulatorMs = 0;
      this.slow = true;
      return 0;
    }
    this.accumulatorMs = Math.max(0, this.accumulatorMs - ticks * this.tickMs);
    return ticks;
  }

  reset(): void {
    this.accumulatorMs = 0;
    this.slow = false;
  }
}
