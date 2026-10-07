import type { AimPreview, AimSelection, Phase, Snapshot } from '../types';
import './hud.css';

export type HUDCallbacks = { start(): void; pause(): void; restart(): void;
  beginAim(pattern: number, owner: string): void; endAim(owner: string): void; cancelAim(owner?: string): void };
const noop = () => {};
const formatTime = (tick: number) => {
  const seconds = Math.floor(tick / 60);
  return `${String(Math.floor(seconds / 60)).padStart(2, '0')}:${String(seconds % 60).padStart(2, '0')}`;
};
const patterns = [
  { name: '绿色圆圈好辣', short: '环震', source: '桃李苑', purpose: '近中距宽弧', color: 'green', description: '按住后用鼠标确定方向，松开释放两波前向宽弧。靠近敌人寻找进攻机会，范围以场内预瞄为准。' },
  { name: '课表华容道', short: '封路', source: '选课系统', purpose: '分列封路', color: 'purple', description: '沿鼠标方向压下分列弹墙，封住两带、留出一带。适合持续封锁路线，压制分散敌人。' },
  { name: '绩点淘金', short: '速攻', source: '一教金矿', purpose: '单目标速攻', color: 'gold', description: '从角色位置向鼠标方向打出窄扇三连。消耗低，适合追击与收尾；距任一学生不足 120 时无法释放。' },
  { name: '绩点淋浴', short: '弹雨', source: '期末总评', purpose: '满能量全场弹雨', color: 'blue', description: '攒满能量后沿鼠标方向释放大范围弹雨，保留公开的扫描缝隙。适合学生较多时集中压制。' },
];

export class HUD {
  private callbacks: HUDCallbacks;
  private readonly panel: HTMLElement;
  private readonly overlay: HTMLElement;
  private readonly attackButtons: HTMLButtonElement[] = [];
  private readonly students: HTMLElement[] = [];
  private lastEvent = '';
  private noticeUntil = -1;
  private notice = '准备好后，选择一招开始进攻。';
  private noticeKind = '';
  private previousPhase: Phase | null = null;
  private previousSnapshot: Snapshot | null | undefined;
  private previousReason = '';
  private currentPhase: Phase = 'menu';
  private aimSelection: AimSelection | null = null;
  private aimPreview: AimPreview | null = null;
  private pointerHold: { id: number; owner: string; button: HTMLButtonElement } | null = null;
  private readonly lookup = new Map<string, HTMLElement>();

  constructor(host: HTMLElement, callbacks?: HUDCallbacks) {
    this.callbacks = callbacks ?? { start: noop, pause: noop, restart: noop, beginAim: noop, endAim: noop, cancelAim: noop };
    this.panel = document.createElement('section');
    this.panel.className = 'demo-hud';
    this.panel.setAttribute('aria-label', '战斗状态与出招');
    this.panel.innerHTML = `
      <section class="demo-match-summary"><div class="demo-summary-head"><span class="demo-phase" data-ui="phase">准备开始</span><span class="demo-clock" data-ui="clock">00:00</span></div><div class="demo-score-row"><span class="demo-gpa">GPA <strong data-ui="gpa">0.00</strong></span><span>第 <b data-ui="wave">1</b> 波</span><span>击倒 <b data-ui="kills">0</b></span><span data-ui="student-count">3 人存活</span></div><div class="demo-health-row"><span>生命 <strong data-ui="health-text">6 / 6</strong></span><div class="demo-health" data-ui="health" aria-label="剩余生命"></div></div><small data-ui="gpa-rule" hidden>击倒数计算 · 趋近 4.30</small><div data-ui="student-list" hidden></div></section>
      <section class="demo-energy-box"><div class="demo-meter-label"><span>共享能量</span><strong data-ui="energy-text">60 / 100</strong></div><div class="demo-energy-track" data-ui="energy-track" role="progressbar" aria-label="共享能量" aria-valuemin="0" aria-valuemax="100"><div data-ui="energy-fill"></div></div><span class="demo-meter-caption" data-ui="energy-caption">蓄能中 · 成功出招才扣除</span></section>
      <section class="demo-cd-box" data-ui="cd-box"><div class="demo-meter-label"><span>共享出招 CD</span><strong data-ui="cd-text">就绪</strong></div><div class="demo-attack-track" data-ui="cd-track" role="progressbar" aria-label="当前招式剩余进度" aria-valuemin="0" aria-valuemax="100"><div data-ui="attack-fill"></div></div><div class="demo-attack-state" data-ui="attack-state">就绪 · 可选择下一招</div></section>
      <section class="demo-skills" aria-label="按住 1 到 4 预瞄，松开释放，空格取消"><div data-ui="skills"></div></section>
      <div class="demo-notice" data-ui="notice" role="status" aria-live="polite"></div>
      <div class="demo-system-buttons"><button type="button" data-ui="pause">暂停 <kbd>Esc</kbd></button><button type="button" data-ui="restart">重开 <kbd>R</kbd></button></div>`;
    host.append(this.panel);
    this.panel.querySelectorAll<HTMLElement>('[data-ui]').forEach((element) => this.lookup.set(element.dataset.ui!, element));
    patterns.forEach((pattern, index) => {
      const button = document.createElement('button');
      button.type = 'button';
      button.className = `demo-skill ${pattern.color}`;
      button.innerHTML = `<kbd>${index + 1}</kbd><span class="demo-skill-copy"><strong>${pattern.short}</strong><small class="demo-skill-status">就绪</small></span><span class="demo-cost"></span>`;
      button.addEventListener('pointerdown', event => {
        if (event.button !== 0 || this.currentPhase !== 'playing') return;
        event.preventDefault();
        this.clearPointerHold(true);
        const owner = `pointer:${event.pointerId}`;
        this.pointerHold = { id: event.pointerId, owner, button };
        this.callbacks.beginAim(index, owner);
        try { button.setPointerCapture(event.pointerId); }
        catch { this.clearPointerHold(true); }
      });
      button.addEventListener('pointerup', event => {
        const held = this.pointerHold;
        if (!held || held.id !== event.pointerId || held.button !== button) return;
        event.preventDefault();
        // Clear ownership before releasing capture. The resulting lost-capture
        // event must not cancel the release or produce a second attack.
        this.clearPointerHold(false);
        this.callbacks.endAim(held.owner);
      });
      const cancel = (event: PointerEvent) => {
        if (this.pointerHold?.id === event.pointerId && this.pointerHold.button === button) this.clearPointerHold(true);
      };
      button.addEventListener('pointercancel', cancel);
      button.addEventListener('lostpointercapture', cancel);
      this.ui('skills').append(button);
      this.attackButtons.push(button);
    });
    for (let index = 0; index < 8; index++) {
      const row = document.createElement('div');
      row.className = 'demo-student-row';
      row.innerHTML = '<span></span><div></div>';
      this.ui('student-list').append(row);
      this.students.push(row);
    }
    this.ui('pause').addEventListener('click', () => this.callbacks.pause());
    this.ui('restart').addEventListener('click', () => this.callbacks.restart());
    this.overlay = document.createElement('section');
    this.overlay.className = 'demo-status-overlay';
    this.overlay.setAttribute('aria-label', '游戏状态');
    this.overlay.innerHTML = `<div class="demo-status-card"><span class="demo-eyebrow" data-overlay="tag">科大弹幕录 · 无尽 Boss</span><h2 data-overlay="title">四招，撑到最后。</h2><p class="demo-overlay-description" data-overlay="description"></p><div class="demo-overlay-skills" data-overlay="skills">${patterns.map((pattern, index) => `<article class="demo-skill-guide ${pattern.color}"><div><kbd>${index + 1}</kbd><strong>${pattern.purpose}</strong><span data-guide-cost="${index}">— 能量</span></div><h3>${pattern.source} · ${pattern.name}</h3><p>${pattern.description}</p></article>`).join('')}</div><div class="demo-overlay-controls"><span><kbd>鼠标</kbd>移动并瞄准；预瞄中可继续用 WASD 走位</span><span><kbd>WASD / ↑↓←→</kbd>键盘八方向移动，不改变瞄准方向</span><span><kbd>按住 1 — 4</kbd>预瞄，松开释放；也可拖动技能按钮后松手</span><span><kbd>空格</kbd>取消预瞄 <kbd>Esc / 右键</kbd>暂停 <kbd>R</kbd>重开</span></div><dl class="demo-report" data-overlay="report" hidden><div><dt>最终 GPA</dt><dd data-report="gpa">0.00</dd></div><div><dt>存活时间</dt><dd data-report="time">00:00</dd></div><div><dt>到达波次 / 清空波次</dt><dd data-report="waves">1 / 0</dd></div><div><dt>累计击倒 / 累计派出</dt><dd data-report="kills">0 / 3</dd></div></dl><button type="button" class="demo-primary-action" data-overlay="action">开始试玩 <span>→</span></button><p class="demo-overlay-footnote">清波后增加 1 人，最多同时 8 人；生命耗尽时结算，无胜利终点。<br>GPA 只由累计击倒数决定；学生当前使用脚本 AI。</p></div>`;
    (host.parentElement ?? host).append(this.overlay);
    this.overlay.querySelector<HTMLButtonElement>('[data-overlay="action"]')!.addEventListener('click', () => {
      if (this.previousPhase === 'menu') this.callbacks.start();
      else if (this.previousPhase === 'paused') this.callbacks.pause();
      else this.callbacks.restart();
    });
  }

  private ui(name: string): HTMLElement { return this.lookup.get(name)!; }
  setCallbacks(callbacks: HUDCallbacks): void { this.callbacks = callbacks; }

  private clearPointerHold(cancel: boolean): void {
    const held = this.pointerHold;
    if (!held) return;
    this.pointerHold = null;
    if (held.button.hasPointerCapture(held.id)) {
      try { held.button.releasePointerCapture(held.id); } catch { /* Already lost by the browser. */ }
    }
    if (cancel) this.callbacks.cancelAim(held.owner);
  }

  private paintAim(): void {
    const selection = this.currentPhase === 'playing' ? this.aimSelection : null;
    const preview = selection ? this.aimPreview : null;
    this.attackButtons.forEach((button, index) => {
      const aiming = selection?.pattern === index;
      button.classList.toggle('aiming', aiming);
      button.classList.toggle('aim-blocked', aiming && preview?.valid === false);
      button.dataset.aimReady = aiming ? String(preview?.valid ?? false) : '';
      button.setAttribute('aria-pressed', String(aiming || button.classList.contains('selected')));
      button.querySelector('.demo-skill-status')!.textContent = aiming
        ? preview?.valid === false ? '不可发' : '预瞄'
        : button.dataset.baseStatus ?? '就绪';
    });
    let message = this.notice;
    let kind = this.noticeKind;
    if (selection) {
      const reasons: Record<number, string> = {
        0: '出生几何不安全，调整位置或方向', 1: '能量不足', 2: '共享 CD 尚未结束',
        3: '当前没有存活学生', 4: '没有出招请求', 5: '瞄准方向无效',
      };
      const readiness = preview ? preview.valid ? '可释放' : `松手会拒绝：${reasons[preview.reason] ?? '当前无法释放'}` : '正在更新方向';
      message = `按住预瞄 · 松开释放 · 空格取消 · ${patterns[selection.pattern]?.name ?? '技能'} · ${readiness}`;
      kind = preview?.valid === false ? 'aim-rejected' : 'aiming';
    }
    this.ui('notice').textContent = message;
    this.ui('notice').title = message;
    this.ui('notice').dataset.kind = kind;
  }

  /** Display-only aiming state; also updates on frames with no logical tick. */
  renderAim(selection: AimSelection | null, preview: AimPreview | null): void {
    this.aimSelection = this.currentPhase === 'playing' ? selection : null;
    this.aimPreview = this.aimSelection && preview?.pattern === this.aimSelection.pattern ? preview : null;
    // Space/phase cancellation clears input ownership before this render. Do
    // not let the later physical pointerup issue a release after cancellation.
    if (!this.aimSelection) this.clearPointerHold(false);
    this.paintAim();
  }

  render(snapshot: Snapshot | null, phase: Phase, reason = ''): void {
    if (snapshot === this.previousSnapshot && phase === this.previousPhase && reason === this.previousReason) return;
    this.currentPhase = phase;
    if (phase !== 'playing') {
      this.clearPointerHold(true);
      this.aimSelection = null;
      this.aimPreview = null;
    }
    const stateText: Record<Phase, string> = { menu: '准备开始', playing: '无尽对局', paused: '已暂停', over: 'Boss 已倒下', error: '加载失败' };
    this.ui('phase').textContent = phase === 'playing' && snapshot?.wavePhase === 1 ? '下一波准备中' : stateText[phase];
    this.ui('phase').dataset.phase = phase;
    this.ui('pause').textContent = phase === 'paused' ? '继续  Esc' : '暂停  Esc';
    (this.ui('pause') as HTMLButtonElement).disabled = phase !== 'playing' && phase !== 'paused';
    (this.ui('restart') as HTMLButtonElement).disabled = phase === 'error';
    if (snapshot) {
      if (snapshot.tick === 0 && (phase !== this.previousPhase || this.previousSnapshot?.tick !== 0)) {
        this.notice = '按住 1—4 或技能按钮预瞄，松开释放；空格取消。';
        this.noticeKind = '';
        this.lastEvent = '';
        this.noticeUntil = -1;
      }
      this.ui('clock').textContent = formatTime(snapshot.tick);
      this.ui('gpa').textContent = (snapshot.gpaHundredths / 100).toFixed(2);
      this.ui('gpa-rule').textContent = `增长渐缓 · 趋近 ${(snapshot.gpaMaxHundredths / 100).toFixed(2)}`;
      this.ui('gpa-rule').title = `GPA = ${(snapshot.gpaMaxHundredths / 100).toFixed(2)} × 击倒人数 / (击倒人数 + ${snapshot.gpaHalfSaturationKills})；只由累计击倒人数决定，显示保留两位小数。`;
      this.ui('gpa').title = this.ui('gpa-rule').title;
      this.ui('wave').textContent = String(snapshot.wave);
      this.ui('kills').textContent = String(snapshot.kills);
      const boss = snapshot.actors[0];
      if (boss) {
        this.ui('health-text').textContent = `${Math.max(0, boss.hp)} / ${boss.hpMax}`;
        const health = this.ui('health');
        if (health.childElementCount !== boss.hpMax) {
          health.replaceChildren(...Array.from({ length: boss.hpMax }, () => document.createElement('span')));
        }
        Array.from(health.children).forEach((element, index) => element.classList.toggle('lost', index >= boss.hp));
      }
      this.ui('energy-text').textContent = `${Math.floor(snapshot.energy)} / ${snapshot.energyMax}`;
      const energyPercent = Math.max(0, Math.min(100, snapshot.energy / Math.max(1, snapshot.energyMax) * 100));
      this.ui('energy-fill').style.width = `${energyPercent}%`;
      this.ui('energy-track').setAttribute('aria-valuenow', String(Math.floor(snapshot.energy)));
      this.ui('energy-track').setAttribute('aria-valuemax', String(snapshot.energyMax));
      this.ui('energy-track').classList.toggle('full', snapshot.energy >= snapshot.energyMax);
      this.ui('energy-caption').textContent = phase === 'paused' ? '暂停中 · 蓄能已冻结' : snapshot.energy >= snapshot.energyMax ? '能量已满 · 等待出招机会' : '蓄能中 · 成功出招才扣除';
      this.attackButtons.forEach((button, index) => {
        button.disabled = phase !== 'playing';
        button.classList.toggle('unavailable', !snapshot.available[index]);
        button.classList.toggle('selected', snapshot.attackState !== 0 && snapshot.pattern === index);
        button.setAttribute('aria-pressed', String(snapshot.attackState !== 0 && snapshot.pattern === index));
        button.querySelector('.demo-cost')!.textContent = `${snapshot.costs[index] ?? '—'}`;
        const status = snapshot.attackState !== 0 ? (snapshot.pattern === index ? '出招中' : '锁定')
          : snapshot.wavePhase === 1 ? '待入场'
          : snapshot.energy < snapshot.costs[index] ? `差 ${Math.ceil(snapshot.costs[index] - snapshot.energy)}`
          : snapshot.available[index] ? '就绪' : '站位';
        button.querySelector('.demo-skill-status')!.textContent = status;
        button.dataset.baseStatus = status;
        button.title = `${index + 1} · ${patterns[index].name}，消耗 ${snapshot.costs[index]} 能量；${status}。按住预瞄，松开释放，空格取消。`;
        button.setAttribute('aria-label', button.title);
        this.overlay.querySelector<HTMLElement>(`[data-guide-cost="${index}"]`)!.textContent = `${snapshot.costs[index]} 能量`;
      });
      const attackName = patterns[snapshot.pattern]?.name ?? '攻击';
      const elapsed = Math.max(0, snapshot.tick - snapshot.startTick);
      const total = snapshot.attackState === 1 ? snapshot.windup : snapshot.active;
      const localElapsed = snapshot.attackState === 1 ? elapsed : Math.max(0, elapsed - snapshot.windup);
      const remaining = Math.max(0, total - localElapsed) / 60;
      // The core permits one plan at a time. This is the visible remainder of
      // that shared lock (windup + active), not a new per-skill cooldown rule.
      const lockTotal = Math.max(1, snapshot.windup + snapshot.active + 1);
      const lockRemaining = snapshot.attackState === 0 ? 0 : Math.max(1, snapshot.startTick + lockTotal - snapshot.tick);
      const lockPercent = lockRemaining / lockTotal * 100;
      this.ui('cd-text').textContent = snapshot.attackState === 0 ? '就绪' : `${(Math.ceil(lockRemaining / 6) / 10).toFixed(1)}s`;
      this.ui('cd-box').dataset.state = snapshot.attackState === 0 ? 'ready' : snapshot.attackState === 1 ? 'windup' : 'active';
      this.ui('cd-track').setAttribute('aria-valuenow', String(Math.round(lockPercent)));
      this.ui('attack-state').textContent = snapshot.wavePhase === 1 ? `入场倒计时 · ${(Math.max(0, snapshot.waveSpawnTick - snapshot.tick) / 60).toFixed(1)}s`
        : snapshot.attackState === 0 ? '就绪 · 可选择下一招'
        : `${snapshot.attackState === 1 ? '预警' : '攻击'} · ${attackName} · ${remaining.toFixed(1)}s`;
      this.ui('attack-fill').style.width = `${Math.max(0, Math.min(100, lockPercent))}%`;
      const students = snapshot.actors.slice(1);
      this.ui('student-list').classList.toggle('many', students.length >= 4);
      this.ui('student-count').textContent = snapshot.wavePhase === 1 ? `下波 ${snapshot.nextWaveStudents} 人`
        : `${students.filter((student) => student.alive).length} / ${students.length} 人存活`;
      this.students.forEach((row, index) => {
        const student = students[index];
        row.hidden = !student;
        if (!student) return;
        row.classList.toggle('defeated', !student.alive);
        row.classList.toggle('targeted', student.id === snapshot.markedTarget && student.alive);
        row.children[0].textContent = `学生 ${student.id}${student.id === snapshot.target && snapshot.attackState !== 0 ? ' · 锁定' : ''}`;
        const hearts = row.children[1];
        if (hearts.childElementCount !== student.hpMax) hearts.replaceChildren(...Array.from({ length: student.hpMax }, () => document.createElement('i')));
        Array.from(hearts.children).forEach((element, hp) => element.classList.toggle('lost', hp >= student.hp));
      });
      const relevant = [...snapshot.events].reverse().find((event) => event.type === 1 || event.type === 2);
      if (relevant) {
        const key = `${relevant.tick}/${relevant.type}/${relevant.pattern}/${relevant.reject}`;
        if (key !== this.lastEvent) {
          this.lastEvent = key;
          this.noticeUntil = snapshot.tick + 150;
          const name = patterns[relevant.pattern]?.name ?? '出招';
          if (relevant.type === 2) {
            const reasons: Record<number, string> = { 0: '出生几何不安全，移动后重试', 1: '能量不足，稍等蓄能', 2: '这一招尚未结束', 3: '没有存活目标', 4: '没有出招请求' };
            this.notice = `${name}：${reasons[relevant.reject] ?? '请求被拒绝'}。`;
            this.noticeKind = 'rejected';
          } else {
            this.notice = `${name}已接受，预警位置已锁定。`;
            this.noticeKind = 'accepted';
          }
        }
      }
      if (snapshot.tick > this.noticeUntil && this.noticeUntil >= 0) {
        this.notice = snapshot.attackState === 0 ? '选择下一招，或移动躲避并等待蓄能。' : '预警位置不会追随目标；这一招结束后可再出招。';
        this.noticeKind = '';
      }
      if (snapshot.wavePhase === 1) {
        this.notice = `本波已清空。蓝色标记处将派出 ${snapshot.nextWaveStudents} 名学生，生命与能量延续。`;
        this.noticeKind = '';
      } else if (this.previousSnapshot?.wavePhase === 1) {
        this.notice = `第 ${snapshot.wave} 波已入场。优先躲避反击，再寻找出招机会。`;
        this.noticeUntil = snapshot.tick + 120;
        this.noticeKind = '';
      }
      if (phase === 'over') {
        this.ui('attack-state').textContent = '本局已结束 · 重开可再次挑战';
        this.ui('cd-text').textContent = '结束';
        this.ui('energy-caption').textContent = '本局结束 · 生命与能量已冻结';
        this.notice = `最终 GPA ${(snapshot.gpaHundredths / 100).toFixed(2)}，累计击倒 ${snapshot.kills} 人。按 R 开始新的一局。`;
        this.noticeKind = '';
      }
    } else {
      this.attackButtons.forEach((button) => { button.disabled = true; });
    }
    this.paintAim();
    this.overlay.hidden = phase === 'playing';
    const tag = this.overlay.querySelector<HTMLElement>('[data-overlay="tag"]')!;
    const title = this.overlay.querySelector<HTMLElement>('[data-overlay="title"]')!;
    const description = this.overlay.querySelector<HTMLElement>('[data-overlay="description"]')!;
    const action = this.overlay.querySelector<HTMLButtonElement>('[data-overlay="action"]')!;
    const controls = this.overlay.querySelector<HTMLElement>('.demo-overlay-controls')!;
    const skillGuide = this.overlay.querySelector<HTMLElement>('[data-overlay="skills"]')!;
    const report = this.overlay.querySelector<HTMLElement>('[data-overlay="report"]')!;
    controls.hidden = phase === 'over' || phase === 'error';
    skillGuide.hidden = phase !== 'menu';
    report.hidden = phase !== 'over';
    if (phase === 'menu') {
      tag.textContent = '科大弹幕录 · 无尽 Boss'; title.textContent = '四招，撑到最后。';
      description.textContent = '你操控校徽 Boss，躲开学生反击。按住 1—4 或技能按钮，用鼠标预瞄，松开释放，空格取消。四招共享能量与出招 CD；击倒学生提升 GPA，逐渐趋近 4.30。第四招需要 100 能量。';
      action.innerHTML = '开始试玩 <span>→</span>';
    } else if (phase === 'paused') {
      tag.textContent = 'PAUSED'; title.textContent = '喘口气，再继续。';
      description.textContent = reason || '对局已暂停，生命、能量和弹幕都停在这一刻。';
      action.innerHTML = '继续对局 <span>→</span>';
    } else if (phase === 'over') {
      tag.textContent = 'BOSS DOWN · 本局成绩单';
      title.textContent = '这一局，到此为止。';
      description.textContent = '生命已耗尽。你的击倒记录和 GPA 已结算，再开一局可以尝试不同出招节奏。';
      if (snapshot) {
        report.querySelector<HTMLElement>('[data-report="gpa"]')!.textContent = `${(snapshot.gpaHundredths / 100).toFixed(2)} / ${(snapshot.gpaMaxHundredths / 100).toFixed(2)}`;
        report.querySelector<HTMLElement>('[data-report="time"]')!.textContent = formatTime(snapshot.tick);
        report.querySelector<HTMLElement>('[data-report="waves"]')!.textContent = `${snapshot.wave} / ${snapshot.wavesCleared}`;
        report.querySelector<HTMLElement>('[data-report="kills"]')!.textContent = `${snapshot.kills} / ${snapshot.studentsDeployed}`;
      }
      action.innerHTML = '再来一局 <span>↻</span>';
    } else if (phase === 'error') {
      tag.textContent = 'LOAD ERROR'; title.textContent = '暂时无法开始。';
      description.textContent = reason || '核心或 WebGL 初始化失败，请查看浏览器控制台。';
      action.textContent = '无法开始';
    }
    action.disabled = phase === 'error';
    this.previousPhase = phase;
    this.previousSnapshot = snapshot;
    this.previousReason = reason;
  }

  dispose(): void { this.clearPointerHold(true); this.panel.remove(); this.overlay.remove(); }
}
