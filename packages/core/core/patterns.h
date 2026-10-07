/* patterns.h - 招式注册表(母代理独占维护)
 * 接口版本: 1
 * 每个招式模块只实现自己的 pattern_<name>_{make_plan,emit,warning};
 * 注册表把 pattern ID 映射到这三个函数, world 只通过注册表调用。
 */
#ifndef DEMO_PATTERNS_H
#define DEMO_PATTERNS_H

#include "demo_base.h"
#include "pattern_course.h"
#include "pattern_mine.h"
#include "pattern_ring.h"
#include "pattern_shower.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*PatternMakePlanFn)(const PatternRequest *request, const DemoConfig *config,
                                  Rng *rng, AttackPlan *out);
typedef bool (*PatternEmitFn)(const AttackPlan *plan, const DemoConfig *config,
                              uint32_t attack_tick, ProjectileSpawnBuffer *out);
typedef void (*PatternWarningFn)(const AttackPlan *plan, const DemoConfig *config,
                                 PatternWarning *out);

typedef struct PatternVTable {
    const char *name;
    PatternMakePlanFn make_plan;
    PatternEmitFn emit;
    PatternWarningFn warning;
} PatternVTable;

/* 按 DemoPattern 索引; 越界返回 NULL */
const PatternVTable *pattern_vtable(DemoPattern pattern);
const char *pattern_name(DemoPattern pattern);

#ifdef __cplusplus
}
#endif

#endif /* DEMO_PATTERNS_H */
