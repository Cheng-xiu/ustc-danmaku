/* attack.h - 单招状态机: 合法性检查、锁定、扣能量、预警/攻击转移
 * 接口版本: 1
 */
#ifndef DEMO_ATTACK_H
#define DEMO_ATTACK_H

#include "demo_base.h"
#include "world.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 尝试接受一次出招请求。
 * 成功: 生成完整不可变 AttackPlan, 扣一次能量, 进入 WINDUP, 写 ACCEPTED 事件, 返回 true。
 * 失败: 不扣能量、不排队、不改变状态, 写 REJECTED 事件(带原因), 返回 false。 */
bool attack_try_request(World *world, DemoPattern pattern, DemoRejectReason *out_reason);

/* 推进预警/攻击状态机。每 tick 调用一次(在角色移动与碰撞结算之前)。
 * 攻击结束条件: 所有波次已生成且 active 时长用尽。结束时按配置清本招剩余 Boss 弹。 */
void attack_step(World *world);

/* 当前是否有计划占用唯一攻击通道 */
static inline bool attack_busy(const World *world) {
    return world->attack_state != DEMO_ATTACK_IDLE;
}

/* 攻击进度(0..1), 供 HUD 使用; 无计划返回 0 */
float attack_progress(const World *world);

#ifdef __cplusplus
}
#endif

#endif /* DEMO_ATTACK_H */
