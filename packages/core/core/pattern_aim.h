#ifndef DEMO_PATTERN_AIM_H
#define DEMO_PATTERN_AIM_H
#include "demo_base.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Pure deterministic factory: no world mutation or random stream access.
 * Failed requests leave out untouched. Accepted plans freeze all emit geometry. */
bool pattern_aim_make_plan(const PatternRequest *request, const DemoConfig *config,
                           AttackPlan *out);
bool pattern_aim_emit(const AttackPlan *plan, const DemoConfig *config,
                     uint32_t attack_tick, ProjectileSpawnBuffer *out);
#ifdef __cplusplus
}
#endif
#endif
