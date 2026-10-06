/* patterns.c - 招式注册表（母代理独占维护）
 * 接口版本: 1
 */
#include "patterns.h"

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

void pattern_warning(const AttackPlan *plan, const DemoConfig *config, PatternWarning *out) {
    if (out == NULL) {
        return;
    }
    if (plan == NULL || config == NULL || !plan->active) {
        /* 无有效计划时不暴露任何几何: 调用方必须检查 valid */
        out->valid = false;
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
