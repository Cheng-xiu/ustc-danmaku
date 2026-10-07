/* pattern_ring.c - 招 0 桃李苑·绿色圆圈好辣: 带缺口分时环弹
 *
 * 规则: 以"接受请求时的 Boss 位置"为圆心, 分 wave_count 波放出环形弹幕;
 * 每波在 gap_angle_deg 附近留出 gap_span_deg 宽的缺口, 缺口中心每波
 * 旋转 gap_drift_deg_per_wave(缓慢旋转、不突变封死), 供学生穿越。
 *
 * 纯 C11, 只依赖 demo_base.h 与标准数学库。
 */
#include "pattern_ring.h"

#include <math.h>
#include <string.h>

/* 每波缺口中心相对上一波的固定旋转量(度)。
 * 缓慢旋转而非跳变, 保证已经找到缺口的玩家不会被瞬间封死。 */
#define RING_GAP_DRIFT_DEG_PER_WAVE 20.0f

#define RING_DEG_TO_RAD 0.01745329251994329577f /* pi / 180 */
#define RING_FULL_TURN_DEG 360.0f

/* 把角度归一到 [0, 360) */
static float ring_norm_deg(float deg)
{
    float r = fmodf(deg, RING_FULL_TURN_DEG);
    if (r < 0.0f) {
        r += RING_FULL_TURN_DEG;
    }
    return r;
}

/* 角度是否落在以 center 为中心、跨度 span(度, 关于 360 取模)的扇区内 */
static bool ring_in_gap(float deg, float center, float span)
{
    float half = span * 0.5f;
    float delta;

    if (half <= 0.0f) {
        return false;
    }
    if (half >= RING_FULL_TURN_DEG * 0.5f) {
        return true; /* 缺口覆盖整圈: 该波无弹 */
    }

    delta = ring_norm_deg(deg - center);
    return delta <= half || delta >= (RING_FULL_TURN_DEG - half);
}

bool pattern_ring_make_plan(const PatternRequest *request, const DemoConfig *config, Rng *rng,
                           AttackPlan *out)
{
    const PatternConfig *pc;
    float dx;
    float dy;
    float len;
    uint32_t i;

    if (request == NULL || config == NULL || rng == NULL || out == NULL) {
        return false;
    }

    /* 不接受半成品计划: 先校验, 再写入。 */
    pc = &config->patterns[DEMO_PATTERN_RING];
    if (pc->wave_count <= 0 || pc->shots_per_wave <= 0 || pc->bullet_speed <= 0.0f) {
        return false;
    }
    if (pc->wave_count > (int32_t)(DEMO_PATTERN_COUNT * 8)) {
        return false;
    }

    /* 出生安全检查: 原点与学生过近(无法生成合法环弹)则拒绝本次请求。 */
    for (i = 0u; i < request->student_count && i < DEMO_MAX_STUDENTS; ++i) {
        float sx;
        float sy;
        float ddx;
        float ddy;

        if (!request->student_alive[i]) {
            continue;
        }
        sx = request->student_x[i];
        sy = request->student_y[i];
        ddx = sx - request->origin_x;
        ddy = sy - request->origin_y;
        if (sqrtf(ddx * ddx + ddy * ddy) < request->student_radius) {
            return false;
        }
    }

    memset(out, 0, sizeof(*out));

    dx = request->target_x - request->origin_x;
    dy = request->target_y - request->origin_y;
    len = sqrtf(dx * dx + dy * dy);
    if (len > 1e-6f) {
        out->aim_dir_x = dx / len;
        out->aim_dir_y = dy / len;
    } else {
        /* 退化输入: 取朝上方向, 保证方向是单位向量 */
        out->aim_dir_x = 0.0f;
        out->aim_dir_y = -1.0f;
    }

    out->pattern = DEMO_PATTERN_RING;
    out->target_id = request->target_id;
    /* 锁定接受请求时的 Boss 位置: 之后 Boss 移动不再改变本次环弹圆心 */
    out->origin_x = request->origin_x;
    out->origin_y = request->origin_y;
    out->aim_x = request->target_x;
    out->aim_y = request->target_y;
    out->lock_speed = pc->bullet_speed;
    out->start_tick = request->start_tick;
    out->windup_ticks = pc->windup_ticks;
    out->active_ticks = pc->active_ticks;
    out->wave_count = pc->wave_count;
    out->shots_per_wave = pc->shots_per_wave;
    out->gap_span_deg = pc->gap_span_deg;
    out->corridor_width = pc->corridor_width;

    for (i = 0u; i < (uint32_t)pc->wave_count; ++i) {
        out->wave_tick[i] = pc->first_spawn_sec * (float)DEMO_TICKS_PER_SECOND +
                            (float)i * pc->wave_interval_sec * (float)DEMO_TICKS_PER_SECOND;
    }

    /* 本函数唯一一次角度随机: 仅决定初始缺口朝向, 几何从此完全确定。 */
    out->gap_angle_deg = rng_unit_f32(rng) * RING_FULL_TURN_DEG;
    out->gap_drift_deg_per_wave = RING_GAP_DRIFT_DEG_PER_WAVE;
    out->geometry_seed = rng_next_u64(rng);

    out->active = true;
    return true;
}

bool pattern_ring_emit(const AttackPlan *plan, const DemoConfig *config, uint32_t attack_tick,
                       ProjectileSpawnBuffer *out)
{
    int32_t wave = -1;
    int32_t i;
    float step_deg;
    float gap_center;

    if (plan == NULL || config == NULL || out == NULL) {
        return false;
    }
    if (!plan->active || plan->pattern != DEMO_PATTERN_RING) {
        return false;
    }
    if (plan->shots_per_wave <= 0 || plan->wave_count <= 0) {
        return false;
    }

    /* 本 tick 是否正好是某一波的生成 tick; 不是则完全不写 out。 */
    for (i = 0; i < plan->wave_count && i < (int32_t)(DEMO_PATTERN_COUNT * 8); ++i) {
        /* attack_tick 与 wave_tick 都以预警结束、攻击开始为零点。 */
        long long tick = (long long)lroundf(plan->wave_tick[i]);

        if (tick == (long long)attack_tick) {
            wave = i;
            break;
        }
    }
    if (wave < 0) {
        return false;
    }

    step_deg = RING_FULL_TURN_DEG / (float)plan->shots_per_wave;
    gap_center = plan->gap_angle_deg + (float)wave * plan->gap_drift_deg_per_wave;

    for (i = 0; i < plan->shots_per_wave; ++i) {
        Projectile spec;
        float deg;
        float rad;
        float cs;
        float sn;

        /* 每波整体偏移一个步长, 使相邻波弹道互相错开(与规格一致)。 */
        deg = (float)wave * step_deg + (float)i * step_deg;
        if (ring_in_gap(deg, gap_center, plan->gap_span_deg)) {
            continue; /* 缺口扇区: 不发弹 */
        }

        rad = deg * RING_DEG_TO_RAD;
        cs = cosf(rad);
        sn = sinf(rad);

        memset(&spec, 0, sizeof(spec));
        spec.faction = DEMO_FACTION_BOSS;
        spec.source_id = (DemoEntityId)1;
        spec.plan_id = plan->plan_id;
        spec.source_pattern = DEMO_PATTERN_RING;
        spec.px = plan->origin_x;
        spec.py = plan->origin_y;
        spec.x = plan->origin_x;
        spec.y = plan->origin_y;
        spec.vx = plan->lock_speed * cs;
        spec.vy = plan->lock_speed * sn;
        spec.radius = config->boss_bullet_radius;
        spec.damage = config->boss_bullet_damage;
        spec.lifetime_ticks = config->boss_bullet_lifetime_ticks;

        (void)spawn_buffer_push(out, &spec);
    }

    return true;
}

void pattern_ring_warning(const AttackPlan *plan, const DemoConfig *config, PatternWarning *out)
{
    (void)config;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));

    if (plan == NULL || !plan->active || plan->pattern != DEMO_PATTERN_RING) {
        return;
    }

    out->valid = true;
    out->pattern = DEMO_PATTERN_RING;
    out->target_id = plan->target_id;
    out->origin_x = plan->origin_x;
    out->origin_y = plan->origin_y;
    out->aim_x = plan->aim_x;
    out->aim_y = plan->aim_y;
    out->gap_angle_deg = plan->gap_angle_deg;
    out->gap_span_deg = plan->gap_span_deg;
    out->corridor_width = plan->corridor_width;
    out->wave_count = plan->wave_count;
    out->radius_hint = 0.0f;
}
