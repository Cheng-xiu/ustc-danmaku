/* pattern_mine.h - 招 2 一教金矿·绩点淘金: 矿点预警后喷射扇形弹
 * 用途档位: 低消耗·追击/局部打击
 * 接口版本: 1
 */
#ifndef DEMO_PATTERN_MINE_H
#define DEMO_PATTERN_MINE_H

#include "demo_base.h"

#ifdef __cplusplus
extern "C" {
#endif

bool pattern_mine_make_plan(const PatternRequest *request, const DemoConfig *config, Rng *rng,
                            AttackPlan *out);
bool pattern_mine_emit(const AttackPlan *plan, const DemoConfig *config, uint32_t attack_tick,
                       ProjectileSpawnBuffer *out);
void pattern_mine_warning(const AttackPlan *plan, const DemoConfig *config, PatternWarning *out);

#ifdef __cplusplus
}
#endif

#endif /* DEMO_PATTERN_MINE_H */
