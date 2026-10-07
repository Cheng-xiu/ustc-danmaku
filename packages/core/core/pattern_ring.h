/* pattern_ring.h - 招 0 桃李苑·绿色圆圈好辣: 带缺口分时环弹
 * 用途档位: 中消耗·周边压力
 * 接口版本: 1
 */
#ifndef DEMO_PATTERN_RING_H
#define DEMO_PATTERN_RING_H

#include "demo_base.h"

#ifdef __cplusplus
extern "C" {
#endif

bool pattern_ring_make_plan(const PatternRequest *request, const DemoConfig *config, Rng *rng,
                            AttackPlan *out);
bool pattern_ring_emit(const AttackPlan *plan, const DemoConfig *config, uint32_t attack_tick,
                       ProjectileSpawnBuffer *out);
void pattern_ring_warning(const AttackPlan *plan, const DemoConfig *config, PatternWarning *out);

#ifdef __cplusplus
}
#endif

#endif /* DEMO_PATTERN_RING_H */
