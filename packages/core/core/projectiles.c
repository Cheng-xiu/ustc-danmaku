/* projectiles.c - S05 固定容量弹池与弹的直线运动
 *
 * 接口版本: 2 (core/projectiles.h 增加可变场地入口，旧入口兼容)
 * 配置版本: 5 (docs/demo-rules.md)
 *
 * 职责与边界:
 *   - pool_init / pool_spawn / pool_advance / pool_clear_plan / pool_count_faction
 *     / spawn_buffer_init / spawn_buffer_push 的唯一实现。
 *   - 只做"弹池 + 直线运动 + 寿命 + 出界移除"; 不做碰撞、不做伤害、不判胜负、
 *     不决定场力(已批准: 直线运动, 不启用原场力)、不做穿透规则。
 *   - 纯 C11; 不使用 EasyX / 墙钟 / 随机数 / 文件日志 / 全局可变状态。
 *   - 渲染坐标只是读取 Projectile 的只读副本, 真实世界位置只由本文件推进。
 *
 * ---------------------------------------------------------------- ID 构成
 *
 *   id = ((uint64_t)index << 40) ^ ((uint64_t)generation << 20)
 *        ^ (id_seed_tick & 0xFFFFF)
 *
 * 唯一性论证:
 *   1. index < DEMO_MAX_PROJECTILES = 800 < 2^24, 所以 index 段占 bit 40..63;
 *      generation < 2^20 时占 bit 20..39; (id_seed_tick & 0xFFFFF) 占 bit 0..19。
 *      三段互不重叠, 因此这里的 XOR 等价于三段拼接, id 与三元组
 *      (index, generation, id_seed_tick & 0xFFFFF) 在该取值域内一一对应。
 *   2. generation 取自池级单调递增序列 pool->next_generation (初值 1, 每次成功
 *      spawn 后 +1, 永不回退; 0 保留表示"槽位从未使用")。一个弹的 generation
 *      在整个池生命周期内不会重复, 因此即使同一 tick 连续 spawn 多次、或不同 tick
 *      复用同一槽位, (index, generation) 组合始终唯一, id 必然不同 ---- 不依赖
 *      tick 段, 也不把可复用下标当整局 ID。
 *   3. 同一槽位复用时, 新 generation 严格大于该槽位上一次的 generation, 因为旧的
 *      generation 已被更早地发放过, 而 next_generation 单调递增。
 *   4. 额外去重(防御): 若候选 id 与池内 0..capacity-1 任一槽位(含已失效弹残留的
 *      id)相同, 则 generation 递增后重算, 最多尝试 capacity+1 次。因此即使
 *      generation 段溢出到 bit 40 以上(index 段重叠, 需单池连续 spawn >= 2^20 发),
 *      池内也不会出现重复 id。
 *   5. 已知上界(如实记录, 未隐藏): 单池连续 spawn 超过 2^20 (1,048,576) 发后,
 *      generation 段会进入 index 段, 此时"已被移除且槽位被别的弹覆盖"的历史 id
 *      理论上可能与新 id 相同; 池内重复仍由第 4 条挡住。一局 60 Hz 的弹量远小于
 *      该上界, 且 world_reset -> pool_init 会重置 next_generation。若后续需要刚性
 *      无上界, 可把 generation 段扩到 bit 32..63(单行改动), 需母代理决定。
 *
 * ---------------------------------------------------------------- 位置语义
 *
 *   spawn:  px = py = x = y = 起点
 *   advance: 先 px = x, py = y (保存本 tick 起点), 再 x += vx*dt, y += vy*dt
 *            (直线运动, 无场力、无加速度、不做追踪)
 *   寿命:   lifetime_ticks 每次 advance 递减 1, <= 0 时移除
 *   出界:   用本 tick 终点判断, 见 POOL_OOB_MARGIN
 */
#include "projectiles.h"

#include <math.h>
#include <string.h>

/* 出界判定保留 64 px 固定边距；真实场宽高由拥有该弹池的世界传入。
 * 旧 pool_advance(dt) 入口仍使用默认 960x720，保持原生调用兼容。 */
#define POOL_OOB_MARGIN 64.0f

/* 池的实际可用槽位数: 防御伪造的 capacity, 保证任何循环都不会越界访问 items[]。 */
static uint32_t pool_effective_capacity(const ProjectilePool *pool)
{
    uint32_t cap = pool->capacity;

    if (cap > DEMO_MAX_PROJECTILES) {
        cap = DEMO_MAX_PROJECTILES;
    }
    return cap;
}

static uint64_t pool_make_id(uint32_t index, uint32_t generation, uint64_t id_seed_tick)
{
    uint64_t tick_bits = id_seed_tick & UINT64_C(0xFFFFF);

    return ((uint64_t)index << 40) ^ ((uint64_t)generation << 20) ^ tick_bits;
}

/* 该 id 是否已被池内任一槽位占用(含 active == false 的残留 id)。 */
static bool pool_id_present(const ProjectilePool *pool, uint32_t cap, uint64_t id)
{
    for (uint32_t i = 0u; i < cap; ++i) {
        if (pool->items[i].id == id) {
            return true;
        }
    }
    return false;
}

/* 递增世代并跳过保留值 0(0 只表示槽位从未使用)。 */
static uint32_t pool_next_gen(uint32_t generation)
{
    uint32_t next = generation + 1u;

    if (next == 0u) {
        next = 1u;
    }
    return next;
}

void pool_init(ProjectilePool *pool, uint32_t capacity)
{
    if (pool == NULL) {
        return;
    }

    /* 清零全部内容: items[]、capacity、live_count、next_generation、overflow_events */
    memset(pool, 0, sizeof(*pool));

    if (capacity > DEMO_MAX_PROJECTILES) {
        capacity = DEMO_MAX_PROJECTILES; /* 夹紧到 [0, DEMO_MAX_PROJECTILES] */
    }
    pool->capacity = capacity;
    pool->live_count = 0u;
    pool->next_generation = 1u;
    pool->overflow_events = 0u;
}

bool pool_spawn(ProjectilePool *pool, DemoFaction faction, DemoEntityId source_id,
                uint64_t plan_id, DemoPattern source_pattern, float x, float y, float vx,
                float vy, float radius, float damage, int32_t lifetime_ticks,
                uint64_t id_seed_tick)
{
    uint32_t cap;
    uint32_t index;
    uint32_t generation;
    uint32_t tries;
    uint64_t id = 0u;
    Projectile *slot;

    if (pool == NULL) {
        return false;
    }
    /* 非法输入: 不占用槽位, 也不计容量溢出(溢出只表示"池真的满了")。 */
    if (lifetime_ticks <= 0) {
        return false;
    }
    if (!isfinite(x) || !isfinite(y) || !isfinite(vx) || !isfinite(vy)) {
        return false; /* 不发 NaN/Inf 坐标弹 */
    }
    if (!isfinite(damage)) {
        return false; /* 数值安全: 不把 NaN/Inf 伤害写进池(见交付报告"任务外扩展") */
    }
    if (!(radius > 0.0f)) {
        radius = 0.0f; /* radius < 0 视为 0; NaN 同样归 0, 不写入非法半径 */
    }

    cap = pool_effective_capacity(pool);

    /* 空闲槽位: active == false 的槽位, 低下标优先。 */
    index = cap;
    for (uint32_t i = 0u; i < cap; ++i) {
        if (!pool->items[i].active) {
            index = i;
            break;
        }
    }
    if (index == cap) {
        /* 无空位: 计数诊断并失败, 不越界、不覆盖已激活弹。 */
        pool->overflow_events += 1u;
        return false;
    }

    /* 分配整局唯一 id: 池级单调世代 + 额外去重。 */
    generation = pool->next_generation;
    for (tries = 0u; tries <= cap; ++tries) {
        id = pool_make_id(index, generation, id_seed_tick);
        if (!pool_id_present(pool, cap, id)) {
            break;
        }
        generation = pool_next_gen(generation); /* 去重: 世代递增重算 */
    }
    if (tries > cap) {
        /* 理论不可达(见文件头唯一性论证第 4 条): 保守失败, 不写槽位、不产生重复 id。 */
        return false;
    }

    pool->next_generation = pool_next_gen(generation);

    slot = &pool->items[index];
    slot->id = id;
    slot->generation = generation;
    slot->active = true;
    slot->faction = faction;
    slot->source_id = source_id;
    slot->plan_id = plan_id;
    /* 接口 v2: source_pattern 由调用方传入, 不再依赖槽位残留值。
     * 学生弹传 0; Boss 弹传其所属招式, 保证命中事件的招式归属正确。 */
    slot->source_pattern = source_pattern;
    slot->px = x;
    slot->py = y;
    slot->x = x;
    slot->y = y;
    slot->vx = vx;
    slot->vy = vy;
    slot->radius = radius;
    slot->damage = damage;
    slot->lifetime_ticks = lifetime_ticks;

    pool->live_count += 1u;
    return true;
}

void pool_advance_in_field(ProjectilePool *pool, float dt, float width, float height)
{
    uint32_t cap;

    if (pool == NULL) {
        return;
    }
    /* 防御: dt 非有限或为负时不推进(既不做 NaN 积分, 也不消耗寿命)。 */
    if (!isfinite(dt) || dt < 0.0f || !isfinite(width) || !isfinite(height) ||
        width <= 0.0f || height <= 0.0f) {
        return;
    }

    cap = pool_effective_capacity(pool);
    for (uint32_t i = 0u; i < cap; ++i) {
        Projectile *p = &pool->items[i];

        if (!p->active) {
            continue;
        }

        p->px = p->x; /* 保存本 tick 起点(上一 tick 终点) */
        p->py = p->y;
        p->x = p->px + p->vx * dt; /* 直线运动积分, 无场力 */
        p->y = p->py + p->vy * dt;

        p->lifetime_ticks -= 1;
        if (p->lifetime_ticks <= 0) {
            p->active = false;
            if (pool->live_count > 0u) {
                pool->live_count -= 1u;
            }
            continue;
        }

        /* 出界: 用本 tick 终点判断, x 与 y 两个方向都覆盖。 */
        if (p->x < -POOL_OOB_MARGIN || p->x > width + POOL_OOB_MARGIN ||
            p->y < -POOL_OOB_MARGIN || p->y > height + POOL_OOB_MARGIN) {
            p->active = false;
            if (pool->live_count > 0u) {
                pool->live_count -= 1u;
            }
        }
    }
}

void pool_advance(ProjectilePool *pool, float dt)
{
    pool_advance_in_field(pool, dt, DEMO_FIELD_WIDTH, DEMO_FIELD_HEIGHT);
}

uint32_t pool_clear_plan(ProjectilePool *pool, uint64_t plan_id)
{
    uint32_t cap;
    uint32_t cleared = 0u;

    if (pool == NULL) {
        return 0u;
    }
    if (plan_id == 0u) {
        /* 0 属于学生反击弹; 按计划清弹绝不能影响学生弹。 */
        return 0u;
    }

    cap = pool_effective_capacity(pool);
    for (uint32_t i = 0u; i < cap; ++i) {
        Projectile *p = &pool->items[i];

        if (p->active && p->plan_id == plan_id) {
            p->active = false;
            if (pool->live_count > 0u) {
                pool->live_count -= 1u;
            }
            cleared += 1u;
        }
    }
    return cleared;
}

uint32_t pool_count_faction(const ProjectilePool *pool, DemoFaction faction)
{
    uint32_t cap;
    uint32_t count = 0u;

    if (pool == NULL) {
        return 0u;
    }

    cap = pool_effective_capacity(pool);
    for (uint32_t i = 0u; i < cap; ++i) {
        if (pool->items[i].active && pool->items[i].faction == faction) {
            count += 1u;
        }
    }
    return count;
}

void spawn_buffer_init(ProjectileSpawnBuffer *buf)
{
    if (buf == NULL) {
        return;
    }

    memset(buf, 0, sizeof(*buf));
    buf->count = 0u;
    buf->capacity = DEMO_MAX_ACTIVE_PLAN_PROJECTILES;
    buf->overflow = 0u;
}

bool spawn_buffer_push(ProjectileSpawnBuffer *buf, const Projectile *spec)
{
    uint32_t cap;

    if (buf == NULL || spec == NULL) {
        return false;
    }

    /* 有效容量同时受领域上限约束, 保证 spec[] 永不越界写。 */
    cap = buf->capacity;
    if (cap > DEMO_MAX_ACTIVE_PLAN_PROJECTILES) {
        cap = DEMO_MAX_ACTIVE_PLAN_PROJECTILES;
    }

    if (buf->count >= cap) {
        buf->overflow += 1u;
        return false;
    }

    buf->spec[buf->count] = *spec;
    buf->count += 1u;
    return true;
}
