/* pattern_mine.c - 招 2 一教金矿·绩点淘金（低消耗·追击/局部打击）
 * 接口版本 2、配置版本 1
 * 几何: 矿点定在目标位置沿"Boss→目标"方向偏移 spawn_safety_radius 处, 夹紧到场地内;
 *       三个扇面从矿点向外喷射, 扇面之间有间隙 (gap_span_deg)。
 * 计划编码: origin = 锁定矿点; aim = 目标位置; gap_angle_deg = 中心扇面方向(度);
 *           corridor_width = 扇面间隙(度); wave_offset = 扇面数。
 */
#include "pattern_mine.h"

#include <math.h>
#include <string.h>

#define MINE_FANS 3
#define MINE_PI 3.14159265358979323846f

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

bool pattern_mine_make_plan(const PatternRequest *request, const DemoConfig *config, Rng *rng,
                            AttackPlan *out) {
    if (request == NULL || config == NULL || rng == NULL || out == NULL) {
        return false;
    }
    const PatternConfig *pc = &config->patterns[DEMO_PATTERN_MINE];
    if (pc->wave_count <= 0 || pc->shots_per_wave <= 0 || pc->bullet_speed <= 0.0f) {
        return false;
    }

    /* 矿点 = 目标位置沿 (目标 - Boss) 方向外推 spawn_safety_radius, 即位于目标与 Boss 连线上、
     * 距目标 spawn_safety_radius, 从而与目标保持安全距离。 */
    float dx = request->target_x - request->origin_x;
    float dy = request->target_y - request->origin_y;
    float len = sqrtf(dx * dx + dy * dy);
    float ux;
    float uy;
    if (len > 1e-4f) {
        ux = dx / len;
        uy = dy / len;
    } else {
        ux = 0.0f;
        uy = -1.0f;
    }
    float mine_x = request->target_x + ux * pc->spawn_safety_radius;
    float mine_y = request->target_y + uy * pc->spawn_safety_radius;
    mine_x = clampf(mine_x, 0.0f, config->field_w);
    mine_y = clampf(mine_y, 0.0f, config->field_h);

    /* 出生安全检查: 全部存活学生与矿点距离 >= spawn_safety_radius(目标本身可等于) */
    for (uint32_t i = 0u; i < request->student_count && i < DEMO_MAX_STUDENTS; ++i) {
        if (!request->student_alive[i]) {
            continue;
        }
        float sx = request->student_x[i] - mine_x;
        float sy = request->student_y[i] - mine_y;
        float d = sqrtf(sx * sx + sy * sy);
        if (d < pc->spawn_safety_radius - 1e-3f && d > 1e-3f) {
            /* 夹紧可能让目标也不足安全距离；拒绝，不移动或重抽矿点。 */
            return false;
        }
        if (d <= 1e-3f) {
            return false; /* 学生与矿点重叠 */
        }
    }

    memset(out, 0, sizeof(*out));
    out->active = true;
    out->pattern = DEMO_PATTERN_MINE;
    out->target_id = request->target_id;
    out->origin_x = mine_x;
    out->origin_y = mine_y;
    out->aim_x = request->target_x;
    out->aim_y = request->target_y;
    out->aim_dir_x = ux;
    out->aim_dir_y = uy;
    out->lock_speed = pc->bullet_speed;
    out->start_tick = request->start_tick;
    out->windup_ticks = pc->windup_ticks;
    out->active_ticks = pc->active_ticks;
    out->wave_count = pc->wave_count;
    out->shots_per_wave = pc->shots_per_wave;
    out->gap_span_deg = pc->gap_span_deg;
    out->corridor_width = pc->corridor_width;
    out->lock_checked_at_spawn = true;
    out->wave_tick[0] = pc->first_spawn_sec * (float)DEMO_TICKS_PER_SECOND;

    /* 中心扇面朝目标(即 -u 方向, 从矿点指向目标); 间隙为 gap_span_deg */
    out->gap_angle_deg = atan2f(request->target_y - mine_y, request->target_x - mine_x) *
                         (180.0f / MINE_PI);
    out->wave_offset = (float)MINE_FANS;
    (void)rng;
    out->geometry_seed = (uint64_t)request->target_id;
    return true;
}

bool pattern_mine_emit(const AttackPlan *plan, const DemoConfig *config, uint32_t attack_tick,
                       ProjectileSpawnBuffer *out) {
    if (plan == NULL || config == NULL || out == NULL || !plan->active) {
        return false;
    }
    /* 第三个参数是攻击阶段相对 tick，不加接受请求的绝对 tick。 */
    int32_t due = (int32_t)lroundf(plan->wave_tick[0]);
    if ((int32_t)attack_tick != due) {
        return false;
    }

    const float center_deg = plan->gap_angle_deg;
    const float gap_deg = (plan->gap_span_deg > 0.0f) ? plan->gap_span_deg : 40.0f;
    /* 三个扇面: 中心偏移 -gap, 0, +gap 度; 每面覆盖 span_deg 度 */
    const float span_deg = 34.0f;
    const int shots = plan->shots_per_wave;

    for (int fan = 0; fan < MINE_FANS; ++fan) {
        float fan_center = center_deg + (float)(fan - 1) * gap_deg;
        for (int k = 0; k < shots; ++k) {
            float frac = (shots > 1) ? (((float)k / (float)(shots - 1)) - 0.5f) : 0.0f;
            float ang_deg = fan_center + frac * span_deg;
            float rad = ang_deg * (MINE_PI / 180.0f);
            Projectile spec;
            memset(&spec, 0, sizeof(spec));
            spec.active = true;
            spec.faction = DEMO_FACTION_BOSS;
            spec.source_id = 1u;
            spec.plan_id = plan->plan_id;
            spec.source_pattern = DEMO_PATTERN_MINE;
            spec.x = plan->origin_x;
            spec.y = plan->origin_y;
            spec.px = spec.x;
            spec.py = spec.y;
            spec.vx = cosf(rad) * plan->lock_speed;
            spec.vy = sinf(rad) * plan->lock_speed;
            spec.radius = config->boss_bullet_radius;
            spec.damage = config->boss_bullet_damage;
            spec.lifetime_ticks = config->boss_bullet_lifetime_ticks;
            if (!spawn_buffer_push(out, &spec)) {
                return true;
            }
        }
    }
    return true;
}

void pattern_mine_warning(const AttackPlan *plan, const DemoConfig *config, PatternWarning *out) {
    if (plan == NULL || out == NULL) {
        return;
    }
    (void)config;
    memset(out, 0, sizeof(*out));
    out->valid = plan->active;
    out->pattern = DEMO_PATTERN_MINE;
    out->target_id = plan->target_id;
    out->origin_x = plan->origin_x;
    out->origin_y = plan->origin_y;
    out->aim_x = plan->aim_x;
    out->aim_y = plan->aim_y;
    out->radius_hint = plan->gap_span_deg;
    out->gap_angle_deg = plan->gap_angle_deg;
    out->gap_span_deg = plan->gap_span_deg;
    out->corridor_width = plan->corridor_width;
    out->wave_count = plan->wave_count;
}
