/* hud.cpp - S15 右侧 320 px HUD 与界面文案（只读绘制）
 *
 * 接口版本: 2（docs/demo-interfaces.md）　配置版本: 1（demo-config-v1）
 * 权威规则: docs/demo-rules.md
 *
 * 设计边界（与任务卡逐条对应）:
 *  1. 只读 WorldView / DemoConfig。不写世界、不消耗世界 RNG、不裁决胜负。
 *     全部读取字段名在文件末尾 “UI 元素 → WorldView 字段” 注释表里逐条列出。
 *  2. 只绘制到“当前图形设备”。窗口/离屏 IMAGE 由调用方创建与切换，
 *     本文件不调用 initgraph/closegraph，不写日志文件，不做任何文件 I/O。
 *  3. 文本一律“先测量、再收窄，最后绘制”。EasyX 的 textwidth/textheight 依赖
 *     设备上下文，因此测量必须在设备就绪、settextstyle 之后再调用。做法是先
 *     settextstyle，再用 textwidth() 量真实像素宽度；超宽时二分求最长可容纳前缀
 *     并补省略号（见 draw_units_fit）。按 UTF-16 代码单元切分 —— EasyX 的
 *     LPCTSTR 即 wchar_t，中文与 ASCII 各占一个代码单元，切分不会切坏字符。
 *     中文文案不写死宽度估算，因此不会因字体回退而算错。
 *  4. 从不显示 GPA、训练次数、“正在学习”等未实现内容；结果文字只来自
 *     view->status / view->truncated，界面不自行判定。
 *  5. pattern_name() 返回的是 UTF-8 字节串（源文件 UTF-8 + /utf-8），
 *     必须经 widen_utf8() 转成 UTF-16 再绘制，否则中文会变成乱码。
 */
#include "hud.h"

#include "patterns.h" /* pattern_name(r): 招式中文名由母代理独占维护 */

#include <easyx.h>

#include <cstdio>
#include <cstring>
#include <cwchar>

namespace {

/* ---------------------------------------------------------------- 调色板 */

const COLORREF kPanelBg = RGB(26, 29, 39);
const COLORREF kPanelEdge = RGB(70, 78, 98);
const COLORREF kSectionBg = RGB(34, 38, 50);
const COLORREF kText = RGB(232, 236, 242);
const COLORREF kTextDim = RGB(168, 176, 190);
const COLORREF kTextFaint = RGB(128, 136, 152);
const COLORREF kAccent = RGB(120, 190, 255);
const COLORREF kHpBoss = RGB(232, 96, 96);
const COLORREF kHpStudent = RGB(120, 220, 150);
const COLORREF kEnergy = RGB(250, 205, 110);
const COLORREF kOk = RGB(130, 220, 150);
const COLORREF kBad = RGB(230, 110, 110);
const COLORREF kWarn = RGB(250, 190, 90);
const COLORREF kTrack = RGB(46, 51, 66);
const COLORREF kOverlay = RGB(10, 12, 18);

/* ---------------------------------------------------------------- 版式 */

const int kPanelX = HUD_PANEL_X;
const int kPanelW = HUD_PANEL_W;
const int kPadX = 10;                       /* 面板左右内边距 */
const int kContentX = kPanelX + kPadX;      /* 970 */
const int kContentW = kPanelW - 2 * kPadX;  /* 300: 文本可用宽度硬上限 */

const int kFsTitle = 19;
const int kFsBanner = 22;
const int kFsSection = 16;
const int kFsBody = 15;
const int kFsSmall = 13;
const int kFsBig = 30;
const wchar_t *kFont = L"Microsoft YaHei";

/* ---------------------------------------------------------------- 自检 */

HudTextAudit g_audit;
bool g_audit_init = false;

/* 当前绘制范围。只有面板（320 px 约束）的文本才会进入 max_* 统计，
 * 因为开始说明/结果卡片画在整个战场宽度上，约束不同（960 px），
 * 混在一起会让“最长文本 vs 面板宽度”的证据失去意义。行数仍然全部累计。 */
bool g_scope_panel = false;

void audit_ensure_init() {
    if (g_audit_init) {
        return;
    }
    std::memset(&g_audit, 0, sizeof(g_audit));
    g_audit.panel_width = kPanelW;
    g_audit.content_width = kContentW;
    g_audit.min_elide_chars = -1;
    g_audit_init = true;
}

void audit_note_raw(const wchar_t *text, int limit) {
    audit_ensure_init();
    if (!g_scope_panel) {
        return;
    }
    const int w = textwidth(text);
    if (w > g_audit.max_raw_width) {
        g_audit.max_raw_width = w;
        g_audit.max_raw_limit = limit;
        std::wcsncpy(g_audit.max_raw_text, text, 191);
        g_audit.max_raw_text[191] = L'\0';
    }
}

void audit_note_drawn(int drawn_width, int limit, bool clipped, int kept_chars) {
    audit_ensure_init();
    g_audit.line_count++;
    if (!g_scope_panel) {
        return;
    }
    g_audit.panel_line_count++;
    if (clipped) {
        g_audit.clipped_count++;
        if (g_audit.min_elide_chars < 0 || kept_chars < g_audit.min_elide_chars) {
            g_audit.min_elide_chars = kept_chars;
        }
    }
    if (drawn_width > g_audit.max_drawn_width) {
        g_audit.max_drawn_width = drawn_width;
        g_audit.max_drawn_limit = limit;
    }
}

/* ---------------------------------------------------------------- 文本工具 */

int measure(const wchar_t *text) { return (text != nullptr) ? textwidth(text) : 0; }

void put_line(int x, int y, const wchar_t *text, COLORREF color) {
    settextcolor(color);
    outtextxy(x, y, text);
}

/* UTF-8 -> UTF-16。pattern_name() 与配置版本串是 char*，
 * 直接按 %hs 输出会把 UTF-8 字节逐字节当成字符，中文必然乱码。 */
void widen_utf8(const char *src, wchar_t *dst, int dst_count) {
    if (dst == nullptr || dst_count <= 0) {
        return;
    }
    dst[0] = L'\0';
    if (src == nullptr) {
        return;
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, dst_count);
    if (n <= 0) {
        /* 回退：至少保证有可见内容且不越界（正常路径不会走到这里） */
        int i = 0;
        for (; src[i] != '\0' && i < dst_count - 1; ++i) {
            dst[i] = (wchar_t)(unsigned char)src[i];
        }
        dst[i] = L'\0';
    }
}

/* 收窄算法本体（纯计算，不绘制）。
 * 目标：在 max_width 像素内输出 text 的前 len 个代码单元，超宽时补省略号。
 * 思路：先用 EasyX 的 textwidth() 量整串真实宽度；超宽则二分求最长可容纳前缀。
 * 返回实际绘制宽度，结果写入 out（容量 out_cap）。 */
int fit_units(const wchar_t *text, int len, int max_width, wchar_t *out, int out_cap,
              int *out_kept_chars, bool *out_clipped) {
    if (out == nullptr || out_cap <= 1) {
        return 0;
    }
    out[0] = L'\0';
    if (out_kept_chars != nullptr) {
        *out_kept_chars = 0;
    }
    if (out_clipped != nullptr) {
        *out_clipped = false;
    }
    if (text == nullptr || len <= 0 || max_width <= 0) {
        return 0;
    }

    /* 预留 2 个代码单元：末尾要追加省略号与终止符。若不预留，len 取到数组上限时
     * wcscat 会越界写 —— 这是本文件早期版本真实存在的栈溢出，已修正。 */
    const int cap = out_cap - 2;
    if (len > cap) {
        len = cap;
    }

    std::wcsncpy(out, text, (size_t)len);
    out[len] = L'\0';
    if (measure(out) <= max_width) {
        return measure(out);
    }
    if (out_clipped != nullptr) {
        *out_clipped = true;
    }

    /* 二分：找最长的“前缀 + …”仍不超宽的前缀长度 */
    int lo = 0;
    int hi = len;
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        std::wcsncpy(out, text, (size_t)mid);
        out[mid] = L'\0';
        std::wcscat(out, L"…");
        if (measure(out) <= max_width) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    if (lo <= 0) {
        std::wcscpy(out, L"…"); /* 栏宽连“一个字+省略号”都放不下：只画省略号 */
        if (out_kept_chars != nullptr) {
            *out_kept_chars = 0;
        }
    } else {
        std::wcsncpy(out, text, (size_t)lo);
        out[lo] = L'\0';
        std::wcscat(out, L"…");
        if (out_kept_chars != nullptr) {
            *out_kept_chars = lo;
        }
    }
    return measure(out);
}

/* 绘制 text 的前 len 个代码单元，保证实际绘制宽度 <= max_width。 */
int draw_units_fit(int x, int y, const wchar_t *text, int len, int max_width, COLORREF color,
                   bool *out_clipped, int *out_kept_chars) {
    wchar_t buf[256];
    const int w = fit_units(text, len, max_width, buf, 256, out_kept_chars, out_clipped);
    put_line(x, y, buf, color);
    return w;
}

/* 单行绘制入口：测量 -> 收窄 -> 绘制 -> 记录自检 */
int draw_line_fit(int x, int y, const wchar_t *text, int max_width, COLORREF color) {
    if (text == nullptr) {
        return 0;
    }
    audit_note_raw(text, max_width);
    bool clipped = false;
    int kept = 0;
    const int w = draw_units_fit(x, y, text, (int)std::wcslen(text), max_width, color, &clipped,
                                 &kept);
    audit_note_drawn(w, max_width, clipped, kept);
    return textheight(text);
}

/* 居中绘制。过宽时退化为左对齐收窄，保证不越界。 */
int put_line_center(int left, int width, int y, const wchar_t *text, COLORREF color) {
    if (text == nullptr) {
        return 0;
    }
    audit_note_raw(text, width);
    const int w = measure(text);
    if (w > width) {
        return draw_line_fit(left, y, text, width, color);
    }
    put_line(left + (width - w) / 2, y, text, color);
    audit_note_drawn(w, width, false, 0);
    return textheight(text);
}

/* 按栏宽换行绘制，最多 max_lines 行。优先在空格断行；中文无空格则按字符累积。
 * 最后一行若仍有剩余内容，用省略号收尾。任何一行的绘制宽度都 <= max_width。 */
int draw_wrapped(int x, int y, const wchar_t *text, int max_width, int max_lines, int line_step,
                 COLORREF color) {
    if (text == nullptr || max_lines <= 0 || max_width <= 0) {
        return 0;
    }
    audit_note_raw(text, max_width);

    const int len = (int)std::wcslen(text);
    wchar_t probe[256];
    const int cap = (int)(sizeof(probe) / sizeof(probe[0])) - 1;
    int i = 0;
    int drawn = 0;

    while (i < len && drawn < max_lines) {
        int take = 0;
        int last_space = -1;
        while (i + take < len && take < cap) {
            std::wcsncpy(probe, text + i, (size_t)(take + 1));
            probe[take + 1] = L'\0';
            if (measure(probe) > max_width) {
                break;
            }
            if (text[i + take] == L' ' || text[i + take] == L'　') {
                last_space = take;
            }
            take++;
        }
        if (i + take < len) {
            if (take <= 0) {
                take = 1; /* 单字都放不下：至少推进一个字符，避免死循环 */
            } else if (last_space > 0) {
                take = last_space; /* 在空格处断行，丢弃该空格 */
            }
        }

        const bool last_allowed = (drawn == max_lines - 1);
        const bool more = (i + take < len);
        bool clipped = false;
        int kept = 0;
        const int w = draw_units_fit(x, y + drawn * line_step, text + i, take, max_width, color,
                                     &clipped, &kept);
        audit_note_drawn(w, max_width, clipped || (last_allowed && more), kept);
        drawn++;
        i += take;
        while (i < len && (text[i] == L' ' || text[i] == L'　')) {
            i++;
        }
        if (last_allowed && more) {
            break; /* 已达行数上限，剩余内容由省略号代表 */
        }
    }
    return drawn;
}

/* ---------------------------------------------------------------- 图形小工具 */

void section_header(int y, const wchar_t *title) {
    setfillcolor(kSectionBg);
    solidrectangle(kContentX, y, kContentX + kContentW, y + 21);
    settextstyle(kFsSection, 0, kFont);
    setbkmode(TRANSPARENT);
    draw_line_fit(kContentX + 5, y + 3, title, kContentW - 10, kAccent);
}

/* 进度条。ratio 内部夹紧到 [0,1]，因此调用方传超范围值也不会画出界。 */
void draw_bar(int x, int y, int w, int h, float ratio, COLORREF fill, bool framed) {
    if (ratio < 0.0f) {
        ratio = 0.0f;
    }
    if (ratio > 1.0f) {
        ratio = 1.0f;
    }
    setfillcolor(kTrack);
    solidrectangle(x, y, x + w, y + h);
    const int fw = (int)(ratio * (float)w + 0.5f);
    if (fw > 0) {
        setfillcolor(fill);
        solidrectangle(x, y, x + fw, y + h);
    }
    if (framed) {
        setlinecolor(kPanelEdge);
        rectangle(x, y, x + w, y + h);
    }
}

/* 生命方块：实心=剩余，空心=已失去。只读 hp / hp_max。 */
void draw_hp_pips(int x, int y, int hp, int hp_max, int pip_w, int pip_h, int gap, int max_pips,
                  COLORREF color) {
    if (hp < 0) {
        hp = 0;
    }
    if (hp_max <= 0) {
        hp_max = 1;
    }
    if (hp_max > max_pips) {
        hp_max = max_pips;
    }
    if (hp > hp_max) {
        hp = hp_max;
    }
    for (int i = 0; i < hp_max; ++i) {
        const int px = x + i * (pip_w + gap);
        if (i < hp) {
            setfillcolor(color);
            solidrectangle(px, y, px + pip_w, y + pip_h);
        } else {
            setlinecolor(kPanelEdge);
            rectangle(px, y, px + pip_w, y + pip_h);
        }
    }
}

/* 设置当前绘制范围：true = 面板（320 px 约束），false = 整屏/战场区（960 px 约束） */
void audit_scope(bool panel) { g_scope_panel = panel; }

/* 结果文案。截断优先于状态：截断局绝不可能显示成胜负（demo-rules 1.4）。 */
const wchar_t *status_text(const HudInput *in) {
    if (in->view->truncated) {
        return L"未完成（实验截断）";
    }
    switch (in->view->status) {
        case DEMO_STATUS_BOSS_WIN:
            return L"Boss 胜";
        case DEMO_STATUS_BOSS_LOSE:
            return L"Boss 败";
        case DEMO_STATUS_DRAW:
            return L"平局";
        case DEMO_STATUS_RUNNING:
        default:
            return L"进行中";
    }
}

COLORREF status_color(const HudInput *in) {
    if (in->view->truncated) {
        return kWarn;
    }
    switch (in->view->status) {
        case DEMO_STATUS_BOSS_WIN:
            return kOk;
        case DEMO_STATUS_BOSS_LOSE:
            return kBad;
        case DEMO_STATUS_DRAW:
            return kWarn;
        default:
            return kTextDim;
    }
}

/* 在战场区域压暗：隔行隔列打点，保留战场轮廓可读，且不依赖 alpha 支持。
 * 只在暂停/结果帧调用，不参与逻辑。 */
void dim_field() {
    for (int y = 0; y < HUD_FIELD_H; y += 2) {
        for (int x = 0; x < HUD_FIELD_W; x += 2) {
            putpixel(x, y, kOverlay);
        }
    }
}

/* 居中卡片 */
void draw_card(int cx, int cy, int half_w, int half_h) {
    setfillcolor(RGB(26, 29, 39));
    solidrectangle(cx - half_w, cy - half_h, cx + half_w, cy + half_h);
    setlinecolor(kPanelEdge);
    rectangle(cx - half_w, cy - half_h, cx + half_w, cy + half_h);
}

} /* namespace */

/* ================================================================ 纯查询 */

bool hud_is_paused(const HudInput *in) {
    if (in == nullptr || in->view == nullptr) {
        return false;
    }
    /* core 目前恒置 view->paused = false（core/world.c）；取或以便 core 表达暂停时仍正确。 */
    return in->paused || in->view->paused;
}

bool hud_is_finished(const HudInput *in) {
    if (in == nullptr || in->view == nullptr) {
        return false;
    }
    return in->view->status != DEMO_STATUS_RUNNING || in->view->truncated;
}

/* 不可用原因。判定顺序与 core/world.c 的 world_pattern_available() 一致
 * （忙碌 -> 能量 -> 目标），因此 HUD 的解释与核心真正拒绝时的 DEMO_REJECT_* 一致。
 * 这里只做只读解释，不代替核心裁决，也不扣能量。 */
const wchar_t *hud_pattern_block_reason(const WorldView *view, const DemoConfig *config,
                                        int pattern_index) {
    if (view == nullptr || config == nullptr) {
        return L"数据不可用";
    }
    if (pattern_index < 0 || pattern_index >= (int)DEMO_PATTERN_COUNT) {
        return L"招式 ID 越界";
    }
    if (view->pattern_available[pattern_index]) {
        return L"可用";
    }
    if (view->status != DEMO_STATUS_RUNNING) {
        return L"对局已结束";
    }
    if (view->attack_state != DEMO_ATTACK_IDLE) {
        return L"正在出招";
    }
    if (view->energy < config->patterns[pattern_index].cost) {
        return L"能量不足";
    }
    if (view->marked_target == 0u) {
        return L"无目标";
    }
    return L"不可用";
}

/* ================================================================ 面板 */

void hud_draw_panel(const HudInput *in) {
    if (in == nullptr || in->view == nullptr || in->config == nullptr) {
        return;
    }
    audit_ensure_init();
    audit_scope(true);
    const WorldView *v = in->view;
    const DemoConfig *cfg = in->config;

    /* 面板背景与左边框（x 960..1279） */
    setfillcolor(kPanelBg);
    solidrectangle(kPanelX, HUD_PANEL_Y, kPanelX + kPanelW, HUD_PANEL_Y + HUD_PANEL_H);
    setlinecolor(kPanelEdge);
    line(kPanelX, 0, kPanelX, HUD_PANEL_H - 1);
    setbkmode(TRANSPARENT);

    int y = 8;

    /* ---- 标题：明确标识 Boss demo / 脚本 AI ---- */
    settextstyle(kFsTitle, 0, kFont);
    put_line_center(kContentX, kContentW, y, L"Boss demo / 脚本 AI", kText);
    y += 24;
    settextstyle(kFsSmall, 0, kFont);
    {
        wchar_t ver[64];
        wchar_t buf[128];
        widen_utf8(demo_config_version_string(), ver, 64);
        std::swprintf(buf, 128, L"接口 v2 · %ls", ver);
        put_line_center(kContentX, kContentW, y, buf, kTextFaint);
    }
    y += 18;
    setlinecolor(kPanelEdge);
    line(kContentX, y, kContentX + kContentW, y);
    y += 6;

    /* ---- 状态横幅：预警中 / 攻击中 / 已暂停 / 结果 ---- */
    {
        const wchar_t *banner = L"待命 · 可出招";
        COLORREF bcolor = kTextDim;
        wchar_t buf[128];
        if (hud_is_finished(in)) {
            banner = status_text(in);
            bcolor = status_color(in);
        } else if (hud_is_paused(in)) {
            banner = L"已暂停";
            bcolor = kWarn;
        } else if (v->attack_state == DEMO_ATTACK_WINDUP) {
            /* WorldView 没有 progress 字段，故用 view->tick 与计划时刻自算：
             * 预警进度 = (tick - plan_start_tick) / plan_windup_ticks，
             * 与 core/attack.h 的 attack_progress() 在同一状态下表达同一比值。 */
            const int elapsed = v->tick - v->plan_start_tick;
            const int total = (v->plan_windup_ticks > 1) ? v->plan_windup_ticks : 1;
            int pct = (int)(100.0f * (float)elapsed / (float)total + 0.5f);
            if (pct < 0) {
                pct = 0;
            }
            if (pct > 100) {
                pct = 100;
            }
            std::swprintf(buf, 128, L"预警中 %d%%", pct);
            banner = buf;
            bcolor = kWarn;
        } else if (v->attack_state == DEMO_ATTACK_ACTIVE) {
            /* 攻击进度：从 start_tick + windup_ticks 起算，除以 plan_active_ticks */
            const int elapsed = v->tick - (v->plan_start_tick + v->plan_windup_ticks);
            const int total = (v->plan_active_ticks > 1) ? v->plan_active_ticks : 1;
            int pct = (int)(100.0f * (float)elapsed / (float)total + 0.5f);
            if (pct < 0) {
                pct = 0;
            }
            if (pct > 100) {
                pct = 100;
            }
            std::swprintf(buf, 128, L"攻击中 %d%%", pct);
            banner = buf;
            bcolor = kAccent;
        }
        settextstyle(kFsBanner, 0, kFont);
        setfillcolor(kSectionBg);
        solidrectangle(kContentX, y, kContentX + kContentW, y + 27);
        put_line_center(kContentX, kContentW, y + 4, banner, bcolor);
        y += 32;
    }

    /* ---- Boss 血量 ---- */
    {
        const Actor *boss = v->boss;
        const int hp = (boss != nullptr) ? boss->hp : 0;
        const int hp_max = (boss != nullptr && boss->hp_max > 0) ? boss->hp_max : 1;
        wchar_t buf[96];
        settextstyle(kFsBody, 0, kFont);
        std::swprintf(buf, 96, L"Boss 生命  %d / %d", hp, hp_max);
        draw_line_fit(kContentX, y, buf, kContentW, kText);
        y += 19;
        draw_bar(kContentX, y, kContentW, 14, (float)hp / (float)hp_max, kHpBoss, true);
        y += 20;
    }

    /* ---- 共享能量 ---- */
    {
        const int e = v->energy;
        const int emax = (v->energy_max > 0) ? v->energy_max : 1;
        wchar_t buf[160];
        settextstyle(kFsBody, 0, kFont);
        std::swprintf(buf, 96, L"共享能量  %d / %d", e, emax);
        draw_line_fit(kContentX, y, buf, kContentW, kText);
        y += 19;
        draw_bar(kContentX, y, kContentW, 14, (float)e / (float)emax, kEnergy, true);
        y += 17;
        settextstyle(kFsSmall, 0, kFont);
        /* 恢复速率取自配置，不写死数字 */
        std::swprintf(buf, 160, L"按逻辑时间恢复 %d / 秒（暂停不恢复）",
                      (int)(cfg->energy_regen_per_sec + 0.5f));
        y += draw_wrapped(kContentX, y, buf, kContentW, 2, 15, kTextFaint);
    }
    y += 6;

    /* ---- 四招列表：ID + 中文名 + 消耗 + 可用性 + 不可用原因 ---- */
    section_header(y, L"四招（按 1-4 出招）");
    y += 25;
    for (int r = 0; r < (int)DEMO_PATTERN_COUNT; ++r) {
        const bool avail = v->pattern_available[r];
        const int cost = cfg->patterns[r].cost;
        wchar_t wname[128];
        wchar_t buf[224];
        /* 中文名来自 pattern_name(r)（core/patterns.c 注册表），UTF-8 -> UTF-16 */
        widen_utf8(pattern_name((DemoPattern)r), wname, 128);

        settextstyle(kFsBody, 0, kFont);
        std::swprintf(buf, 224, L"%d %ls  消耗 %d", r + 1, wname, cost);
        draw_line_fit(kContentX, y, buf, kContentW, avail ? kText : kTextDim);
        y += 18;

        settextstyle(kFsSmall, 0, kFont);
        if (avail) {
            std::swprintf(buf, 224, L"    可用");
            draw_line_fit(kContentX, y, buf, kContentW, kOk);
        } else {
            std::swprintf(buf, 224, L"    不可用 · %ls",
                          hud_pattern_block_reason(v, cfg, r));
            draw_line_fit(kContentX, y, buf, kContentW, kBad);
        }
        y += 16;
    }
    y += 6;

    /* ---- 目标与逐学生生命 ---- */
    section_header(y, L"目标 / 学生");
    y += 25;
    {
        uint32_t alive = 0;
        for (uint32_t i = 0; i < v->student_count && i < DEMO_MAX_STUDENTS; ++i) {
            if (v->students[i].alive) {
                alive++;
            }
        }
        wchar_t buf[160];
        settextstyle(kFsBody, 0, kFont);
        if (v->marked_target == 0u) {
            std::swprintf(buf, 160, L"当前目标：无（无存活学生）");
        } else {
            std::swprintf(buf, 160, L"当前目标：学生 #%u", (unsigned)v->marked_target);
        }
        draw_line_fit(kContentX, y, buf, kContentW, kText);
        y += 18;
        std::swprintf(buf, 160, L"剩余学生 %u / %u", (unsigned)alive, (unsigned)v->student_count);
        draw_line_fit(kContentX, y, buf, kContentW, kTextDim);
        y += 18;

        /* 每个学生一行：编号 + 生命方块 + hp/max */
        settextstyle(kFsSmall, 0, kFont);
        for (uint32_t i = 0; i < v->student_count && i < DEMO_MAX_STUDENTS; ++i) {
            const Actor *s = &v->students[i];
            wchar_t num[32];
            const int row_y = y;
            if (s->alive) {
                std::swprintf(buf, 160, L"学生 #%u", (unsigned)s->id);
            } else {
                std::swprintf(buf, 160, L"学生 #%u 已倒下", (unsigned)s->id);
            }
            draw_line_fit(kContentX, row_y, buf, 120, s->alive ? kText : kTextFaint);
            {
                const int hp_max = (s->hp_max > 0) ? s->hp_max : 1;
                const int pip_x = kContentX + 124;
                draw_hp_pips(pip_x, row_y + 2, s->hp, hp_max, 8, 10, 3, 8,
                             s->alive ? kHpStudent : kTextFaint);
                std::swprintf(num, 32, L"%d/%d", s->hp, hp_max);
                draw_line_fit(pip_x + 8 * (8 + 3) + 6, row_y, num, 60,
                              s->alive ? kTextDim : kTextFaint);
            }
            y += 16;
        }
    }
    y += 8;

    /* ---- 底部短提示（不遮挡战场，也不与结果提示重复裁决） ---- */
    if (v->truncated) {
        settextstyle(kFsSmall, 0, kFont);
        y += draw_wrapped(kContentX, y, L"未完成（实验截断）：仅是实验上限，不是胜负。",
                          kContentW, 2, 15, kWarn);
    } else if (v->status != DEMO_STATUS_RUNNING) {
        settextstyle(kFsSmall, 0, kFont);
        y += draw_wrapped(kContentX, y, L"R 重开 · Esc 返回 / 关闭窗口退出。", kContentW, 1, 15,
                          kTextFaint);
    } else if (hud_is_paused(in)) {
        settextstyle(kFsSmall, 0, kFont);
        y += draw_wrapped(kContentX, y, L"暂停中：Esc 或鼠标右键继续。", kContentW, 1, 15, kWarn);
    }
    audit_scope(false);
}

/* ================================================================ 开始说明 */

void hud_draw_intro(void) {
    setfillcolor(RGB(14, 16, 23));
    solidrectangle(0, 0, HUD_FIELD_W - 1, HUD_FIELD_H - 1);
    setbkmode(TRANSPARENT);

    int y = 92;
    settextstyle(34, 0, kFont);
    put_line_center(0, HUD_FIELD_W, y, L"Boss demo · 脚本 AI 对手", kText);
    y += 46;

    settextstyle(kFsSection, 0, kFont);
    put_line_center(0, HUD_FIELD_W, y, L"你是 Boss：击倒全部学生即获胜", kAccent);
    y += 40;

    settextstyle(kFsBody, 0, kFont);
    const wchar_t *lines[] = {
        L"操作：鼠标移动（朝指针走，到指针处停）；无鼠标时用 WASD / 方向键。",
        L"出招：1 环弹（30）· 2 课表（35）· 3 金矿（15）· 4 淋浴（60）。",
        L"能量：一条共享能量，按逻辑时间每秒恢复 10；出招扣一次，不足则拒绝、不排队。",
        L"出招后先有 1.2 秒预警，再进入攻击；同一时间只执行一招。",
        L"暂停：Esc 或鼠标右键；重开：R。Boss 生命耗尽即失败。",
    };
    const int n = (int)(sizeof(lines) / sizeof(lines[0]));
    for (int i = 0; i < n; ++i) {
        const int used = draw_wrapped(70, y, lines[i], HUD_FIELD_W - 140, 2, 21, kText);
        y += used * 21 + 7;
    }

    y += 18;
    settextstyle(kFsBody, 0, kFont);
    put_line_center(0, HUD_FIELD_W, y, L"按空格 / 左键 / 1-4 开始", kOk);

    y += 32;
    settextstyle(kFsSmall, 0, kFont);
    put_line_center(0, HUD_FIELD_W, y,
                    L"说明：对手是可复现脚本 AI，不是训练模型；界面不显示未实现的数值。",
                    kTextFaint);
}

/* ================================================================ 遮罩与结果 */

void hud_draw_pause_overlay(const HudInput *in) {
    if (!hud_is_paused(in)) {
        return;
    }
    dim_field();
    const int cx = HUD_FIELD_W / 2;
    const int cy = HUD_FIELD_H / 2;
    draw_card(cx, cy, 170, 52);
    setbkmode(TRANSPARENT);
    settextstyle(30, 0, kFont);
    put_line_center(cx - 160, 320, cy - 36, L"已暂停", kWarn);
    settextstyle(kFsSmall, 0, kFont);
    put_line_center(cx - 160, 320, cy + 4, L"Esc 或鼠标右键继续", kTextDim);
    put_line_center(cx - 160, 320, cy + 24, L"R 重开本局", kTextDim);
}

void hud_draw_result(const HudInput *in) {
    if (in == nullptr || in->view == nullptr) {
        return;
    }
    /* 未终局且未截断：不显示任何结果，界面不自行裁决 */
    if (hud_is_finished(in) == false) {
        return;
    }
    dim_field();
    const int cx = HUD_FIELD_W / 2;
    const int cy = HUD_FIELD_H / 2;
    draw_card(cx, cy, 220, 76);
    setbkmode(TRANSPARENT);

    settextstyle(kFsBig, 0, kFont);
    put_line_center(cx - 210, 420, cy - 54, status_text(in), status_color(in));

    settextstyle(kFsBody, 0, kFont);
    {
        wchar_t buf[160];
        if (in->view->truncated) {
            std::swprintf(buf, 160, L"未到终局，实验截断于 tick %d", in->view->tick);
        } else {
            std::swprintf(buf, 160, L"结束于 tick %d", in->view->tick);
        }
        put_line_center(cx - 210, 420, cy - 8, buf, kTextDim);
    }
    settextstyle(kFsSmall, 0, kFont);
    put_line_center(cx - 210, 420, cy + 22, L"R 重开 · Esc 返回 / 关闭窗口退出", kTextFaint);
}

/* ================================================================ 组合入口 */

void hud_draw_all(const HudInput *in) {
    if (in == nullptr || in->view == nullptr || in->config == nullptr) {
        return;
    }
    hud_draw_panel(in);
    if (hud_is_finished(in)) {
        hud_draw_result(in); /* 终局/截断：只画结果，不叠暂停遮罩 */
    } else if (hud_is_paused(in)) {
        hud_draw_pause_overlay(in);
    }
}

/* ================================================================ 自检导出 */

const HudTextAudit *hud_text_audit(void) {
    audit_ensure_init();
    return &g_audit;
}

void hud_text_audit_reset(void) {
    std::memset(&g_audit, 0, sizeof(g_audit));
    g_audit.panel_width = kPanelW;
    g_audit.content_width = kContentW;
    g_audit.min_elide_chars = -1;
    g_audit_init = true;
}

/* 布局自检入口：把 text 收窄到 max_width 后的实际字符串与宽度。
 * 复用绘制路径同一套收窄算法，因此自检结论与真正画出来的结果一致。 */
int hud_fit_preview(const wchar_t *text, int max_width, wchar_t *out, int out_cap) {
    int kept = 0;
    return fit_units(text, (text != nullptr) ? (int)std::wcslen(text) : 0, max_width, out, out_cap,
                     &kept, nullptr);
}

/* ================================================================
 * UI 元素 → 读取的 WorldView 字段（验收条件 3）
 *
 * 面板标题 “Boss demo / 脚本 AI”
 *      固定文案（模块常量），不读任何 WorldView 字段；
 *      配置行读 demo_config_version_string()（配置 v1）。
 * 状态横幅
 *      view->status、view->truncated、view->paused（并与调用方传入的 paused 取或）、
 *      view->attack_state、view->plan_start_tick、view->plan_windup_ticks、
 *      view->plan_active_ticks、view->tick
 * Boss 血量条与数值
 *      view->boss->hp、view->boss->hp_max
 * 共享能量条与数值、恢复说明
 *      view->energy、view->energy_max；恢复速率读 DemoConfig.energy_regen_per_sec
 * 四招列表（名称 / 消耗 / 可用性 / 不可用原因）
 *      view->pattern_available[r]、view->attack_state、view->marked_target、
 *      view->energy、view->status；
 *      消耗读 DemoConfig.patterns[r].cost；
 *      中文名读 pattern_name((DemoPattern)r)（core/patterns.c 的注册表）。
 * 当前目标
 *      view->marked_target
 * 剩余学生数与逐学生生命
 *      view->student_count、view->students[i].alive、view->students[i].id、
 *      view->students[i].hp、view->students[i].hp_max
 * 攻击状态与进度
 *      view->attack_state、view->tick、view->plan_start_tick、
 *      view->plan_windup_ticks、view->plan_active_ticks
 *      （WorldView 无独立 progress 字段，故用上列字段自算；等价量亦见
 *        core/attack.h 的 attack_progress()，HUD 刻意不调用它，因为该函数取
 *        World*，而渲染层只允许持有 WorldView，不得触碰可变世界。）
 * 预警几何
 *      view->warning 由 render/scene.cpp 使用；HUD 不读取，避免与 S14 重复绘制。
 * 结果提示
 *      view->status、view->truncated、view->tick
 * 未读取字段（明确不使用，属于 S14/S16 范围）:
 *      view->projectiles、projectile_capacity、projectile_live、events、event_count、
 *      world_seed、plan_id、plan_target、plan_pattern、boss_hits_taken、
 *      student_hits_taken、attack_accept_count、attack_reject_count。
 * ================================================================ */
