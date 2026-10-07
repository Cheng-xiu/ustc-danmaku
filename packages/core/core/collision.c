/* collision.c - 纯 C 相对运动扫掠碰撞与必要的最早交点计算
 *
 * 接口版本: 1 (签名来自 core/demo_base.h, 本文件只实现, 不修改头文件)
 * 依据: docs/demo-rules.md 1.3「普通弹不穿透, 多目标按最早扫掠交点命中」、
 *       技术约束「高速弹必须做相对运动线段扫掠, 不能只查端点」。
 * 纯度: 本文件不含 EasyX/Windows/墙钟/随机/日志/全局可变状态, 只做数学判定;
 *       不扣血、不移除弹、不决定胜负。
 *
 * ---------------------------------------------------------------- 扫掠语义
 * A、B 两条线段都被看作"本 tick 内实体的移动轨迹":
 *     A(t) = A.a + t * (A.b - A.a)
 *     B(t) = B.a + t * (B.b - B.a),  t ∈ [0, 1]
 * 相对位置:
 *     P(t) = A(t) - B(t) = P0 + t * D
 *     P0 = A.a - B.a
 *     D  = (A.b - A.a) - (B.b - B.a)      (A 相对 B 的位移)
 * 于是 A 与 B 的最小距离 = 相对线段 { P0 + t*D : t ∈ [0,1] } 到原点的距离,
 * 等价于把 B 视作静止、求以 A.a 为起点、D 为方向的线段到点 B.a 的距离
 * (两者只差一个常量平移 B.a)。双方同时移动由 D 的差自动包含, 调用方不需要
 * 预先扣掉对方的位移。
 *
 * 命中判据: [0,1] 上的最小距离 <= radius_sum。
 *
 * ------------------------------------------------------- 输出与边界约定
 * t_min:
 *   - 命中时: [0,1] 内最早的命中参数(进入点), 起点已接触时为 0;
 *   - 未命中时: [0,1] 内取到最小距离的参数(此时它不是"命中参数", 调用方
 *     必须先看返回值)。
 * min_distance: t ∈ [0,1] 上的最小距离(全局最小)。命中时它可能小于 radius_sum
 *   (例如正对穿过时 min_distance == 0); 相切时恰等于 radius_sum。
 *   说明: 任务卡的措辞是"min_distance 为该参数处距离", 与头文件"最小距离"在
 *   深穿透情形下读法不同(相切、静止、错过等验收用例下两者相同)。本实现取头文件
 *   的"最小距离", 因为命中判据本身就是"最小距离 <= radius_sum", 且该值比
 *   "进入点处距离(= radius_sum)" 多携带穿透深度信息。此读法已登记在
 *   work/agents/S03/S03-report.md, 若母代理改判, 只需把 sweep_relative 中
 *   info.min_distance 一处改成"t_hit 处距离"。
 *
 * 零相对运动 (D == 0): 不除零, 直接用静止点距离判定, 命中时 t_min == 0。
 *
 * 非法输入 (保证返回值/输出不是 NaN/Inf):
 *   - a、b 任一为 NULL, 或线段坐标/半径和不是有限数: 不命中;
 *     sweep_hit 返回 false 且写出 *t_min = 0、*min_distance = 0;
 *     sweep_min_distance 返回 0.0f 且写出 *t_min = 0。
 *     (任务卡允许"返回 0 或 +INF", 这里统一取 0 以满足"返回值不得为 Inf"。)
 *   - radius_sum < 0 视为 0; radius_sum == 0 表示只判"恰好接触/重合"。
 * size_t/float 精度策略: 中间计算全部用 double (float 坐标平方在大数时会溢出
 *   成 Inf 并让 t 变成 NaN; float 全范围在 double 下不会溢出), 判定与输出在
 *   float 精度上收敛, 因此低于 float 分辨率(相对 1e-7 量级)的差值不参与判定。
 */

#include "demo_base.h"

#include <float.h>
#include <math.h>

/* ---------------------------------------------------------------- 内部工具 */

/* 线段四个坐标是否都是有限数 (NULL 视为不可用)。 */
static bool segment_usable(const Segment *s) {
    return s != NULL && isfinite((double)s->ax) && isfinite((double)s->ay) &&
           isfinite((double)s->bx) && isfinite((double)s->by);
}

/* double -> float 出口: 永不产生 NaN/Inf (超出 float 范围时饱和)。 */
static float to_float_finite(double v) {
    if (!isfinite(v)) {
        return 0.0f;
    }
    if (v > (double)FLT_MAX) {
        return FLT_MAX;
    }
    if (v < -(double)FLT_MAX) {
        return -FLT_MAX;
    }
    return (float)v;
}

/* 距离比较在 float 精度上进行: 调用方给的半径和就是 float, 低于 float 分辨率
 * 的差值属于输入本身无法表达的噪声; 中间量仍是 double, 避免溢出与抵消误差。 */
static bool distance_within(double d, double r) {
    return to_float_finite(d) <= to_float_finite(r);
}

static double clamp01(double t) {
    if (t < 0.0) {
        return 0.0;
    }
    if (t > 1.0) {
        return 1.0;
    }
    return t;
}

/* 相对扫掠的中间结果。 */
typedef struct SweepInfo {
    double min_distance; /* [0,1] 上的最小距离 */
    double t_nearest;    /* 取到最小距离的参数(未命中时即 t_min) */
    double t_hit;        /* 最早命中参数(未命中时等于 t_nearest) */
    bool hit;            /* 最小距离 <= radius_sum */
} SweepInfo;

/* 核心: 相对线段 { P0 + t*D } 到原点的距离。所有分支都不会产生 NaN/Inf。 */
static SweepInfo sweep_relative(const Segment *a, const Segment *b, double radius_sum) {
    SweepInfo info;
    info.min_distance = 0.0;
    info.t_nearest = 0.0;
    info.t_hit = 0.0;
    info.hit = false;

    if (!segment_usable(a) || !segment_usable(b) || !isfinite(radius_sum)) {
        return info; /* 非法输入: 不命中, 输出 0 (见文件头约定) */
    }
    if (radius_sum < 0.0) {
        radius_sum = 0.0; /* 负半径和视为 0 */
    }

    const double p0x = (double)a->ax - (double)b->ax;
    const double p0y = (double)a->ay - (double)b->ay;
    const double dx = ((double)a->bx - (double)a->ax) - ((double)b->bx - (double)b->ax);
    const double dy = ((double)a->by - (double)a->ay) - ((double)b->by - (double)b->ay);
    const double len2 = dx * dx + dy * dy;
    const double p0_len2 = p0x * p0x + p0y * p0y;

    if (len2 <= 0.0) {
        /* 零相对运动: 不除零; 直接以静止点距离判定, 命中参数固定为 0。 */
        const double d = sqrt(p0_len2);
        info.min_distance = d;
        info.t_nearest = 0.0;
        info.t_hit = 0.0;
        info.hit = distance_within(d, radius_sum);
        return info;
    }

    /* t* = argmin |P0 + t D|, 再夹到 [0,1]: 最近点在段内还是端点上。 */
    const double p0_dot_d = p0x * dx + p0y * dy;
    const double t_star = clamp01(-p0_dot_d / len2);
    const double qx = p0x + t_star * dx;
    const double qy = p0y + t_star * dy;
    const double d_star = sqrt(qx * qx + qy * qy);
    info.min_distance = d_star;
    info.t_nearest = t_star;
    info.t_hit = t_star;

    if (!distance_within(d_star, radius_sum)) {
        return info; /* 全程最小距离都大于半径和: 一定不命中 */
    }
    info.hit = true;

    const double d0 = sqrt(p0_len2);
    if (distance_within(d0, radius_sum)) {
        info.t_hit = 0.0; /* 起点已在接触范围内: 最早命中参数是 0 */
        return info;
    }

    /* 解 |P0 + t D|^2 = r^2 的较小根(进入点)作为最早命中参数。 */
    const double c = p0_len2 - radius_sum * radius_sum;
    double disc = p0_dot_d * p0_dot_d - len2 * c;
    if (disc < 0.0) {
        disc = 0.0; /* 浮点噪声: 已判定命中, 退化为切点 */
    }
    info.t_hit = clamp01((-p0_dot_d - sqrt(disc)) / len2);
    return info;
}

/* ---------------------------------------------------------------- 公共接口 */

bool sweep_hit(const Segment *a, const Segment *b, float radius_sum, float *t_min,
               float *min_distance) {
    const SweepInfo info = sweep_relative(a, b, (double)radius_sum);
    if (t_min != NULL) {
        *t_min = to_float_finite(info.t_hit);
    }
    if (min_distance != NULL) {
        *min_distance = to_float_finite(info.min_distance);
    }
    return info.hit;
}

float sweep_min_distance(const Segment *a, const Segment *b, float *t_min) {
    /* 只求距离, 不做命中判定: 半径和取 0 不影响最小距离与最近参数。
     * NULL/非有限输入时返回 0.0f 并写出 *t_min = 0 (见文件头约定)。 */
    const SweepInfo info = sweep_relative(a, b, 0.0);
    if (t_min != NULL) {
        *t_min = to_float_finite(info.t_nearest);
    }
    return to_float_finite(info.min_distance);
}

bool segment_hits_aabb(const Segment *s, float min_x, float min_y, float max_x, float max_y) {
    if (!segment_usable(s) || !isfinite((double)min_x) || !isfinite((double)min_y) ||
        !isfinite((double)max_x) || !isfinite((double)max_y)) {
        return false;
    }

    double lo_x = (double)min_x;
    double hi_x = (double)max_x;
    double lo_y = (double)min_y;
    double hi_y = (double)max_y;
    if (lo_x > hi_x) { /* 容忍上下界写反: 闭区间语义只取决于两点 */
        const double tmp = lo_x;
        lo_x = hi_x;
        hi_x = tmp;
    }
    if (lo_y > hi_y) {
        const double tmp = lo_y;
        lo_y = hi_y;
        hi_y = tmp;
    }

    const double x0 = (double)s->ax;
    const double y0 = (double)s->ay;
    const double dx = (double)s->bx - (double)s->ax;
    const double dy = (double)s->by - (double)s->ay;

    /* 闭区间的 slab 裁剪: 接触边界即算命中; 零长度线段退化为"点在框内"。
     * 这里不用 eps, 以保证"贴着边界"与"刚好在外"可区分。 */
    double t_enter = 0.0;
    double t_exit = 1.0;

    if (dx == 0.0) {
        if (x0 < lo_x || x0 > hi_x) {
            return false;
        }
    } else {
        double t1 = (lo_x - x0) / dx;
        double t2 = (hi_x - x0) / dx;
        if (t1 > t2) {
            const double tmp = t1;
            t1 = t2;
            t2 = tmp;
        }
        if (t1 > t_enter) {
            t_enter = t1;
        }
        if (t2 < t_exit) {
            t_exit = t2;
        }
        if (t_enter > t_exit) {
            return false;
        }
    }

    if (dy == 0.0) {
        if (y0 < lo_y || y0 > hi_y) {
            return false;
        }
    } else {
        double t1 = (lo_y - y0) / dy;
        double t2 = (hi_y - y0) / dy;
        if (t1 > t2) {
            const double tmp = t1;
            t1 = t2;
            t2 = tmp;
        }
        if (t1 > t_enter) {
            t_enter = t1;
        }
        if (t2 < t_exit) {
            t_exit = t2;
        }
        if (t_enter > t_exit) {
            return false;
        }
    }

    return t_enter <= t_exit;
}

bool segment_hits_circle(const Segment *s, float cx, float cy, float radius) {
    if (!segment_usable(s) || !isfinite((double)cx) || !isfinite((double)cy) ||
        !isfinite((double)radius)) {
        return false;
    }

    double r = (double)radius;
    if (r < 0.0) {
        r = 0.0; /* 负半径视为 0: 退化为"圆心是否落在线段上" */
    }

    const double x0 = (double)s->ax;
    const double y0 = (double)s->ay;
    const double dx = (double)s->bx - (double)s->ax;
    const double dy = (double)s->by - (double)s->ay;
    const double len2 = dx * dx + dy * dy;

    /* 圆心在直线上的投影参数, 夹到 [0,1] 即线段上的最近点; 零长度线段直接取端点。 */
    double t = 0.0;
    if (len2 > 0.0) {
        t = clamp01((((double)cx - x0) * dx + ((double)cy - y0) * dy) / len2);
    }
    const double px = x0 + t * dx - (double)cx;
    const double py = y0 + t * dy - (double)cy;
    const double d = sqrt(px * px + py * py);

    return distance_within(d, r); /* 闭圆: 距离 <= 半径 */
}
