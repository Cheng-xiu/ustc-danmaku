/* test_projectiles.c - S05 弹池与弹运动的验收测试
 *
 * 覆盖任务卡 S05 的全部 10 条验收条件, 外加"必须保持的行为"中未被这 10 条覆盖的
 * 非正参数与防御分支(第 11 项)。每条子检查打印 PASS/FAIL, 每个项目打印汇总,
 * 末尾打印总计。退出码: 全部通过为 0, 任一失败为 1。
 *
 * 编译(任务卡): 
 *   gcc -std=c11 -Wall -Wextra -I <repo>/core tests/test_projectiles.c core/projectiles.c \
 *       -o test_projectiles.exe
 *
 * 本文件只使用已冻结接口 core/demo_base.h 中的 7 个弹池函数, 不修改任何头文件。
 * 出界边距: TEST_OOB_MARGIN 与 core/projectiles.c 中 POOL_OOB_MARGIN 的已记录值
 * (64.0f) 一致; 若母代理批准改用别的边距, 本文件需同步更新。
 */
#include "demo_base.h"

#include <math.h>
#include <stdio.h>

#define TEST_OOB_MARGIN 64.0f
#define TEST_DT (1.0f / 60.0f)

/* ---------------------------------------------------------------- 测试框架 */

static int g_checks = 0;
static int g_failed = 0;
static int g_item_checks = 0;
static int g_item_failed = 0;

static void check(bool ok, const char *name)
{
    g_checks += 1;
    g_item_checks += 1;
    if (!ok) {
        g_failed += 1;
        g_item_failed += 1;
    }
    printf("    [%s] %s\n", ok ? "PASS" : "FAIL", name);
}

static void check_near(float got, float want, float eps, const char *name)
{
    float diff = got - want;
    bool ok;

    if (diff < 0.0f) {
        diff = -diff;
    }
    ok = isfinite(got) && (diff <= eps);
    g_checks += 1;
    g_item_checks += 1;
    if (!ok) {
        g_failed += 1;
        g_item_failed += 1;
    }
    printf("    [%s] %s (got=%.9g want=%.9g eps=%.9g)\n", ok ? "PASS" : "FAIL", name,
           (double)got, (double)want, (double)eps);
}

static void check_u32(uint32_t got, uint32_t want, const char *name)
{
    bool ok = (got == want);

    g_checks += 1;
    g_item_checks += 1;
    if (!ok) {
        g_failed += 1;
        g_item_failed += 1;
    }
    printf("    [%s] %s (got=%u want=%u)\n", ok ? "PASS" : "FAIL", name, (unsigned)got,
           (unsigned)want);
}

static void item_begin(const char *title)
{
    g_item_checks = 0;
    g_item_failed = 0;
    printf("\n== %s ==\n", title);
}

static void item_end(const char *title)
{
    printf("  -> %s: %s (%d 子项, %d 失败)\n", title, (g_item_failed == 0) ? "PASS" : "FAIL",
           g_item_checks, g_item_failed);
}

/* ---------------------------------------------------------------- 测试工具 */

/* 生成一发"标准"合法的 Boss 弹; 返回值直接来自 pool_spawn。 */
static bool spawn_boss(ProjectilePool *pool, uint64_t plan_id, float x, float y, float vx,
                       float vy, int32_t lifetime, uint64_t tick)
{
    return pool_spawn( pool, DEMO_FACTION_BOSS, 1u, plan_id, (DemoPattern)0, x, y, vx, vy, 6.0f, 1.0f, lifetime, tick);
}

/* 已激活弹的数量(独立于 pool->live_count 的交叉校验)。 */
static uint32_t count_active(const ProjectilePool *pool)
{
    uint32_t n = 0u;

    for (uint32_t i = 0u; i < DEMO_MAX_PROJECTILES; ++i) {
        if (pool->items[i].active) {
            n += 1u;
        }
    }
    return n;
}

/* 加速推进 n 个 tick。 */
static void advance_n(ProjectilePool *pool, uint32_t n)
{
    for (uint32_t i = 0u; i < n; ++i) {
        pool_advance(pool, TEST_DT);
    }
}

/* 在真实池上 spawn 并返回被分配的槽位下标。
 * pool_spawn 采用"低下标优先"的空闲槽位分配, 弹被移除后下标会立刻被后续 spawn
 * 复用, 因此测试不能假设固定下标, 必须按 active 位的变化定位实际槽位。 */
static uint32_t spawn_and_find_slot(ProjectilePool *pool, uint64_t plan_id, float x, float y,
                                    float vx, float vy, int32_t lifetime, uint64_t tick)
{
    bool before[DEMO_MAX_PROJECTILES];
    uint32_t found = DEMO_MAX_PROJECTILES;

    for (uint32_t i = 0u; i < DEMO_MAX_PROJECTILES; ++i) {
        before[i] = pool->items[i].active;
    }
    if (!spawn_boss(pool, plan_id, x, y, vx, vy, lifetime, tick)) {
        return DEMO_MAX_PROJECTILES;
    }
    for (uint32_t i = 0u; i < DEMO_MAX_PROJECTILES; ++i) {
        if (!before[i] && pool->items[i].active) {
            found = i;
            break;
        }
    }
    return found;
}

/* ---------------------------------------------------------------- 1 容量 */

static void test_capacity(void)
{
    ProjectilePool pool;

    item_begin("1) 容量: 容量 2 的池 spawn 3 发");
    pool_init(&pool, 2u);
    check_u32(pool.capacity, 2u, "pool_init capacity == 2");
    check_u32(pool.live_count, 0u, "pool_init live_count == 0");
    check_u32(pool.next_generation, 1u, "pool_init next_generation == 1");
    check_u32(pool.overflow_events, 0u, "pool_init overflow_events == 0");

    check(spawn_boss(&pool, 7u, 10.0f, 10.0f, 30.0f, 0.0f, 100, 1u), "第 1 发成功");
    check(spawn_boss(&pool, 7u, 20.0f, 20.0f, 0.0f, 30.0f, 100, 1u), "第 2 发成功");

    /* 记录原有两发, 验证溢出不会覆盖已激活弹。 */
    float vx0 = pool.items[0].vx;
    float vy0 = pool.items[0].vy;
    float vx1 = pool.items[1].vx;
    float vy1 = pool.items[1].vy;
    uint32_t live_before = pool.live_count;

    check(!spawn_boss(&pool, 7u, 30.0f, 30.0f, 0.0f, 0.0f, 100, 1u), "第 3 发返回 false");
    check_u32(pool.overflow_events, 1u, "第 3 发后 overflow_events == 1");
    check_u32(pool.live_count, 2u, "第 3 发后 live_count == 2");
    check_u32(live_before, 2u, "第 3 发前 live_count == 2");
    check_u32(count_active(&pool), 2u, "实际 active 槽位仍为 2");
    check(pool.items[0].active && pool.items[1].active, "原两发仍 active");
    check(pool.items[0].vx == vx0 && pool.items[0].vy == vy0, "槽位 0 速度未被覆盖");
    check(pool.items[1].vx == vx1 && pool.items[1].vy == vy1, "槽位 1 速度未被覆盖");
    check(!pool.items[2].active, "越界下标 2 未被写入(池容量外不写)");

    /* 容量夹紧与空指针安全。 */
    pool_init(&pool, DEMO_MAX_PROJECTILES + 100u);
    check_u32(pool.capacity, DEMO_MAX_PROJECTILES, "capacity 夹紧到 DEMO_MAX_PROJECTILES");
    pool_init(&pool, 0u);
    check_u32(pool.capacity, 0u, "capacity == 0 合法");
    check(!spawn_boss(&pool, 7u, 0.0f, 0.0f, 0.0f, 0.0f, 10, 1u), "容量 0 时 spawn 失败");
    check_u32(pool.overflow_events, 1u, "容量 0 时 overflow_events == 1");
    check(!pool_spawn( NULL, DEMO_FACTION_BOSS, 0u, 1u, (DemoPattern)0, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 10, 1u),
          "pool_spawn(NULL) 返回 false");
    pool_init(NULL, 4u); /* 不得崩溃 */
    check(true, "pool_init(NULL) 不崩溃");
    item_end("1) 容量");
}

/* ---------------------------------------------------------------- 2 唯一 ID */

static void test_unique_ids(void)
{
    enum { N = 200 };
    uint64_t ids[N];
    ProjectilePool pool;
    uint32_t n = 0u;
    uint32_t dup = 0u;
    bool bad = false;

    item_begin("2) 唯一 ID: 容量内连续复用 200 发");
    pool_init(&pool, 4u);
    for (uint32_t i = 0u; i < (uint32_t)N; ++i) {
        uint32_t slot = spawn_and_find_slot(&pool, 7u, 100.0f, 100.0f, 10.0f, 0.0f, 1,
                                            1000u + i);

        if (slot >= DEMO_MAX_PROJECTILES) {
            bad = true;
            break;
        }
        ids[n] = pool.items[slot].id; /* 寿命 1: 每次 advance 后槽位 0 立刻空出 */
        n += 1u;
        advance_n(&pool, 1u);
    }
    check(!bad, "200 发全部 spawn 成功(无意外溢出)");
    check_u32(n, (uint32_t)N, "收集到 200 个 id");
    for (uint32_t i = 0u; i < n; ++i) {
        for (uint32_t j = i + 1u; j < n; ++j) {
            if (ids[i] == ids[j]) {
                dup += 1u;
            }
        }
    }
    check_u32(dup, 0u, "200 个 id 无重复");
    check(ids[0] != 0u && ids[n - 1u] != 0u, "id 非 0");

    /* 填满 -> 全部寿命结束 -> 重新填满; 新旧 id 不得冲突。 */
    {
        enum { CAP = 8 };
        ProjectilePool p2;
        uint64_t old_ids[CAP];
        uint64_t new_ids[CAP];
        uint32_t cross = 0u;

        pool_init(&p2, CAP);
        for (uint32_t i = 0u; i < CAP; ++i) {
            check(spawn_boss(&p2, 9u, 50.0f, 50.0f, 0.0f, 0.0f, 2, 5000u), "填充阶段 spawn 成功");
            old_ids[i] = p2.items[i].id;
        }
        check_u32(p2.live_count, CAP, "填充后 live_count == 容量");
        advance_n(&p2, 2u); /* lifetime = 2 -> 全部移除 */
        check_u32(p2.live_count, 0u, "全部寿命结束后 live_count == 0");
        check_u32(count_active(&p2), 0u, "全部寿命结束后无 active 弹");

        for (uint32_t i = 0u; i < CAP; ++i) {
            check(spawn_boss(&p2, 9u, 50.0f, 50.0f, 0.0f, 0.0f, 2, 5000u), "重填阶段 spawn 成功");
            new_ids[i] = p2.items[i].id;
        }
        for (uint32_t i = 0u; i < CAP; ++i) {
            for (uint32_t j = 0u; j < CAP; ++j) {
                if (old_ids[i] == new_ids[j]) {
                    cross += 1u;
                }
            }
        }
        check_u32(cross, 0u, "新旧 id 交叉无冲突(整局唯一)");
    }

    /* 压力: 在 64 槽位的池上连续 spawn 5000 发(远超容量, 强制反复复用全部槽位),
     * 收集每个 id 并做全对全比较, 确认没有任何两个 id 相同。
     * 每满 64 发推进 3 tick 让这一批寿命结束, 因此每个槽位都会被反复回收复用,
     * 且不会触发容量溢出(溢出不等于错误, 但会让本测试失去意义)。 */
    {
        enum { SCALE = 5000, BATCH = 64 };
        static uint64_t all_ids[SCALE];
        uint32_t n_ids = 0u;
        uint32_t abnormal = 0u;
        uint32_t collisions = 0u;
        uint32_t max_live = 0u;
        ProjectilePool p3;

        pool_init(&p3, (uint32_t)BATCH);
        for (uint32_t i = 0u; i < (uint32_t)SCALE; ++i) {
            uint32_t slot = spawn_and_find_slot(&p3, 7u, 0.0f, 0.0f, 0.0f, 0.0f, 3, i);

            if (slot >= DEMO_MAX_PROJECTILES) {
                abnormal += 1u; /* spawn 意外失败也记为异常 */
                continue;
            }
            all_ids[n_ids] = p3.items[slot].id;
            n_ids += 1u;
            if (p3.live_count > max_live) {
                max_live = p3.live_count;
            }
            if (((i + 1u) % (uint32_t)BATCH) == 0u) {
                advance_n(&p3, 3u); /* lifetime = 3: 整批移除, 槽位全部可复用 */
            }
        }
        for (uint32_t i = 0u; i < n_ids; ++i) {
            for (uint32_t j = i + 1u; j < n_ids; ++j) {
                if (all_ids[i] == all_ids[j]) {
                    collisions += 1u;
                }
            }
        }
        printf("    (压力测试: 收集 %u 个 id, 容量 %u, 峰值 live %u)\n", (unsigned)n_ids,
               (unsigned)BATCH, (unsigned)max_live);
        check_u32(n_ids, (uint32_t)SCALE, "5000 发全部成功收集");
        check_u32(abnormal, 0u, "5000 发压力测试: 无 spawn 异常失败");
        check_u32(collisions, 0u, "5000 发压力测试: 全对全比较无重复 id");
        check_u32(max_live, (uint32_t)BATCH, "5000 发压力测试: 每批都填满全部 64 个槽位");
        check_u32(p3.live_count, count_active(&p3), "5000 发压力测试: live_count 一致");
    }
    item_end("2) 唯一 ID");
}

/* ---------------------------------------------------------------- 3 复用世代 */

static void test_reuse_generation(void)
{
    ProjectilePool pool;
    uint32_t gen_slot0[5];
    uint32_t gen_slot1[3];
    bool ok = true;

    item_begin("3) 复用无混淆: 同一槽位复用后 generation 严格递增");
    pool_init(&pool, 2u);

    /* 槽位 0 连续复用 5 次(寿命 1, 每次 advance 后空出)。 */
    for (uint32_t i = 0u; i < 5u; ++i) {
        if (!spawn_boss(&pool, 7u, 10.0f, 10.0f, 0.0f, 0.0f, 1, 100u + i)) {
            ok = false;
        }
        gen_slot0[i] = pool.items[0].generation;
        advance_n(&pool, 1u);
    }
    check(ok, "槽位 0 复用 5 次全部成功");
    for (uint32_t i = 1u; i < 5u; ++i) {
        if (!(gen_slot0[i] > gen_slot0[i - 1u])) {
            ok = false;
        }
    }
    check(ok, "槽位 0 的 generation 严格递增");
    check(gen_slot0[0] != 0u, "generation 从 1 起(0 保留)");

    /* 槽位 1 复用: 先占住槽位 0 不释放, 再让槽位 1 反复空出。 */
    ok = spawn_boss(&pool, 7u, 0.0f, 0.0f, 0.0f, 0.0f, 1000, 200u);
    check(ok, "占位槽位 0 成功");
    for (uint32_t i = 0u; i < 3u; ++i) {
        if (!spawn_boss(&pool, 7u, 20.0f, 20.0f, 0.0f, 0.0f, 1, 300u + i)) {
            ok = false;
        }
        gen_slot1[i] = pool.items[1].generation;
        advance_n(&pool, 1u);
    }
    check(ok, "槽位 1 复用 3 次全部成功");
    for (uint32_t i = 1u; i < 3u; ++i) {
        if (!(gen_slot1[i] > gen_slot1[i - 1u])) {
            ok = false;
        }
    }
    check(ok, "槽位 1 的 generation 严格递增");
    check(pool.items[0].active && !pool.items[1].active, "槽位占用关系符合预期");

    /* 池级 next_generation 与最近一次发放的 generation 同源且不回退。 */
    check(pool.next_generation > gen_slot1[2], "next_generation 已越过最近发放的 generation");
    item_end("3) 复用世代");
}

/* ---------------------------------------------------------------- 4 运动 */

static void test_motion(void)
{
    ProjectilePool pool;
    float prev_x;
    float prev_y;
    float vx = 120.0f;
    float vy = -90.0f;
    float want_x;
    float want_y;

    item_begin("4) 运动与位置语义: px/py 保存本 tick 起点, 直线积分");
    pool_init(&pool, 4u);
    check(spawn_boss(&pool, 7u, 100.0f, 200.0f, vx, vy, 1000, 42u), "spawn 成功");
    check_near(pool.items[0].px, 100.0f, 1e-4f, "spawn 后 px == 起点 x");
    check_near(pool.items[0].py, 200.0f, 1e-4f, "spawn 后 py == 起点 y");
    check_near(pool.items[0].x, 100.0f, 1e-4f, "spawn 后 x == 起点 x");
    check_near(pool.items[0].y, 200.0f, 1e-4f, "spawn 后 y == 起点 y");

    prev_x = pool.items[0].x;
    prev_y = pool.items[0].y;
    pool_advance(&pool, TEST_DT);
    check_near(pool.items[0].px, prev_x, 1e-4f, "advance 后 px == 上一 tick 的 x");
    check_near(pool.items[0].py, prev_y, 1e-4f, "advance 后 py == 上一 tick 的 y");
    want_x = prev_x + vx * TEST_DT;
    want_y = prev_y + vy * TEST_DT;
    check_near(pool.items[0].x, want_x, 1e-4f, "advance 后 x == px + vx*dt");
    check_near(pool.items[0].y, want_y, 1e-4f, "advance 后 y == py + vy*dt");
    check_near(pool.items[0].x - pool.items[0].px, vx * TEST_DT, 1e-4f, "单 tick 位移 == vx*dt");

    /* 第二个 tick: 起点必须等于上一个 tick 的终点(不跳变)。 */
    prev_x = pool.items[0].x;
    prev_y = pool.items[0].y;
    pool_advance(&pool, TEST_DT);
    check_near(pool.items[0].px, prev_x, 1e-4f, "第二个 tick 的 px == 上一 tick 终点 x");
    check_near(pool.items[0].py, prev_y, 1e-4f, "第二个 tick 的 py == 上一 tick 终点 y");
    check_near(pool.items[0].x, 100.0f + vx * (2.0f * TEST_DT), 1e-4f, "两 tick 后 x == 起点 + 2*vx*dt");
    check_near(pool.items[0].y, 200.0f + vy * (2.0f * TEST_DT), 1e-4f, "两 tick 后 y == 起点 + 2*vy*dt");

    /* 直线运动: 速度在整个生命周期内不变(无场力/加速度)。 */
    check_near(pool.items[0].vx, vx, 1e-6f, "vx 不随时间改变");
    check_near(pool.items[0].vy, vy, 1e-6f, "vy 不随时间改变");

    /* 静止弹不移动, px == x。 */
    check(spawn_boss(&pool, 7u, 300.0f, 300.0f, 0.0f, 0.0f, 10, 43u), "静止弹 spawn 成功");
    pool_advance(&pool, TEST_DT);
    {
        uint32_t idx = 1u;
        check_near(pool.items[idx].x, 300.0f, 1e-4f, "零速度弹 x 不变");
        check_near(pool.items[idx].y, 300.0f, 1e-4f, "零速度弹 y 不变");
        check_near(pool.items[idx].px, 300.0f, 1e-4f, "零速度弹 px == x");
    }
    item_end("4) 运动");
}

/* ---------------------------------------------------------------- 5 寿命 */

static void test_lifetime(void)
{
    ProjectilePool pool;

    item_begin("5) 寿命: lifetime_ticks = 2 在两次 advance 后被移除");
    pool_init(&pool, 2u);
    check(spawn_boss(&pool, 7u, 400.0f, 400.0f, 0.0f, 0.0f, 2, 7u), "lifetime = 2 的弹 spawn 成功");
    check(pool.items[0].lifetime_ticks == 2, "spawn 后 lifetime_ticks == 2");
    check_u32(pool.live_count, 1u, "spawn 后 live_count == 1");

    pool_advance(&pool, TEST_DT);
    check(pool.items[0].active, "第 1 次 advance 后仍 active");
    check(pool.items[0].lifetime_ticks == 1, "第 1 次 advance 后 lifetime_ticks == 1");
    check_u32(pool.live_count, 1u, "第 1 次 advance 后 live_count == 1");

    pool_advance(&pool, TEST_DT);
    check(!pool.items[0].active, "第 2 次 advance 后已移除");
    check(pool.items[0].lifetime_ticks == 0, "第 2 次 advance 后 lifetime_ticks == 0");
    check_u32(pool.live_count, 0u, "第 2 次 advance 后 live_count == 0");
    check_u32(count_active(&pool), 0u, "实际无 active 弹");

    /* lifetime = 1 只活一个 tick, 且移除后槽位可立即复用。 */
    check(spawn_boss(&pool, 7u, 0.0f, 0.0f, 0.0f, 0.0f, 1, 8u), "lifetime = 1 的弹 spawn 成功");
    pool_advance(&pool, TEST_DT);
    check(!pool.items[0].active, "lifetime = 1 的弹在一个 tick 后移除");
    check(spawn_boss(&pool, 7u, 0.0f, 0.0f, 0.0f, 0.0f, 5, 9u), "移除后槽位可复用");
    check(pool.items[0].active && pool.items[0].lifetime_ticks == 5, "复用后寿命正确");
    item_end("5) 寿命");
}

/* ---------------------------------------------------------------- 6 出界 */

static void test_out_of_bounds(void)
{
    ProjectilePool pool;
    uint32_t ix;
    uint32_t iy;

    item_begin("6) 出界: 用终点判断, x 与 y 方向都被移除");
    pool_init(&pool, 8u);

    /* +x: 从 (900,300) 以 6000 px/s 前进, 单 tick 位移 100 px。
     * 场地右边界 960 + margin 64 = 1024, 因此 1000 仍在场内, 1100 已出界。 */
    ix = spawn_and_find_slot(&pool, 7u, 900.0f, 300.0f, 6000.0f, 0.0f, 1000, 11u);
    check(ix < DEMO_MAX_PROJECTILES, "+x 弹 spawn 成功");
    pool_advance(&pool, TEST_DT);
    check_near(pool.items[ix].x, 1000.0f, 1e-3f, "+x 第 1 tick 终点 x == 1000(仍在场内)");
    check(pool.items[ix].active, "+x 第 1 tick 未提前移除");
    pool_advance(&pool, TEST_DT);
    check_near(pool.items[ix].x, 1100.0f, 1e-3f, "+x 第 2 tick 终点 x == 1100");
    check(!pool.items[ix].active, "+x 飞出场地后被移除");
    check(pool.items[ix].x > DEMO_FIELD_WIDTH + TEST_OOB_MARGIN,
          "+x 移除判据: 终点 x 超出 field+margin");

    /* -y: 从 (300,100) 以 -6000 px/s 前进, 单 tick 位移 100 px。
     * 下边界 -margin = -64, 因此 0 仍在场内, -100 已出界。 */
    iy = spawn_and_find_slot(&pool, 7u, 300.0f, 100.0f, 0.0f, -6000.0f, 1000, 12u);
    check(iy < DEMO_MAX_PROJECTILES, "-y 弹 spawn 成功");
    pool_advance(&pool, TEST_DT);
    check_near(pool.items[iy].y, 0.0f, 1e-3f, "-y 第 1 tick 终点 y == 0(仍在场内)");
    check(pool.items[iy].active, "-y 第 1 tick 未提前移除");
    pool_advance(&pool, TEST_DT);
    check_near(pool.items[iy].y, -100.0f, 1e-3f, "-y 第 2 tick 终点 y == -100");
    check(!pool.items[iy].active, "-y 飞出场地后被移除");
    check(pool.items[iy].y < -TEST_OOB_MARGIN, "-y 移除判据: 终点 y 超出 -margin");

    /* -x 与 +y 方向(出界判定必须同时覆盖两轴的四个方向)。 */
    ix = spawn_and_find_slot(&pool, 7u, 60.0f, 300.0f, -6000.0f, 0.0f, 1000, 13u);
    check(ix < DEMO_MAX_PROJECTILES, "-x 弹 spawn 成功");
    advance_n(&pool, 2u);
    check(!pool.items[ix].active, "-x 飞出场地后被移除");
    check(pool.items[ix].x < -TEST_OOB_MARGIN, "-x 移除判据: 终点 x 超出 -margin");

    iy = spawn_and_find_slot(&pool, 7u, 300.0f, 700.0f, 0.0f, 6000.0f, 1000, 14u);
    check(iy < DEMO_MAX_PROJECTILES, "+y 弹 spawn 成功");
    advance_n(&pool, 2u);
    check(!pool.items[iy].active, "+y 飞出场地后被移除");
    check(pool.items[iy].y > DEMO_FIELD_HEIGHT + TEST_OOB_MARGIN,
          "+y 移除判据: 终点 y 超出 field+margin");

    /* 场内慢速弹在多 tick 后仍存在(不能误杀场内弹)。
     * 30 px/s, 1000 tick 寿命: 60 tick 只走 30 px, 终点远在边距之内。 */
    ix = spawn_and_find_slot(&pool, 7u, 480.0f, 360.0f, 30.0f, -20.0f, 1000, 15u);
    check(ix < DEMO_MAX_PROJECTILES, "场内弹 spawn 成功");
    advance_n(&pool, 60u);
    check(pool.items[ix].active, "场内慢速弹 60 tick 后仍 active");
    check_near(pool.items[ix].x, 480.0f + 30.0f * (60.0f * TEST_DT), 1e-2f,
               "场内弹 60 tick 后 x 累计位移正确");
    check_u32(pool.live_count, 1u, "出界测试后仅剩场内弹");

    /* 出界移除后 live_count 与真实 active 数一致。 */
    check_u32(count_active(&pool), pool.live_count, "live_count 与实际 active 数一致");
    item_end("6) 出界");
}

/* ---------------------------------------------------------------- 7 按计划清弹 */

static void test_clear_plan(void)
{
    ProjectilePool pool;
    uint32_t cleared;

    item_begin("7) 按计划清弹: 只清指定 plan_id, plan_id == 0 非法");
    pool_init(&pool, 16u);

    check(spawn_boss(&pool, 7u, 100.0f, 100.0f, 0.0f, 0.0f, 1000, 21u), "plan 7 第 1 发");
    check(spawn_boss(&pool, 7u, 110.0f, 100.0f, 0.0f, 0.0f, 1000, 21u), "plan 7 第 2 发");
    check(spawn_boss(&pool, 7u, 120.0f, 100.0f, 0.0f, 0.0f, 1000, 21u), "plan 7 第 3 发");
    check(spawn_boss(&pool, 8u, 130.0f, 100.0f, 0.0f, 0.0f, 1000, 21u), "plan 8 第 4 发");
    /* 学生反击弹: plan_id == 0。 */
    check(pool_spawn( &pool, DEMO_FACTION_STUDENT, 2u, 0u, (DemoPattern)0, 200.0f, 200.0f, 0.0f, 0.0f, 5.0f, 1.0f, 240, 21u),
          "学生弹 plan_id == 0 第 1 发");
    check(pool_spawn( &pool, DEMO_FACTION_STUDENT, 3u, 0u, (DemoPattern)0, 210.0f, 200.0f, 0.0f, 0.0f, 5.0f, 1.0f, 240, 21u),
          "学生弹 plan_id == 0 第 2 发");
    check_u32(pool.live_count, 6u, "6 发全部 live");

    cleared = pool_clear_plan(&pool, 7u);
    check_u32(cleared, 3u, "clear_plan(7) 返回 3");
    check_u32(pool.live_count, 3u, "清弹后 live_count == 3");
    check_u32(pool_count_faction(&pool, DEMO_FACTION_BOSS), 1u, "只剩 1 发 Boss 弹(plan 8)");
    check_u32(pool_count_faction(&pool, DEMO_FACTION_STUDENT), 2u, "2 发学生弹不受影响");
    check(pool.items[3].active && pool.items[3].plan_id == 8u, "plan 8 的弹仍在");
    check(pool.items[4].active && pool.items[4].plan_id == 0u, "学生弹 1 仍在");
    check(pool.items[5].active && pool.items[5].plan_id == 0u, "学生弹 2 仍在");

    /* plan_id == 0: 非法调用, 返回 0 且不清任何弹。 */
    cleared = pool_clear_plan(&pool, 0u);
    check_u32(cleared, 0u, "clear_plan(0) 返回 0");
    check_u32(pool.live_count, 3u, "clear_plan(0) 后 live_count 不变");
    check(pool.items[3].active && pool.items[4].active && pool.items[5].active,
          "clear_plan(0) 未清除任何弹(学生弹安全)");
    check_u32(pool_count_faction(&pool, DEMO_FACTION_STUDENT), 2u, "clear_plan(0) 后学生弹计数不变");

    /* 无匹配计划的清弹返回 0 且不改变池。 */
    cleared = pool_clear_plan(&pool, 99u);
    check_u32(cleared, 0u, "clear_plan(99) 无匹配返回 0");
    check_u32(pool.live_count, 3u, "clear_plan(99) 后 live_count 不变");
    check_u32(pool_clear_plan(&pool, 8u), 1u, "clear_plan(8) 清掉最后 1 发 Boss 弹");
    check_u32(pool_count_faction(&pool, DEMO_FACTION_STUDENT), 2u, "清完 Boss 弹后学生弹仍为 2");
    check_u32(pool_clear_plan(NULL, 8u), 0u, "clear_plan(NULL) 返回 0");

    /* 被清除的槽位可复用, 且不影响仍在场的学生弹。 */
    check(spawn_boss(&pool, 7u, 500.0f, 500.0f, 0.0f, 0.0f, 1000, 22u), "清弹后槽位可复用");
    check_u32(pool.live_count, 3u, "复用后 live_count == 3");
    item_end("7) 按计划清弹");
}

/* ---------------------------------------------------------------- 8 阵营计数 */

static void test_count_faction(void)
{
    ProjectilePool pool;

    item_begin("8) count_faction: 两阵营混排各自计数");
    pool_init(&pool, 8u);
    check_u32(pool_count_faction(&pool, DEMO_FACTION_BOSS), 0u, "空池 Boss 计数 == 0");
    check_u32(pool_count_faction(&pool, DEMO_FACTION_STUDENT), 0u, "空池学生计数 == 0");

    check(spawn_boss(&pool, 7u, 0.0f, 0.0f, 0.0f, 0.0f, 100, 31u), "Boss 弹 1");
    check(pool_spawn( &pool, DEMO_FACTION_STUDENT, 1u, 0u, (DemoPattern)0, 0.0f, 0.0f, 0.0f, 0.0f, 5.0f, 1.0f, 100, 31u),
          "学生弹 1");
    check(spawn_boss(&pool, 7u, 0.0f, 0.0f, 0.0f, 0.0f, 100, 31u), "Boss 弹 2");
    check(spawn_boss(&pool, 7u, 0.0f, 0.0f, 0.0f, 0.0f, 100, 31u), "Boss 弹 3");
    check(pool_spawn( &pool, DEMO_FACTION_STUDENT, 2u, 0u, (DemoPattern)0, 0.0f, 0.0f, 0.0f, 0.0f, 5.0f, 1.0f, 100, 31u),
          "学生弹 2");
    check_u32(pool_count_faction(&pool, DEMO_FACTION_BOSS), 3u, "混排后 Boss 计数 == 3");
    check_u32(pool_count_faction(&pool, DEMO_FACTION_STUDENT), 2u, "混排后学生计数 == 2");
    check_u32(pool_count_faction(&pool, DEMO_FACTION_NONE), 0u, "FACTION_NONE 计数 == 0");
    check_u32(pool.live_count, 5u, "live_count == 5");

    /* 移除与清弹后计数同步更新。 */
    pool_clear_plan(&pool, 7u);
    check_u32(pool_count_faction(&pool, DEMO_FACTION_BOSS), 0u, "清 plan 7 后 Boss 计数 == 0");
    check_u32(pool_count_faction(&pool, DEMO_FACTION_STUDENT), 2u, "清 plan 7 后学生计数仍 == 2");
    check_u32(pool_count_faction(NULL, DEMO_FACTION_BOSS), 0u, "count_faction(NULL) 返回 0");

    pool_advance(&pool, TEST_DT); /* 学生弹寿命 100 -> 仍存活 */
    check_u32(pool_count_faction(&pool, DEMO_FACTION_STUDENT), 2u, "advance 后学生计数不变");

    /* 寿命耗尽后计数归零。 */
    check(pool_spawn( &pool, DEMO_FACTION_STUDENT, 1u, 0u, (DemoPattern)0, 300.0f, 300.0f, 0.0f, 0.0f, 5.0f, 1.0f, 1, 31u),
          "短寿命学生弹 spawn 成功");
    pool_advance(&pool, TEST_DT);
    check_u32(pool_count_faction(&pool, DEMO_FACTION_STUDENT), 2u,
              "短寿命学生弹移除后计数 == 2");
    item_end("8) 阵营计数");
}

/* ---------------------------------------------------------------- 9 生成缓冲 */

static void test_spawn_buffer(void)
{
    ProjectileSpawnBuffer buf;
    Projectile spec;
    uint32_t i;

    item_begin("9) spawn_buffer: 容量边界、overflow 计数、不越界");
    spawn_buffer_init(&buf);
    check_u32(buf.count, 0u, "init 后 count == 0");
    check_u32(buf.capacity, DEMO_MAX_ACTIVE_PLAN_PROJECTILES, "init capacity == 上限");
    check_u32(buf.overflow, 0u, "init 后 overflow == 0");

    /* 真容量边界: 填满 256 条, 第 257 条失败且 overflow == 1。 */
    spec.active = true;
    spec.faction = DEMO_FACTION_BOSS;
    spec.plan_id = 7u;
    for (i = 0u; i < DEMO_MAX_ACTIVE_PLAN_PROJECTILES; ++i) {
        spec.id = 1000u + (uint64_t)i;
        if (!spawn_buffer_push(&buf, &spec)) {
            break;
        }
    }
    check_u32(buf.count, DEMO_MAX_ACTIVE_PLAN_PROJECTILES, "推入 256 条后 count == 256");
    check_u32(buf.overflow, 0u, "容量未满时 overflow == 0");
    spec.id = 999999u;
    check(!spawn_buffer_push(&buf, &spec), "第 257 条返回 false");
    check_u32(buf.count, DEMO_MAX_ACTIVE_PLAN_PROJECTILES, "第 257 条后 count 仍 == 256");
    check_u32(buf.overflow, 1u, "第 257 条后 overflow == 1");
    check(buf.spec[255].id == 1255u, "第 256 条内容正确(未越界覆盖)");
    check(buf.spec[0].id == 1000u, "第 1 条内容未被覆盖");

    /* 小容量边界: 手工把 capacity 设小, 验证不越界写且 overflow 累计。 */
    spawn_buffer_init(&buf);
    buf.capacity = 3u;
    for (i = 0u; i < 5u; ++i) {
        spec.id = 2000u + (uint64_t)i;
        spawn_buffer_push(&buf, &spec);
    }
    check_u32(buf.count, 3u, "capacity = 3 时只接受 3 条");
    check_u32(buf.overflow, 2u, "capacity = 3 时 overflow == 2");
    check(buf.spec[2].id == 2002u, "第 3 条内容为第 3 个 spec(未越界)");
    check(buf.spec[3].id != 2003u, "下标 3 未被写入(capacity 之外)");

    /* 伪造超大 capacity 时仍受领域上限保护。 */
    spawn_buffer_init(&buf);
    buf.capacity = DEMO_MAX_ACTIVE_PLAN_PROJECTILES + 500u;
    for (i = 0u; i < DEMO_MAX_ACTIVE_PLAN_PROJECTILES + 10u; ++i) {
        spec.id = 3000u + (uint64_t)i;
        spawn_buffer_push(&buf, &spec);
    }
    check_u32(buf.count, DEMO_MAX_ACTIVE_PLAN_PROJECTILES, "超大 capacity 仍只存 256 条");
    check_u32(buf.overflow, 10u, "超大 capacity 下 overflow == 10");

    /* 空指针安全。 */
    check(!spawn_buffer_push(NULL, &spec), "push(NULL, spec) 返回 false");
    check(!spawn_buffer_push(&buf, NULL), "push(buf, NULL) 返回 false");
    spawn_buffer_init(NULL); /* 不得崩溃 */
    check(true, "spawn_buffer_init(NULL) 不崩溃");
    item_end("9) 生成缓冲");
}

/* ---------------------------------------------------------------- 10 非法输入 */

static void test_invalid_input(void)
{
    ProjectilePool pool;
    uint64_t nan_id = 0u;
    uint64_t inf_id = 0u;

    item_begin("10) 非法输入: NaN 速度被拒、lifetime <= 0 被拒");
    pool_init(&pool, 8u);

    check(!pool_spawn( &pool, DEMO_FACTION_BOSS, 1u, 7u, (DemoPattern)0, 10.0f, 10.0f, NAN, 0.0f, 6.0f, 1.0f, 100, 41u),
          "vx = NaN 被拒");
    check(!pool_spawn( &pool, DEMO_FACTION_BOSS, 1u, 7u, (DemoPattern)0, 10.0f, 10.0f, 0.0f, NAN, 6.0f, 1.0f, 100, 41u),
          "vy = NaN 被拒");
    check(!pool_spawn( &pool, DEMO_FACTION_BOSS, 1u, 7u, (DemoPattern)0, 10.0f, 10.0f, INFINITY, 0.0f, 6.0f, 1.0f, 100, 41u),
          "vx = +Inf 被拒");
    check(!pool_spawn( &pool, DEMO_FACTION_BOSS, 1u, 7u, (DemoPattern)0, 10.0f, 10.0f, 0.0f, -INFINITY, 6.0f, 1.0f, 100, 41u),
          "vy = -Inf 被拒");
    check(!pool_spawn( &pool, DEMO_FACTION_BOSS, 1u, 7u, (DemoPattern)0, NAN, 10.0f, 0.0f, 0.0f, 6.0f, 1.0f, 100, 41u),
          "x = NaN 被拒");
    check(!pool_spawn( &pool, DEMO_FACTION_BOSS, 1u, 7u, (DemoPattern)0, 10.0f, INFINITY, 0.0f, 0.0f, 6.0f, 1.0f, 100, 41u),
          "y = +Inf 被拒");

    check(!spawn_boss(&pool, 7u, 10.0f, 10.0f, 0.0f, 0.0f, 0, 41u), "lifetime_ticks == 0 被拒");
    check(!spawn_boss(&pool, 7u, 10.0f, 10.0f, 0.0f, 0.0f, -5, 41u), "lifetime_ticks < 0 被拒");

    check_u32(pool.live_count, 0u, "全部非法输入后 live_count == 0");
    check_u32(pool.overflow_events, 0u, "非法输入不计入 overflow_events");
    check_u32(count_active(&pool), 0u, "全部非法输入后无 active 弹");

    /* 池内不得残留 NaN 坐标: 紧接一次正常 spawn + advance 后坐标必须有限。 */
    check(spawn_boss(&pool, 7u, 10.0f, 10.0f, 1.0f, 1.0f, 100, 41u), "非法输入后正常 spawn 成功");
    nan_id = pool.items[0].id;
    pool_advance(&pool, TEST_DT);
    check(isfinite(pool.items[0].x) && isfinite(pool.items[0].y), "advance 后坐标有限(无 NaN)");
    check(nan_id != 0u, "合法弹获得非 0 id");

    /* radius < 0 视为 0。 */
    check(pool_spawn( &pool, DEMO_FACTION_BOSS, 1u, 7u, (DemoPattern)0, 20.0f, 20.0f, 0.0f, 0.0f, -3.0f, 1.0f, 100, 42u),
          "radius < 0 仍然生成");
    check_near(pool.items[1].radius, 0.0f, 1e-6f, "radius < 0 被夹紧为 0");

    /* 非有限 dt 不推进、不破坏状态。 */
    inf_id = pool.items[0].id;
    {
        float keep_x = pool.items[0].x;
        int32_t keep_life = pool.items[0].lifetime_ticks;

        pool_advance(&pool, NAN);
        check(pool.items[0].x == keep_x && pool.items[0].lifetime_ticks == keep_life,
              "dt = NaN 时不推进(状态不变)");
        pool_advance(&pool, -TEST_DT);
        check(pool.items[0].x == keep_x && pool.items[0].lifetime_ticks == keep_life,
              "dt < 0 时不推进(状态不变)");
        check(pool.items[0].id == inf_id, "非有限 dt 后 id 不变");
    }

    pool_advance(NULL, TEST_DT); /* 不得崩溃 */
    check(true, "pool_advance(NULL) 不崩溃");
    item_end("10) 非法输入");
}

/* ---------------------------------------------------------------- 11 附加覆盖 */

static void test_misc_contract(void)
{
    ProjectilePool pool;
    Projectile *p;

    item_begin("11) 附加: 归属字段、init 清零、live_count 一致性");
    pool_init(&pool, 4u);
    check(  pool.items[0].active == false && pool.items[0].id == 0u &&
            pool.items[0].generation == 0u && pool.items[0].faction == DEMO_FACTION_NONE &&
            pool.items[0].source_id == 0u && pool.items[0].plan_id == 0u &&
            pool.items[0].px == 0.0f && pool.items[0].x == 0.0f && pool.items[0].vx == 0.0f &&
            pool.items[0].radius == 0.0f && pool.items[0].damage == 0.0f &&
            pool.items[0].lifetime_ticks == 0,
          "pool_init 清零全部内容(含 items[])");
    check(pool.items[DEMO_MAX_PROJECTILES - 1u].active == false &&
              pool.items[DEMO_MAX_PROJECTILES - 1u].id == 0u,
          "pool_init 清零最后一个槽位(整个 items[] 被清零)");

    check(pool_spawn( &pool, DEMO_FACTION_BOSS, 5u, 42u, (DemoPattern)0, 100.0f, 120.0f, 60.0f, -30.0f, 6.0f, 2.0f, 300, 77u),
          "带完整归属的弹 spawn 成功");
    p = &pool.items[0];
    check_u32(p->faction, (uint32_t)DEMO_FACTION_BOSS, "faction 正确");
    check_u32(p->source_id, 5u, "source_id 正确");
    check(p->plan_id == 42u, "plan_id 正确");
    check_near(p->vx, 60.0f, 1e-6f, "vx 正确");
    check_near(p->vy, -30.0f, 1e-6f, "vy 正确");
    check_near(p->radius, 6.0f, 1e-6f, "radius 正确");
    check_near(p->damage, 2.0f, 1e-6f, "damage 正确");
    check(p->lifetime_ticks == 300, "lifetime_ticks 正确");
    check(p->generation != 0u, "generation 非 0");

    /* 两条不同弹同 tick spawn: id 必须不同(同 tick 去重)。 */
    check(pool_spawn( &pool, DEMO_FACTION_BOSS, 5u, 42u, (DemoPattern)0, 0.0f, 0.0f, 0.0f, 0.0f, 6.0f, 1.0f, 300, 77u),
          "同 tick 第二发 spawn 成功");
    check(pool.items[0].id != pool.items[1].id, "同 tick 两发 id 不同");
    check(pool.items[1].generation > pool.items[0].generation, "同 tick 第二发 generation 更大");

    /* live_count 与真实 active 数在混合操作后一致。 */
    check(pool_spawn( &pool, DEMO_FACTION_STUDENT, 2u, 0u, (DemoPattern)0, 0.0f, 0.0f, 0.0f, 0.0f, 5.0f, 1.0f, 1, 78u),
          "短寿命学生弹 spawn 成功");
    check_u32(pool.live_count, count_active(&pool), "spawn 后 live_count 一致");
    advance_n(&pool, 1u);
    check_u32(pool.live_count, count_active(&pool), "advance 后 live_count 一致");
    pool_clear_plan(&pool, 42u);
    check_u32(pool.live_count, count_active(&pool), "清弹后 live_count 一致");
    check_u32(pool.live_count, 0u, "清弹后池为空");

    /* 槽位复用: Projectile.source_pattern 不能被上一发弹的残留值污染。
     * pool_spawn 签名没有该入参(接口缺口已上报母代理), 本实现显式清零,
     * 因此复用后的值必须是确定的 0, 而不是跨世代脏值。 */
    {
        ProjectilePool p4;

        pool_init(&p4, 1u);
        check(spawn_boss(&p4, 7u, 0.0f, 0.0f, 0.0f, 0.0f, 100, 91u), "复用测试第 1 发");
        p4.items[0].source_pattern = DEMO_PATTERN_SHOWER; /* 模拟旧弹或外部写入 */
        p4.items[0].active = false;
        p4.items[0].lifetime_ticks = 0;
        p4.live_count = 0u;
        check(spawn_boss(&p4, 7u, 0.0f, 0.0f, 0.0f, 0.0f, 100, 92u), "复用测试第 2 发");
        check(p4.items[0].source_pattern == (DemoPattern)0,
              "槽位复用后 source_pattern 被重置(无跨世代脏值)");
        check(p4.items[0].id != 0u && p4.items[0].active, "复用后弹有效");
    }

    /* id_seed_tick 只影响低位段: 同槽位同 generation 不会发生, 但不同 tick 的 id 必须不同。 */
    {
        uint64_t id_a;
        uint64_t id_b;

        pool_init(&pool, 1u);
        check(spawn_boss(&pool, 7u, 0.0f, 0.0f, 0.0f, 0.0f, 100, 1u), "tick 1 弹");
        id_a = pool.items[0].id;
        advance_n(&pool, 1u);
        pool_clear_plan(&pool, 7u);
        check(spawn_boss(&pool, 7u, 0.0f, 0.0f, 0.0f, 0.0f, 100, 2u), "tick 2 弹");
        id_b = pool.items[0].id;
        check(id_a != id_b, "不同 tick 复用同槽位 id 不同");
    }
    item_end("11) 附加覆盖");
}

/* ---------------------------------------------------------------- main */

int main(void)
{
    printf("S05 弹池与弹运动 验收测试 (接口版本 1, 配置版本 1)\n");
    printf("核心: core/projectiles.c; 头文件: core/demo_base.h(只读)\n");

    test_capacity();
    test_unique_ids();
    test_reuse_generation();
    test_motion();
    test_lifetime();
    test_out_of_bounds();
    test_clear_plan();
    test_count_faction();
    test_spawn_buffer();
    test_invalid_input();
    test_misc_contract();

    printf("\n================ 汇总 ================\n");
    printf("总子项: %d, 失败: %d\n", g_checks, g_failed);
    printf("结果: %s\n", (g_failed == 0) ? "ALL PASS" : "FAILED");
    return (g_failed == 0) ? 0 : 1;
}
