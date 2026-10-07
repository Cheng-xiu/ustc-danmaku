import type { Phase, Snapshot } from '../types';
import './hud.css';

export type HUDCallbacks = { start(): void; pause(): void; restart(): void; attack(pattern: number): void };
const noop = () => {};
const patterns = [
  { name: '绿色圆圈好辣', source: '桃李苑', purpose: '周边压力', color: 'green' },
  { name: '课表华容道', source: '选课系统', purpose: '封住路线', color: 'purple' },
  { name: '绩点淘金', source: '一教金矿', purpose: '局部追击', color: 'gold' },
  { name: '绩点淋浴', source: '期末总评', purpose: '多目标压制', color: 'blue' },
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
  private readonly lookup = new Map<string, HTMLElement>();

  constructor(host: HTMLElement, callbacks?: HUDCallbacks) {
    this.callbacks = callbacks ?? { start: noop, pause: noop, restart: noop, attack: noop };
    this.panel = document.createElement('aside');
    this.panel.className = 'demo-hud';
    this.panel.innerHTML = `
      <header class="demo-hud-header"><span class="demo-eyebrow">USTC · DANMAKU</span><h1>校园弹幕</h1><div class="demo-subtitle">Boss 试玩场 <span>DEMO 01</span></div></header>
      <div class="demo-match-row"><span class="demo-phase" data-ui="phase">准备开始</span><span class="demo-clock" data-ui="clock">00:00</span></div>
      <section class="demo-vitals"><div class="demo-line"><span>你的生命</span><strong data-ui="health-text">6 / 6</strong></div><div class="demo-health" data-ui="health"></div><div class="demo-line demo-energy-label"><span>共享能量</span><strong data-ui="energy-text">60 <small>/ 100</small></strong></div><div class="demo-energy-track"><div data-ui="energy-fill"></div></div><div class="demo-energy-note">等待时蓄能 · 出招成功才扣除</div></section>
      <section class="demo-skills"><div class="demo-section-label">主动出招 <span>按 1 — 4</span></div><div data-ui="skills"></div><div class="demo-attack-state" data-ui="attack-state">当前没有攻击</div><div class="demo-attack-track"><div data-ui="attack-fill"></div></div></section>
      <div class="demo-notice" data-ui="notice" role="status" aria-live="polite"></div>
      <section class="demo-students"><div class="demo-section-label">学生反击 <span data-ui="student-count">3 人存活</span></div><div data-ui="student-list"></div></section>
      <footer class="demo-hud-footer"><div class="demo-legend"><span><i class="student-bullet"></i>反击弹</span><span><i class="warning-ray"></i>锁定预警</span></div><p>鼠标靠近即停；WASD / 方向键备用<br>Esc / 右键暂停，R 重开</p><div class="demo-system-buttons"><button type="button" data-ui="pause">暂停 <kbd>Esc</kbd></button><button type="button" data-ui="restart">重开 <kbd>R</kbd></button></div></footer>`;
    host.append(this.panel);
    this.panel.querySelectorAll<HTMLElement>('[data-ui]').forEach((element) => this.lookup.set(element.dataset.ui!, element));
    patterns.forEach((pattern, index) => {
      const button = document.createElement('button');
      button.type = 'button';
      button.className = `demo-skill ${pattern.color}`;
      button.innerHTML = `<kbd>${index + 1}</kbd><span class="demo-skill-copy"><small>${pattern.source} · ${pattern.purpose}</small><strong>${pattern.name}</strong></span><span class="demo-cost"></span>`;
      button.addEventListener('click', () => this.callbacks.attack(index));
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
    this.overlay.innerHTML = `<div class="demo-status-card"><span class="demo-eyebrow" data-overlay="tag">最小可玩 Demo</span><h2 data-overlay="title">四招，打一场。</h2><p class="demo-overlay-description" data-overlay="description"></p><div class="demo-overlay-controls"><span><kbd>鼠标</kbd>朝指针移动，靠近减速</span><span><kbd>1 — 4</kbd>主动出招，等待蓄能</span><span><kbd>Esc</kbd>随时暂停 <kbd>R</kbd>重开</span></div><button type="button" class="demo-primary-action" data-overlay="action">开始试玩 <span>→</span></button><p class="demo-overlay-footnote">击倒全部学生获胜 · 躲开红色反击弹<br>学生使用脚本 AI，四招数值等待你的试玩反馈</p></div>`;
    (host.parentElement ?? host).append(this.overlay);
    this.overlay.querySelector<HTMLButtonElement>('[data-overlay="action"]')!.addEventListener('click', () => {
      if (this.previousPhase === 'menu') this.callbacks.start();
      else if (this.previousPhase === 'paused') this.callbacks.pause();
      else this.callbacks.restart();
    });
  }

  private ui(name: string): HTMLElement { return this.lookup.get(name)!; }
  setCallbacks(callbacks: HUDCallbacks): void { this.callbacks = callbacks; }

  render(snapshot: Snapshot | null, phase: Phase, reason = ''): void {
    if (snapshot === this.previousSnapshot && phase === this.previousPhase && reason === this.previousReason) return;
    const stateText: Record<Phase, string> = { menu: '准备开始', playing: '对局进行中', paused: '已暂停', over: '对局结束', error: '加载失败' };
    this.ui('phase').textContent = stateText[phase];
    this.ui('phase').dataset.phase = phase;
    this.ui('pause').textContent = phase === 'paused' ? '继续  Esc' : '暂停  Esc';
    (this.ui('pause') as HTMLButtonElement).disabled = phase !== 'playing' && phase !== 'paused';
    (this.ui('restart') as HTMLButtonElement).disabled = phase === 'error';
    if (snapshot) {
      if (snapshot.tick === 0 && (phase !== this.previousPhase || this.previousSnapshot?.tick !== 0)) {
        this.notice = '选择一招主动进攻；松手等待蓄能。';
        this.noticeKind = '';
        this.lastEvent = '';
        this.noticeUntil = -1;
      }
      const seconds = Math.floor(snapshot.tick / 60);
      this.ui('clock').textContent = `${String(Math.floor(seconds / 60)).padStart(2, '0')}:${String(seconds % 60).padStart(2, '0')}`;
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
      this.ui('energy-fill').style.width = `${Math.max(0, Math.min(100, snapshot.energy / Math.max(1, snapshot.energyMax) * 100))}%`;
      this.attackButtons.forEach((button, index) => {
        button.disabled = phase !== 'playing';
        button.classList.toggle('unavailable', !snapshot.available[index]);
        button.classList.toggle('selected', snapshot.attackState !== 0 && snapshot.pattern === index);
        button.querySelector('.demo-cost')!.textContent = `${snapshot.costs[index] ?? '—'}`;
        button.title = `${patterns[index].name}，消耗 ${snapshot.costs[index]} 能量${snapshot.available[index] ? '' : '；当前请求会被拒绝'}。`;
      });
      const attackName = patterns[snapshot.pattern]?.name ?? '攻击';
      const elapsed = Math.max(0, snapshot.tick - snapshot.startTick);
      const total = snapshot.attackState === 1 ? snapshot.windup : snapshot.active;
      const localElapsed = snapshot.attackState === 1 ? elapsed : Math.max(0, elapsed - snapshot.windup);
      const remaining = Math.max(0, total - localElapsed) / 60;
      this.ui('attack-state').textContent = snapshot.attackState === 0 ? '就绪 · 可选择下一招'
        : `${snapshot.attackState === 1 ? '预警' : '攻击'} · ${attackName} · ${remaining.toFixed(1)}s`;
      this.ui('attack-fill').style.width = snapshot.attackState === 0 ? '0%' : `${Math.min(100, localElapsed / Math.max(1, total) * 100)}%`;
      const students = snapshot.actors.slice(1);
      this.ui('student-list').classList.toggle('many', students.length > 4);
      this.ui('student-count').textContent = `${students.filter((student) => student.alive).length} / ${students.length} 人存活`;
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
    } else {
      this.attackButtons.forEach((button) => { button.disabled = true; });
    }
    this.ui('notice').textContent = this.notice;
    this.ui('notice').dataset.kind = this.noticeKind;
    this.overlay.hidden = phase === 'playing';
    const tag = this.overlay.querySelector<HTMLElement>('[data-overlay="tag"]')!;
    const title = this.overlay.querySelector<HTMLElement>('[data-overlay="title"]')!;
    const description = this.overlay.querySelector<HTMLElement>('[data-overlay="description"]')!;
    const action = this.overlay.querySelector<HTMLButtonElement>('[data-overlay="action"]')!;
    const controls = this.overlay.querySelector<HTMLElement>('.demo-overlay-controls')!;
    controls.hidden = phase === 'over' || phase === 'error';
    if (phase === 'menu') {
      tag.textContent = '最小可玩 Demo'; title.textContent = '四招，打一场。';
      description.textContent = '你是 Boss。走位躲开学生反击，用一条能量和四种弹幕击倒全部学生。';
      action.innerHTML = '开始试玩 <span>→</span>';
    } else if (phase === 'paused') {
      tag.textContent = 'PAUSED'; title.textContent = '喘口气，再继续。';
      description.textContent = reason || '对局已暂停，生命、能量和弹幕都停在这一刻。';
      action.innerHTML = '继续对局 <span>→</span>';
    } else if (phase === 'over') {
      const win = snapshot?.status === 1;
      tag.textContent = win ? 'BOSS WIN' : snapshot?.status === 3 ? 'DRAW' : 'BOSS DOWN';
      title.textContent = win ? '校园 Boss，胜出。' : snapshot?.status === 3 ? '对局结束。' : '这次，学生赢了。';
      description.textContent = win ? '全部学生已倒下。再试一局，看看四招能否各司其职。' : '试试先躲开红色反击弹，低耗金矿追击，等待能量再压制。';
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

  dispose(): void { this.panel.remove(); this.overlay.remove(); }
}
