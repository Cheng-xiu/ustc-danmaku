/* test_student_fire.c - S12 学生反击的验收测试
 *
 * 覆盖任务卡 S12 的全部 11 条验收条件, 每条子检查打印 PASS/FAIL, 每个项目打印汇总,
 * 末尾打印总计。退出码: 全部通过为 0, 任一失败为 1。
 *
 * 编译(任务卡给出的对象编译 + 本测试所需的链接):
 *   gcc -std=c11 -Wall -Wextra -Werror -I core -c core/student_fire.c -o work/agents/S12/build/student_fire.o
 *   gcc -std=c11 -Wall -Wextra -Werror -I core tests/test_student_fire.c core/student_fire.c \
 *       core/projectiles.c -o work/agents/S12/build/test_student_fire.exe
 *
 * 本文件只使用已冻结的接口 core/student_fire.h、core/demo_base.h 与已完成的
 * core/projectiles.c(真实弹池实现), 不修改任何头文件或配置。
 * 期望数值来自 docs/demo-rules.md 2.3 与 core/demo_config.c 的默认配置(版本 1);
 * 若母代理变更这些数值并递增配置版本, 本文件的期望值需同步更新 ---- 因此本文件
 * 除少量"配置版本 1 的固定期望"外, 一律直接读 DemoConfig 的字段做对照。
 */
#include "demo_base.h"
#include "student_fire.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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
    ok = (isfinite(got) != 0) && (diff <= eps);
    g_checks += 1;
    g_item_checks += 1;
    if (!ok) {
        g_failed += 1;
        g_item_failed += 1;
    }
    printf("    [%s] %s (got=%.9g want=%.9g eps=%.9g)\n", ok ? "PASS" : "FAIL", name,
           (double)got, (double)want, (double)eps);
}

static void check_i32(int32_t got, int32_t want, const char *name)
{
    bool ok = (got == want);

    g_checks += 1;
    g_item_checks += 1;
    if (!ok) {
        g_failed += 1;
        g_item_failed += 1;
    }
    printf("    [%s] %s (got=%d want=%d)\n", ok ? "PASS" : "FAIL", name, (int)got, (int)want);
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

#define TEST_DT (1.0f / 60.0f)

/* 弹池较大(800 槽), 用静态存储避免压在栈上。 */
static ProjectilePool g_pool;
static DemoConfig g_cfg;

/* 配置版本 3 的固定期望(与 core/demo_config.c 默认值一致)。 */
#define EXPECT_VERSION 6u
#define EXPECT_INTERVAL 78
#define EXPECT_BULLET_SPEED 260.0f
#define EXPECT_BULLET_RADIUS 5.0f
#define EXPECT_BULLET_LIFETIME 240
#define EXPECT_FIRE_DAMAGE 1
#define EXPECT_MIN_RANGE 50.0f

static Actor make_student(float x, float y)
{
    Actor a;

    memset(&a, 0, sizeof(a));
    a.id = 7u;
    a.alive = true;
    a.x = x;
    a.y = y;
    a.radius = 20.0f;
    a.hp = 3;
    a.hp_max = 3;
    a.invuln_ticks = 0;
    return a;
}

static void reset_world_state(void)
{
    demo_config_init(&g_cfg);
    pool_init(&g_pool, g_cfg.projectile_cap);
}

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

/* 返回第一发激活弹的只读指针; 没有任何激活弹时返回 NULL。 */
static const Projectile *first_active(const ProjectilePool *pool)
{
    for (uint32_t i = 0u; i < DEMO_MAX_PROJECTILES; ++i) {
        if (pool->items[i].active) {
            return &pool->items[i];
        }
    }
    return NULL;
}

/* 池内任一激活弹是否存在非有限数值(用于"不得返回 NaN"的交叉校验)。 */
static bool any_active_non_finite(const ProjectilePool *pool)
{
    for (uint32_t i = 0u; i < DEMO_MAX_PROJECTILES; ++i) {
        const Projectile *p = &pool->items[i];

        if (!p->active) {
            continue;
        }
        if (!isfinite(p->x) || !isfinite(p->y) || !isfinite(p->px) || !isfinite(p->py) ||
            !isfinite(p->vx) || !isfinite(p->vy) || !isfinite(p->radius) ||
            !isfinite(p->damage)) {
            return true;
        }
    }
    return false;
}

static void advance_n(ProjectilePool *pool, uint32_t n)
{
    for (uint32_t i = 0u; i < n; ++i) {
        pool_advance(pool, TEST_DT);
    }
}

static float vec_len(float x, float y)
{
    return sqrtf(x * x + y * y);
}

/* ---------------------------------------------------------------- 1 直线瞄准 */

static void test_straight_aim(void)
{
    Actor s = make_student(100.0f, 100.0f);
    uint32_t cd = 0u;
    DemoEntityId src = 0u;
    const Projectile *p;

    item_begin("1 直线瞄准: (100,100) 瞄准 (500,100) 得到纯 +x 速度");

    reset_world_state();
    check(student_fire_try(&g_cfg, &s, 500.0f, 100.0f, &g_pool, 0, &cd, &src),
          "student_fire_try 在同水平线瞄准下返回 true");
    check_u32(count_active(&g_pool), 1u, "池内恰好新增 1 发弹");
    check_u32(g_pool.live_count, 1u, "pool.live_count 与弹数一致");

    p = first_active(&g_pool);
    check(p != NULL, "能找到新增的学生弹");
    if (p != NULL) {
        check_near(p->vx, EXPECT_BULLET_SPEED, 1e-3f, "vx == +配置弹速");
        check_near(p->vy, 0.0f, 1e-3f, "vy == 0(纯水平)");
        check_near(vec_len(p->vx, p->vy), g_cfg.student_bullet_speed, 1e-3f,
                   "速度大小 == cfg.student_bullet_speed");
        check(p->faction == DEMO_FACTION_STUDENT, "faction == DEMO_FACTION_STUDENT");
    }

    item_end("1 直线瞄准");
}

/* ---------------------------------------------------------------- 2 斜向瞄准 */

static void test_diagonal_aim(void)
{
    Actor s = make_student(100.0f, 100.0f);
    uint32_t cd = 0u;
    DemoEntityId src = 0u;
    const Projectile *p;

    item_begin("2 斜向瞄准: (100,100) 瞄准 (400,500) 得到 0.6/0.8 方向");

    reset_world_state();
    /* dx = 300, dy = 400, 距离 = 500 -> 方向 (0.6, 0.8) */
    check(student_fire_try(&g_cfg, &s, 400.0f, 500.0f, &g_pool, 0, &cd, &src),
          "student_fire_try 在斜向瞄准下返回 true");
    p = first_active(&g_pool);
    check(p != NULL, "能找到新增的学生弹");
    if (p != NULL) {
        check_near(p->vx, 0.6f * EXPECT_BULLET_SPEED, 1e-2f, "vx == 0.6 * 配置弹速");
        check_near(p->vy, 0.8f * EXPECT_BULLET_SPEED, 1e-2f, "vy == 0.8 * 配置弹速");
        check_near(vec_len(p->vx, p->vy), g_cfg.student_bullet_speed, 1e-3f,
                   "速度大小仍等于配置弹速(方向已归一化)");
    }

    /* 反斜向: 目标在左下, 速度两个分量都应为负。 */
    reset_world_state();
    cd = 0u;
    check(student_fire_try(&g_cfg, &s, 100.0f - 300.0f, 100.0f - 400.0f, &g_pool, 0, &cd, &src),
          "反斜向瞄准返回 true");
    p = first_active(&g_pool);
    if (p != NULL) {
        check_near(p->vx, -0.6f * EXPECT_BULLET_SPEED, 1e-2f, "反斜向 vx == -0.6 * 配置弹速");
        check_near(p->vy, -0.8f * EXPECT_BULLET_SPEED, 1e-2f, "反斜向 vy == -0.8 * 配置弹速");
    } else {
        check(false, "能找到反斜向新增的学生弹");
    }

    item_end("2 斜向瞄准");
}

/* ---------------------------------------------------------------- 3 不追踪 */

static void test_no_homing(void)
{
    Actor s = make_student(100.0f, 100.0f);
    uint32_t cd = 0u;
    DemoEntityId src = 0u;
    const Projectile *p;
    float vx0;
    float vy0;
    float x0;
    float y0;
    uint32_t slot = DEMO_MAX_PROJECTILES;

    item_begin("3 不追踪: 发射后目标移动, 速度与轨迹不变");

    reset_world_state();
    check(student_fire_try(&g_cfg, &s, 500.0f, 100.0f, &g_pool, 0, &cd, &src),
          "朝 (500,100) 发射成功");
    for (uint32_t i = 0u; i < DEMO_MAX_PROJECTILES; ++i) {
        if (g_pool.items[i].active) {
            slot = i;
            break;
        }
    }
    check(slot != DEMO_MAX_PROJECTILES, "定位到弹所在槽位");
    if (slot == DEMO_MAX_PROJECTILES) {
        item_end("3 不追踪");
        return;
    }

    p = &g_pool.items[slot];
    vx0 = p->vx;
    vy0 = p->vy;
    x0 = p->x;
    y0 = p->y;

    /* 目标(模拟 Boss)在发射后移动: 只改测试里的目标坐标, 不调用任何"更新瞄准"接口。 */
    {
        float target_after_x = 480.0f;
        float target_after_y = 620.0f;
        (void)target_after_x;
        (void)target_after_y;
    }
    advance_n(&g_pool, 30u);

    p = &g_pool.items[slot];
    check(p->active, "30 tick 后弹仍存活");
    check_near(p->vx, vx0, 0.0f, "速度 vx 在推进后完全不变(无追踪)");
    check_near(p->vy, vy0, 0.0f, "速度 vy 在推进后完全不变(无追踪)");
    check_near(p->x, x0 + vx0 * TEST_DT * 30.0f, 1e-2f, "位置 == 起点 + 匀速 * 30 tick");
    check_near(p->y, y0 + vy0 * TEST_DT * 30.0f, 1e-2f, "y 位置同样按匀速直线前进");
    check_near(p->vy, 0.0f, 0.0f, "水平弹在 30 tick 后仍无任何 y 分量");

    item_end("3 不追踪");
}

/* ---------------------------------------------------------------- 4 移动/射击分离 */

static void test_aim_separate_from_move(void)
{
    Actor s = make_student(100.0f, 100.0f);
    Actor before;
    uint32_t cd = 0u;
    DemoEntityId src = 0u;
    const Projectile *p;

    item_begin("4 移动意图与射击方向分离");

    reset_world_state();
    before = s;
    check(student_fire_try(&g_cfg, &s, 500.0f, 100.0f, &g_pool, 0, &cd, &src), "发射成功");
    check(s.x == before.x && s.y == before.y, "发射不改变学生位置(射击不驱动移动)");
    check(s.id == before.id && s.alive == before.alive && s.hp == before.hp,
          "发射不改变学生 id/alive/hp");
    p = first_active(&g_pool);
    if (p != NULL) {
        /* 学生位于 (100,100), 目标是正右方; 旧固定向上射击会给出 vy < 0, 必须被否决。 */
        check_near(p->vy, 0.0f, 0.0f, "射击方向只由瞄准决定: 无固定向上分量");
        check(p->vx > 0.0f, "射击方向指向瞄准点(+x)");
    } else {
        check(false, "能找到新增的学生弹");
    }

    /* 同一学生、同一目标位置, 方向只取决于瞄准向量, 与学生"想往哪走"无关:
     * 接口没有移动参数, 世界层的移动由 actor_apply_move 单独处理, 本函数不做移动。 */
    {
        Actor s2 = make_student(100.0f, 100.0f);
        uint32_t cd2 = 0u;
        DemoEntityId src2 = 0u;
        const Projectile *q;

        reset_world_state();
        check(student_fire_try(&g_cfg, &s2, 100.0f + 300.0f, 100.0f - 400.0f, &g_pool, 0, &cd2,
                               &src2),
              "同一学生改瞄准到右下后方仍可发射");
        q = first_active(&g_pool);
        if (q != NULL) {
            check(q->vy < 0.0f, "射击方向随瞄准改变(vy 现在为负), 不受任何移动轴影响");
        } else {
            check(false, "能找到第二发学生弹");
        }
    }

    item_end("4 移动意图与射击方向分离");
}

/* ---------------------------------------------------------------- 5 冷却 */

static void test_cooldown(void)
{
    Actor s = make_student(100.0f, 100.0f);
    uint32_t cd = 0u;
    DemoEntityId src = 0u;

    item_begin("5 冷却设置与递减不出现负数");

    reset_world_state();
    check(student_fire_ready(&s, 0), "student_fire_ready(cooldown=0) == true");
    check(!student_fire_ready(&s, 1), "student_fire_ready(cooldown=1) == false");

    check(student_fire_try(&g_cfg, &s, 500.0f, 100.0f, &g_pool, 0, &cd, &src),
          "首发发射成功");
    check_u32(cd, (uint32_t)EXPECT_INTERVAL, "发射成功后 cooldown == cfg.student_fire_interval_ticks");
    check_u32(cd, (uint32_t)g_cfg.student_fire_interval_ticks, "cooldown 与 cfg 字段一致");
    check(!student_fire_ready(&s, (int32_t)cd), "冷却中 ready == false");

    /* 再调用一次 try: 冷却不为 0, 由调用方负责任 ready 检查; 本函数只按瞄准与参数发射。
     * 这里验证的核心是"发射成功时冷却被设置为间隔、且只设置一次(不会累加)"。 */
    {
        uint32_t cd_after = 5u;

        check(student_fire_try(&g_cfg, &s, 500.0f, 100.0f, &g_pool, 0, &cd_after, &src),
              "第二次发射成功");
        check_u32(cd_after, (uint32_t)g_cfg.student_fire_interval_ticks,
                  "冷却是赋值而不是累加(cd_after == 间隔)");
    }

    /* 递减 78 次到 0, 再递减 10 次仍为 0, 不出现负数。 */
    for (int32_t i = 0; i < g_cfg.student_fire_interval_ticks; ++i) {
        student_fire_tick_cooldown((int32_t *)&cd);
    }
    check_u32(cd, 0u, "递减 cfg.student_fire_interval_ticks 次后 cooldown == 0");
    check(student_fire_ready(&s, (int32_t)cd), "冷却耗尽后 ready == true");
    for (int32_t i = 0; i < 10; ++i) {
        student_fire_tick_cooldown((int32_t *)&cd);
    }
    check_u32(cd, 0u, "继续递减 10 次仍为 0(不为负)");

    /* 负值输入: 必须夹回 0, 不变得更负。 */
    {
        int32_t neg = -7;

        student_fire_tick_cooldown(&neg);
        check_i32(neg, 0, "从 -7 递减后夹回 0(函数后置条件: 结果 >= 0)");
    }
    /* 空指针: 不崩溃。 */
    student_fire_tick_cooldown(NULL);
    check(true, "student_fire_tick_cooldown(NULL) 不崩溃");

    item_end("5 冷却");
}

/* ---------------------------------------------------------------- 6 最小射程 */

static void test_min_range(void)
{
    Actor s = make_student(100.0f, 100.0f);
    uint32_t cd = 7u;
    DemoEntityId src = 0u;

    item_begin("6 最小射程内不发射且冷却不变");

    reset_world_state();
    check_near(g_cfg.student_fire_min_range, EXPECT_MIN_RANGE, 1e-6f,
               "配置版本 4: student_fire_min_range == 50");

    /* 距离恰好等于最小射程: 不发射。 */
    check(!student_fire_try(&g_cfg, &s, 100.0f + g_cfg.student_fire_min_range, 100.0f, &g_pool, 3,
                            &cd, &src),
          "距离 == 最小射程不发射");
    check_u32(cd, 7u, "距离 == 最小射程时冷却完全不变");
    check_u32(count_active(&g_pool), 0u, "距离 == 最小射程时池内无新弹");

    /* 距离小于最小射程: 不发射。 */
    check(!student_fire_try(&g_cfg, &s, 100.0f + 10.0f, 100.0f, &g_pool, 4, &cd, &src),
          "距离 < 最小射程不发射");
    check_u32(cd, 7u, "距离 < 最小射程时冷却完全不变");
    check_u32(count_active(&g_pool), 0u, "距离 < 最小射程时池内无新弹");

    /* 距离刚超过最小射程: 发射。 */
    check(student_fire_try(&g_cfg, &s, 100.0f + g_cfg.student_fire_min_range + 1.0f, 100.0f,
                           &g_pool, 5, &cd, &src),
          "距离 > 最小射程发射成功");
    check_u32(cd, (uint32_t)g_cfg.student_fire_interval_ticks, "成功发射后冷却被设置为间隔");
    check_u32(count_active(&g_pool), 1u, "成功发射后池内 1 发弹");

    item_end("6 最小射程");
}

/* ---------------------------------------------------------------- 7 同点目标 */

static void test_zero_distance(void)
{
    Actor s = make_student(100.0f, 100.0f);
    uint32_t cd = 11u;
    DemoEntityId src = 0u;

    item_begin("7 同点目标不发射且无 NaN");

    reset_world_state();
    check(!student_fire_try(&g_cfg, &s, 100.0f, 100.0f, &g_pool, 0, &cd, &src),
          "目标与学生同点不发射");
    check_u32(cd, 11u, "同点目标时冷却不变");
    check_u32(count_active(&g_pool), 0u, "同点目标时池内无弹");
    check(!any_active_non_finite(&g_pool), "池内无任何非有限数值(未产生 NaN)");

    /* 非有限目标坐标: 同样不发射、不产生 NaN。 */
    check(!student_fire_try(&g_cfg, &s, NAN, 100.0f, &g_pool, 0, &cd, &src),
          "目标 x 为 NaN 时不发射");
    check_u32(cd, 11u, "NaN 目标时冷却不变");
    check(!student_fire_try(&g_cfg, &s, 100.0f, INFINITY, &g_pool, 0, &cd, &src),
          "目标 y 为 Inf 时不发射");
    check_u32(cd, 11u, "Inf 目标时冷却不变");
    check_u32(count_active(&g_pool), 0u, "非有限目标时池内始终无弹");

    /* 学生自身坐标非有限: 不发射。 */
    {
        Actor bad = make_student(NAN, 100.0f);

        check(!student_fire_try(&g_cfg, &bad, 500.0f, 100.0f, &g_pool, 0, &cd, &src),
              "学生 x 为 NaN 时不发射");
        check_u32(cd, 11u, "学生坐标 NaN 时冷却不变");
    }

    item_end("7 同点目标");
}

/* ---------------------------------------------------------------- 8 倒下学生 */

static void test_dead_student(void)
{
    Actor s = make_student(100.0f, 100.0f);
    uint32_t cd = 0u;
    DemoEntityId src = 0u;

    item_begin("8 倒下学生 ready=false 且 fire_try=false");

    reset_world_state();
    s.alive = false;
    s.hp = 0;
    check(!student_fire_ready(&s, 0), "倒下学生 ready(cooldown=0) == false");
    check(!student_fire_ready(&s, -5), "倒下学生 ready(负冷却) == false");
    check(!student_fire_try(&g_cfg, &s, 500.0f, 100.0f, &g_pool, 0, &cd, &src),
          "倒下学生 fire_try == false");
    check_u32(cd, 0u, "倒下学生不设置冷却");
    check_u32(count_active(&g_pool), 0u, "倒下学生不写弹池");
    check_u32(src, 0u, "倒下学生不写 out_bullet_source");

    check(!student_fire_ready(NULL, 0), "student_fire_ready(NULL) == false");
    check(!student_fire_try(&g_cfg, NULL, 500.0f, 100.0f, &g_pool, 0, &cd, &src),
          "student == NULL 时 fire_try == false");

    item_end("8 倒下学生");
}

/* ---------------------------------------------------------------- 9 弹属性 */

static void test_bullet_attributes(void)
{
    Actor s = make_student(100.0f, 100.0f);
    uint32_t cd = 0u;
    DemoEntityId src = 0u;
    const Projectile *p;
    float expect_ox;
    float expect_oy;

    item_begin("9 弹属性与配置一致且 plan_id == 0");

    reset_world_state();
    /* 用非默认的配置数值, 证明属性确实读 cfg, 而不是写死常量。 */
    g_cfg.student_bullet_speed = 333.0f;
    g_cfg.student_bullet_radius = 9.0f;
    g_cfg.student_bullet_lifetime_ticks = 123;
    g_cfg.student_fire_damage = 4;
    g_cfg.student_fire_interval_ticks = 77;
    g_cfg.student_fire_min_range = 60.0f;

    check(student_fire_try(&g_cfg, &s, 500.0f, 100.0f, &g_pool, 42, &cd, &src), "发射成功");
    check_u32(src, 7u, "out_bullet_source == student->id");
    check_u32(cd, 77u, "冷却读 cfg.student_fire_interval_ticks(77)");

    p = first_active(&g_pool);
    check(p != NULL, "能找到新增的学生弹");
    if (p != NULL) {
        check(p->faction == DEMO_FACTION_STUDENT, "faction == DEMO_FACTION_STUDENT");
        check(p->source_id == s.id, "source_id == student->id");
        check(p->plan_id == 0u, "plan_id == 0(学生弹)");
        check(p->source_pattern == (DemoPattern)0, "source_pattern == 0(学生弹无招式归属)");
        check_near(p->radius, g_cfg.student_bullet_radius, 1e-6f, "radius 读 cfg");
        check_near(p->damage, (float)g_cfg.student_fire_damage, 1e-6f, "damage 读 cfg");
        check_i32(p->lifetime_ticks, g_cfg.student_bullet_lifetime_ticks, "lifetime 读 cfg");
        check_near(vec_len(p->vx, p->vy), g_cfg.student_bullet_speed, 1e-3f, "speed 读 cfg");
        /* 起点从学生位置沿瞄准方向外推 student->radius + 弹半径。 */
        expect_ox = 100.0f + (s.radius + g_cfg.student_bullet_radius);
        expect_oy = 100.0f;
        check_near(p->x, expect_ox, 1e-4f, "起点 x = 学生 x + (学生半径 + 弹半径)");
        check_near(p->y, expect_oy, 1e-4f, "起点 y = 学生 y(沿 +x 外推不改变 y)");
        check_near(p->px, p->x, 0.0f, "spawn 时 px == x(本 tick 起点)");
        check_near(p->py, p->y, 0.0f, "spawn 时 py == y(本 tick 起点)");
        check(p->id != 0u, "弹 id 已分配(非 0)");
        check(p->x > s.x, "起点在学生中心之外(不生成在学生体内)");
    }

    /* 配置版本 3 的默认值复核(独立于上面的自定义值)。 */
    reset_world_state();
    check_u32(g_cfg.version, EXPECT_VERSION, "配置版本 == 6");
    check_i32(g_cfg.student_fire_interval_ticks, EXPECT_INTERVAL, "默认发射间隔 == 78");
    check_near(g_cfg.student_bullet_speed, EXPECT_BULLET_SPEED, 1e-6f, "默认弹速 == 260");
    check_near(g_cfg.student_bullet_radius, EXPECT_BULLET_RADIUS, 1e-6f, "默认弹半径 == 5");
    check_i32(g_cfg.student_bullet_lifetime_ticks, EXPECT_BULLET_LIFETIME, "默认弹寿命 == 240");
    check_i32(g_cfg.student_fire_damage, EXPECT_FIRE_DAMAGE, "默认伤害 == 1");

    item_end("9 弹属性");
}

/* ---------------------------------------------------------------- 10 清计划不误伤 */

static void test_clear_plan_keeps_student_bullets(void)
{
    Actor s = make_student(100.0f, 100.0f);
    uint32_t cd = 0u;
    DemoEntityId src = 0u;

    item_begin("10 pool_clear_plan(pool,1) 后学生弹仍在");

    reset_world_state();
    check(student_fire_try(&g_cfg, &s, 500.0f, 100.0f, &g_pool, 0, &cd, &src), "学生弹发射成功");
    /* 再放一发属于 plan 1 的 Boss 弹, 用于证明清弹本身生效(不是"什么都没清")。 */
    check(pool_spawn(&g_pool, DEMO_FACTION_BOSS, 1u, UINT64_C(1), DEMO_PATTERN_RING, 400.0f,
                     400.0f, 0.0f, 10.0f, 6.0f, 1.0f, 480, 0u),
          "Boss 弹(plan_id = 1)生成成功");
    check_u32(g_pool.live_count, 2u, "池内共 2 发弹");

    check_u32(pool_clear_plan(&g_pool, 0u), 0u, "pool_clear_plan(pool, 0) 返回 0(不清任何弹)");
    check_u32(g_pool.live_count, 2u, "plan 0 清弹后池内仍是 2 发弹");
    check_u32(pool_clear_plan(&g_pool, UINT64_C(1)), 1u, "pool_clear_plan(pool, 1) 清除 1 发");
    check_u32(g_pool.live_count, 1u, "清 plan 1 后池内剩 1 发");
    check_u32(pool_count_faction(&g_pool, DEMO_FACTION_STUDENT), 1u, "剩下的正是学生弹");
    check_u32(pool_count_faction(&g_pool, DEMO_FACTION_BOSS), 0u, "Boss 弹已被清除");

    {
        const Projectile *p = first_active(&g_pool);

        check(p != NULL && p->faction == DEMO_FACTION_STUDENT && p->plan_id == 0u,
              "剩余弹的 faction/plan_id 仍是学生弹");
    }

    item_end("10 清计划不误伤");
}

/* ---------------------------------------------------------------- 11 非法参数 */

static void test_illegal_args(void)
{
    Actor s = make_student(100.0f, 100.0f);
    uint32_t cd;
    DemoEntityId src;

    item_begin("11 非法参数防御且池内弹数不变");

    reset_world_state();
    {
        ProjectilePool *pool_ptr = &g_pool;
        DemoConfig *cfg_ptr = &g_cfg;
        float tx = 500.0f;
        float ty = 100.0f;

        cd = 9u;
        src = 123u;
        check(!student_fire_try(NULL, &s, tx, ty, pool_ptr, 0, &cd, &src), "cfg == NULL -> false");
        check(!student_fire_try(cfg_ptr, NULL, tx, ty, pool_ptr, 0, &cd, &src), "student == NULL -> false");
        check(!student_fire_try(cfg_ptr, &s, tx, ty, NULL, 0, &cd, &src), "pool == NULL -> false");
        check(!student_fire_try(cfg_ptr, &s, tx, ty, pool_ptr, 0, NULL, &src),
              "inout_cooldown == NULL -> false");
        check(!student_fire_try(cfg_ptr, &s, tx, ty, pool_ptr, 0, &cd, NULL),
              "out_bullet_source == NULL -> false");

        check_u32(count_active(&g_pool), 0u, "空指针分支不写弹池");
        check_u32(cd, 9u, "空指针分支不改冷却");
        check_u32(src, 123u, "空指针分支不改 out_bullet_source");
    }

    /* 非法配置数值: speed <= 0 / radius < 0 / lifetime <= 0。 */
    {
        DemoConfig bad;

        bad = g_cfg;
        bad.student_bullet_speed = 0.0f;
        cd = 9u;
        src = 123u;
        check(!student_fire_try(&bad, &s, 500.0f, 100.0f, &g_pool, 0, &cd, &src),
              "speed == 0 -> false");
        bad.student_bullet_speed = -1.0f;
        check(!student_fire_try(&bad, &s, 500.0f, 100.0f, &g_pool, 0, &cd, &src),
              "speed < 0 -> false");
        bad.student_bullet_speed = NAN;
        check(!student_fire_try(&bad, &s, 500.0f, 100.0f, &g_pool, 0, &cd, &src),
              "speed == NaN -> false");

        bad = g_cfg;
        bad.student_bullet_radius = -1.0f;
        check(!student_fire_try(&bad, &s, 500.0f, 100.0f, &g_pool, 0, &cd, &src),
              "radius < 0 -> false");
        bad.student_bullet_radius = NAN;
        check(!student_fire_try(&bad, &s, 500.0f, 100.0f, &g_pool, 0, &cd, &src),
              "radius == NaN -> false");
        /* radius == 0 是合法值(接口只禁止 < 0)。 */
        bad.student_bullet_radius = 0.0f;
        check(student_fire_try(&bad, &s, 500.0f, 100.0f, &g_pool, 0, &cd, &src),
              "radius == 0 合法(接口只禁止 < 0)");
        check_u32(count_active(&g_pool), 1u, "radius == 0 的那发正常进池");
        check_u32(pool_clear_plan(&g_pool, 0u), 0u, "plan 0 清弹返回 0");
        /* 手工移除该弹, 恢复空池用于后续用例。 */
        for (uint32_t i = 0u; i < DEMO_MAX_PROJECTILES; ++i) {
            if (g_pool.items[i].active) {
                g_pool.items[i].active = false;
                g_pool.live_count -= 1u;
            }
        }
        check_u32(count_active(&g_pool), 0u, "测试自清理: 池已空");

        /* 上面 radius == 0 的合法用例成功发射过, 会把 src 写成学生 id;
         * 这里重新设置哨兵值, 才能验证接下来的失败分支不写输出参数。 */
        cd = 9u;
        src = 123u;
        bad = g_cfg;
        bad.student_bullet_lifetime_ticks = 0;
        check(!student_fire_try(&bad, &s, 500.0f, 100.0f, &g_pool, 0, &cd, &src),
              "lifetime == 0 -> false");
        bad.student_bullet_lifetime_ticks = -3;
        check(!student_fire_try(&bad, &s, 500.0f, 100.0f, &g_pool, 0, &cd, &src),
              "lifetime < 0 -> false");

        check_u32(count_active(&g_pool), 0u, "非法配置分支不写弹池");
        check_u32(cd, 9u, "非法配置分支不改冷却");
        check_u32(src, 123u, "非法配置分支不改 out_bullet_source");
        check_u32(g_pool.overflow_events, 0u, "非法配置不计入 overflow(池并未满)");
    }

    /* 弹池容量不足: 返回 false, 不越界、不覆盖已激活弹、不改冷却。 */
    {
        ProjectilePool tiny;

        pool_init(&tiny, 1u);
        cd = 9u;
        src = 123u;
        check(student_fire_try(&g_cfg, &s, 500.0f, 100.0f, &tiny, 0, &cd, &src), "小池第 1 发成功");
        check_u32(cd, (uint32_t)g_cfg.student_fire_interval_ticks, "第 1 发后冷却被设置");
        cd = 9u;
        src = 123u; /* 重置哨兵: 第 1 发已把 src 写成学生 id */
        check(!student_fire_try(&g_cfg, &s, 500.0f, 100.0f, &tiny, 0, &cd, &src),
              "池满时第 2 发返回 false");
        check_u32(tiny.live_count, 1u, "池满失败后池内弹数不变");
        check_u32(tiny.overflow_events, 1u, "池满失败被计入 overflow_events");
        check_u32(cd, 9u, "池满失败不改冷却");
        check_u32(src, 123u, "池满失败不改 out_bullet_source");
    }

    item_end("11 非法参数");
}

/* ---------------------------------------------------------------- main */

int main(void)
{
    printf("test_student_fire (S12 学生反击) - 接口版本 1 / 配置版本 1\n");

    test_straight_aim();
    test_diagonal_aim();
    test_no_homing();
    test_aim_separate_from_move();
    test_cooldown();
    test_min_range();
    test_zero_distance();
    test_dead_student();
    test_bullet_attributes();
    test_clear_plan_keeps_student_bullets();
    test_illegal_args();

    printf("\n============================================================\n");
    printf("总计: %d 项子检查, %d 项失败 -> %s\n", g_checks, g_failed,
           (g_failed == 0) ? "全部 PASS" : "存在 FAIL");
    printf("============================================================\n");
    return (g_failed == 0) ? 0 : 1;
}
