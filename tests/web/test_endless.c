/* Real-core regressions for config v4 endless waves and GPA curve.
 * Tests inject visible bullets to isolate exact collision/wave contracts.
 * Fixture HP/speed/regen overrides are test inputs, not gameplay defaults. */
#include "world.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static unsigned failures;
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #value); } } while (0)

static DemoConfig fixture(void) {
    DemoConfig cfg;
    CHECK(demo_config_init(&cfg));
    cfg.student_hp = 1;
    cfg.student_speed = 0.0001f;
    cfg.energy_regen_per_sec = 0.0f;
    cfg.boss_hp = 100000;
    return cfg;
}

static void reset(World *w, const DemoConfig *cfg) {
    CHECK(world_reset(w, cfg, UINT64_C(20261006)));
}

static bool has_event(const World *w, DemoEventType type) {
    for (uint32_t i = 0; i < w->events.count; ++i)
        if (w->events.items[i].type == type) return true;
    return false;
}

static void fire_at_student(World *w, unsigned i) {
    const Actor *s = &w->students[i];
    CHECK(pool_spawn(&w->pool, DEMO_FACTION_BOSS, w->boss.id, 999,
        DEMO_PATTERN_RING, s->x, s->y, 0, 0, 6, 1, 300, (uint64_t)w->tick));
}

static void kill_wave(World *w) {
    for (uint32_t i = 0; i < w->student_count; ++i) fire_at_student(w, i);
    world_step(w, NULL);
    CHECK(w->status == DEMO_STATUS_RUNNING);
    CHECK(w->wave_phase == DEMO_STUDENT_WAVE_PREVIEW);
    CHECK(has_event(w, DEMO_EVENT_STUDENT_WAVE_CLEAR));
    CHECK(!has_event(w, DEMO_EVENT_GAME_OVER));
}

static void finish_gap(World *w) {
    int32_t due = w->wave_spawn_tick;
    while (w->tick < due) {
        world_step(w, NULL);
        if (w->tick < due) CHECK(w->wave_phase == DEMO_STUDENT_WAVE_PREVIEW);
    }
    CHECK(w->wave_phase == DEMO_STUDENT_WAVE_ACTIVE);
    CHECK(w->status == DEMO_STATUS_RUNNING);
    CHECK(has_event(w, DEMO_EVENT_STUDENT_WAVE_BEGIN));
}

static void test_defaults_and_cost(void) {
    DemoConfig cfg;
    CHECK(demo_config_init(&cfg));
    CHECK(cfg.version == 4);
    CHECK(cfg.endless_mode);
    CHECK(cfg.patterns[DEMO_PATTERN_SHOWER].cost == 100);
    CHECK(cfg.wave_gap_ticks == 120);
    CHECK(cfg.gpa_half_saturation_kills == 20u);
    CHECK(cfg.gpa_max_hundredths == 430);
    World w;
    reset(&w, &cfg);
    CHECK(w.wave_index == 1 && w.student_count == 3);
    CHECK(w.students_deployed == 3 && w.students_defeated == 0);
    CHECK(w.gpa_hundredths == 0);
    CHECK(!world_pattern_available(&w, DEMO_PATTERN_SHOWER));
    BossInput in;
    boss_input_clear(&in);
    in.attack_requested[DEMO_PATTERN_SHOWER] = true;
    world_step(&w, &in);
    CHECK(!w.last_result.attack_accepted);
    CHECK(w.last_result.last_reject == DEMO_REJECT_NO_ENERGY);
    CHECK(w.energy == 60);
    CHECK(w.attack_accept_count == 0 && w.attack_reject_count == 1);
    CHECK(!w.plan.active);

    cfg.energy_start = 99;
    cfg.energy_regen_per_sec = 0;
    reset(&w, &cfg);
    world_step(&w, &in);
    CHECK(w.energy == 99 && !w.last_result.attack_accepted);
    CHECK(w.last_result.last_reject == DEMO_REJECT_NO_ENERGY);
    cfg.energy_start = 100;
    reset(&w, &cfg);
    world_step(&w, &in);
    CHECK(w.last_result.attack_accepted);
    CHECK(w.energy == 0);
    CHECK(w.attack_state == DEMO_ATTACK_WINDUP);
    CHECK(w.plan.pattern == DEMO_PATTERN_SHOWER);
}

static void test_gpa_curve_boundaries(void) {
    DemoConfig cfg;
    CHECK(demo_config_init(&cfg));
    const uint32_t kills[] = {0u, 1u, 10u, 20u, 50u, 100u, UINT32_MAX};
    const int32_t expected[] = {0, 20, 143, 215, 307, 358, 429};
    for (unsigned i = 0; i < sizeof(kills) / sizeof(kills[0]); ++i) {
        CHECK(world_gpa_for_kills(&cfg, kills[i]) == expected[i]);
    }
    int32_t previous = 0;
    for (uint32_t n = 0u; n <= 1000u; ++n) {
        int32_t score = world_gpa_for_kills(&cfg, n);
        CHECK(score >= previous);
        CHECK(score >= 0 && score < cfg.gpa_max_hundredths);
        previous = score;
    }
    /* Decreasing average growth over later intervals; two-decimal display may plateau. */
    CHECK((215 - 143) * 30 > (307 - 215) * 10);
    CHECK((307 - 215) * 50 > (358 - 307) * 30);
    CHECK(world_gpa_for_kills(NULL, 10u) == 0);
    cfg.gpa_half_saturation_kills = 0u;
    CHECK(!demo_config_validate(&cfg, NULL, 0));
    CHECK(world_gpa_for_kills(&cfg, 10u) == 0);
    /* Both operands can approach UINT32_MAX: denominator must be widened first. */
    cfg.gpa_half_saturation_kills = UINT32_MAX;
    CHECK(world_gpa_for_kills(&cfg, UINT32_MAX) == 215);
    cfg.gpa_half_saturation_kills = 20u;
    cfg.gpa_max_hundredths = 100;
    CHECK(world_gpa_for_kills(&cfg, 20u) == 50);
    cfg.gpa_max_hundredths = 0;
    CHECK(world_gpa_for_kills(&cfg, 10u) == 0);
}

static void test_gpa_only_real_kills_and_order(void) {
    DemoConfig cfg = fixture();
    cfg.student_hp = 2;
    World partial;
    reset(&partial, &cfg);
    fire_at_student(&partial, 0);
    world_step(&partial, NULL);
    CHECK(partial.students[0].alive && partial.students[0].hp == 1);
    CHECK(partial.student_hits_taken == 1);
    CHECK(partial.students_defeated == 0 && partial.gpa_hundredths == 0);
    fire_at_student(&partial, 0);
    world_step(&partial, NULL); /* Invulnerable hit neither damages nor scores. */
    CHECK(partial.student_hits_taken == 1);
    CHECK(partial.students_defeated == 0 && partial.gpa_hundredths == 0);
    for (int i = 0; i < cfg.student_hurt_invuln_ticks; ++i) world_step(&partial, NULL);
    CHECK(partial.gpa_hundredths == 0);
    fire_at_student(&partial, 0);
    world_step(&partial, NULL);
    CHECK(partial.students_defeated == 1 && partial.gpa_hundredths == 20);

    cfg.student_hp = 1;
    World first, second;
    reset(&first, &cfg);
    reset(&second, &cfg);
    const unsigned first_order[] = {0, 1, 2};
    const unsigned second_order[] = {2, 0, 1};
    const int32_t expected[] = {20, 39, 56};
    for (unsigned i = 0; i < 3; ++i) {
        fire_at_student(&first, first_order[i]);
        world_step(&first, NULL);
        for (unsigned wait = 0; wait < 7u + i; ++wait) world_step(&second, NULL);
        fire_at_student(&second, second_order[i]);
        world_step(&second, NULL);
        CHECK(first.students_defeated == i + 1u);
        CHECK(second.students_defeated == first.students_defeated);
        CHECK(first.gpa_hundredths == expected[i]);
        CHECK(second.gpa_hundredths == first.gpa_hundredths);
        CHECK(second.tick > first.tick);
    }
    /* GPA belongs to the whole run: spawning more students does not alter it. */
    finish_gap(&first);
    CHECK(first.wave_index == 2 && first.students_deployed == 7);
    CHECK(first.gpa_hundredths == 56);
    CHECK(first.gpa_hundredths == second.gpa_hundredths);
    fire_at_student(&first, 0);
    world_step(&first, NULL);
    CHECK(first.students_defeated == 4 && first.gpa_hundredths == 71);
}

static void test_waves_geometry_ids_and_gpa(void) {
    DemoConfig cfg = fixture();
    World w;
    reset(&w, &cfg);
    w.boss.hp = 42;
    w.energy = 47;
    w.energy_regen_accum = 0.375f;
    uint32_t expected_deployed = 3, expected_defeated = 0;
    DemoEntityId last_id = w.students[2].id;
    uint64_t previous_bullet_id = 0;
    for (unsigned wave = 1; wave <= 9; ++wave) {
        unsigned expected_count = wave + 2;
        if (expected_count > 8) expected_count = 8;
        CHECK(w.wave_index == wave);
        CHECK(w.student_count == expected_count);
        CHECK(w.students_deployed == expected_deployed);
        CHECK(w.boss.hp == 42 && w.energy == 47);
        CHECK(w.energy_regen_accum == 0.375f);
        CHECK(w.pool.live_count == 0);
        CHECK(w.attack_state == DEMO_ATTACK_IDLE);
        /* Exercise cleanup of a live plan and harmless old student/boss bullets. */
        w.next_plan_id = UINT64_C(1000) + wave;
        w.plan.active = true;
        w.plan.plan_id = w.next_plan_id - 1;
        w.plan.pattern = DEMO_PATTERN_RING;
        w.plan.start_tick = w.tick;
        w.plan.windup_ticks = 999999;
        w.attack_state = DEMO_ATTACK_WINDUP;
        CHECK(pool_spawn(&w.pool, DEMO_FACTION_STUDENT, w.students[0].id, 0,
                         DEMO_PATTERN_RING, 900, 690, 0, 0, 5, 1, 300, w.tick));
        uint64_t current_id = w.pool.items[0].id;
        CHECK(current_id != previous_bullet_id);
        previous_bullet_id = current_id;
        uint32_t generation_before = w.pool.next_generation;
        kill_wave(&w);
        expected_defeated += expected_count;
        CHECK(w.students_defeated == expected_defeated);
        int32_t expected_gpa = world_gpa_for_kills(&cfg, expected_defeated);
        CHECK(w.gpa_hundredths == expected_gpa);
        CHECK(w.waves_cleared == wave);
        CHECK(w.pool.live_count == 0);
        CHECK(w.pool.next_generation > generation_before);
        CHECK(w.next_plan_id == UINT64_C(1000) + wave);
        CHECK(w.attack_state == DEMO_ATTACK_IDLE && !w.plan.active);
        CHECK(w.spawn_preview_count == w.next_wave_students);
        CHECK(w.wave_spawn_tick - w.tick == 120);
        Vec2 frozen[DEMO_MAX_STUDENTS];
        memcpy(frozen, w.spawn_preview, sizeof(frozen));
        for (unsigned i = 0; i < w.spawn_preview_count; ++i) {
            float dx = frozen[i].x - w.boss.x, dy = frozen[i].y - w.boss.y;
            CHECK(dx * dx + dy * dy >= (22 + 20 + 60) * (22 + 20 + 60));
            CHECK(frozen[i].x >= 20 && frozen[i].x <= 940);
            CHECK(frozen[i].y >= 20 && frozen[i].y <= 700);
            for (unsigned j = 0; j < i; ++j) {
                dx = frozen[i].x - frozen[j].x; dy = frozen[i].y - frozen[j].y;
                CHECK(dx * dx + dy * dy >= 60 * 60);
            }
        }
        /* The Boss may deliberately occupy a preview: fixed time/geometry still hold. */
        if (wave == 2) { w.boss.x = frozen[0].x; w.boss.y = frozen[0].y; }
        int gpa_before = w.gpa_hundredths;
        finish_gap(&w);
        CHECK(w.gpa_hundredths == gpa_before);
        CHECK(w.students_defeated == expected_defeated);
        CHECK(w.boss.hp == 42 && w.energy == 47);
        CHECK(w.spawn_preview_count == 0 && w.wave_spawn_tick == 0);
        for (unsigned i = 0; i < w.student_count; ++i) {
            CHECK(w.students[i].alive);
            CHECK(w.students[i].id > last_id);
            last_id = w.students[i].id;
            CHECK(w.students[i].x == frozen[i].x && w.students[i].y == frozen[i].y);
            CHECK(w.students[i].hp == 1);
            CHECK(w.bot[i].fire_cooldown_ticks == cfg.student_fire_interval_ticks);
        }
        expected_deployed += w.student_count;
        CHECK(w.students_deployed == expected_deployed);
    }
    CHECK(w.students_defeated == 57u);
    CHECK(w.gpa_hundredths == 318);
    CHECK(w.gpa_hundredths < cfg.gpa_max_hundredths);
    WorldView view;
    world_make_view(&w, &view);
    CHECK(view.gpa_hundredths == w.gpa_hundredths);
    CHECK(view.wave_index == w.wave_index && view.waves_cleared == w.waves_cleared);
    CHECK(view.students_defeated == w.students_defeated);
    CHECK(view.students_deployed == w.students_deployed);
    CHECK(view.wave_phase == w.wave_phase && view.next_wave_students == 0);
}

static void test_one_knockdown_and_death_priority(void) {
    DemoConfig cfg = fixture();
    World w;
    reset(&w, &cfg);
    fire_at_student(&w, 0);
    world_step(&w, NULL);
    CHECK(w.gpa_hundredths == 20 && w.students_defeated == 1);
    CHECK(w.students[0].hp == 0 && !w.students[0].alive);
    for (unsigned i = 0; i < 15; ++i) world_step(&w, NULL);
    CHECK(w.gpa_hundredths == 20 && w.students_defeated == 1);
    /* Repeated bullets at an already defeated actor never count it again. */
    for (unsigned i = 0; i < 3; ++i) fire_at_student(&w, 0);
    world_step(&w, NULL);
    CHECK(w.gpa_hundredths == 20 && w.students_defeated == 1);
    CHECK(!has_event(&w, DEMO_EVENT_KNOCKDOWN));

    /* Same-tick real student and Boss deaths must count GPA and finish as LOSE. */
    cfg.student_count = 1;
    cfg.outcome_rule = DEMO_OUTCOME_BOSS_WIN; /* Endless explicitly overrides old win rule. */
    reset(&w, &cfg);
    w.boss.hp = 1;
    w.boss.invuln_ticks = 0;
    fire_at_student(&w, 0);
    CHECK(pool_spawn(&w.pool, DEMO_FACTION_STUDENT, w.students[0].id, 0,
        DEMO_PATTERN_RING, w.boss.x, w.boss.y, 0, 0, 5, 1, 300, w.tick));
    world_step(&w, NULL);
    CHECK(w.status == DEMO_STATUS_BOSS_LOSE);
    CHECK(w.gpa_hundredths == 20 && w.students_defeated == 1);
    CHECK(w.waves_cleared == 0);
    CHECK(has_event(&w, DEMO_EVENT_GAME_OVER));
    CHECK(!has_event(&w, DEMO_EVENT_STUDENT_WAVE_CLEAR));
    int32_t tick = w.tick, energy = w.energy;
    for (unsigned i = 0; i < 130; ++i) world_step(&w, NULL);
    CHECK(w.tick == tick && w.energy == energy);
    CHECK(w.gpa_hundredths == 20 && w.students_defeated == 1);
    CHECK(w.events.count == 0);
    CHECK(!world_pattern_available(&w, DEMO_PATTERN_RING));
}

static void test_new_students_do_not_fire_immediately(void) {
    DemoConfig cfg = fixture();
    World w;
    reset(&w, &cfg);
    kill_wave(&w);
    finish_gap(&w);
    uint32_t fired = w.student_bullets_spawned;
    for (int i = 0; i < cfg.student_fire_interval_ticks; ++i) {
        world_step(&w, NULL);
        CHECK(w.student_bullets_spawned == fired);
    }
    world_step(&w, NULL);
    CHECK(w.student_bullets_spawned > fired);
}

static void test_reset_truncate_and_legacy(void) {
    DemoConfig cfg = fixture();
    World w;
    reset(&w, &cfg);
    kill_wave(&w);
    finish_gap(&w);
    CHECK(w.wave_index == 2 && w.students_defeated == 3);
    reset(&w, &cfg);
    CHECK(w.tick == 0 && w.wave_index == 1 && w.waves_cleared == 0);
    CHECK(w.gpa_hundredths == 0 && w.students_defeated == 0 && w.students_deployed == 3);
    CHECK(w.next_student_id == 103 && w.students[0].id == 100);
    CHECK(w.next_plan_id == 1 && w.pool.next_generation == 1);
    CHECK(w.spawn_preview_count == 0 && w.wave_spawn_tick == 0);
    CHECK(w.energy == 60 && w.boss.hp == cfg.boss_hp);

    cfg.max_ticks = 2;
    reset(&w, &cfg);
    kill_wave(&w);
    world_step(&w, NULL);
    CHECK(w.truncated && w.tick == 2 && w.status == DEMO_STATUS_RUNNING);
    CHECK(w.wave_phase == DEMO_STUDENT_WAVE_PREVIEW);
    CHECK(w.gpa_hundredths == 56 && w.students_defeated == 3);
    for (unsigned i = 0; i < 200; ++i) world_step(&w, NULL);
    CHECK(w.tick == 2 && w.wave_index == 1 && w.gpa_hundredths == 56);
    CHECK(w.events.count == 0 && !world_pattern_available(&w, DEMO_PATTERN_RING));

    cfg.max_ticks = 0;
    cfg.endless_mode = false;
    cfg.outcome_rule = DEMO_OUTCOME_BOSS_WIN;
    reset(&w, &cfg);
    for (uint32_t i = 0; i < w.student_count; ++i) fire_at_student(&w, i);
    world_step(&w, NULL);
    CHECK(w.status == DEMO_STATUS_BOSS_WIN);
    CHECK(w.wave_index == 1 && w.waves_cleared == 0);
    CHECK(w.gpa_hundredths == 56);
    CHECK(has_event(&w, DEMO_EVENT_GAME_OVER));
}

int main(void) {
    test_defaults_and_cost();
    test_gpa_curve_boundaries();
    test_gpa_only_real_kills_and_order();
    test_waves_geometry_ids_and_gpa();
    test_one_knockdown_and_death_priority();
    test_new_students_do_not_fire_immediately();
    test_reset_truncate_and_legacy();
    printf("endless-core: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
