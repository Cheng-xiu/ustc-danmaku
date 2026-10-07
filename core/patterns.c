/* patterns.c - 招式注册表（母代理独占维护）
 * 接口版本: 1
 */
#include "patterns.h"
#include "pattern_aim.h"

#include <string.h>

static const PatternVTable kPatterns[DEMO_PATTERN_COUNT] = {
    {"桃李苑·绿色圆圈好辣", pattern_ring_make_plan, pattern_ring_emit, pattern_ring_warning},
    {"选课系统·课表华容道", pattern_course_make_plan, pattern_course_emit, pattern_course_warning},
    {"一教金矿·绩点淘金", pattern_mine_make_plan, pattern_mine_emit, pattern_mine_warning},
    {"期末总评·绩点淋浴", pattern_shower_make_plan, pattern_shower_emit, pattern_shower_warning},
};

const PatternVTable *pattern_vtable(DemoPattern pattern) {
    if ((int)pattern < 0 || (int)pattern >= (int)DEMO_PATTERN_COUNT) {
        return NULL;
    }
    return &kPatterns[(int)pattern];
}

const char *pattern_name(DemoPattern pattern) {
    const PatternVTable *vt = pattern_vtable(pattern);
    return (vt != NULL) ? vt->name : "unknown";
}

bool pattern_emit(const AttackPlan *plan, const DemoConfig *config, uint32_t attack_tick,
                  Rng *rng, ProjectileSpawnBuffer *out) {
    (void)rng;
    if (plan == NULL || config == NULL || out == NULL) return false;
    if (plan->manual_aim) return pattern_aim_emit(plan, config, attack_tick, out);
    const PatternVTable *vt = pattern_vtable(plan->pattern);
    return vt != NULL && vt->emit != NULL && vt->emit(plan, config, attack_tick, out);
}

void pattern_warning(const AttackPlan *plan, const DemoConfig *config, PatternWarning *out) {
    if (out == NULL) {
        return;
    }
    if (plan == NULL || config == NULL || !plan->active) {
        /* 无有效计划时不暴露任何几何: 调用方必须检查 valid */
        out->valid = false;
        return;
    }
    if (plan->manual_aim) {
        memset(out, 0, sizeof(*out));
        if (pattern_vtable(plan->pattern) == NULL) return;
        out->valid = true; out->pattern = plan->pattern; out->target_id = 0u;
        out->origin_x = plan->origin_x; out->origin_y = plan->origin_y;
        out->aim_x = plan->aim_x; out->aim_y = plan->aim_y;
        out->gap_angle_deg = plan->gap_angle_deg; out->gap_span_deg = plan->gap_span_deg;
        out->corridor_width = plan->corridor_width; out->wave_count = plan->wave_count;
        out->radius_hint = plan->lock_speed * plan->active_ticks / DEMO_TICKS_PER_SECOND;
        return;
    }
    const PatternVTable *vt = pattern_vtable(plan->pattern);
    if (vt == NULL || vt->warning == NULL) {
        out->valid = false;
        return;
    }
    vt->warning(plan, config, out);
    out->valid = true;
    out->pattern = plan->pattern;
    out->target_id = plan->target_id;
}
