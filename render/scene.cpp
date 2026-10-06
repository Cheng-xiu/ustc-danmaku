/* scene.cpp - S14 战场绘制（只读 WorldView + DemoConfig，全部程序绘制）
 *
 * 任务 ID: S14
 * 接口版本: 2（docs/demo-interfaces.md）
 * 配置版本: 1（docs/demo-rules.md / core/demo_config.c 的 demo-config-v1）
 *
 * 平台边界（docs/demo-interfaces.md 第 4 节）:
 *  - 只读 WorldView 与 DemoConfig。不写世界、不调用任何 core 写函数、
 *    不消耗世界 RNG、不改变物理与判定。本文件不链接任何 core/*.c 目标文件。
 *  - 命中判定完全由 core 负责；渲染只保证"画出来的圆半径 == Actor.radius"。
 *
 * 布局（docs/demo-rules.md 2.1）:
 *  窗口 1280x720；左侧逻辑战场 960x720（世界坐标 1:1）；右侧 320 px
 *  （x ∈ [960,1280)）留给 S15 的 HUD，本文件只画骨架与分隔线。
 *
 * 素材（遵守"不用设计配图冒充实时画面"）:
 *  全部图形由程序图元绘制，不调用 loadimage，不读 docs/assets/**。
 *  Boss 本体不使用校徽图片（素材未取得）: 用醒目程序图形代替 ——
 *  实心圆(半径 = cfg.boss_radius = 22 px) + 高亮描边 + 白色菱形本体核心
 *  + 指向锁定目标的方向箭头。拿到正式素材后只替换 draw_boss_body()。
 *
 * 编码: 源文件 UTF-8，构建加 /utf-8；UNICODE 已开，中文用宽字符字面量；
 *  控制台日志保持 ASCII 以避免 CP936 乱码。
 */
#include "scene.h"

#include <graphics.h> /* EasyX（内含 windows.h: RGB/HRGN/TRANSPARENT） */

#include <cmath>
#include <cstring>

/* ---------------------------------------------------------------- 布局 */

#define SCENE_WINDOW_W 1280
#define SCENE_WINDOW_H 720
#define SCENE_HUD_X 960 /* 战场右边界 = HUD 左边界 */
#define SCENE_HUD_W 320

/* 配色一律用 windows.h 的 RGB()（EasyX 的 RED/BLUE 常量是历史 BGR 语义，易误用）。 */
#define COL_FIELD_BG RGB(18, 22, 32)
#define COL_FIELD_EDGE RGB(96, 128, 176)
#define COL_LEGAL_HINT RGB(58, 78, 108)
#define COL_HUD_BG RGB(14, 16, 24)
#define COL_HUD_EDGE RGB(80, 104, 144)

#define COL_BOSS_BODY RGB(236, 92, 56)
#define COL_BOSS_EDGE RGB(255, 206, 176)
#define COL_BOSS_CORE RGB(255, 255, 255)
#define COL_BOSS_DIR RGB(255, 236, 140)
#define COL_BOSS_HIT RGB(255, 150, 120)

#define COL_STUDENT_BODY RGB(72, 176, 236)
#define COL_STUDENT_EDGE RGB(206, 238, 255)
#define COL_STUDENT_DOWN RGB(68, 72, 82)
#define COL_STUDENT_DOWN_X RGB(168, 64, 64)
#define COL_HP_BACK RGB(38, 38, 46)
#define COL_HP_OK RGB(96, 226, 128)
#define COL_HP_LOW RGB(238, 96, 96)
#define COL_FLASH RGB(255, 255, 255)

#define COL_TARGET_RING RGB(255, 232, 120)
#define COL_BOSS_BULLET RGB(255, 208, 72)
#define COL_BOSS_BULLET_CORE RGB(255, 255, 255)
#define COL_STUDENT_BULLET RGB(248, 120, 200)
#define COL_WARN RGB(255, 84, 84)
#define COL_WARN_LOCK RGB(255, 160, 96)
#define COL_TEXT RGB(200, 214, 232)
#define COL_TEXT_DIM RGB(132, 146, 166)

/* ---------------------------------------------------------------- 统计 */

static SceneDrawStats g_stats;
static SceneFrameStats g_frame;

const SceneDrawStats *scene_stats(void) { return &g_stats; }
const SceneFrameStats *scene_frame_stats(void) { return &g_frame; }

void scene_stats_reset(void) {
    std::memset(&g_stats, 0, sizeof(g_stats));
    std::memset(&g_frame, 0, sizeof(g_frame));
}

uint32_t scene_draw_radius_px(const Actor *actor) {
    if (actor == NULL) {
        return 0u;
    }
    /* 判定区域与画面一致: 绘制半径 == Actor.radius，不做任何缩放。
     * 配置 v1: boss_hit_radius_equals_body == true → Boss 命中半径 ==
     * boss_radius == 22 px；学生 == student_radius == 20 px。
     * 渲染不参与判定。 */
    float r = actor->radius;
    if (!(r > 0.0f)) {
        return 0u;
    }
    int ri = (int)(r + 0.5f);
    if (ri < 1) {
        ri = 1;
    }
    return (uint32_t)ri;
}

bool scene_warning_visible(const WorldView *view) {
    /* 预警的唯一读取入口: 读锁定计划的公开预警 view.warning（core 的
     * world_make_view 用 pattern_warning(&world->plan, ...) 填充）。
     * valid == false 时不画，也绝不用 marked_target 或规则文档补一份几何。 */
    if (view == NULL) {
        return false;
    }
    return view->warning.valid;
}

/* ---------------------------------------------------------------- 数学 */

static int px(float v) {
    int i = (int)(v + 0.5f);
    if (i < -4096) { i = -4096; }
    if (i > 8192) { i = 8192; }
    return i;
}

static double deg2rad(double d) { return d * 3.14159265358979323846 / 180.0; }

static double norm360(double d) {
    while (d < 0.0) { d += 360.0; }
    while (d >= 360.0) { d -= 360.0; }
    return d;
}

static void line_to(int x1, int y1, int x2, int y2, COLORREF color) {
    setlinecolor(color);
    line(x1, y1, x2, y2);
}

/* 虚线: 用于"提示/预警"这类非实体几何，与实体轮廓的实线区分。 */
static void dashed_line(int x1, int y1, int x2, int y2, int dash, int gap, COLORREF color) {
    double dx = (double)(x2 - x1);
    double dy = (double)(y2 - y1);
    double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.0) { return; }
    if (dash < 1) { dash = 1; }
    if (gap < 1) { gap = 1; }
    double ux = dx / len;
    double uy = dy / len;
    double t = 0.0;
    while (t < len) {
        double t2 = t + (double)dash;
        if (t2 > len) { t2 = len; }
        line_to(x1 + (int)(ux * t + 0.5), y1 + (int)(uy * t + 0.5),
                x1 + (int)(ux * t2 + 0.5), y1 + (int)(uy * t2 + 0.5), color);
        t = t2 + (double)gap;
    }
}

static void dashed_rect(int l, int t, int r, int b, int dash, int gap, COLORREF color) {
    dashed_line(l, t, r, t, dash, gap, color);
    dashed_line(r, t, r, b, dash, gap, color);
    dashed_line(r, b, l, b, dash, gap, color);
    dashed_line(l, b, l, t, dash, gap, color);
}

/* 点状弧: 用于缺口环等"警示性"几何（与弹幕的实心/空心轮廓都不同）。 */
static void dashed_arc(int cx, int cy, int radius, double from_deg, double to_deg,
                       int step_deg, int dash_period, COLORREF color) {
    if (radius < 1 || step_deg < 1) { return; }
    int i = 0;
    for (double a = from_deg; a + (double)step_deg <= to_deg + 0.001; a += (double)step_deg) {
        if ((i / 2) % dash_period == 0) {
            int x1 = cx + (int)(std::cos(deg2rad(a)) * (double)radius + 0.5);
            int y1 = cy + (int)(std::sin(deg2rad(a)) * (double)radius + 0.5);
            int x2 = cx + (int)(std::cos(deg2rad(a + (double)step_deg)) * (double)radius + 0.5);
            int y2 = cy + (int)(std::sin(deg2rad(a + (double)step_deg)) * (double)radius + 0.5);
            line_to(x1, y1, x2, y2, color);
        }
        ++i;
    }
}

static void text_cn(int x, int y, const wchar_t *s, COLORREF color, int height) {
    setbkmode(TRANSPARENT);
    settextcolor(color);
    settextstyle(height, 0, L"Microsoft YaHei");
    outtextxy(x, y, s);
}

/* ---------------------------------------------------------------- 查询 */

static const Actor *find_actor(const WorldView *view, DemoEntityId id) {
    if (view == NULL || id == 0u) { return NULL; }
    if (view->boss != NULL && view->boss->id == id) { return view->boss; }
    if (view->students == NULL) { return NULL; }
    for (uint32_t i = 0u; i < view->student_count; ++i) {
        if (view->students[i].id == id) { return &view->students[i]; }
    }
    return NULL;
}

/* 预警字段可能为 0（占位含义: 本招不使用该字段，或该招式模块尚未给出）。
 * 回退顺序严格限定为: warning 字段 -> 只读 DemoConfig 中同一招式的配置值。
 * 两者都为 0 时返回 0，由调用方决定"不画该部分"，绝不自行编造几何。 */
static const PatternConfig *pattern_cfg(const DemoConfig *cfg, DemoPattern pattern) {
    if (cfg == NULL) { return NULL; }
    if ((int)pattern < 0 || (int)pattern >= (int)DEMO_PATTERN_COUNT) { return NULL; }
    return &cfg->patterns[(int)pattern];
}

static float pick_positive(float from_warning, float from_config) {
    if (from_warning > 0.0f) { return from_warning; }
    if (from_config > 0.0f) { return from_config; }
    return 0.0f;
}

/* ---------------------------------------------------------------- 场景骨架 */

/* 战场边框与合法区域提示。
 * 合法区域 = DemoConfig 的 Boss 移动边界（配置 v1: x ∈ [22,938], y ∈ [22,698]）。
 * 实线 = 硬边界，虚线 = 可活动区域提示，两者形状不同。 */
static void draw_field_frame(const DemoConfig *cfg) {
    setfillcolor(COL_FIELD_BG);
    solidrectangle(0, 0, SCENE_HUD_X - 1, SCENE_WINDOW_H - 1);
    setlinecolor(COL_FIELD_EDGE);
    rectangle(0, 0, SCENE_HUD_X - 1, SCENE_WINDOW_H - 1);
    if (cfg != NULL) {
        dashed_rect(px(cfg->boss_move_min_x), px(cfg->boss_move_min_y),
                    px(cfg->boss_move_max_x), px(cfg->boss_move_max_y), 10, 8, COL_LEGAL_HINT);
    }
    setlinecolor(COL_FIELD_EDGE);
    const int c = 26;
    line(0, 0, c, 0); line(0, 0, 0, c);
    line(SCENE_HUD_X - 1, 0, SCENE_HUD_X - 1 - c, 0); line(SCENE_HUD_X - 1, 0, SCENE_HUD_X - 1, c);
    line(0, SCENE_WINDOW_H - 1, c, SCENE_WINDOW_H - 1);
    line(0, SCENE_WINDOW_H - 1, 0, SCENE_WINDOW_H - 1 - c);
    line(SCENE_HUD_X - 1, SCENE_WINDOW_H - 1, SCENE_HUD_X - 1 - c, SCENE_WINDOW_H - 1);
    line(SCENE_HUD_X - 1, SCENE_WINDOW_H - 1, SCENE_HUD_X - 1, SCENE_WINDOW_H - 1 - c);
}

/* HUD 区域骨架: 只画边框、分隔线与标识；详细内容归 S15（render/hud.cpp）。
 * 本函数在场景最后调用，因此它铺满的 320 px 区域会覆盖任何越界的战场图元，
 * 起到"HUD 独占右侧"的效果，无需依赖 GDI 裁剪区（见 scene_render 的注释）。 */
static void draw_hud_skeleton(const WorldView *view) {
    setfillcolor(COL_HUD_BG);
    solidrectangle(SCENE_HUD_X, 0, SCENE_WINDOW_W - 1, SCENE_WINDOW_H - 1);
    setlinecolor(COL_HUD_EDGE);
    line(SCENE_HUD_X, 0, SCENE_HUD_X, SCENE_WINDOW_H - 1);
    rectangle(SCENE_HUD_X, 0, SCENE_WINDOW_W - 1, SCENE_WINDOW_H - 1);

    text_cn(SCENE_HUD_X + 14, 14, L"Boss demo", COL_TEXT, 26);
    text_cn(SCENE_HUD_X + 14, 46, L"脚本 AI（未训练）", COL_TEXT_DIM, 16);
    dashed_line(SCENE_HUD_X + 12, 74, SCENE_WINDOW_W - 14, 74, 6, 5, COL_HUD_EDGE);

    text_cn(SCENE_HUD_X + 14, 86, L"共享能量", COL_TEXT_DIM, 16);
    const int bx = SCENE_HUD_X + 14;
    const int by = 110;
    const int bw = 292;
    const int bh = 18;
    int emax = (view != NULL && view->energy_max > 0) ? view->energy_max : 1;
    int e = (view != NULL) ? view->energy : 0;
    if (e < 0) { e = 0; }
    if (e > emax) { e = emax; }
    setfillcolor(COL_HP_BACK);
    solidrectangle(bx, by, bx + bw, by + bh);
    setfillcolor(RGB(120, 190, 255));
    if (e > 0) {
        solidrectangle(bx + 1, by + 1, bx + 1 + (bw - 2) * e / emax, by + bh - 1);
    }
    setlinecolor(COL_HUD_EDGE);
    rectangle(bx, by, bx + bw, by + bh);

    dashed_line(SCENE_HUD_X + 14, 152, SCENE_WINDOW_W - 14, 152, 8, 6, COL_HUD_EDGE);
    text_cn(SCENE_HUD_X + 14, 164, L"HUD 内容由 S15 负责", COL_TEXT_DIM, 16);
    text_cn(SCENE_HUD_X + 14, 188, L"（本模块只留出区域）", COL_TEXT_DIM, 16);
}

/* ---------------------------------------------------------------- Boss */

/* Boss 本体: 实心圆 + 高亮描边 + 白色菱形本体核心 + 方向箭头。
 *
 * 形状语言（不依赖配色即可识别）:
 *   - 实心圆，半径 == Actor.radius（== 命中半径 22 px）；
 *   - 圆内白色实心菱形 = 本体标识（素材未取得，用程序图形替代校徽图片）；
 *   - 从圆心指向锁定目标的箭头 = 方向标识。
 *
 * 方向来源: WorldView 未暴露 Boss 朝向字段。本模块用 view->marked_target
 * （core 计算的"最近存活学生"= 自动锁定目标）推出朝向，属只读派生，不发明世界状态。
 * 无存活目标时退化为"朝上"。若母代理后续在 WorldView 增加朝向字段，只需改这里。
 *
 * 受击反馈: invuln_ticks > 0 时闪白 + 绘制抖动；抖动只作用于绘制坐标
 * （局部 jx/jy），不写 Actor、不改判定。 */
static void draw_boss_body(const DemoConfig *cfg, const WorldView *view) {
    const Actor *b = view->boss;
    if (b == NULL) { return; }
    int r = (int)scene_draw_radius_px(b);
    if (r <= 0) { return; }

    int jx = 0;
    int jy = 0;
    bool flash = false;
    if (b->invuln_ticks > 0) {
        flash = ((view->tick / 2) % 2) == 0;
        jx = (flash ? 2 : -2);
        jy = (((view->tick / 3) % 2) == 0) ? 2 : -2;
    }
    int cx = px(b->x) + jx;
    int cy = px(b->y) + jy;

    setfillcolor(flash ? COL_FLASH : COL_BOSS_BODY);
    setlinecolor(COL_BOSS_EDGE);
    fillcircle(cx, cy, r);
    /* 命中半径 == 身体半径的可视证据: 恰好 r 处再描一圈。 */
    setlinecolor(COL_BOSS_HIT);
    circle(cx, cy, r);

    POINT core[4];
    int k = (r * 2) / 3;
    if (k < 3) { k = 3; }
    core[0].x = cx;     core[0].y = cy - k;
    core[1].x = cx + k; core[1].y = cy;
    core[2].x = cx;     core[2].y = cy + k;
    core[3].x = cx - k; core[3].y = cy;
    setfillcolor(COL_BOSS_CORE);
    setlinecolor(COL_BOSS_CORE);
    solidpolygon(core, 4);

    double dirx = 0.0;
    double diry = -1.0;
    const Actor *tgt = find_actor(view, view->marked_target);
    if (tgt != NULL && tgt->alive) {
        double dx = (double)tgt->x - (double)b->x;
        double dy = (double)tgt->y - (double)b->y;
        double len = std::sqrt(dx * dx + dy * dy);
        if (len > 0.001) {
            dirx = dx / len;
            diry = dy / len;
        }
    }
    int tail = r + 6;
    int head = r + 24;
    int ax = cx + (int)(dirx * (double)head);
    int ay = cy + (int)(diry * (double)head);
    setlinecolor(COL_BOSS_DIR);
    line(cx + (int)(dirx * (double)tail), cy + (int)(diry * (double)tail), ax, ay);
    double nx = -diry;
    double ny = dirx;
    line(ax, ay, ax - (int)(dirx * 10.0 + nx * 7.0), ay - (int)(diry * 10.0 + ny * 7.0));
    line(ax, ay, ax - (int)(dirx * 10.0 - nx * 7.0), ay - (int)(diry * 10.0 - ny * 7.0));
    (void)cfg;
}

/* ---------------------------------------------------------------- 学生 */

static void draw_hp_bar(int cx, int top_y, int width, int32_t hp, int32_t hp_max) {
    if (width < 4) { width = 4; }
    if (hp_max <= 0) { hp_max = 1; }
    if (hp < 0) { hp = 0; }
    if (hp > hp_max) { hp = hp_max; }
    const int h = 5;
    int l = cx - width / 2;
    setfillcolor(COL_HP_BACK);
    solidrectangle(l, top_y, l + width, top_y + h);
    int fillw = (width - 2) * hp / hp_max;
    if (fillw > 0) {
        setfillcolor((hp * 3 <= hp_max) ? COL_HP_LOW : COL_HP_OK);
        solidrectangle(l + 1, top_y + 1, l + 1 + fillw, top_y + h - 1);
    }
    setlinecolor(COL_STUDENT_EDGE);
    rectangle(l, top_y, l + width, top_y + h);
}

/* 学生: 圆形（半径 == Actor.radius == 20 px）+ 血量条 + 倒下状态。
 *
 * 形状语言:
 *   - 存活: 空心圆 + 圆内小十字（"+"），血量条在圆上方；
 *   - 倒下: 暗色空心圆 + 大叉号（"×"），血量条清空 —— 只画结果，
 *     "倒下后不发新弹"由 core 保证，渲染不产生任何弹；
 *   - 受击: invuln_ticks > 0 时闪白 + 绘制抖动；
 *   - 目标环: id == view->marked_target 时由 draw_target_ring 叠加十字准星环。 */
static void draw_student(const WorldView *view, const Actor *s) {
    int r = (int)scene_draw_radius_px(s);
    if (r <= 0) { return; }

    int jx = 0;
    int jy = 0;
    bool flash = false;
    if (s->invuln_ticks > 0) {
        flash = ((view->tick / 2) % 2) == 0;
        jx = (flash ? 1 : -1);
        jy = (((view->tick / 3) % 2) == 0) ? 1 : -1;
    }
    int cx = px(s->x) + jx;
    int cy = px(s->y) + jy;

    if (s->alive) {
        setlinecolor(flash ? COL_FLASH : COL_STUDENT_EDGE);
        setlinestyle(PS_SOLID, 2);
        circle(cx, cy, r);
        setlinestyle(PS_SOLID, 1);
        setlinecolor(flash ? COL_FLASH : COL_STUDENT_BODY);
        line(cx - r + 3, cy, cx + r - 3, cy);
        line(cx, cy - r + 3, cx, cy + r - 3);
        draw_hp_bar(cx, cy - r - 12, r * 2, s->hp, s->hp_max);
    } else {
        setlinecolor(COL_STUDENT_DOWN);
        setlinestyle(PS_SOLID, 2);
        circle(cx, cy, r);
        setlinecolor(COL_STUDENT_DOWN_X);
        line(cx - r + 3, cy - r + 3, cx + r - 3, cy + r - 3);
        line(cx + r - 3, cy - r + 3, cx - r + 3, cy + r - 3);
        setlinestyle(PS_SOLID, 1);
        draw_hp_bar(cx, cy - r - 12, r * 2, 0, s->hp_max);
    }
}

/* 目标标记: view->marked_target 对应的学生画目标环（双层准星 + 四角卡爪）。
 * 只读 view 派生量，不重新计算"最近学生"。 */
static void draw_target_ring(const WorldView *view, const Actor *s) {
    int r = (int)scene_draw_radius_px(s) + 10;
    int cx = px(s->x);
    int cy = px(s->y);
    setlinecolor(COL_TARGET_RING);
    circle(cx, cy, r);
    circle(cx, cy, r + 4);
    const int claw = 7;
    line(cx - r, cy - r, cx - r + claw, cy - r); line(cx - r, cy - r, cx - r, cy - r + claw);
    line(cx + r, cy - r, cx + r - claw, cy - r); line(cx + r, cy - r, cx + r, cy - r + claw);
    line(cx - r, cy + r, cx - r + claw, cy + r); line(cx - r, cy + r, cx - r, cy + r - claw);
    line(cx + r, cy + r, cx + r - claw, cy + r); line(cx + r, cy + r, cx + r, cy + r - claw);
    (void)view;
}

/* ---------------------------------------------------------------- 弹幕 */

/* Boss 弹: 实心圆 + 白色实心内芯（"实心 + 白芯"）。
 * 学生弹: 空心圆 + 内接空心三角形（"空心/三角"）。
 * 二者去色后仍可区分: 一个是实心盘带白点，一个是空心环带三角。 */
static void draw_boss_bullet(const Projectile *p) {
    int r = (int)(p->radius + 0.5f);
    if (r < 1) { r = 1; }
    int cx = px(p->x);
    int cy = px(p->y);
    setfillcolor(COL_BOSS_BULLET);
    setlinecolor(COL_BOSS_BULLET);
    solidcircle(cx, cy, r);
    int core = r / 2;
    if (core < 1) { core = 1; }
    setfillcolor(COL_BOSS_BULLET_CORE);
    solidcircle(cx, cy, core);
}

static void draw_student_bullet(const Projectile *p) {
    int r = (int)(p->radius + 0.5f);
    if (r < 1) { r = 1; }
    int cx = px(p->x);
    int cy = px(p->y);
    setlinecolor(COL_STUDENT_BULLET);
    circle(cx, cy, r); /* 空心 */
    POINT tri[4];
    for (int i = 0; i < 3; ++i) {
        double a = deg2rad(90.0 + 120.0 * (double)i);
        double rr = (double)r * 0.62;
        tri[i].x = cx + (int)(std::cos(a) * rr + 0.5);
        tri[i].y = cy + (int)(std::sin(a) * rr + 0.5);
    }
    tri[3] = tri[0];
    setlinecolor(COL_STUDENT_BULLET);
    polyline(tri, 4);
}

/* ---------------------------------------------------------------- 预警

 * 前置条件一律是 scene_warning_visible(view)；几何只读 view->warning，
 * 字段为 0 时回落到只读 DemoConfig 中同一招式的配置值，绝不自行编造锁定几何。
 *
 * 形状语言（均与弹幕形状明显不同）:
 *   RING   环弹: 缺口环（点状环 + 缺口两侧径向标记）
 *   COURSE 课表: 封锁列（竖虚线列阵 + 顶部生成带）+ 连续通道（实线边界）
 *   MINE   金矿: 矿点（实心菱形 + 外环）+ 扇面（朝锁定目标的虚线扇形）
 *   SHOWER 淋浴: 扫描带（垂直于锁定方向的虚线带）+ 缝隙（实线竖向通道）
 *
 * PatternWarning 只暴露 origin/aim/radius_hint/gap_angle_deg/gap_span_deg/
 * corridor_width/wave_count，缺少"矿点数 / 列坐标列表 / 扫描线位置"这类列表字段，
 * 因此只画 warning 能唯一确定的那一份几何，不按规则文档的数量补画。 */

static void draw_warning_lock_axis(const WorldView *view) {
    const PatternWarning *w = &view->warning;
    int ox = px(w->origin_x);
    int oy = px(w->origin_y);
    /* 锁定计划可视化: origin = 接受请求时的 Boss 位置，aim = 锁定的目标位置；
     * 两者在 windup/active 期间不变（core 的不可变 AttackPlan 语义）。 */
    dashed_line(ox, oy, px(w->aim_x), px(w->aim_y), 8, 6, COL_WARN_LOCK);
    setlinecolor(COL_WARN_LOCK);
    line(ox - 6, oy, ox + 6, oy);
    line(ox, oy - 6, ox, oy + 6);
    /* 锁定目标: 虚线菱形包住 warning.target_id 指向的学生（与 marked_target 环区分）。 */
    const Actor *tgt = find_actor(view, w->target_id);
    if (tgt != NULL) {
        int r = (int)scene_draw_radius_px(tgt) + 6;
        int cx = px(tgt->x);
        int cy = px(tgt->y);
        dashed_line(cx - r, cy, cx, cy - r, 6, 4, COL_WARN_LOCK);
        dashed_line(cx, cy - r, cx + r, cy, 6, 4, COL_WARN_LOCK);
        dashed_line(cx + r, cy, cx, cy + r, 6, 4, COL_WARN_LOCK);
        dashed_line(cx, cy + r, cx - r, cy, 6, 4, COL_WARN_LOCK);
    }
}

static void draw_warning_ring(const DemoConfig *cfg, const WorldView *view) {
    const PatternWarning *w = &view->warning;
    const PatternConfig *pc = pattern_cfg(cfg, w->pattern);
    int cx = px(w->origin_x);
    int cy = px(w->origin_y);
    float hint = pick_positive(w->radius_hint, pc != NULL ? pc->spawn_safety_radius : 0.0f);
    int radius;
    if (hint > 1.0f) {
        radius = (int)(hint + 0.5f);
    } else {
        /* 无半径提示时退化用锁定距离（仍来自只读 warning 的 origin/aim），不编造。 */
        double dx = (double)w->aim_x - (double)w->origin_x;
        double dy = (double)w->aim_y - (double)w->origin_y;
        double d = std::sqrt(dx * dx + dy * dy);
        radius = (d > 1.0) ? (int)(d + 0.5) : 0;
    }
    if (radius < 8) { return; }
    float span = pick_positive(w->gap_span_deg, pc != NULL ? pc->gap_span_deg : 0.0f);
    double gap_c = norm360((double)w->gap_angle_deg);
    double half = (double)span * 0.5;
    if (half < 1.0) { half = 1.0; }
    /* 环本体: 跳过缺口区间（显示"哪里安全"）。 */
    dashed_arc(cx, cy, radius, gap_c + half, gap_c + 360.0 - half, 3, 3, COL_WARN);
    /* 缺口两侧径向标记。 */
    for (int s = -1; s <= 1; s += 2) {
        double a = gap_c + (double)s * half;
        line_to(cx + (int)(std::cos(deg2rad(a)) * (double)(radius - 14) + 0.5),
                cy + (int)(std::sin(deg2rad(a)) * (double)(radius - 14) + 0.5),
                cx + (int)(std::cos(deg2rad(a)) * (double)(radius + 14) + 0.5),
                cy + (int)(std::sin(deg2rad(a)) * (double)(radius + 14) + 0.5), COL_WARN);
    }
}

static void draw_warning_course(const DemoConfig *cfg, const WorldView *view) {
    const PatternWarning *w = &view->warning;
    const PatternConfig *pc = pattern_cfg(cfg, w->pattern);
    float cw = pick_positive(w->corridor_width, pc != NULL ? pc->corridor_width : 0.0f);
    if (cw < 8.0f) { return; } /* 无通道宽度信息: 不猜列布局 */
    int corridor_cx = px(w->aim_x);
    int half = (int)(cw * 0.5f + 0.5f);
    if (half < 4) { half = 4; }
    int cl = corridor_cx - half;
    int cr = corridor_cx + half;
    const int top = 0;
    const int bot = SCENE_WINDOW_H - 1;

    /* 封锁列: 每 24 px 一条竖虚线，跳过通道区间。 */
    for (int x = 12; x < SCENE_HUD_X; x += 24) {
        if (x > cl - 24 && x < cr + 24) { continue; }
        dashed_line(x, top, x, bot, 9, 9, COL_WARN);
    }
    /* 顶部生成带（课表/淋浴均从批准顶部区域生成）。 */
    dashed_line(0, 40, SCENE_HUD_X - 1, 40, 10, 7, COL_WARN);
    /* 通道: 实线边界 + 中线，与封锁列的虚线形成对比。 */
    setlinecolor(COL_TARGET_RING);
    line(cl, top, cl, bot);
    line(cr, top, cr, bot);
    line(corridor_cx, top, corridor_cx, bot);
    dashed_line(cl, 56, cr, 56, 5, 4, COL_TARGET_RING);
}

/* 金矿: 矿点 + 扇面。
 * 矿点由 warning 的 aim(锁定目标) + radius_hint(安全距离) + gap_angle_deg
 * 唯一确定: mine = aim + radius_hint * (cos(gap_angle), sin(gap_angle))。
 * 扇面从矿点朝锁定目标张开，半角 = gap_span_deg / 2。
 * PatternWarning 没有"矿点列表/矿点数"，故只画唯一确定的这一个矿点，
 * 不按规则 2.5 的"三扇面"自行补画（见报告"接口缺口"）。 */
static void draw_warning_mine(const DemoConfig *cfg, const WorldView *view) {
    const PatternWarning *w = &view->warning;
    const PatternConfig *pc = pattern_cfg(cfg, w->pattern);
    float dist = pick_positive(w->radius_hint, pc != NULL ? pc->spawn_safety_radius : 0.0f);
    if (dist < 8.0f) { return; }
    double a = deg2rad((double)w->gap_angle_deg);
    double mx = (double)w->aim_x + std::cos(a) * (double)dist;
    double my = (double)w->aim_y + std::sin(a) * (double)dist;
    int mxi = px((float)mx);
    int myi = px((float)my);

    const int r = 14;
    POINT d[4];
    d[0].x = mxi;     d[0].y = myi - r;
    d[1].x = mxi + r; d[1].y = myi;
    d[2].x = mxi;     d[2].y = myi + r;
    d[3].x = mxi - r; d[3].y = myi;
    setlinecolor(COL_WARN);
    setfillcolor(COL_WARN);
    solidpolygon(d, 4);
    setlinecolor(COL_TARGET_RING);
    circle(mxi, myi, r + 6);

    double base = std::atan2((double)w->aim_y - my, (double)w->aim_x - mx);
    float span = pick_positive(w->gap_span_deg, pc != NULL ? pc->gap_span_deg : 0.0f);
    double half = (double)span * 0.5;
    if (half < 1.0) { half = 1.0; }
    int fan_r = (int)(dist + 0.5f);
    if (fan_r < 8) { fan_r = 8; }
    const int steps = 10;
    POINT prev;
    prev.x = mxi + (int)(std::cos(base - deg2rad(half)) * (double)fan_r + 0.5);
    prev.y = myi + (int)(std::sin(base - deg2rad(half)) * (double)fan_r + 0.5);
    for (int i = 1; i <= steps; ++i) {
        double aa = base - deg2rad(half) + deg2rad(half * 2.0) * (double)i / (double)steps;
        POINT cur;
        cur.x = mxi + (int)(std::cos(aa) * (double)fan_r + 0.5);
        cur.y = myi + (int)(std::sin(aa) * (double)fan_r + 0.5);
        if ((i % 2) == 0) {
            line_to(prev.x, prev.y, cur.x, cur.y, COL_WARN);
        }
        prev = cur;
    }
    for (int s = -1; s <= 1; s += 2) {
        double aa = base + deg2rad(half) * (double)s;
        line_to(mxi, myi, mxi + (int)(std::cos(aa) * (double)fan_r + 0.5),
                myi + (int)(std::sin(aa) * (double)fan_r + 0.5), COL_WARN);
    }
}

/* 淋浴: 扫描带 + 缝隙。
 * 锁定方向 = origin -> aim。扫描带垂直于锁定方向并穿过锁定 aim 点，
 * 带宽取 radius_hint（为 0 时回落配置 spawn_safety_radius）；
 * 缝隙是带中宽 corridor_width 的实线竖向通道（规则 2.5: 保留 >=120 px 竖向缝隙）。 */
static void draw_warning_shower(const DemoConfig *cfg, const WorldView *view) {
    const PatternWarning *w = &view->warning;
    const PatternConfig *pc = pattern_cfg(cfg, w->pattern);
    float cw = pick_positive(w->corridor_width, pc != NULL ? pc->corridor_width : 0.0f);
    if (cw < 8.0f) { return; }
    double dx = (double)w->aim_x - (double)w->origin_x;
    double dy = (double)w->aim_y - (double)w->origin_y;
    double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.0) { return; } /* 锁定方向退化: 不猜扫描方向 */
    double dirx = dx / len;
    double diry = dy / len;
    double nx = -diry;
    double ny = dirx;
    int cx = px(w->aim_x);
    int cy = px(w->aim_y);

    const int big = 1200;
    float band = pick_positive(w->radius_hint, pc != NULL ? pc->spawn_safety_radius : 0.0f);
    int bandw = (band > 1.0f) ? (int)(band + 0.5f) : 20;
    if (bandw < 8) { bandw = 8; }
    for (int off = -bandw / 2; off <= bandw / 2; off += 6) {
        int x1 = cx + (int)(nx * (double)off - dirx * (double)big);
        int y1 = cy + (int)(ny * (double)off - diry * (double)big);
        int x2 = cx + (int)(nx * (double)off + dirx * (double)big);
        int y2 = cy + (int)(ny * (double)off + diry * (double)big);
        dashed_line(x1, y1, x2, y2, 12, 10, COL_WARN);
    }
    int half = (int)(cw * 0.5f + 0.5f);
    setlinecolor(COL_TARGET_RING);
    line(cx - half, 0, cx - half, SCENE_WINDOW_H - 1);
    line(cx + half, 0, cx + half, SCENE_WINDOW_H - 1);
    line(cx, 0, cx, SCENE_WINDOW_H - 1);
    int ax = px(w->origin_x) + (int)(dirx * 220.0);
    int ay = px(w->origin_y) + (int)(diry * 220.0);
    setlinecolor(COL_WARN);
    line(px(w->origin_x), px(w->origin_y), ax, ay);
    line(ax, ay, ax - (int)(dirx * 14.0 + nx * 9.0), ay - (int)(diry * 14.0 + ny * 9.0));
    line(ax, ay, ax - (int)(dirx * 14.0 - nx * 9.0), ay - (int)(diry * 14.0 - ny * 9.0));
}

/* 预警总入口: 先读 valid，再按 pattern 分派。
 * 返回是否真的画了预警几何。 */
static bool draw_warning(const DemoConfig *cfg, const WorldView *view) {
    if (!scene_warning_visible(view)) {
        /* 唯一允许"不画预警"的路径。 */
        g_stats.warnings_skipped++;
        return false;
    }
    draw_warning_lock_axis(view);
    switch (view->warning.pattern) {
        case DEMO_PATTERN_RING:   draw_warning_ring(cfg, view);   break;
        case DEMO_PATTERN_COURSE: draw_warning_course(cfg, view); break;
        case DEMO_PATTERN_MINE:   draw_warning_mine(cfg, view);   break;
        case DEMO_PATTERN_SHOWER: draw_warning_shower(cfg, view); break;
        default: break;
    }
    g_stats.warnings_drawn++;
    g_frame.warning_drawn = 1u;
    return true;
}

/* ---------------------------------------------------------------- 主入口 */

void scene_render(const WorldView *view, const DemoConfig *cfg) {
    if (view == NULL || cfg == NULL) {
        return;
    }
    g_stats.frames++;
    g_frame.boss_bullets = 0u;
    g_frame.student_bullets = 0u;
    g_frame.students_alive = 0u;
    g_frame.students_down = 0u;
    g_frame.warning_drawn = 0u;

    /* 注意（实测结论）: 本模块不使用 EasyX 的 setcliprgn/clearcliprgn。
     * 在 EasyX 26.9.25 上，clearcliprgn() 会把已经画好的内容一并清除
     * （probe8: 裁剪区内画好的战场底色在 clearcliprgn 后变回背景色），
     * 且它作用于"当前图形设备"，会破坏调用方（game_main / hud）的同帧绘制。
     * 因此改为: 先画战场与实体，最后由 draw_hud_skeleton 铺满右侧 320 px，
     * 用绘制顺序实现"HUD 独占右侧"，不依赖 GDI 裁剪区。 */

    draw_field_frame(cfg);

    /* 预警画在实体下方（几何提示不应遮挡角色与弹）。 */
    bool warn_drawn = draw_warning(cfg, view);
    if (warn_drawn && !view->warning.valid) {
        g_stats.warnings_drawn_when_invalid++; /* 不可能发生；用于验收条件 4 */
    }

    if (view->boss != NULL) {
        g_stats.boss_drawn++;
        draw_boss_body(cfg, view);
    }
    if (view->students != NULL) {
        for (uint32_t i = 0u; i < view->student_count; ++i) {
            const Actor *s = &view->students[i];
            if (s->alive) {
                g_frame.students_alive++;
                g_stats.students_drawn++;
            } else {
                g_frame.students_down++;
                g_stats.students_down_drawn++;
            }
            draw_student(view, s);
        }
    }
    /* 目标环: 只认 view->marked_target。 */
    {
        const Actor *mt = find_actor(view, view->marked_target);
        if (mt != NULL && mt->alive) {
            g_stats.target_rings_drawn++;
            draw_target_ring(view, mt);
        }
    }
    /* 弹幕: 形状区分（Boss 实心+白芯 / 学生空心+三角）。 */
    if (view->projectiles != NULL) {
        for (uint32_t i = 0u; i < view->projectile_capacity; ++i) {
            const Projectile *p = &view->projectiles[i];
            if (!p->active) { continue; }
            if (p->faction == DEMO_FACTION_BOSS) {
                g_frame.boss_bullets++;
                g_stats.boss_bullets_drawn++;
                draw_boss_bullet(p);
            } else if (p->faction == DEMO_FACTION_STUDENT) {
                g_frame.student_bullets++;
                g_stats.student_bullets_drawn++;
                draw_student_bullet(p);
            }
        }
    }

    draw_hud_skeleton(view);
}
