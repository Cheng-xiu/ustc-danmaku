/* scene.cpp - 战场绘制（只读 WorldView，母代理维护）
 * 接口版本: 2    配置版本: 1
 * 只读 WorldView 与 DemoConfig；不写世界、不消耗 RNG、不改变物理、不自行裁决。
 * Boss 弹与学生弹靠形状/轮廓区分（不依赖颜色）；预警读取锁定计划。
 */
#include "scene.h"

#include <graphics.h>

#include <cmath>
#include <cwchar>

extern "C" {
#include "demo_base.h"
#include "world.h"
}

namespace {

void draw_wtext(int x, int y, const wchar_t *text) {
    outtextxy(x, y, text);
}

void draw_circle_outline(float x, float y, float r, int color, int thickness = 1) {
    setlinecolor(color);
    setlinestyle(PS_SOLID, thickness);
    circle((int)lroundf(x), (int)lroundf(y), (int)lroundf(r));
}

/* Boss 弹: 实心圆 + 白芯, 有明确轮廓 */
void draw_boss_bullet(const Projectile *p) {
    int x = (int)lroundf(p->x);
    int y = (int)lroundf(p->y);
    int r = (int)lroundf(p->radius);
    if (r < 2) {
        r = 2;
    }
    setfillcolor(RGB(255, 96, 96));
    solidcircle(x, y, r);
    setfillcolor(RGB(255, 240, 240));
    solidcircle(x, y, (r > 3) ? 2 : 1);
}

/* 学生弹: 空心菱形/三角轮廓, 形状与学生/敌人弹都可区分 */
void draw_student_bullet(const Projectile *p) {
    int x = (int)lroundf(p->x);
    int y = (int)lroundf(p->y);
    int r = (int)lroundf(p->radius) + 2;
    if (r < 3) {
        r = 3;
    }
    POINT pts[4];
    pts[0] = {x, y - r};
    pts[1] = {x + r, y};
    pts[2] = {x, y + r};
    pts[3] = {x - r, y};
    setlinecolor(RGB(180, 230, 255));
    setfillcolor(RGB(30, 60, 100));
    setlinestyle(PS_SOLID, 2);
    fillpolygon(pts, 4);
}

void draw_warning(const PatternWarning *w, const DemoConfig *cfg) {
    if (w == NULL || !w->valid) {
        return;
    }
    switch (w->pattern) {
        case DEMO_PATTERN_RING: {
            /* 缺口环: 用扇形表示缺口位置 */
            float r = 150.0f;
            setlinecolor(RGB(120, 255, 120));
            setlinestyle(PS_SOLID, 2);
            circle((int)lroundf(w->origin_x), (int)lroundf(w->origin_y), (int)lroundf(r));
            float a0 = (w->gap_angle_deg - w->gap_span_deg * 0.5f) * 3.14159265f / 180.0f;
            float a1 = (w->gap_angle_deg + w->gap_span_deg * 0.5f) * 3.14159265f / 180.0f;
            setlinecolor(RGB(60, 160, 60));
            setlinestyle(PS_SOLID, 3);
            line((int)lroundf(w->origin_x), (int)lroundf(w->origin_y),
                 (int)lroundf(w->origin_x + cosf(a0) * r),
                 (int)lroundf(w->origin_y + sinf(a0) * r));
            line((int)lroundf(w->origin_x), (int)lroundf(w->origin_y),
                 (int)lroundf(w->origin_x + cosf(a1) * r),
                 (int)lroundf(w->origin_y + sinf(a1) * r));
            break;
        }
        case DEMO_PATTERN_COURSE: {
            /* 封锁列与通道: 画 3 列示意, 通道列高亮 */
            float colw = cfg->field_w / 3.0f;
            for (int c = 0; c < 3; ++c) {
                int x0 = (int)lroundf(colw * (float)c);
                int x1 = (int)lroundf(colw * (float)(c + 1));
                setlinecolor(RGB(120, 170, 255));
                setlinestyle(PS_SOLID, 2);
                rectangle(x0 + 2, 0, x1 - 2, (int)cfg->field_h - 2);
            }
            break;
        }
        case DEMO_PATTERN_MINE: {
            /* 矿点与三扇面方向 */
            setfillcolor(RGB(255, 210, 90));
            solidcircle((int)lroundf(w->origin_x), (int)lroundf(w->origin_y), 8);
            setlinecolor(RGB(255, 210, 90));
            setlinestyle(PS_SOLID, 2);
            for (int fan = -1; fan <= 1; ++fan) {
                float ang = (w->gap_angle_deg + (float)fan * w->gap_span_deg) * 3.14159265f / 180.0f;
                line((int)lroundf(w->origin_x), (int)lroundf(w->origin_y),
                     (int)lroundf(w->origin_x + cosf(ang) * 200.0f),
                     (int)lroundf(w->origin_y + sinf(ang) * 200.0f));
            }
            break;
        }
        case DEMO_PATTERN_SHOWER: {
            /* 淋浴: 顶部扫描带与竖向缝隙 */
            setlinecolor(RGB(200, 200, 255));
            setlinestyle(PS_SOLID, 1);
            line(0, 100, (int)cfg->field_w, 100);
            float gap = w->corridor_width;
            float center = cfg->field_w * 0.5f;
            setfillcolor(RGB(60, 80, 130));
            solidrectangle((int)(center - gap * 0.5f), 0, (int)(center + gap * 0.5f), 140);
            break;
        }
        default:
            break;
    }
}

}  // namespace

void scene_render(const WorldView *view, const DemoConfig *cfg) {
    if (view == NULL || cfg == NULL) {
        return;
    }

    /* 战场边框与背景网格 */
    setlinecolor(RGB(50, 60, 90));
    setlinestyle(PS_SOLID, 1);
    rectangle(0, 0, (int)cfg->field_w, (int)cfg->field_h - 1);
    for (int x = 0; x < (int)cfg->field_w; x += 120) {
        line(x, 0, x, (int)cfg->field_h);
    }
    for (int y = 0; y < (int)cfg->field_h; y += 120) {
        line(0, y, (int)cfg->field_w, y);
    }
    /* 合法移动区提示 */
    setlinecolor(RGB(70, 90, 130));
    rectangle((int)cfg->boss_move_min_x, (int)cfg->boss_move_min_y, (int)cfg->boss_move_max_x,
              (int)cfg->boss_move_max_y);

    if (view->plan_active) {
        draw_warning(&view->warning, cfg);
    }

    /* 弹幕 */
    for (uint32_t i = 0; i < view->projectile_capacity; ++i) {
        const Projectile *p = &view->projectiles[i];
        if (!p->active) {
            continue;
        }
        if (p->faction == DEMO_FACTION_BOSS) {
            draw_boss_bullet(p);
        } else {
            draw_student_bullet(p);
        }
    }

    /* 学生 */
    for (uint32_t i = 0; i < view->student_count; ++i) {
        const Actor *s = &view->students[i];
        if (!s->alive) {
            /* 倒下: 暗色叉号 */
            int x = (int)lroundf(s->x);
            int y = (int)lroundf(s->y);
            int r = (int)lroundf(s->radius);
            setlinecolor(RGB(90, 90, 100));
            setlinestyle(PS_SOLID, 2);
            line(x - r, y - r, x + r, y + r);
            line(x - r, y + r, x + r, y - r);
            continue;
        }
        int x = (int)lroundf(s->x);
        int y = (int)lroundf(s->y);
        int r = (int)lroundf(s->radius);
        bool inv = s->invuln_ticks > 0;
        setfillcolor(inv ? RGB(255, 255, 255) : RGB(90, 200, 160));
        solidcircle(x, y, r);
        /* 血量条 */
        int bw = r * 2;
        int bh = 4;
        int bx = x - r;
        int by = y - r - 10;
        setfillcolor(RGB(60, 60, 70));
        solidrectangle(bx, by, bx + bw, by + bh);
        if (s->hp_max > 0) {
            int fill = (int)((float)bw * (float)s->hp / (float)s->hp_max);
            setfillcolor(RGB(255, 120, 120));
            solidrectangle(bx, by, bx + fill, by + bh);
        }
        /* 目标标记 */
        if (view->marked_target == s->id) {
            draw_circle_outline(s->x, s->y, s->radius + 7.0f, RGB(255, 220, 90), 3);
        }
    }

    /* Boss: 本体半径即判定半径 */
    if (view->boss != NULL) {
        const Actor *b = view->boss;
        int x = (int)lroundf(b->x);
        int y = (int)lroundf(b->y);
        int r = (int)lroundf(b->radius);
        bool inv = b->invuln_ticks > 0;
        setfillcolor(inv ? RGB(255, 255, 255) : RGB(230, 90, 90));
        solidcircle(x, y, r);
        draw_circle_outline(b->x, b->y, b->radius, RGB(255, 200, 200), 2);
        /* 朝向/本体标识: 小方口, 明确不使用校徽图片素材 */
        setfillcolor(RGB(30, 30, 40));
        solidrectangle(x - r / 2, y - r / 2, x + r / 2, y + r / 2);
        /* 生命条 */
        int bw = r * 2 + 10;
        int bx = x - bw / 2;
        int by = y + r + 10;
        setfillcolor(RGB(60, 60, 70));
        solidrectangle(bx, by, bx + bw, by + 5);
        if (b->hp_max > 0) {
            int fill = (int)((float)bw * (float)b->hp / (float)b->hp_max);
            setfillcolor(RGB(120, 255, 160));
            solidrectangle(bx, by, bx + fill, by + 5);
        }
    }

    /* 面板分隔线 */
    setlinecolor(RGB(60, 70, 100));
    setlinestyle(PS_SOLID, 2);
    line((int)cfg->field_w, 0, (int)cfg->field_w, (int)cfg->field_h);
}
