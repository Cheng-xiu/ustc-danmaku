/* test_collision.c - S03 扫掠碰撞验收测试
 *
 * 覆盖任务卡 10 项验收条件, 逐项在 stdout 打印 PASS/FAIL 与最终汇总。
 * 只测试 core/collision.c 的数学判定: 不涉及扣血、移除弹、胜负。
 * 所有"恰好命中/恰好相切"的用例都使用 float 可精确表示的值(3-4-5 直角三角形等),
 * 以免把浮点噪声误判为实现错误。
 *
 * 编译(在仓库根目录执行, <repo> 为仓库绝对路径):
 *   gcc -std=c11 -Wall -Wextra -Werror -I core tests/test_collision.c core/collision.c \
 *       -o work/agents/S03/build/test_collision.exe
 */

#include "demo_base.h"

#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

/* ---------------------------------------------------------------- 断言工具 */

static int g_pass = 0;
static int g_fail = 0;
static const char *g_section = "";

static void section(const char *name) {
    g_section = name;
    printf("\n== %s ==\n", name);
}

static void check(bool ok, const char *what) {
    if (ok) {
        ++g_pass;
        printf("  PASS  %s\n", what);
    } else {
        ++g_fail;
        printf("  FAIL  %s   [%s]\n", what, g_section);
    }
}

static bool finite_f(float v) {
    return !isnan(v) && !isinf(v);
}

static bool near_f(float got, float want, float tol) {
    if (!finite_f(got) || !finite_f(want)) {
        return false;
    }
    return fabsf(got - want) <= tol;
}

static Segment seg(float ax, float ay, float bx, float by) {
    Segment s;
    s.ax = ax;
    s.ay = ay;
    s.bx = bx;
    s.by = by;
    return s;
}

/* 独立复算: 相对运动轨迹 P(t) = (A.a - B.a) + t * ((A.b-A.a) - (B.b-B.a)) 的最小距离。
 * 用于交叉核对 sweep_min_distance, 不调用被测函数。 */
static float brute_min_distance(const Segment *a, const Segment *b, int samples) {
    float best = 1e30f;
    for (int i = 0; i <= samples; ++i) {
        const float t = (float)i / (float)samples;
        const float pax = a->ax + t * (a->bx - a->ax);
        const float pay = a->ay + t * (a->by - a->ay);
        const float pbx = b->ax + t * (b->bx - b->ax);
        const float pby = b->ay + t * (b->by - b->ay);
        const float dx = pax - pbx;
        const float dy = pay - pby;
        const float d = sqrtf(dx * dx + dy * dy);
        if (d < best) {
            best = d;
        }
    }
    return best;
}

/* ---------------------------------------------------------------- 1 静止对静止 */

static void test_static_pair(void) {
    section("1 静止对静止");
    const Segment a = seg(0.0f, 0.0f, 0.0f, 0.0f);
    const Segment b = seg(3.0f, 4.0f, 3.0f, 4.0f); /* 精确 3-4-5 距离 = 5 */
    float t = -1.0f;
    float d = -1.0f;

    bool hit = sweep_hit(&a, &b, 5.0f, &t, &d);
    check(hit, "距离 5 恰等于 radius_sum 5 -> 命中");
    check(near_f(d, 5.0f, 1e-5f), "min_distance == 5");
    check(near_f(t, 0.0f, 1e-6f), "零位移命中时 t_min == 0");
    check(finite_f(t) && finite_f(d), "静止命中输出无 NaN/Inf");

    t = -1.0f;
    d = -1.0f;
    hit = sweep_hit(&a, &b, 4.0f, &t, &d);
    check(!hit, "距离 5 > radius_sum 4 -> 不命中");
    check(near_f(d, 5.0f, 1e-5f), "不命中时 min_distance 仍为 5");
    check(finite_f(t) && finite_f(d), "静止不命中输出无 NaN/Inf");
    check(near_f(sweep_min_distance(&a, &b, &t), 5.0f, 1e-5f),
          "sweep_min_distance 与 sweep_hit 的 min_distance 一致");
}

/* ---------------------------------------------------------------- 2 相切 */

static void test_tangent(void) {
    section("2 相切与略大于半径和");
    const Segment a = seg(-10.0f, 0.0f, 10.0f, 0.0f); /* 沿 x 轴经过原点 */
    const Segment b = seg(0.0f, 3.0f, 0.0f, 3.0f);    /* 静止点, 垂直距离 3 */
    float t = -1.0f;
    float d = -1.0f;

    bool hit = sweep_hit(&a, &b, 3.0f, &t, &d);
    check(hit, "距离恰等于 radius_sum 3 -> 命中(相切)");
    check(near_f(d, 3.0f, 1e-5f), "相切 min_distance == 3");
    check(near_f(t, 0.5f, 1e-4f), "相切 t_min == 0.5");
    check(t >= 0.0f && t <= 1.0f, "t_min 落在 [0,1]");

    const Segment b_far = seg(0.0f, 3.001f, 0.0f, 3.001f); /* 比半径和大 1e-3 */
    t = -1.0f;
    d = -1.0f;
    hit = sweep_hit(&a, &b_far, 3.0f, &t, &d);
    check(!hit, "距离 3.001 > radius_sum 3 -> 不命中");
    check(d > 3.0f, "不命中时 min_distance 略大于半径和");
    check(near_f(d, 3.001f, 1e-3f), "min_distance 与解析值 3.001 相符(误差 <= 1e-3)");
}

/* ---------------------------------------------------------------- 3 错过 */

static void test_miss_parallel(void) {
    section("3 平行错过与解析最小距离");
    const Segment a = seg(-10.0f, 5.0f, 10.0f, 5.0f); /* 平行于 x 轴, y = 5 */
    const Segment b = seg(0.0f, 0.0f, 0.0f, 0.0f);
    float t = -1.0f;
    float d = -1.0f;

    bool hit = sweep_hit(&a, &b, 4.0f, &t, &d);
    check(!hit, "平行经过, 最小距离 5 > 4 -> 不命中");
    check(near_f(d, 5.0f, 1e-3f), "min_distance 等于解析值 5(误差 <= 1e-3)");
    check(near_f(t, 0.5f, 1e-4f), "t_min 给出取到最小距离之处 0.5");

    t = 0.0f;
    const float only_min = sweep_min_distance(&a, &b, &t);
    check(near_f(only_min, 5.0f, 1e-3f), "sweep_min_distance 返回解析值 5");
    check(near_f(t, 0.5f, 1e-4f), "sweep_min_distance 输出 t_min == 0.5");

    /* 与独立采样复算交叉核对(2001 个采样点) */
    const float sampled = brute_min_distance(&a, &b, 2000);
    check(fabsf(sampled - only_min) <= 1e-3f, "与独立采样最小距离一致(<= 1e-3)");
    check(only_min <= sampled + 1e-5f, "解析最小值不大于采样最小值");
}

/* ---------------------------------------------------------------- 4 双方同时移动 */

static void test_both_moving(void) {
    section("4 双方同时移动");
    /* (a) 同向同速: 相对位移为 0, 但两者绝对都在运动, 结果须与静止判定一致 */
    const Segment a_m = seg(0.0f, 0.0f, 1.0f, 0.0f);
    const Segment b_m = seg(0.0f, 10.0f, 1.0f, 10.0f);
    const Segment a_s = seg(0.0f, 0.0f, 0.0f, 0.0f);
    const Segment b_s = seg(0.0f, 10.0f, 0.0f, 10.0f);

    for (int i = 0; i < 2; ++i) {
        const float r = (i == 0) ? 9.0f : 10.0f;
        float tm = -1.0f;
        float dm = -1.0f;
        float ts = -1.0f;
        float ds = -1.0f;
        const bool hm = sweep_hit(&a_m, &b_m, r, &tm, &dm);
        const bool hs = sweep_hit(&a_s, &b_s, r, &ts, &ds);
        check(hm == hs, (r < 10.0f) ? "同向同速: 与静止判定同为不命中"
                                    : "同向同速: 与静止判定同为命中");
        check(near_f(dm, 10.0f, 1e-4f), "同向同速: 最小距离仍为 10");
        check(near_f(dm, ds, 1e-5f) && near_f(tm, ts, 1e-5f),
              "同向同速: 输出与静止判定逐值一致");
    }

    /* (b) 相对靠近: A 正向、B 反向, 相对速度叠加, 恰在 t = 0.5 相遇 */
    const Segment a = seg(0.0f, 0.0f, 10.0f, 0.0f);
    const Segment b = seg(10.0f, 0.0f, 0.0f, 0.0f);
    float t = -1.0f;
    float d = -1.0f;
    const bool hit = sweep_hit(&a, &b, 0.0f, &t, &d);
    check(hit, "正面相对靠近(radius_sum 0) -> 命中");
    check(near_f(t, 0.5f, 1e-4f), "相对靠近 t_min == 0.5");
    check(near_f(d, 0.0f, 1e-5f), "相对靠近 min_distance == 0");

    /* (c) 一前一后同向但速度不同: 快者追上前者 */
    const Segment a_fast = seg(0.0f, 0.0f, 20.0f, 0.0f);
    const Segment b_slow = seg(10.0f, 0.0f, 20.0f, 0.0f);
    t = -1.0f;
    d = -1.0f;
    const bool hit2 = sweep_hit(&a_fast, &b_slow, 3.0f, &t, &d);
    check(hit2, "同向不同速追及 -> 命中");
    /* 相对位置 r(t) = -10 + 10t: |r| == 3 首次成立在 t = 0.7 */
    check(near_f(t, 0.7f, 1e-4f), "追及 t_min == 0.7(相对位移 10, 起始间距 10, 半径和 3)");
    check(near_f(d, 0.0f, 1e-5f), "追及最小距离 == 0(相对轨迹终点落在原点)");
}

/* ---------------------------------------------------------------- 5 高速穿过 */

static void test_high_speed(void) {
    section("5 高速一步跨过");
    const Segment a = seg(-1000.0f, 0.0f, 1000.0f, 0.0f); /* 一步跨 2000 px */
    const Segment b = seg(0.0f, 0.0f, 0.0f, 0.0f);
    float t = -1.0f;
    float d = -1.0f;

    const bool hit = sweep_hit(&a, &b, 1.0f, &t, &d);
    check(hit, "相对位移 >> 半径和, 端点不在圆内 -> 仍命中");
    check(t > 0.0f && t < 1.0f, "t_min 落在 (0,1) 开区间");
    /* 相对位置 r(t) = -1000 + 2000t: |r| == 1 首次成立在 t = 999/2000 = 0.4995 */
    check(near_f(t, 0.4995f, 1e-4f), "t_min == 0.4995(几何解析值, 非端点)");
    check(near_f(d, 0.0f, 1e-5f), "min_distance == 0(正对穿过)");

    /* 双方互相高速反向穿过 */
    const Segment a2 = seg(-1000.0f, 0.0f, 1000.0f, 0.0f);
    const Segment b2 = seg(1000.0f, 5.0f, -1000.0f, 5.0f);
    t = -1.0f;
    d = -1.0f;
    const bool hit2 = sweep_hit(&a2, &b2, 5.0f, &t, &d);
    check(hit2, "双方高速反向擦过(间距 5 = 半径和) -> 命中");
    check(t > 0.0f && t < 1.0f, "双向高速 t_min 仍落在 (0,1)");
    check(near_f(d, 5.0f, 1e-3f), "双向高速最小距离 == 5");
}

/* ---------------------------------------------------------------- 6 零相对运动 */

static void test_zero_relative(void) {
    section("6 零相对运动(不除零)");
    /* 绝对都在动但相对位移为 0 */
    const Segment a = seg(3.0f, 4.0f, 13.0f, 4.0f);
    const Segment b = seg(0.0f, 0.0f, 10.0f, 0.0f); /* 相对位移 (0,0), 距离 5 */
    float t = -1.0f;
    float d = -1.0f;

    bool hit = sweep_hit(&a, &b, 5.0f, &t, &d);
    check(hit, "相对位移 0 且距离 5 <= radius_sum 5 -> 命中");
    check(t == 0.0f, "零相对运动命中时 t_min == 0");
    check(near_f(d, 5.0f, 1e-5f), "零相对运动 min_distance == 5");
    check(finite_f(t) && finite_f(d), "零相对运动输出无 NaN");

    t = -1.0f;
    d = -1.0f;
    hit = sweep_hit(&a, &b, 4.0f, &t, &d);
    check(!hit, "相对位移 0 且距离 5 > radius_sum 4 -> 不命中");
    check(t >= 0.0f && t <= 1.0f, "零相对运动不命中时 t_min 仍在 [0,1]");
    check(finite_f(t) && finite_f(d), "零相对运动不命中输出无 NaN");

    /* 完全静止且重合 */
    const Segment same = seg(1.0f, 2.0f, 1.0f, 2.0f);
    t = -1.0f;
    d = -1.0f;
    hit = sweep_hit(&same, &same, 0.0f, &t, &d);
    check(hit && t == 0.0f && near_f(d, 0.0f, 0.0f),
          "零相对运动且完全重合 -> 命中, t_min == 0, min_distance == 0");

    t = -1.0f;
    const float md = sweep_min_distance(&a, &b, &t);
    check(near_f(md, 5.0f, 1e-5f) && t == 0.0f,
          "sweep_min_distance 零相对运动分支: 距离 5, t == 0");
}

/* ---------------------------------------------------------------- 7 最早交点 */

static void test_earliest(void) {
    section("7 最早交点(进入点而非离开点)");
    /* 一个先接近又离开的相对线段: 进入 t = 0.3, 离开 t = 0.7, 半径和 4 */
    const Segment a = seg(0.0f, 0.0f, 0.0f, 0.0f);
    const Segment b = seg(-10.0f, 0.0f, 10.0f, 0.0f);
    float t = -1.0f;
    float d = -1.0f;
    bool hit = sweep_hit(&a, &b, 4.0f, &t, &d);
    check(hit, "先接近又离开的相对线段 -> 命中");
    check(near_f(t, 0.3f, 1e-4f), "给出较早参数 t_min == 0.3(而非离开点 0.7)");
    check(t < 0.7f, "t_min 严格小于离开参数");
    check(near_f(d, 0.0f, 1e-5f), "穿透时 min_distance == 0");

    /* 两条不同半径和的 B 线段分别测试: 半径和越大, 最早交点参数越小 */
    const Segment a2 = seg(-10.0f, 0.0f, 10.0f, 0.0f);
    const Segment b2 = seg(0.0f, 0.0f, 0.0f, 0.0f);
    float t_small = -1.0f;
    float t_big = -1.0f;
    float d_small = -1.0f;
    float d_big = -1.0f;
    const bool hit_small = sweep_hit(&a2, &b2, 1.0f, &t_small, &d_small);
    const bool hit_big = sweep_hit(&a2, &b2, 6.0f, &t_big, &d_big);
    check(hit_small && hit_big, "不同半径和的两条 B 线段都命中");
    check(near_f(t_small, 0.45f, 1e-4f), "radius_sum 1 -> t_min == 0.45");
    check(near_f(t_big, 0.2f, 1e-4f), "radius_sum 6 -> t_min == 0.2");
    check(t_big < t_small, "更大的半径和给出更早的交点参数(单调)");
    check(near_f(d_small, 0.0f, 1e-5f) && near_f(d_big, 0.0f, 1e-5f),
          "两种半径和的正对穿过 min_distance 均为 0");
}

/* ---------------------------------------------------------------- 8 线段-矩形 AABB */

static void test_aabb(void) {
    section("8 线段-矩形 AABB");
    const float lo_x = 0.0f;
    const float lo_y = 0.0f;
    const float hi_x = 10.0f;
    const float hi_y = 10.0f;

    Segment s = seg(-10.0f, 5.0f, 10.0f, 5.0f);
    check(segment_hits_aabb(&s, lo_x, lo_y, hi_x, hi_y), "横穿矩形 -> 命中");

    s = seg(5.0f, 5.0f, 20.0f, 20.0f);
    check(segment_hits_aabb(&s, lo_x, lo_y, hi_x, hi_y), "一端在内部、一端在外 -> 命中");

    s = seg(2.0f, 2.0f, 8.0f, 8.0f);
    check(segment_hits_aabb(&s, lo_x, lo_y, hi_x, hi_y), "线段完全在内部 -> 命中");

    s = seg(20.0f, 20.0f, 30.0f, 30.0f);
    check(!segment_hits_aabb(&s, lo_x, lo_y, hi_x, hi_y), "线段完全在外 -> 不命中");

    s = seg(11.0f, 5.0f, 20.0f, 5.0f);
    check(!segment_hits_aabb(&s, lo_x, lo_y, hi_x, hi_y), "平行于边界但与矩形不相接 -> 不命中");

    s = seg(-5.0f, 10.0f, 5.0f, 10.0f);
    check(segment_hits_aabb(&s, lo_x, lo_y, hi_x, hi_y), "贴着上边界(闭区间) -> 命中");

    s = seg(-5.0f, 0.0f, 5.0f, 0.0f);
    check(segment_hits_aabb(&s, lo_x, lo_y, hi_x, hi_y), "贴着下边界(闭区间) -> 命中");

    s = seg(0.0f, -5.0f, 0.0f, 5.0f);
    check(segment_hits_aabb(&s, lo_x, lo_y, hi_x, hi_y), "贴着左边界(闭区间) -> 命中");

    s = seg(-1.0f, 10.5f, 11.0f, 10.5f);
    check(!segment_hits_aabb(&s, lo_x, lo_y, hi_x, hi_y), "在边界外 0.5 px 平行经过 -> 不命中");

    s = seg(10.0f, 10.0f, 10.0f, 10.0f);
    check(segment_hits_aabb(&s, lo_x, lo_y, hi_x, hi_y), "零长度线段位于角点(闭区间) -> 命中");

    s = seg(5.0f, 5.0f, 5.0f, 5.0f);
    check(segment_hits_aabb(&s, lo_x, lo_y, hi_x, hi_y), "零长度线段在内部 -> 命中");

    s = seg(10.5f, 10.5f, 10.5f, 10.5f);
    check(!segment_hits_aabb(&s, lo_x, lo_y, hi_x, hi_y), "零长度线段在外部 -> 不命中");

    s = seg(-5.0f, -5.0f, -1.0f, -1.0f);
    check(!segment_hits_aabb(&s, lo_x, lo_y, hi_x, hi_y), "完全在外的斜线段 -> 不命中");

    s = seg(0.0f, 0.0f, -10.0f, -10.0f);
    check(segment_hits_aabb(&s, lo_x, lo_y, hi_x, hi_y), "起点在角点、朝外 -> 命中(闭区间)");

    s = seg(-5.0f, 5.0f, 5.0f, 5.0f);
    check(segment_hits_aabb(&s, lo_x, lo_y, hi_x, hi_y) ==
              segment_hits_aabb(&s, lo_x, lo_y, hi_x, hi_y),
          "同一输入重复调用结果一致(无隐藏状态)");

    /* 上下界写反: 实现按两点归一化, 结果与正序一致(避免把调用方的参数错误
     * 静默变成"永不命中"); 此行为在此显式测试, 不是未覆盖的隐式行为。 */
    s = seg(5.0f, 5.0f, 6.0f, 6.0f);
    check(segment_hits_aabb(&s, hi_x, hi_y, lo_x, lo_y) ==
              segment_hits_aabb(&s, lo_x, lo_y, hi_x, hi_y),
          "上下界写反时与正序结果一致(线段在内)");
    s = seg(20.0f, 20.0f, 30.0f, 30.0f);
    check(!segment_hits_aabb(&s, hi_x, hi_y, lo_x, lo_y), "上下界写反且线段在外 -> 不命中");

    /* 零面积矩形(退化为点/线): 闭区间语义 */
    s = seg(0.0f, 0.0f, 10.0f, 0.0f);
    check(segment_hits_aabb(&s, 5.0f, 0.0f, 5.0f, 0.0f), "零面积矩形(点)落在线段上 -> 命中");
    check(segment_hits_aabb(&s, 5.0f, 0.0f, 5.0f, 10.0f), "零宽度矩形与线段相交 -> 命中");
    check(!segment_hits_aabb(&s, 5.0f, 1.0f, 5.0f, 10.0f), "零宽度矩形与线段不相交 -> 不命中");
}

/* ---------------------------------------------------------------- 9 线段-圆 */

static void test_circle(void) {
    section("9 线段-圆");
    Segment s = seg(-10.0f, 0.0f, 10.0f, 0.0f);
    check(segment_hits_circle(&s, 0.0f, 0.0f, 5.0f), "穿过圆心 -> 命中");

    s = seg(-10.0f, 5.0f, 10.0f, 5.0f);
    check(segment_hits_circle(&s, 0.0f, 0.0f, 5.0f), "恰好相切(距离 == 半径) -> 命中(闭圆)");

    s = seg(-10.0f, 5.001f, 10.0f, 5.001f);
    check(!segment_hits_circle(&s, 0.0f, 0.0f, 5.0f), "距离 5.001 > 半径 5 -> 不命中");

    s = seg(1.0f, 0.0f, 100.0f, 0.0f);
    check(segment_hits_circle(&s, 0.0f, 0.0f, 5.0f), "起点在圆内 -> 命中");

    s = seg(6.0f, 0.0f, 100.0f, 0.0f);
    check(!segment_hits_circle(&s, 0.0f, 0.0f, 5.0f), "整段在圆外 -> 不命中");

    s = seg(3.0f, 4.0f, 3.0f, 4.0f);
    check(segment_hits_circle(&s, 0.0f, 0.0f, 5.0f), "零长度线段在圆上(距离 == 半径) -> 命中");

    s = seg(0.0f, 0.0f, 0.0f, 0.0f);
    check(segment_hits_circle(&s, 0.0f, 0.0f, 0.0f), "零长度线段与半径 0 的圆重合 -> 命中");

    s = seg(10.0f, 0.0f, 10.0f, 0.0f);
    check(!segment_hits_circle(&s, 0.0f, 0.0f, 5.0f), "零长度线段在圆外 -> 不命中");

    s = seg(-10.0f, -10.0f, -5.0f, -10.0f);
    check(!segment_hits_circle(&s, 0.0f, 0.0f, 5.0f), "圆外线段的最近点(端点)在圆外 -> 不命中");

    /* 端点离圆心 3(圆内), 另一端 20: 最近点在段内 */
    s = seg(3.0f, 0.0f, 20.0f, 0.0f);
    check(segment_hits_circle(&s, 10.0f, 0.0f, 5.0f), "线段穿过圆但两端都在外 -> 命中");
}

/* ---------------------------------------------------------------- 10 防御分支 */

static void test_defensive(void) {
    section("10 空指针与负半径等防御分支");
    const Segment a = seg(0.0f, 0.0f, 1.0f, 0.0f);
    const Segment b = seg(0.5f, 0.0f, 0.5f, 0.0f);
    float t = -1.0f;
    float d = -1.0f;

    check(!sweep_hit(NULL, &b, 1.0f, &t, &d), "a == NULL -> false");
    check(t == 0.0f && d == 0.0f, "a == NULL 时输出被置为 0(非 NaN)");
    t = -1.0f;
    d = -1.0f;
    check(!sweep_hit(&a, NULL, 1.0f, &t, &d), "b == NULL -> false");
    check(finite_f(t) && finite_f(d), "b == NULL 时输出有限");

    /* 输出指针为 NULL 时不得崩溃 */
    check(!sweep_hit(NULL, NULL, 1.0f, NULL, NULL), "两个线段指针与输出指针均为 NULL -> false 不崩溃");
    check((sweep_hit(&a, &b, 1.0f, NULL, NULL)), "输出指针为 NULL 但命中 -> 仍返回 true");
    check(near_f(sweep_min_distance(&a, &b, NULL), 0.0f, 1e-5f),
          "sweep_min_distance 输出指针 NULL 不崩溃");
    check(sweep_min_distance(NULL, &b, NULL) == 0.0f, "sweep_min_distance(a == NULL) 返回 0");

    t = -1.0f;
    const float md = sweep_min_distance(NULL, &b, &t);
    check(md == 0.0f && t == 0.0f, "sweep_min_distance 空指针: 返回 0 且 t_min == 0, 不是 +INF/NaN");
    t = -1.0f;
    check(sweep_min_distance(&a, NULL, &t) == 0.0f && t == 0.0f,
          "sweep_min_distance(b == NULL): 返回 0 且 t_min == 0");

    /* 非有限输入 */
    const Segment nan_seg = seg(NAN, 0.0f, 1.0f, 0.0f);
    const Segment inf_seg = seg(0.0f, 0.0f, INFINITY, 0.0f);
    t = -1.0f;
    d = -1.0f;
    check(!sweep_hit(&nan_seg, &b, 1.0f, &t, &d), "NaN 坐标 -> false");
    check(finite_f(t) && finite_f(d), "NaN 坐标输出有限");
    t = -1.0f;
    d = -1.0f;
    check(!sweep_hit(&inf_seg, &b, 1.0f, &t, &d), "Inf 坐标 -> false");
    check(finite_f(t) && finite_f(d), "Inf 坐标输出有限");
    check(sweep_min_distance(&nan_seg, &b, &t) == 0.0f, "NaN 坐标最小距离返回 0");
    check(!segment_hits_aabb(&nan_seg, 0.0f, 0.0f, 10.0f, 10.0f), "NaN 坐标 AABB -> false");
    check(!segment_hits_circle(&nan_seg, 0.0f, 0.0f, 5.0f), "NaN 坐标圆 -> false");
    check(!segment_hits_aabb(NULL, 0.0f, 0.0f, 10.0f, 10.0f), "AABB 空线段指针 -> false");
    check(!segment_hits_circle(NULL, 0.0f, 0.0f, 5.0f), "圆 空线段指针 -> false");

    /* 负半径和视为 0 */
    const Segment p0 = seg(0.0f, 0.0f, 0.0f, 0.0f);
    const Segment p0b = seg(0.0f, 0.0f, 0.0f, 0.0f);
    t = -1.0f;
    d = -1.0f;
    check(sweep_hit(&p0, &p0b, -5.0f, &t, &d), "负 radius_sum 视为 0: 重合 -> 命中");
    check(t == 0.0f && near_f(d, 0.0f, 0.0f), "负 radius_sum 命中输出 t == 0, d == 0");
    const Segment p3 = seg(3.0f, 0.0f, 3.0f, 0.0f);
    t = -1.0f;
    d = -1.0f;
    check(!sweep_hit(&p0, &p3, -5.0f, &t, &d), "负 radius_sum 视为 0: 距离 3 -> 不命中");

    /* 半径为 0 的判定 */
    t = -1.0f;
    d = -1.0f;
    check(sweep_hit(&a, &b, 0.0f, &t, &d) && near_f(d, 0.0f, 1e-5f),
          "radius_sum == 0: 轨迹真正相交才算命中");

    /* 圆负半径视为 0 */
    Segment s = seg(-5.0f, 0.0f, 5.0f, 0.0f);
    check(segment_hits_circle(&s, 0.0f, 0.0f, -3.0f), "圆半径 -3 视为 0: 线段穿过圆心 -> 命中");
    s = seg(1.0f, 0.0f, 5.0f, 0.0f);
    check(!segment_hits_circle(&s, 0.0f, 0.0f, -3.0f), "圆半径 -3 视为 0: 不经过圆心 -> 不命中");

    /* 极端有限值: float 中间量会平方溢出成 Inf, 实现内部用 double 必须仍然安全 */
    const Segment big_a = seg(-FLT_MAX, 0.0f, FLT_MAX, 0.0f);
    const Segment big_b = seg(0.0f, 0.0f, 0.0f, 0.0f);
    t = -1.0f;
    d = -1.0f;
    const bool big_hit = sweep_hit(&big_a, &big_b, 1.0f, &t, &d);
    check(big_hit, "FLT_MAX 尺度的高速扫掠仍判定命中(内部 double 不溢出)");
    check(finite_f(t) && finite_f(d), "FLT_MAX 尺度输出无 NaN/Inf(t 仍有限)");
    check(t > 0.0f && t < 1.0f, "FLT_MAX 尺度 t_min 仍落在 (0,1)");
    t = -1.0f;
    d = -1.0f;
    check(sweep_hit(&big_a, &big_b, -1.0f, &t, &d), "FLT_MAX 尺度 + 负半径和 -> 视为 0 仍命中");
    check(finite_f(t) && finite_f(d), "FLT_MAX 尺度 + 负半径和输出有限");
    t = -1.0f;
    const float big_min = sweep_min_distance(&big_a, &big_b, &t);
    check(finite_f(big_min) && finite_f(t), "FLT_MAX 尺度 sweep_min_distance 输出有限");
    check(big_min == 0.0f, "FLT_MAX 尺度正对穿过最小距离为 0");
    check(segment_hits_aabb(&big_a, -1.0f, -1.0f, 1.0f, 1.0f), "FLT_MAX 尺度线段穿过小矩形 -> 命中");
    check(segment_hits_circle(&big_a, 0.0f, 0.0f, 1.0f), "FLT_MAX 尺度线段穿过小圆 -> 命中");

    /* 所有输出参数都必须有限 */
    check(finite_f(t) && finite_f(d), "防御分支全部输出有限(无 NaN/Inf)");
}

/* ---------------------------------------------------------------- main */

int main(void) {
    printf("test_collision: S03 扫掠碰撞验收测试 (接口版本 1)\n");

    test_static_pair();
    test_tangent();
    test_miss_parallel();
    test_both_moving();
    test_high_speed();
    test_zero_relative();
    test_earliest();
    test_aabb();
    test_circle();
    test_defensive();

    printf("\n---- 汇总 ----\n");
    printf("PASS: %d\n", g_pass);
    printf("FAIL: %d\n", g_fail);
    printf("RESULT: %s\n", (g_fail == 0) ? "ALL PASS" : "FAILURES PRESENT");
    return (g_fail == 0) ? 0 : 1;
}
