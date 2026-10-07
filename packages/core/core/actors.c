/* actors.c - 角色移动、受击、无敌与倒下状态
 *
 * 接口版本: 1 (core/demo_base.h)
 * 纯 C 实现: 不读时间/键盘/鼠标/EasyX, 不写日志, 不使用全局可变状态。
 * 本文件只负责单个 Actor 的位置与生命状态; 终局裁决由 world 集成层决定。
 *
 * 移动语义("模拟摇杆"):
 *   - dir 是意图向量, 不是位移。
 *   - 长度 > 1 时归一化到 1 (斜向与直向同速)。
 *   - 长度 <= 1 时保留原幅度 (幅度 0.5 表示半速)。
 *   - 长度 == 0 或非有限值, speed 无效时按"不移动"处理。
 * 边界语义:
 *   - 只做夹紧, 不留半径; 半径留边由调用方在 min/max 里体现。
 *   - 本函数不穿透边界。
 */
#include "demo_base.h"

#include <math.h>

/* ---------------------------------------------------------------- 内部工具 */

/* 有限性检查: NaN 与 ±Inf 都是非法输入。 */
static bool actor_float_finite(float value)
{
    return isfinite(value) != 0;
}

/* 向量长度。
 * 不能用 sqrtf(x*x + y*y): 中间量会溢出/下溢, 例如
 *   (1e30, 1e30) -> x*x 变 INF, 长度被误判为无穷;
 *   (1e-30, 0)   -> 下溢为 0, 非零方向被误判为零向量。
 * hypotf 内部做了缩放, 两种极端都能得到正确长度。
 * NaN/Inf 输入仍会得到 NaN/Inf, 由调用方的有限性检查拦下。 */
static float actor_vector_length(float x, float y)
{
    return hypotf(x, y);
}

/* 夹紧单轴坐标。
 * 边界本身非法(非有限或 lo > hi)时不夹紧, 只把值原样返回;
 * 调用方在写入前还会再确认一次结果有限, 避免把坏坐标写进 Actor。 */
static float actor_clamp_axis(float value, float lo, float hi)
{
    if (!actor_float_finite(lo) || !actor_float_finite(hi) || lo > hi) {
        return value;
    }
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

/* 统一收尾: 夹紧并写回。任一坐标无法得到有限值时整体放弃本次移动。 */
static void actor_commit_position(Actor *a, float new_x, float new_y, float min_x,
                                  float max_x, float min_y, float max_y)
{
    new_x = actor_clamp_axis(new_x, min_x, max_x);
    new_y = actor_clamp_axis(new_y, min_y, max_y);
    if (!actor_float_finite(new_x) || !actor_float_finite(new_y)) {
        return;
    }
    a->x = new_x;
    a->y = new_y;
}

/* ---------------------------------------------------------------- 移动 */

void actor_apply_move(Actor *a, float dir_x, float dir_y, float speed, float dt,
                      float min_x, float max_x, float min_y, float max_y)
{
    float len;
    float nx;
    float ny;
    float mag;
    float step;
    float dx;
    float dy;

    if (a == NULL) {
        return;
    }
    /* dt <= 0 或非有限: 不动 */
    if (!actor_float_finite(dt) || dt <= 0.0f) {
        return;
    }
    /* speed < 0 视为 0; 非有限按无效输入处理 */
    if (!actor_float_finite(speed) || speed <= 0.0f) {
        return;
    }
    /* 非有限输入方向: 不动, 也不产生 NaN 坐标 */
    if (!actor_float_finite(dir_x) || !actor_float_finite(dir_y)) {
        return;
    }
    /* 当前位置已被写坏时不参与运算 */
    if (!actor_float_finite(a->x) || !actor_float_finite(a->y)) {
        return;
    }

    len = actor_vector_length(dir_x, dir_y);
    if (!actor_float_finite(len) || len <= 0.0f) {
        return; /* 零向量: 松开停止 (非有限长度同样在此拦下) */
    }

    nx = dir_x / len;
    ny = dir_y / len;
    /* 长度 > 1 归一化到 1; 长度 <= 1 保留原幅度(慢速) */
    mag = (len > 1.0f) ? 1.0f : len;

    step = speed * dt;
    dx = nx * mag * step;
    dy = ny * mag * step;
    if (!actor_float_finite(dx) || !actor_float_finite(dy)) {
        return; /* 极端输入导致溢出: 整体放弃, 不写半成品坐标 */
    }

    actor_commit_position(a, a->x + dx, a->y + dy, min_x, max_x, min_y, max_y);
}

void actor_move_toward(Actor *a, float target_x, float target_y, float speed, float dt,
                       float deadzone, float min_x, float max_x, float min_y, float max_y)
{
    float dx;
    float dy;
    float dist;
    float dz;
    float step;
    float travel;
    float ux;
    float uy;
    float mx;
    float my;

    if (a == NULL) {
        return;
    }
    if (!actor_float_finite(dt) || dt <= 0.0f) {
        return;
    }
    if (!actor_float_finite(speed) || speed <= 0.0f) {
        return;
    }
    if (!actor_float_finite(target_x) || !actor_float_finite(target_y)) {
        return;
    }
    if (!actor_float_finite(a->x) || !actor_float_finite(a->y)) {
        return;
    }

    dx = target_x - a->x;
    dy = target_y - a->y;
    if (!actor_float_finite(dx) || !actor_float_finite(dy)) {
        return;
    }
    dist = actor_vector_length(dx, dy);
    if (!actor_float_finite(dist)) {
        return;
    }

    /* deadzone 防抖动: 非法或负值按 0 处理 */
    dz = (actor_float_finite(deadzone) && deadzone > 0.0f) ? deadzone : 0.0f;
    /* dist == 0 (目标恰等于当前坐标) 也走这里返回, 不除零 */
    if (dist <= dz) {
        return;
    }

    step = speed * dt;
    if (!actor_float_finite(step)) {
        return;
    }
    /* 一步走不完就走到目标, 不越过 */
    travel = (step < dist) ? step : dist;

    ux = dx / dist;
    uy = dy / dist;
    mx = ux * travel;
    my = uy * travel;
    if (!actor_float_finite(mx) || !actor_float_finite(my)) {
        return;
    }

    actor_commit_position(a, a->x + mx, a->y + my, min_x, max_x, min_y, max_y);
}

/* ---------------------------------------------------------------- 生命 */

bool actor_apply_damage(Actor *a, int32_t damage)
{
    int64_t remaining;

    if (a == NULL) {
        return false;
    }
    /* 已倒下: 不再变化, 也保证倒下只发生一次 */
    if (!a->alive) {
        return false;
    }
    if (damage <= 0) {
        return false;
    }
    /* 无敌期: 不扣血, 也不重置/消耗无敌计时 */
    if (a->invuln_ticks > 0) {
        return false;
    }

    /* 用 64 位中间量避免 int32 减法溢出 */
    remaining = (int64_t)a->hp - (int64_t)damage;
    if (remaining < 0) {
        remaining = 0;
    }
    a->hp = (int32_t)remaining;
    if (a->hp == 0) {
        a->alive = false;
    }
    return true;
}

void actor_tick_timers(Actor *a)
{
    if (a == NULL) {
        return;
    }
    if (a->invuln_ticks > 0) {
        a->invuln_ticks -= 1;
    }
}
