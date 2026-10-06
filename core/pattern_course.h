/* pattern_course.h - 招 1 选课系统·课表华容道: 封路弹带与连续可达通道
 * 用途档位: 中消耗·封路
 * 接口版本: 1
 */
#ifndef DEMO_PATTERN_COURSE_H
#define DEMO_PATTERN_COURSE_H

#include "demo_base.h"

#ifdef __cplusplus
extern "C" {
#endif

bool pattern_course_make_plan(const PatternRequest *request, const DemoConfig *config,
                              Rng *rng, AttackPlan *out);
bool pattern_course_emit(const AttackPlan *plan, const DemoConfig *config, uint32_t attack_tick,
                         ProjectileSpawnBuffer *out);
void pattern_course_warning(const AttackPlan *plan, const DemoConfig *config,
                            PatternWarning *out);

#ifdef __cplusplus
}
#endif

#endif /* DEMO_PATTERN_COURSE_H */
