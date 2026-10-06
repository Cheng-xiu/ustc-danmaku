/* pattern_shower.h - 招 3 期末总评·绩点淋浴: 锁定方向的扫描弹雨
 * 用途档位: 高消耗·多目标压制
 * 接口版本: 1
 */
#ifndef DEMO_PATTERN_SHOWER_H
#define DEMO_PATTERN_SHOWER_H

#include "demo_base.h"

#ifdef __cplusplus
extern "C" {
#endif

bool pattern_shower_make_plan(const PatternRequest *request, const DemoConfig *config, Rng *rng,
                              AttackPlan *out);
bool pattern_shower_emit(const AttackPlan *plan, const DemoConfig *config, uint32_t attack_tick,
                         ProjectileSpawnBuffer *out);
void pattern_shower_warning(const AttackPlan *plan, const DemoConfig *config,
                            PatternWarning *out);

#ifdef __cplusplus
}
#endif

#endif /* DEMO_PATTERN_SHOWER_H */
