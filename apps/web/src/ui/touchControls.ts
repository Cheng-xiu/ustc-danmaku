import type { AimPreview, AimSelection, Phase, Snapshot } from '../types';
import './touchControls.css';

export type TouchControlCallbacks = {
  joystick(x: number, y: number): void;
  beginAim(pattern: number, owner: string): void;
  endAim(owner: string): void;
  cancelAim(owner?: string): void;
};

type JoystickHold = { id: number; x: number; y: number; radius: number };
type SkillHold = { id: number; owner: string; pattern: number | null; cancelled: boolean };
const skillNames = ['桃李苑', '课表', '金矿', '淋浴'];
const skillTitles = ['桃李苑·绿色圆圈好辣', '选课系统·课表华容道', '一教金矿·绩点淘金', '期末总评·绩点淋浴'];
const skillColors = ['green', 'purple', 'gold', 'blue'];
const previewReasons: Record<number, string> = {
  0: '调整站位', 1: '能量不足', 2: '共享 CD', 3: '等待学生', 4: '尚未选择', 5: '调整方向',
};

/** Touch presentation and ownership only. Movement and attacks still use the shared input/core. */
export class TouchControls {
  private readonly root: HTMLElement;
  private readonly joystickZone: HTMLElement;
  private readonly base: HTMLElement;
  private readonly knob: HTMLElement;
  private readonly wheel: HTMLElement;
  private readonly trigger: HTMLButtonElement;
  private readonly cancelZone: HTMLElement;
  private readonly cancelButton: HTMLButtonElement;
  private readonly hint: HTMLElement;
  private readonly sectors: HTMLElement[] = [];
  private readonly abort = new AbortController();
  private enabled = false;
  private phase: Phase = 'menu';
  private joystickHold: JoystickHold | null = null;
  private skillHold: SkillHold | null = null;

  constructor(host: HTMLElement, private readonly callbacks: TouchControlCallbacks) {
    this.root = document.createElement('section');
    this.root.className = 'demo-touch-controls';
    this.root.dataset.touch = 'controls';
    this.root.setAttribute('aria-label', '触屏操作');
    this.root.innerHTML = `<div class="demo-joystick-zone" data-touch="joystick-zone" role="group" aria-label="左侧摇杆：拖动移动并选择朝向"><div class="demo-joystick-base" data-touch="joystick-base"><span class="demo-joystick-cross"></span><span class="demo-joystick-knob" data-touch="joystick-knob"></span></div><span class="demo-joystick-caption">移动 · 朝向</span></div><div class="demo-touch-actions"><div class="demo-touch-cancel-zone" data-touch="cancel-zone">↑ 再上滑取消</div><div class="demo-touch-wheel" data-touch="skill-wheel" data-open="false" data-selection="-1" data-deadzone="18" data-select-threshold="20" data-hysteresis-degrees="8" role="group" aria-label="技能转盘：按住中心，滑向四招选招，松手释放"><button class="demo-touch-wheel-trigger" type="button" data-touch="wheel-trigger" data-wheel-center="true" aria-label="按住技能转盘，滑动选招，松手释放" aria-expanded="false"><strong data-touch="wheel-title">选招</strong><span data-touch="wheel-status">按住滑动</span></button></div><button class="demo-touch-cancel" type="button" data-touch="cancel" aria-label="取消当前技能预瞄">取消预瞄</button><span class="demo-touch-hint" data-touch="hint" aria-live="polite">按住中心 · 滑动选招</span></div>`;
    this.joystickZone = this.root.querySelector<HTMLElement>('[data-touch="joystick-zone"]')!;
    this.base = this.root.querySelector<HTMLElement>('[data-touch="joystick-base"]')!;
    this.knob = this.root.querySelector<HTMLElement>('[data-touch="joystick-knob"]')!;
    this.wheel = this.root.querySelector<HTMLElement>('[data-touch="skill-wheel"]')!;
    this.trigger = this.root.querySelector<HTMLButtonElement>('[data-touch="wheel-trigger"]')!;
    this.cancelZone = this.root.querySelector<HTMLElement>('[data-touch="cancel-zone"]')!;
    this.cancelButton = this.root.querySelector<HTMLButtonElement>('[data-touch="cancel"]')!;
    this.hint = this.root.querySelector<HTMLElement>('[data-touch="hint"]')!;
    host.append(this.root);
    const options = { signal: this.abort.signal };
    this.joystickZone.addEventListener('pointerdown', event => this.beginJoystick(event), options);
    this.joystickZone.addEventListener('pointermove', event => this.moveJoystick(event), options);
    const stopJoystick = (event: PointerEvent) => {
      if (this.joystickHold?.id === event.pointerId) {
        event.preventDefault();
        this.clearJoystick();
      }
    };
    this.joystickZone.addEventListener('pointerup', stopJoystick, options);
    this.joystickZone.addEventListener('pointercancel', stopJoystick, options);
    this.joystickZone.addEventListener('lostpointercapture', stopJoystick, options);
    skillNames.forEach((name, pattern) => {
      const sector = document.createElement('div');
      sector.className = `demo-touch-sector ${skillColors[pattern]}`;
      sector.id = `demo-touch-sector-${pattern}`;
      sector.dataset.touchSkill = String(pattern);
      sector.dataset.pattern = String(pattern);
      sector.setAttribute('role', 'option');
      sector.title = skillTitles[pattern];
      sector.innerHTML = `<span class="demo-touch-skill-name"><b>${pattern + 1}</b> ${name}</span><span class="demo-touch-skill-cost">—</span><span class="demo-touch-skill-status">就绪</span>`;
      this.wheel.append(sector);
      this.sectors.push(sector);
    });
    this.wheel.addEventListener('pointerdown', event => this.beginSkill(event), options);
    this.wheel.addEventListener('pointermove', event => this.moveSkill(event), options);
    this.wheel.addEventListener('pointerup', event => this.endSkill(event), options);
    const cancelSkill = (event: PointerEvent) => {
      if (this.skillHold?.id === event.pointerId) this.clearSkill(true);
    };
    this.wheel.addEventListener('pointercancel', cancelSkill, options);
    this.wheel.addEventListener('lostpointercapture', cancelSkill, options);
    this.cancelButton.addEventListener('pointerdown', event => {
      if (!this.interactive || event.button !== 0) return;
      event.preventDefault();
      this.clearSkill(true);
      this.callbacks.cancelAim();
      this.hint.textContent = '已取消 · 重新按住技能';
    }, options);
    this.cancelButton.addEventListener('click', event => {
      // Keyboard/assistive activation has no preceding pointerdown.
      if (event.detail === 0 && this.interactive) {
        this.clearSkill(true);
        this.callbacks.cancelAim();
      }
    }, options);
    this.root.addEventListener('contextmenu', event => event.preventDefault(), options);
    this.root.addEventListener('dragstart', event => event.preventDefault(), options);
    window.addEventListener('blur', () => this.reset(), options);
    window.addEventListener('resize', () => this.reset(), options);
    window.addEventListener('orientationchange', () => this.reset(), options);
    window.addEventListener('keydown', event => {
      const target = event.target;
      if (!this.interactive || event.repeat || (target instanceof HTMLElement
        && (target.isContentEditable || /^(INPUT|TEXTAREA|SELECT)$/.test(target.tagName)))) return;
      // Synchronize gesture cancellation with input's key handler, before a
      // sector move in this same frame could start a new selection again.
      if (event.code === 'Space') this.cancelWheelGesture();
      else if (/^(Digit|Numpad)[1-4]$/.test(event.code)) this.clearSkill(true);
    }, options);
    document.addEventListener('visibilitychange', () => { if (document.hidden) this.reset(); }, options);
    this.setEnabled(false);
  }

  private get interactive(): boolean { return this.enabled && this.phase === 'playing'; }

  setEnabled(enabled: boolean): void {
    if (this.enabled !== enabled) this.reset();
    this.enabled = enabled;
    this.root.dataset.enabled = String(enabled);
    this.root.hidden = !this.interactive;
  }

  private beginJoystick(event: PointerEvent): void {
    if (!this.interactive || event.button !== 0 || this.joystickHold) return;
    event.preventDefault();
    const rect = this.joystickZone.getBoundingClientRect();
    const radius = Math.max(28, Math.min(52, rect.width * .25, rect.height * .3));
    const inset = radius + 18;
    // The base floats under the thumb, with the complete ring kept in the left zone.
    const clamp = (value: number, min: number, max: number) => Math.max(min, Math.min(max, value));
    const x = rect.left + clamp(event.clientX - rect.left, inset, Math.max(inset, rect.width - inset));
    const y = rect.top + clamp(event.clientY - rect.top, inset, Math.max(inset, rect.height - inset));
    // The thumb's actual down point is the neutral input anchor. Clamping the
    // visual ring at an edge must never make a stationary first touch move.
    this.joystickHold = { id: event.pointerId, x: event.clientX, y: event.clientY, radius };
    this.base.style.left = `${x - rect.left}px`;
    this.base.style.top = `${y - rect.top}px`;
    this.base.style.setProperty('--joystick-radius', `${radius}px`);
    this.base.dataset.radius = String(radius);
    this.base.classList.add('active');
    try { this.joystickZone.setPointerCapture(event.pointerId); }
    catch { this.clearJoystick(); return; }
    this.moveJoystick(event);
  }

  private moveJoystick(event: PointerEvent): void {
    const hold = this.joystickHold;
    if (!hold || hold.id !== event.pointerId) return;
    event.preventDefault();
    const dx = event.clientX - hold.x;
    const dy = event.clientY - hold.y;
    const distance = Math.hypot(dx, dy);
    const fraction = Math.min(1, distance / hold.radius);
    const strength = fraction <= .12 ? 0 : Math.pow((fraction - .12) / .88, 1.45);
    const x = distance > 0 ? dx / distance * strength : 0;
    const y = distance > 0 ? dy / distance * strength : 0;
    this.base.dataset.axisX = String(x);
    this.base.dataset.axisY = String(y);
    this.knob.style.transform = `translate(${x * hold.radius}px, ${y * hold.radius}px)`;
    this.callbacks.joystick(x, y);
  }

  private clearJoystick(): void {
    const hold = this.joystickHold;
    this.joystickHold = null;
    this.base.classList.remove('active');
    this.base.dataset.axisX = '0';
    this.base.dataset.axisY = '0';
    this.base.style.removeProperty('left');
    this.base.style.removeProperty('top');
    this.knob.style.transform = 'translate(0px, 0px)';
    this.callbacks.joystick(0, 0);
    if (hold && this.joystickZone.hasPointerCapture(hold.id)) {
      try { this.joystickZone.releasePointerCapture(hold.id); } catch { /* Capture already ended. */ }
    }
  }

  private beginSkill(event: PointerEvent): void {
    if (!this.interactive || event.button !== 0) return;
    event.preventDefault();
    this.clearSkill(true);
    const owner = `touch-skill:${event.pointerId}`;
    this.skillHold = { id: event.pointerId, owner, pattern: null, cancelled: false };
    const rect = this.wheel.getBoundingClientRect();
    this.wheel.dataset.radius = String(rect.width / 2);
    this.wheel.dataset.open = 'true';
    this.wheel.dataset.selection = '-1';
    this.trigger.setAttribute('aria-expanded', 'true');
    this.cancelZone.classList.add('visible');
    // A centre press opens the dial without choosing, charging or firing a skill.
    try { this.wheel.setPointerCapture(event.pointerId); }
    catch { this.clearSkill(true); }
  }

  private moveSkill(event: PointerEvent): void {
    const hold = this.skillHold;
    if (!hold || hold.id !== event.pointerId || hold.cancelled) return;
    event.preventDefault();
    const rect = this.cancelZone.getBoundingClientRect();
    const wheel = this.wheel.getBoundingClientRect();
    const inCancelZone = event.clientX >= rect.left - 20 && event.clientX <= rect.right + 20
      && event.clientY <= rect.bottom && event.clientY >= rect.top - 60;
    const aboveWheel = event.clientY < wheel.top - 12 && event.clientX >= wheel.left - 24 && event.clientX <= wheel.right + 24;
    if (inCancelZone || aboveWheel) {
      this.cancelWheelGesture();
      return;
    }
    const dx = event.clientX - wheel.left - wheel.width / 2;
    const dy = event.clientY - wheel.top - wheel.height / 2;
    const distance = Math.hypot(dx, dy);
    if (distance <= 18) {
      if (hold.pattern !== null) this.callbacks.cancelAim(hold.owner);
      hold.pattern = null;
      this.wheel.dataset.selection = '-1';
      this.wheel.removeAttribute('aria-activedescendant');
      return;
    }
    if (distance < 20) return;
    const angle = Math.atan2(dy, dx);
    let pattern = angle >= -Math.PI / 4 && angle < Math.PI / 4 ? 1
      : angle >= Math.PI / 4 && angle < 3 * Math.PI / 4 ? 2
      : angle >= 3 * Math.PI / 4 || angle < -3 * Math.PI / 4 ? 3 : 0;
    if (hold.pattern !== null) {
      const previousAngle = [-Math.PI / 2, 0, Math.PI / 2, Math.PI][hold.pattern];
      const difference = Math.abs(Math.atan2(Math.sin(angle - previousAngle), Math.cos(angle - previousAngle)));
      if (difference <= Math.PI / 4 + 8 * Math.PI / 180) pattern = hold.pattern;
    }
    if (hold.pattern === pattern) return;
    hold.pattern = pattern;
    this.wheel.dataset.selection = String(pattern);
    this.wheel.setAttribute('aria-activedescendant', this.sectors[pattern].id);
    this.callbacks.beginAim(pattern, hold.owner);
  }

  private cancelWheelGesture(): void {
    const hold = this.skillHold;
    if (!hold || hold.cancelled) return;
    hold.cancelled = true;
    this.wheel.classList.add('cancelled');
    this.wheel.dataset.selection = '-1';
    this.wheel.removeAttribute('aria-activedescendant');
    this.cancelZone.classList.add('cancelled');
    this.callbacks.cancelAim(hold.owner);
    this.hint.textContent = '已取消 · 松手后重新选择';
  }

  private endSkill(event: PointerEvent): void {
    const hold = this.skillHold;
    if (!hold || hold.id !== event.pointerId) return;
    event.preventDefault();
    this.moveSkill(event);
    this.clearSkill(false);
    if (!hold.cancelled && hold.pattern !== null && this.interactive) this.callbacks.endAim(hold.owner);
  }

  private clearSkill(cancel: boolean): void {
    const hold = this.skillHold;
    this.skillHold = null;
    this.cancelZone.classList.remove('visible', 'cancelled');
    this.wheel.classList.remove('cancelled');
    this.wheel.dataset.open = 'false';
    this.wheel.dataset.selection = '-1';
    this.wheel.removeAttribute('aria-activedescendant');
    this.trigger.setAttribute('aria-expanded', 'false');
    if (!hold) return;
    if (cancel && !hold.cancelled) this.callbacks.cancelAim(hold.owner);
    if (this.wheel.hasPointerCapture(hold.id)) {
      try { this.wheel.releasePointerCapture(hold.id); } catch { /* Capture already ended. */ }
    }
  }

  render(snapshot: Snapshot | null, phase: Phase, selection: AimSelection | null, preview: AimPreview | null): void {
    if (phase !== this.phase && phase !== 'playing') this.reset();
    this.phase = phase;
    this.root.dataset.phase = phase;
    this.root.hidden = !this.interactive;
    // Input cancellation must invalidate the captured release, while an upward
    // cancellation keeps its visual acknowledgement until that finger lifts.
    if (!selection && this.skillHold && this.skillHold.pattern !== null && !this.skillHold.cancelled) {
      this.cancelWheelGesture();
    }
    this.sectors.forEach((button, pattern) => {
      const aiming = !!selection && selection.pattern === pattern;
      const matchingPreview = aiming && preview?.pattern === pattern ? preview : null;
      const cost = snapshot?.costs[pattern];
      const status = !snapshot ? '待开始' : snapshot.attackState !== 0 ? snapshot.pattern === pattern ? '出招中' : 'CD'
        : snapshot.wavePhase === 1 ? '待入场' : cost !== undefined && snapshot.energy < cost ? `差 ${Math.ceil(cost - snapshot.energy)}`
        : snapshot.available[pattern] ? '就绪' : '站位';
      const visibleStatus = aiming ? matchingPreview?.valid === false ? previewReasons[matchingPreview.reason] ?? '不可释放' : '预瞄中' : status;
      button.setAttribute('aria-disabled', String(!this.interactive || !snapshot));
      button.dataset.ready = String(!!snapshot?.available[pattern]);
      button.dataset.aimReady = aiming ? String(matchingPreview?.valid ?? false) : '';
      button.dataset.status = visibleStatus;
      button.dataset.cost = String(cost ?? '');
      button.classList.toggle('unavailable', !snapshot?.available[pattern]);
      button.classList.toggle('aiming', aiming);
      button.classList.toggle('aim-blocked', aiming && matchingPreview?.valid === false);
      button.classList.toggle('selected', !!snapshot && snapshot.attackState !== 0 && snapshot.pattern === pattern);
      button.setAttribute('aria-selected', String(aiming));
      button.querySelector('.demo-touch-skill-status')!.textContent = visibleStatus;
      button.querySelector('.demo-touch-skill-cost')!.textContent = cost === undefined ? '—' : `${cost} 能量`;
      button.setAttribute('aria-label', `${pattern + 1} ${skillTitles[pattern]}，${cost ?? '—'} 能量，${visibleStatus}。按住预瞄，松手释放，上滑取消。`);
    });
    this.trigger.disabled = !this.interactive || !snapshot;
    const selected = selection?.pattern;
    this.wheel.classList.toggle('aim-blocked', !!selection && preview?.valid === false);
    this.wheel.style.setProperty('--sector-angle', `${(selected ?? 0) * 90}deg`);
    this.wheel.style.setProperty('--selection-color', ['#9ff27d59', '#ba9bff59', '#ffd16c59', '#89cfff59'][selected ?? 0]);
    this.wheel.querySelector<HTMLElement>('[data-touch="wheel-title"]')!.textContent = this.skillHold?.cancelled ? '已取消'
      : selected !== undefined ? skillNames[selected] : this.skillHold ? '滑向技能' : '选招';
    this.wheel.querySelector<HTMLElement>('[data-touch="wheel-status"]')!.textContent = this.skillHold?.cancelled ? '松手重选'
      : selected !== undefined ? preview?.valid === false ? previewReasons[preview.reason] ?? '不可释放' : '松手释放'
      : this.skillHold ? '回中不发' : '按住滑动';
    this.cancelButton.disabled = !this.interactive || (!selection && !this.skillHold);
    this.cancelButton.dataset.active = String(!!selection || !!this.skillHold);
    if (this.skillHold?.cancelled) this.hint.textContent = '已取消 · 松手后重新选择';
    else this.hint.textContent = selection ? preview?.valid === false ? '调整方向/站位 · 再上滑取消' : '摇杆调方向 · 松手释放'
      : this.skillHold ? '滑向选招 · 回中不发' : '按住中心 · 滑动选招';
  }

  reset(): void {
    this.clearSkill(true);
    this.clearJoystick();
    // A release may already have left pointer ownership but still await the
    // next core tick. Clear that request too, only in this control mode.
    if (this.enabled) this.callbacks.cancelAim();
  }

  dispose(): void {
    this.reset();
    this.abort.abort();
    this.root.remove();
  }
}
