/* student_fire.c - S12 学生反击: 瞄准方式、发射节奏与学生弹生成
 *
 * 接口版本: 1 (core/student_fire.h, core/demo_base.h; 本文件不改任何头文件或配置)
 * 配置版本: 1 (docs/demo-rules.md 2.3 学生反击)
 *
 * 权威规则(已批准, docs/demo-rules.md 1.3 / 2.3):
 *   - 学生每次发射都瞄准 Boss 当时位置, 生成直线弹, 不追踪。
 *   - 移动方向与射击方向分离: 射击方向只由"学生位置 -> 目标位置"决定;
 *     本函数既不读取也不修改学生的移动意图, 也不读取任何移动输入。
 *   - 距离 <= cfg->student_fire_min_range 不发射; 距离为 0 或非有限不发射,
 *     且任何分支都不做除法(先判 distance > 0 再归一化), 不会产生 NaN。
 *   - 发射成功才把冷却设为 cfg->student_fire_interval_ticks, 且只赋值一次。
 *   - 弹属性全部读 cfg; plan_id 恒为 0(学生弹绝不能被 pool_clear_plan 清除,
 *     见 docs/demo-interfaces.md 3.6: pool_clear_plan(pool, 0) 必须返回 0)。
 *   - 不使用随机数、不读墙钟、不写日志、不做穿透判定、不启用场力, 不改任何头文件。
 *
 * 职责边界: 只做"学生的一发反击弹"的瞄准采样、合法性检查、弹生成与冷却写入。
 * 不做碰撞/伤害结算(core/collision.c、core/world.c), 不推进冷却(world_step 调
 * student_fire_tick_cooldown), 不决定是否该发射(world_step 先调 student_fire_ready)。
 */
#include "student_fire.h"

#include <math.h>

/* 学生弹的 plan_id 恒为 0; 0 保留给"无计划"的弹, 由弹池保证不被按计划清弹影响。 */
#define STUDENT_BULLET_PLAN_ID UINT64_C(0)

/* 学生弹的 source_pattern 无意义, 按接口注释(demo_base.h: Projectile.source_pattern)
 * 传 0, 不沿用槽位残留值。 */
#define STUDENT_BULLET_SOURCE_PATTERN ((DemoPattern)0)

bool student_fire_ready(const Actor *student, int32_t cooldown_ticks)
{
    /* 已批准: student 非空、存活、冷却 <= 0 才允许发射。 */
    if (student == NULL) {
        return false;
    }
    if (!student->alive) {
        return false;
    }
    return cooldown_ticks <= 0;
}

void student_fire_tick_cooldown(int32_t *cooldown_ticks)
{
    if (cooldown_ticks == NULL) {
        return;
    }
    if (*cooldown_ticks > 0) {
        *cooldown_ticks -= 1;
    }
    /* 结果不小于 0: 即使调用方传入过负值也夹回 0, 冷却永不为负。 */
    if (*cooldown_ticks < 0) {
        *cooldown_ticks = 0;
    }
}

bool student_fire_try(const DemoConfig *cfg, Actor *student, float target_x, float target_y,
                      ProjectilePool *pool, int32_t tick, uint32_t *inout_cooldown,
                      DemoEntityId *out_bullet_source)
{
    float speed;
    float radius;
    int32_t lifetime;
    float body_radius;
    float dx;
    float dy;
    float distance;
    float dir_x;
    float dir_y;
    float origin_x;
    float origin_y;

    /* ---- 非法参数: 返回 false, 不写池、不改冷却、不写任何输出参数 ----
     * out_bullet_source 在接口注释里没有被标注"可为 NULL"(对比
     * demo_config_validate 的 err 明确写了可为 NULL), 因此按空指针非法处理。 */
    if (cfg == NULL || student == NULL || pool == NULL || inout_cooldown == NULL ||
        out_bullet_source == NULL) {
        return false;
    }
    /* 倒下学生不得发射。 */
    if (!student->alive) {
        return false;
    }

    speed = cfg->student_bullet_speed;
    radius = cfg->student_bullet_radius;
    lifetime = cfg->student_bullet_lifetime_ticks;
    /* 用 !(x > 0) / !(x >= 0) 形式, 同时挡住 0、负值以及 NaN。 */
    if (!(speed > 0.0f)) {
        return false;
    }
    if (!(radius >= 0.0f)) {
        return false;
    }
    if (!(lifetime > 0)) {
        return false;
    }

    /* 非有限坐标: 设备置在场地外的极端输入也不得生成 NaN 弹。 */
    if (!isfinite(student->x) || !isfinite(student->y) || !isfinite(target_x) ||
        !isfinite(target_y)) {
        return false;
    }

    /* 瞄准采样: 只用"发射瞬间的学生位置与目标位置", 不预判、不追踪、不记忆历史。 */
    dx = target_x - student->x;
    dy = target_y - student->y;
    distance = sqrtf(dx * dx + dy * dy);

    /* 距离为 0(同点目标)或非有限: 不发射, 且绝不进入除法, 因此不会产生 NaN。 */
    if (!isfinite(distance) || !(distance > 0.0f)) {
        return false;
    }
    /* 最小射程: 距离 <= 阈值不发射, 也不改冷却(冷却由调用方继续推进)。 */
    if (distance <= cfg->student_fire_min_range) {
        return false;
    }

    /* 到这里 distance > 0 且有限, 归一化安全。射击方向完全由瞄准决定。 */
    dir_x = dx / distance;
    dir_y = dy / distance;

    /* 起点: 从学生中心沿瞄准方向外推 (student->radius + 弹半径), 让学生弹从学生
     * 体表外侧射出, 避免生成瞬间就与学生自身重叠。外推量远小于最小射程, 因此
     * 起点仍严格落在"学生 -> 目标"之间。体半径异常(非有限/负)时按 0 处理。 */
    body_radius = student->radius;
    if (!isfinite(body_radius) || body_radius < 0.0f) {
        body_radius = 0.0f;
    }
    origin_x = student->x + dir_x * (body_radius + radius);
    origin_y = student->y + dir_y * (body_radius + radius);

    /* 直线弹: 速度 = 归一化瞄准方向 * 配置弹速, 无加速度、无场力、无追踪。 */
    if (!pool_spawn(pool, DEMO_FACTION_STUDENT, student->id, STUDENT_BULLET_PLAN_ID,
                    STUDENT_BULLET_SOURCE_PATTERN, origin_x, origin_y, dir_x * speed,
                    dir_y * speed, radius, (float)cfg->student_fire_damage, lifetime,
                    (uint64_t)(uint32_t)tick)) {
        /* 弹池容量不足: pool_spawn 已计数 overflow_events; 冷却保持不变, 下次再试。 */
        return false;
    }

    /* 发射成功: 冷却只在这里、只赋值一次。 */
    *inout_cooldown = (uint32_t)cfg->student_fire_interval_ticks;
    *out_bullet_source = student->id;
    return true;
}
