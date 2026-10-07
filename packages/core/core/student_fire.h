/* student_fire.h - 学生反击: 瞄准采样、发射节奏、伤害与弹生成
 * 接口版本: 1
 */
#ifndef DEMO_STUDENT_FIRE_H
#define DEMO_STUDENT_FIRE_H

#include "demo_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 在指定 tick 为学生生成一发反击弹。
 * 瞄准方式(已批准): 发射瞬间瞄准 Boss 当时位置, 生成直线弹, 不追踪。
 * 返回 false 表示学生倒下、超出射程或弹池不足; 不产生部分效果。 */
bool student_fire_try(const DemoConfig *cfg, Actor *student, float target_x, float target_y,
                      ProjectilePool *pool, int32_t tick, uint32_t *inout_cooldown,
                      DemoEntityId *out_bullet_source);

/* 该学生此刻是否允许发射(冷却与存活检查)。 */
bool student_fire_ready(const Actor *student, int32_t cooldown_ticks);
/* 冷却推进。冷却以非负 tick 计, 但接口统一用 int32_t 表达:
 *   - student_fire_ready 接收 int32_t;
 *   - student_fire_try 的 inout_cooldown 为 uint32_t*（历史签名, 接口 v2 冻结）。
 * 调用方不得用 `(uint32_t *)&int32_t变量` 强转(严格别名违规)。 */
void student_fire_tick_cooldown(int32_t *cooldown_ticks);

#ifdef __cplusplus
}
#endif

#endif /* DEMO_STUDENT_FIRE_H */
