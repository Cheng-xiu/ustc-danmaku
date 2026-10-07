/* test_attack.c - core/attack.c 的窄范围单元测试
 *
 * 覆盖: 拒绝原因/能量不变、忙碌只扣一次、无目标、原子接受、锁定不迁移、
 *       预警边界、结束边界、同 tick 只接受一次、拒绝不排队、结束清弹。
 *
 * 说明(重要): 本测试自带 main(), 为独立运行做了两处测试内桩:
 *   1) pattern_vtable / pattern_name: 测试内实现, 返回四招的固定计划桩,
 *      因此 **本次未链接 core/patterns.c, 未验证真实招式几何**。
 *   2) events_init / events_push: 仓库当前尚无实现(core/world.c 只调用), 测试内给出等价实现。
 * 真实模块 rng.c / projectiles.c / demo_config.c 直接编译进本测试并被使用。
 */
#include <stdio.h>
#include <string.h>

#include "attack.h"
#include "patterns.h"

/* ---------------------------------------------------------------- 测试计数 */

static int g_pass = 0;
static int g_fail = 0;

static void check(const char *name, bool ok) {
    if (ok) {
        g_pass++;
        printf("PASS  %s\n", name);
    } else {
        g_fail++;
        printf("FAIL  %s\n", name);
    }
}

/* ---------------------------------------------------------------- 测试内桩 */

static bool g_stub_fail_make_plan = false;

static bool stub_make_plan(const PatternRequest *request, const DemoConfig *config, Rng *rng,
                           AttackPlan *out) {
    (void)rng;
    if (request == NULL || config == NULL || out == NULL) {
        return false;
    }
    if (g_stub_fail_make_plan) {
        return false;
    }
    const int p = (int)request->pattern;
    if (p < 0 || p >= (int)DEMO_PATTERN_COUNT) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->pattern = request->pattern;
    out->target_id = request->target_id;
    out->origin_x = request->origin_x;
    out->origin_y = request->origin_y;
    out->aim_x = request->target_x;
    out->aim_y = request->target_y;
    out->start_tick = request->start_tick;
    out->windup_ticks = config->patterns[p].windup_ticks;
    out->active_ticks = config->patterns[p].active_ticks;
    out->lock_speed = config->patterns[p].bullet_speed;
    out->wave_count = 1;
    out->shots_per_wave = 1;
    out->geometry_seed = 0x5A5A5A5Au; /* 固定几何: 测试可复现 */
    return true;
}

/* 只在"攻击阶段第一 tick"(start + windup) 生成 1 发, 其余 tick 返回 false。
 * 这样"波次数量"与"边界"都可精确断言(多/少一波都会失败)。 */
static bool stub_emit(const AttackPlan *plan, const DemoConfig *config, uint32_t attack_tick,
                      ProjectileSpawnBuffer *out) {
    (void)config;
    if (plan == NULL || out == NULL) {
        return false;
    }
    /* emit 参数是攻击期相对 tick；第一波在0，不是世界绝对时间。 */
    if (attack_tick != 0u) {
        return false;
    }
    Projectile spec;
    memset(&spec, 0, sizeof(spec));
    spec.faction = DEMO_FACTION_BOSS;
    spec.source_id = plan->target_id;
    spec.plan_id = plan->plan_id;
    spec.source_pattern = plan->pattern;
    spec.x = plan->origin_x;
    spec.y = plan->origin_y;
    spec.vx = 0.0f;
    spec.vy = 100.0f;
    spec.radius = config != NULL ? config->boss_bullet_radius : 6.0f;
    spec.damage = 1.0f;
    spec.lifetime_ticks = 480;
    return spawn_buffer_push(out, &spec);
}

static void stub_warning(const AttackPlan *plan, const DemoConfig *config, PatternWarning *out) {
    (void)plan;
    (void)config;
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
}

static const PatternVTable kVt[DEMO_PATTERN_COUNT] = {
    {"stub-ring", stub_make_plan, stub_emit, stub_warning},
    {"stub-course", stub_make_plan, stub_emit, stub_warning},
    {"stub-mine", stub_make_plan, stub_emit, stub_warning},
    {"stub-shower", stub_make_plan, stub_emit, stub_warning},
};

/* 测试内注册表: 不链接 core/patterns.c(其表引用真实招式符号) */
const PatternVTable *pattern_vtable(DemoPattern pattern) {
    if ((int)pattern < 0 || (int)pattern >= (int)DEMO_PATTERN_COUNT) {
        return NULL;
    }
    return &kVt[(int)pattern];
}

const char *pattern_name(DemoPattern pattern) {
    const PatternVTable *vt = pattern_vtable(pattern);
    return (vt != NULL) ? vt->name : "unknown";
}

/* events_init/events_push: 仓库中尚无实现, 测试内给出等价语义 */
void events_init(StepEvents *events) {
    if (events == NULL) {
        return;
    }
    events->count = 0u;
    events->capacity = DEMO_MAX_STEP_EVENTS;
    events->dropped = 0u;
}

bool events_push(StepEvents *events, const StepEvent *event) {
    if (events == NULL || event == NULL) {
        return false;
    }
    if (events->count >= DEMO_MAX_STEP_EVENTS) {
        return false;
    }
    events->items[events->count] = *event;
    events->count++;
    return true;
}

/* ---------------------------------------------------------------- 测试夹具 */

#define TEST_SEED 20261006u

/* 不链接 core/world.c(它依赖 student_bot/student_fire), 因此手工构造 World。 */
static void world_setup(World *w) {
    DemoConfig cfg;
    if (!demo_config_init(&cfg)) {
        printf("FATAL demo_config_init failed\n");
        return;
    }
    memset(w, 0, sizeof(*w));
    w->cfg = cfg;
    w->seed = TEST_SEED;
    w->tick = 0;
    w->status = DEMO_STATUS_RUNNING;
    rng_seed(&w->world_rng, TEST_SEED, 1u);
    rng_seed(&w->bot_rng, TEST_SEED, 2u);
    rng_seed(&w->explore_rng, TEST_SEED, 3u);

    w->boss.id = 1u;
    w->boss.alive = true;
    w->boss.x = 480.0f;
    w->boss.y = 620.0f;
    w->boss.radius = cfg.boss_radius;
    w->boss.hp = cfg.boss_hp;
    w->boss.hp_max = cfg.boss_hp;

    w->student_count = cfg.student_count;
    for (uint32_t i = 0u; i < w->student_count; ++i) {
        Actor *s = &w->students[i];
        s->id = 100u + i;
        s->alive = true;
        s->x = cfg.student_spawn_x[i];
        s->y = cfg.student_spawn_y[i];
        s->radius = cfg.student_radius;
        s->hp = cfg.student_hp;
        s->hp_max = cfg.student_hp;
    }

    pool_init(&w->pool, cfg.projectile_cap);
    w->energy = cfg.energy_start;
    w->attack_state = DEMO_ATTACK_IDLE;
    w->next_plan_id = 1u;
    events_init(&w->events);
    g_stub_fail_make_plan = false;
}

/* 处理当前 tick 的 attack_step, 然后进入下一 tick(与 world_step 的调用顺序一致:
 * 同 tick 先接受请求, 再 attack_step, 最后 tick++) */
static int32_t tick_advance(World *w) {
    const int32_t processed = w->tick;
    attack_step(w);
    w->tick++;
    return processed;
}

static void advance_ticks(World *w, int32_t n) {
    for (int32_t i = 0; i < n; ++i) {
        (void)tick_advance(w);
    }
}

static int event_count(const World *w, DemoEventType type) {
    int n = 0;
    for (uint32_t i = 0u; i < w->events.count; ++i) {
        if (w->events.items[i].type == type) {
            n++;
        }
    }
    return n;
}

static int event_count_at_tick(const World *w, DemoEventType type, int32_t tick) {
    int n = 0;
    for (uint32_t i = 0u; i < w->events.count; ++i) {
        if (w->events.items[i].type == type && w->events.items[i].tick == tick) {
            n++;
        }
    }
    return n;
}

static uint32_t pool_active_with_plan(const World *w, uint64_t plan_id) {
    uint32_t n = 0u;
    for (uint32_t i = 0u; i < w->pool.capacity && i < DEMO_MAX_PROJECTILES; ++i) {
        if (w->pool.items[i].active && w->pool.items[i].plan_id == plan_id) {
            n++;
        }
    }
    return n;
}

static int32_t ring_cost(const World *w) { return w->cfg.patterns[DEMO_PATTERN_RING].cost; }

/* ---------------------------------------------------------------- a..j */

/* a) 能量不足: 拒绝, 能量不变, 状态 IDLE, reason == NO_ENERGY */
static void test_a_no_energy(void) {
    World w;
    world_setup(&w);
    w.energy = ring_cost(&w) - 1;
    const int32_t before = w.energy;
    const uint64_t before_plan_id = w.next_plan_id;
    DemoRejectReason reason = DEMO_REJECT_NONE;

    const bool ok = attack_try_request(&w, DEMO_PATTERN_RING, &reason);

    check("a1 能量不足返回 false", ok == false);
    check("a2 reason == DEMO_REJECT_NO_ENERGY", reason == DEMO_REJECT_NO_ENERGY);
    check("a3 能量不变", w.energy == before);
    check("a4 状态仍 IDLE", w.attack_state == DEMO_ATTACK_IDLE);
    check("a5 plan 未激活", w.plan.active == false);
    check("a6 next_plan_id 不变", w.next_plan_id == before_plan_id);
    check("a7 未排队/无波次事件", event_count(&w, DEMO_EVENT_WAVE_SPAWN) == 0);
}

/* b) 忙碌: 接受一次后立刻再请求 → BUSY, 能量只扣一次 */
static void test_b_busy_single_deduction(void) {
    World w;
    world_setup(&w);
    const int32_t start_energy = w.energy;
    const int32_t cost = ring_cost(&w);
    DemoRejectReason reason = DEMO_REJECT_NONE;

    const bool first = attack_try_request(&w, DEMO_PATTERN_RING, &reason);
    const int32_t after_first = w.energy;
    const uint64_t plan_id_after_first = w.plan.plan_id;

    const bool second = attack_try_request(&w, DEMO_PATTERN_RING, &reason);

    check("b1 第一次接受", first == true);
    check("b2 第一次能量恰好减 cost", after_first == start_energy - cost);
    check("b3 第二次返回 false", second == false);
    check("b4 第二次 reason == DEMO_REJECT_BUSY", reason == DEMO_REJECT_BUSY);
    check("b5 能量只扣一次", w.energy == start_energy - cost);
    check("b6 状态仍 WINDUP(未被再次接受)", w.attack_state == DEMO_ATTACK_WINDUP);
    check("b7 plan_id 未被覆盖", w.plan.plan_id == plan_id_after_first);
    check("b8 next_plan_id 只前进一次", w.next_plan_id == plan_id_after_first + 1u);
}

/* c) 无目标: 全部学生 alive=false → NO_TARGET 且能量不变 */
static void test_c_no_target(void) {
    World w;
    world_setup(&w);
    for (uint32_t i = 0u; i < w.student_count; ++i) {
        w.students[i].alive = false;
    }
    const int32_t before = w.energy;
    DemoRejectReason reason = DEMO_REJECT_NONE;

    const bool ok = attack_try_request(&w, DEMO_PATTERN_SHOWER, &reason);

    check("c1 无目标返回 false", ok == false);
    check("c2 reason == DEMO_REJECT_NO_TARGET", reason == DEMO_REJECT_NO_TARGET);
    check("c3 能量不变", w.energy == before);
    check("c4 状态仍 IDLE", w.attack_state == DEMO_ATTACK_IDLE);
}

/* d) 原子接受: 能量恰好减 cost、plan.active、target 为最近学生、origin 为接受时 Boss 位置 */
static void test_d_atomic_accept(void) {
    World w;
    world_setup(&w);
    /* 两个等距学生(id 100/101)验证同距取 id 小者; 另有一个更远的 id 102 */
    w.students[0].x = 400.0f;
    w.students[0].y = 620.0f;
    w.students[1].x = 560.0f;
    w.students[1].y = 620.0f;
    w.students[2].x = 480.0f;
    w.students[2].y = 200.0f;
    const float boss_x = 300.0f;
    const float boss_y = 450.0f;
    w.boss.x = boss_x;
    w.boss.y = boss_y;
    w.tick = 17;
    const int32_t start_energy = w.energy;
    const int32_t cost = ring_cost(&w);
    const uint64_t before_plan_id = w.next_plan_id;
    DemoRejectReason reason = DEMO_REJECT_NONE;

    const bool ok = attack_try_request(&w, DEMO_PATTERN_RING, &reason);

    check("d1 接受成功", ok == true);
    check("d2 reason == DEMO_REJECT_NONE", reason == DEMO_REJECT_NONE);
    check("d3 能量恰好减 cost", w.energy == start_energy - cost);
    check("d4 plan.active == true", w.plan.active == true);
    check("d5 plan_id == next_plan_id(接受前)", w.plan.plan_id == before_plan_id);
    check("d6 next_plan_id 递增 1", w.next_plan_id == before_plan_id + 1u);
    check("d7 target_id 为最近学生(同距取 id 小者 100)", w.plan.target_id == 100u);
    check("d8 origin 等于接受时 Boss 位置", w.plan.origin_x == boss_x && w.plan.origin_y == boss_y);
    check("d9 start_tick == 接受 tick", w.plan.start_tick == 17);
    check("d10 状态进入 WINDUP", w.attack_state == DEMO_ATTACK_WINDUP);
    check("d11 写 WINDUP_START 事件", event_count_at_tick(&w, DEMO_EVENT_WINDUP_START, 17) == 1);

    /* make_plan 失败: 不扣能量、不改状态、不排队, reason == NONE */
    World w2;
    world_setup(&w2);
    const int32_t e2 = w2.energy;
    const uint64_t p2 = w2.next_plan_id;
    g_stub_fail_make_plan = true;
    DemoRejectReason r2 = DEMO_REJECT_BUSY;
    const bool ok2 = attack_try_request(&w2, DEMO_PATTERN_RING, &r2);
    g_stub_fail_make_plan = false;
    check("d12 make_plan 失败返回 false", ok2 == false);
    check("d13 make_plan 失败 reason == NONE", r2 == DEMO_REJECT_NONE);
    check("d14 make_plan 失败能量不变", w2.energy == e2);
    check("d15 make_plan 失败状态仍 IDLE 且 next_plan_id 不变",
          w2.attack_state == DEMO_ATTACK_IDLE && w2.next_plan_id == p2);
}

/* e) 锁定不迁移: 接受后移动 Boss 与目标学生 100 tick, origin/target_id 不变 */
static void test_e_lock_does_not_migrate(void) {
    World w;
    world_setup(&w);
    /* 本用例隔离锁定几何；v3 淋浴必须先有 100 能量才可接受。 */
    w.energy = w.cfg.energy_max;
    DemoRejectReason reason = DEMO_REJECT_NONE;
    check("e0 满能量淋浴请求接受", attack_try_request(&w, DEMO_PATTERN_SHOWER, &reason));

    const float ox = w.plan.origin_x;
    const float oy = w.plan.origin_y;
    const DemoEntityId tid = w.plan.target_id;
    const float aimx = w.plan.aim_x;
    const float aimy = w.plan.aim_y;

    for (int i = 0; i < 100; ++i) {
        w.boss.x += 2.0f;
        w.boss.y -= 1.0f;
        for (uint32_t s = 0u; s < w.student_count; ++s) {
            w.students[s].x += 3.0f;
            w.students[s].y += 1.0f;
        }
        (void)tick_advance(&w);
    }

    check("e1 origin_x 不迁移", w.plan.origin_x == ox);
    check("e2 origin_y 不迁移", w.plan.origin_y == oy);
    check("e3 aim 点不迁移", w.plan.aim_x == aimx && w.plan.aim_y == aimy);
    check("e4 target_id 不迁移", w.plan.target_id == tid);
    check("e5 100 tick 后仍在同一计划内", w.plan.active == true &&
                                               w.attack_state != DEMO_ATTACK_IDLE);
}

/* f) 预警边界: start+windup-1 仍 WINDUP, start+windup 为 ACTIVE(且该 tick 恰好 1 波) */
static void test_f_windup_boundary(void) {
    World w;
    world_setup(&w);
    const int32_t windup = w.cfg.patterns[DEMO_PATTERN_RING].windup_ticks;
    DemoRejectReason reason = DEMO_REJECT_NONE;
    (void)attack_try_request(&w, DEMO_PATTERN_RING, &reason);
    const int32_t start = w.plan.start_tick;

    /* 处理 tick 0..(start+windup-1): 共 start+windup 次调用 */
    advance_ticks(&w, start + windup);
    check("f1 start+windup-1 仍 WINDUP", w.attack_state == DEMO_ATTACK_WINDUP);
    check("f2 预警期间无 WAVE_SPAWN", event_count(&w, DEMO_EVENT_WAVE_SPAWN) == 0);

    /* 处理 tick start+windup: 恰好切换 ACTIVE */
    const int32_t t_active = tick_advance(&w);
    check("f3 边界 tick == start+windup", t_active == start + windup);
    check("f4 start+windup 为 ACTIVE", w.attack_state == DEMO_ATTACK_ACTIVE);
    check("f5 切换 tick 写 1 个 ATTACK_START 事件",
          event_count_at_tick(&w, DEMO_EVENT_ATTACK_START, t_active) == 1);
    check("f6 第一波恰好 1 波", event_count_at_tick(&w, DEMO_EVENT_WAVE_SPAWN, t_active) == 1);
    {
        printf("  [dbg] t_active=%d start=%d windup=%d state=%d progress=%.4f events=%u\n",
               (int)t_active, (int)start, (int)windup, (int)w.attack_state,
               (double)attack_progress(&w), (unsigned)w.events.count);
        for (uint32_t i = 0u; i < w.events.count; ++i) {
            printf("  [dbg] ev[%u] type=%d tick=%d amount=%d\n", (unsigned)i,
                   (int)w.events.items[i].type, (int)w.events.items[i].tick,
                   (int)w.events.items[i].amount);
        }
    }
    /* tick_advance 已将 world.tick 从边界递增一次，读到的是第一个完成tick。 */
    check("f7 首个 ACTIVE tick 完成后的进度", attack_progress(&w) == 1.0f + 1.0f / (float)w.plan.active_ticks);
}

/* g) 结束边界: start+windup+active 回到 IDLE, 且该 tick 无 WAVE_SPAWN */
static void test_g_end_boundary(void) {
    World w;
    world_setup(&w);
    const int32_t windup = w.cfg.patterns[DEMO_PATTERN_RING].windup_ticks;
    const int32_t active = w.cfg.patterns[DEMO_PATTERN_RING].active_ticks;
    DemoRejectReason reason = DEMO_REJECT_NONE;
    (void)attack_try_request(&w, DEMO_PATTERN_RING, &reason);

    const int32_t end_tick = w.plan.start_tick + windup + active;
    /* 处理 tick 0..(end_tick-1) */
    advance_ticks(&w, end_tick);
    check("g1 结束前一 tick 仍 ACTIVE", w.attack_state == DEMO_ATTACK_ACTIVE);

    const int32_t t_end = tick_advance(&w);
    check("g2 边界 tick == start+windup+active", t_end == end_tick);
    check("g3 结束 tick 回到 IDLE", w.attack_state == DEMO_ATTACK_IDLE);
    check("g4 结束 tick 无 WAVE_SPAWN", event_count_at_tick(&w, DEMO_EVENT_WAVE_SPAWN, t_end) == 0);
    check("g5 plan.active == false", w.plan.active == false);
    check("g6 全程恰好 1 波", event_count(&w, DEMO_EVENT_WAVE_SPAWN) == 1);
    check("g7 结束后 attack_progress == 0", attack_progress(&w) == 0.0f);

    /* 结束后继续推进不得复活该计划 */
    advance_ticks(&w, 50);
    check("g8 结束后不再产生波次", event_count(&w, DEMO_EVENT_WAVE_SPAWN) == 1);
    check("g9 结束后保持 IDLE", w.attack_state == DEMO_ATTACK_IDLE);
}

/* h) 同 tick 连续 3 次请求只接受 1 次 */
static void test_h_three_requests_one_accept(void) {
    World w;
    world_setup(&w);
    const int32_t start_energy = w.energy;
    const int32_t cost = ring_cost(&w);
    const uint64_t before_plan_id = w.next_plan_id;
    int accepted = 0;
    int busy = 0;

    for (int i = 0; i < 3; ++i) {
        DemoRejectReason reason = DEMO_REJECT_NONE;
        if (attack_try_request(&w, DEMO_PATTERN_RING, &reason)) {
            accepted++;
        } else if (reason == DEMO_REJECT_BUSY) {
            busy++;
        }
    }

    check("h1 三次请求只接受 1 次", accepted == 1);
    check("h2 其余 2 次为 BUSY", busy == 2);
    check("h3 能量只扣一次", w.energy == start_energy - cost);
    check("h4 next_plan_id 只前进 1", w.next_plan_id == before_plan_id + 1u);
    check("h5 只有 1 个 WINDUP_START 事件", event_count(&w, DEMO_EVENT_WINDUP_START) == 1);
}

/* i) 拒绝不排队: 推进 100 tick 不自动补发 */
static void test_i_reject_does_not_queue(void) {
    World w;
    world_setup(&w);
    w.energy = 0;
    DemoRejectReason reason = DEMO_REJECT_NONE;
    const bool ok = attack_try_request(&w, DEMO_PATTERN_RING, &reason);

    check("i1 低能量请求被拒", ok == false && reason == DEMO_REJECT_NO_ENERGY);

    advance_ticks(&w, 100);
    check("i2 100 tick 后仍 IDLE", w.attack_state == DEMO_ATTACK_IDLE);
    check("i3 100 tick 内无波次", event_count(&w, DEMO_EVENT_WAVE_SPAWN) == 0);
    check("i4 无 Boss 弹生成", pool_active_with_plan(&w, 0u) == 0u);
    check("i5 next_plan_id 未推进", w.next_plan_id == 1u);

    /* 即使能量被补满(模拟回复), 被拒请求也不得自动补发 */
    w.energy = w.cfg.energy_max;
    advance_ticks(&w, 100);
    check("i6 能量补满后仍不自动出招", w.attack_state == DEMO_ATTACK_IDLE);
    check("i7 仍无波次", event_count(&w, DEMO_EVENT_WAVE_SPAWN) == 0);
    check("i8 plan 仍非激活", w.plan.active == false);
}

/* j) 攻击结束清弹: 本 plan_id 的 Boss 弹被清, plan_id==0 的学生弹仍在 */
static void test_j_clear_on_end(void) {
    World w;
    world_setup(&w);
    DemoRejectReason reason = DEMO_REJECT_NONE;
    (void)attack_try_request(&w, DEMO_PATTERN_RING, &reason);
    const uint64_t plan_id = w.plan.plan_id;

    /* 本招 Boss 弹 3 发 */
    for (int i = 0; i < 3; ++i) {
        const bool ok = pool_spawn(&w.pool, DEMO_FACTION_BOSS, w.boss.id, plan_id,
                                   DEMO_PATTERN_RING, 100.0f + (float)i, 200.0f, 0.0f, 60.0f,
                                   6.0f, 1.0f, 480, (uint64_t)w.tick);
        (void)ok;
    }
    /* 学生反击弹 2 发: plan_id == 0 */
    for (int i = 0; i < 2; ++i) {
        const bool ok = pool_spawn(&w.pool, DEMO_FACTION_STUDENT, 100u, 0u, (DemoPattern)0,
                                   300.0f + (float)i, 400.0f, 0.0f, -60.0f, 5.0f, 1.0f, 240,
                                   (uint64_t)w.tick);
        (void)ok;
    }
    check("j1 结束前本招 Boss 弹为 3", pool_active_with_plan(&w, plan_id) == 3u);
    check("j2 结束前学生弹(plan_id 0)为 2", pool_active_with_plan(&w, 0u) == 2u);

    const int32_t windup = w.cfg.patterns[DEMO_PATTERN_RING].windup_ticks;
    const int32_t active = w.cfg.patterns[DEMO_PATTERN_RING].active_ticks;
    advance_ticks(&w, windup + active + 1);

    check("j3 结束时回到 IDLE", w.attack_state == DEMO_ATTACK_IDLE);
    check("j4 本 plan_id 的 Boss 弹被清空", pool_active_with_plan(&w, plan_id) == 0u);
    check("j5 plan_id==0 的学生弹仍在(2 发)", pool_active_with_plan(&w, 0u) == 2u);

    /* 关闭清弹配置时不清: 同一计划设定下重跑一次 */
    World w2;
    world_setup(&w2);
    w2.cfg.boss_bullet_clear_on_attack_end = 0;
    DemoRejectReason r2 = DEMO_REJECT_NONE;
    const bool ok2 = attack_try_request(&w2, DEMO_PATTERN_RING, &r2);
    const uint64_t plan_id2 = w2.plan.plan_id;
    pool_spawn(&w2.pool, DEMO_FACTION_BOSS, w2.boss.id, plan_id2, DEMO_PATTERN_RING, 100.0f,
               200.0f, 0.0f, 60.0f, 6.0f, 1.0f, 480, (uint64_t)w2.tick);
    advance_ticks(&w2, windup + active + 1);
    /* 同一计划还会生成一发桩弹；精确检查手工植入的弹，不能断言总数为1。 */
    bool implanted_survives = false;
    for (uint32_t i = 0; i < w2.pool.capacity; i++) {
        const Projectile *p = &w2.pool.items[i];
        if (p->active && p->plan_id == plan_id2 && p->x == 100.0f && p->y == 200.0f)
            implanted_survives = true;
    }
    check("j6 关闭清弹配置时 Boss 弹保留", ok2 && implanted_survives);
}

/* 附加: attack_progress 形状与 attack_step 空指针安全 */
static void test_k_progress_and_null(void) {
    World w;
    world_setup(&w);
    check("k1 NULL world 返回 0", attack_progress(NULL) == 0.0f);
    check("k2 IDLE 返回 0", attack_progress(&w) == 0.0f);

    DemoRejectReason reason = DEMO_REJECT_NONE;
    (void)attack_try_request(&w, DEMO_PATTERN_RING, &reason);
    const int32_t windup = w.plan.windup_ticks;
    const int32_t active = w.plan.active_ticks;

    advance_ticks(&w, windup / 2);
    const float half = attack_progress(&w);
    check("k3 预警中点约 0.5", half > 0.4f && half < 0.6f);

    advance_ticks(&w, (windup - windup / 2) + active / 2);
    const float mid = attack_progress(&w);
    check("k4 攻击中点约 1.5", mid > 1.4f && mid < 1.6f);

    advance_ticks(&w, active);
    check("k5 结束后回到 0", attack_progress(&w) == 0.0f);

    attack_step(NULL); /* 不得崩溃 */
    const bool null_ok = attack_try_request(NULL, DEMO_PATTERN_RING, &reason) == false;
    const bool oob_ok = attack_try_request(&w, (DemoPattern)DEMO_PATTERN_COUNT, &reason) == false;
    check("k6 NULL world / 越界 pattern 安全拒绝", null_ok && oob_ok);
}

int main(void) {
    printf("== test_attack (core/attack.c) ==\n");
    printf("note: pattern vtable + events_* are TEST-LOCAL stubs; core/patterns.c not linked\n");

    test_a_no_energy();
    test_b_busy_single_deduction();
    test_c_no_target();
    test_d_atomic_accept();
    test_e_lock_does_not_migrate();
    test_f_windup_boundary();
    test_g_end_boundary();
    test_h_three_requests_one_accept();
    test_i_reject_does_not_queue();
    test_j_clear_on_end();
    test_k_progress_and_null();

    printf("== summary: %d passed, %d failed ==\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
