/* Web migration regressions against all 14 real C core/AI sources.
 * Fixtures deliberately use large HP, nearly stationary students and long fire
 * intervals. These overrides are test data, not proposed gameplay defaults.
 */
#include "world.h"
#include "attack.h"
#include "patterns.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static unsigned failures;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); } } while (0)

static DemoConfig fixture_config(void) {
    DemoConfig cfg;
    if (!demo_config_init(&cfg)) exit(2);
    cfg.student_count = 1;
    cfg.student_spawn_x[0] = 480.0f;
    cfg.student_spawn_y[0] = 300.0f;
    cfg.boss_hp = 100000;
    cfg.student_hp = 100000;
    cfg.student_speed = 0.0001f;
    cfg.student_fire_interval_ticks = 100000;
    cfg.energy_start = cfg.energy_max;
    cfg.energy_regen_per_sec = 0.0f;
    cfg.max_ticks = 0;
    return cfg;
}

static void reset(World *world, const DemoConfig *cfg) {
    if (!world_reset(world, cfg, UINT64_C(12345))) {
        fprintf(stderr, "invalid regression fixture\n");
        exit(2);
    }
}

static bool event_present(const World *world, DemoEventType type) {
    for (uint32_t i = 0; i < world->events.count; ++i)
        if (world->events.items[i].type == type) return true;
    return false;
}

static const Projectile *find_projectile(const World *world, uint64_t id) {
    for (uint32_t i = 0; i < world->pool.capacity; ++i)
        if (world->pool.items[i].active && world->pool.items[i].id == id)
            return &world->pool.items[i];
    return NULL;
}

static uint64_t add_student_marker(World *world) {
    CHECK(pool_spawn(&world->pool, DEMO_FACTION_STUDENT, 100, 0,
                     DEMO_PATTERN_RING, 900, 690, 0, 0, 5, 1, 100000, 0));
    return world->pool.items[0].id;
}

static void test_wave_schedule(void) {
    const int32_t starts[] = {0, 300, 1000};
    for (int pattern = 0; pattern < DEMO_PATTERN_COUNT; ++pattern) {
        for (unsigned scenario = 0; scenario < 3; ++scenario) {
            DemoConfig cfg = fixture_config();
            World world;
            BossInput input;
            reset(&world, &cfg);
            uint64_t marker_id = add_student_marker(&world);
            while (world.tick < starts[scenario]) world_step(&world, NULL);
            CHECK(world.boss_bullets_spawned == 0);
            boss_input_clear(&input);
            input.attack_requested[pattern] = true;
            world_step(&world, &input);
            CHECK(world.last_result.attack_accepted);
            CHECK(world.plan.start_tick == starts[scenario]);
            const PatternConfig *pc = &cfg.patterns[pattern];
            const int32_t end = starts[scenario] + pc->windup_ticks + pc->active_ticks;
            unsigned waves = 0;
            unsigned spawned = 0;
            while (world.tick <= end) {
                int32_t stepped_tick = world.tick;
                world_step(&world, NULL);
                if (stepped_tick < starts[scenario] + pc->windup_ticks)
                    CHECK(world.boss_bullets_spawned == 0);
                for (uint32_t i = 0; i < world.events.count; ++i) {
                    const StepEvent *ev = &world.events.items[i];
                    if (ev->type != DEMO_EVENT_WAVE_SPAWN) continue;
                    int32_t expected = starts[scenario] + pc->windup_ticks +
                        (int32_t)lroundf((pc->first_spawn_sec +
                            (float)waves * pc->wave_interval_sec) * 60.0f);
                    CHECK(waves < (unsigned)pc->wave_count);
                    CHECK(ev->tick == expected);
                    CHECK(ev->amount > 0);
                    if (pattern == DEMO_PATTERN_RING) {
                        CHECK(ev->amount >= 13 && ev->amount <= 14);
                    } else if (pattern == DEMO_PATTERN_MINE) {
                        CHECK(ev->amount == 3 * pc->shots_per_wave);
                    } else {
                        CHECK(ev->amount == pc->shots_per_wave);
                    }
                    spawned += (unsigned)ev->amount;
                    ++waves;
                }
                CHECK(world.events.dropped == 0);
                CHECK(world.status == DEMO_STATUS_RUNNING);
            }
            CHECK(waves == (unsigned)pc->wave_count);
            CHECK(spawned == world.boss_bullets_spawned);
            CHECK(world.spawn_overflow_count == 0);
            CHECK(world.attack_state == DEMO_ATTACK_IDLE);
            CHECK(!world.plan.active);
            CHECK(pool_count_faction(&world.pool, DEMO_FACTION_BOSS) == 0);
            CHECK(find_projectile(&world, marker_id) != NULL);
        }
    }
}

static void test_geometry_lock(void) {
    for (int pattern = 0; pattern < DEMO_PATTERN_COUNT; ++pattern) {
        DemoConfig cfg = fixture_config();
        cfg.student_speed = 150.0f;
        cfg.student_count = 2;
        cfg.student_spawn_x[0] = 200;
        cfg.student_spawn_y[0] = 300;
        cfg.student_spawn_x[1] = 760;
        cfg.student_spawn_y[1] = 300;
        World world;
        BossInput input;
        reset(&world, &cfg);
        boss_input_clear(&input);
        input.attack_requested[pattern] = true;
        world_step(&world, &input);
        CHECK(world.last_result.attack_accepted);
        AttackPlan locked = world.plan;
        boss_input_clear(&input);
        input.move_x = 1;
        bool student_moved = false;
        for (int step = 0; step < cfg.patterns[pattern].windup_ticks + 10; ++step) {
            if (world.students[0].x != locked.aim_x || world.students[0].y != locked.aim_y)
                student_moved = true;
            /* Another living student keeps the world running after target death. */
            if (step == 20) {
                world.students[0].alive = false;
                world.students[0].hp = 0;
            }
            world_step(&world, &input);
            CHECK(world.plan.active);
            CHECK(world.plan.plan_id == locked.plan_id);
            CHECK(world.plan.target_id == locked.target_id);
            CHECK(world.plan.origin_x == locked.origin_x && world.plan.origin_y == locked.origin_y);
            CHECK(world.plan.aim_x == locked.aim_x && world.plan.aim_y == locked.aim_y);
            CHECK(world.plan.gap_angle_deg == locked.gap_angle_deg);
            CHECK(world.plan.wave_offset == locked.wave_offset);
            CHECK(world.plan.geometry_seed == locked.geometry_seed);
        }
        CHECK(world.boss.x != 480.0f);
        CHECK(student_moved);
        DemoEntityId marked = 0;
        CHECK(world_nearest_student(&world, &marked, NULL, NULL));
        CHECK(marked == 101); /* display target changes; locked attack target does not */
    }
}

static void test_course_birth_band(void) {
    DemoConfig cfg = fixture_config();
    World world;
    BossInput input;
    reset(&world, &cfg);
    boss_input_clear(&input);
    input.attack_requested[DEMO_PATTERN_COURSE] = true;
    world_step(&world, &input);
    for (int wave = 0; wave < world.plan.wave_count; ++wave) {
        ProjectileSpawnBuffer buf;
        spawn_buffer_init(&buf);
        CHECK(pattern_course_emit(&world.plan, &cfg,
              (uint32_t)lroundf(world.plan.wave_tick[wave]), &buf));
        CHECK(buf.count == 24);
        bool saw_top = false, saw_bottom = false;
        for (uint32_t k = 0; k < buf.count; ++k) {
            const Projectile *p = &buf.spec[k];
            CHECK(p->y >= 0 && p->y <= 100.001f);
            CHECK(p->vy == cfg.patterns[DEMO_PATTERN_COURSE].bullet_speed && p->vx == 0);
            if (p->y == 0) saw_top = true;
            if (fabsf(p->y - 100) < 0.001f) saw_bottom = true;
        }
        CHECK(saw_top && saw_bottom);
    }
}

static void test_rejections(void) {
    DemoConfig cfg = fixture_config();
    cfg.energy_start = 10;
    World world;
    BossInput input;
    reset(&world, &cfg);
    boss_input_clear(&input);
    input.attack_requested[DEMO_PATTERN_SHOWER] = true;
    world_step(&world, &input);
    CHECK(world.energy == 10);
    CHECK(world.last_result.last_reject == DEMO_REJECT_NO_ENERGY);
    CHECK(world.attack_accept_count == 0);
    world.energy = 100; /* test a rejected request after energy becomes sufficient */
    world_step(&world, NULL);
    CHECK(world.attack_accept_count == 0);
    boss_input_clear(&input);
    input.attack_requested[DEMO_PATTERN_MINE] = true;
    world_step(&world, &input);
    CHECK(world.last_result.attack_accepted);
    CHECK(world.energy == 85);
    boss_input_clear(&input);
    input.attack_requested[DEMO_PATTERN_SHOWER] = true;
    world_step(&world, &input);
    CHECK(world.last_result.last_reject == DEMO_REJECT_BUSY);
    CHECK(world.energy == 85);
    for (int step = 0; step < 250; ++step) world_step(&world, NULL);
    CHECK(world.attack_accept_count == 1);
    CHECK(world.attack_state == DEMO_ATTACK_IDLE);
    CHECK(world.energy == 85);

    cfg = fixture_config();
    reset(&world, &cfg);
    world.students[0].y = 700; /* clamping makes mine-target distance only 20 */
    boss_input_clear(&input);
    input.attack_requested[DEMO_PATTERN_MINE] = true;
    world_step(&world, &input);
    CHECK(!world.last_result.attack_accepted);
    CHECK(world.attack_accept_count == 0 && world.attack_reject_count == 1);
    CHECK(world.energy == cfg.energy_start);
    world.students[0].y = 300;
    world_step(&world, NULL);
    CHECK(world.attack_accept_count == 0); /* no delayed request */
    world_step(&world, &input);
    CHECK(world.last_result.attack_accepted);
    float dx = world.plan.aim_x - world.plan.origin_x;
    float dy = world.plan.aim_y - world.plan.origin_y;
    CHECK(hypotf(dx, dy) >= 179.999f);
}

static void check_frozen(World *world) {
    int32_t tick = world->tick, energy = world->energy;
    DemoWorldStatus status = world->status;
    float boss_x = world->boss.x, student_x = world->students[0].x;
    uint32_t live = world->pool.live_count;
    uint64_t rng_state = world->world_rng.state;
    BossInput input;
    boss_input_clear(&input);
    input.move_x = 1;
    input.attack_requested[DEMO_PATTERN_RING] = true;
    for (int step = 0; step < 10; ++step) {
        world_step(world, &input);
        CHECK(world->tick == tick && world->energy == energy);
        CHECK(world->status == status);
        CHECK(world->boss.x == boss_x && world->students[0].x == student_x);
        CHECK(world->pool.live_count == live);
        CHECK(world->world_rng.state == rng_state);
        CHECK(world->events.count == 0);
    }
}

static void test_outcomes_and_truncation(void) {
    for (int outcome = 0; outcome < 3; ++outcome) {
        DemoConfig cfg = fixture_config();
        cfg.student_hp = 1;
        cfg.boss_hp = 1;
        World world;
        reset(&world, &cfg);
        world.boss.invuln_ticks = 0;
        if (outcome != 1) {
            CHECK(pool_spawn(&world.pool, DEMO_FACTION_BOSS, 1, 99, DEMO_PATTERN_RING,
                  world.students[0].x, world.students[0].y, 0, 0, 6, 1, 100, 0));
        }
        if (outcome != 0) {
            CHECK(pool_spawn(&world.pool, DEMO_FACTION_STUDENT, 100, 0, DEMO_PATTERN_RING,
                  world.boss.x, world.boss.y, 0, 0, 5, 1, 100, 0));
        }
        world_step(&world, NULL);
        CHECK(world.status == (outcome == 1 ? DEMO_STATUS_BOSS_LOSE : DEMO_STATUS_BOSS_WIN));
        CHECK(event_present(&world, DEMO_EVENT_GAME_OVER));
        if (outcome == 2) CHECK(!world.boss.alive && !world.students[0].alive);
        check_frozen(&world);
    }
    DemoConfig cfg = fixture_config();
    cfg.max_ticks = 3;
    cfg.energy_start = 10;
    cfg.energy_regen_per_sec = 60;
    World world;
    reset(&world, &cfg);
    for (int step = 0; step < 3; ++step) world_step(&world, NULL);
    CHECK(world.truncated && world.tick == 3);
    CHECK(world.status == DEMO_STATUS_RUNNING);
    CHECK(event_present(&world, DEMO_EVENT_TRUNCATED));
    CHECK(!event_present(&world, DEMO_EVENT_GAME_OVER));
    check_frozen(&world);
}

int main(void) {
    test_wave_schedule();
    test_geometry_lock();
    test_course_birth_band();
    test_rejections();
    test_outcomes_and_truncation();
    printf("core regression: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
