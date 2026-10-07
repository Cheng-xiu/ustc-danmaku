/* Real-core field initialization and projectile boundary regressions.
 * No browser state or injected gameplay balancing changes. */
#include "field_config.h"
#include "patterns.h"
#include "projectiles.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #value); } } while (0)

static bool near(float a, float b) { return fabsf(a - b) <= 0.0002f; }

static void geometry_accepts(const DemoConfig *cfg) {
    PatternRequest request;
    memset(&request, 0, sizeof(request));
    request.origin_x = cfg->field_w * 0.5f;
    request.origin_y = cfg->boss_radius + (620.0f - cfg->boss_radius) /
                       (720.0f - 2.0f * cfg->boss_radius) *
                       (cfg->field_h - 2.0f * cfg->boss_radius);
    request.target_id = 100u;
    request.target_x = cfg->student_spawn_x[0];
    request.target_y = cfg->student_spawn_y[0];
    request.student_count = cfg->student_count;
    request.student_radius = cfg->student_radius;
    request.field_w = cfg->field_w;
    request.field_h = cfg->field_h;
    for (uint32_t i = 0; i < cfg->student_count; ++i) {
        request.student_x[i] = cfg->student_spawn_x[i];
        request.student_y[i] = cfg->student_spawn_y[i];
        request.student_alive[i] = true;
    }
    for (int p = 0; p < DEMO_PATTERN_COUNT; ++p) {
        const PatternVTable *vt = pattern_vtable((DemoPattern)p);
        AttackPlan plan;
        Rng rng;
        memset(&plan, 0, sizeof(plan));
        request.pattern = (DemoPattern)p;
        rng_seed(&rng, UINT64_C(20261007), UINT64_C(1));
        CHECK(vt != NULL);
        CHECK(vt->make_plan(&request, cfg, &rng, &plan));
        CHECK(plan.active);
        ProjectileSpawnBuffer output;
        spawn_buffer_init(&output);
        CHECK(vt->emit(&plan, cfg, (uint32_t)lroundf(plan.wave_tick[0]), &output));
        CHECK(output.count > 0 && output.overflow == 0u);
        for (uint32_t i = 0; i < output.count; ++i) {
            CHECK(isfinite(output.spec[i].x) && isfinite(output.spec[i].y));
            CHECK(output.spec[i].x >= 0.0f && output.spec[i].x <= cfg->field_w);
            CHECK(output.spec[i].y >= 0.0f && output.spec[i].y <= cfg->field_h);
        }
    }
}

static void test_field(float width, float height) {
    DemoConfig before, cfg;
    CHECK(demo_config_init(&before));
    cfg = before;
    CHECK(demo_config_set_field_size(&cfg, width, height));
    CHECK(cfg.field_w == width && cfg.field_h == height);
    CHECK(demo_config_validate(&cfg, NULL, 0u));
    CHECK(near(cfg.boss_move_min_x, cfg.boss_radius));
    CHECK(near(cfg.boss_move_max_x, width - cfg.boss_radius));
    CHECK(near(cfg.boss_move_min_y, cfg.boss_radius));
    CHECK(near(cfg.boss_move_max_y, height - cfg.boss_radius));
    CHECK(cfg.boss_speed == before.boss_speed && cfg.student_speed == before.student_speed);
    CHECK(cfg.boss_radius == before.boss_radius && cfg.student_radius == before.student_radius);
    CHECK(cfg.energy_max == before.energy_max && cfg.energy_regen_per_sec == before.energy_regen_per_sec);
    for (uint32_t i = 0; i < DEMO_MAX_STUDENTS; ++i) {
        CHECK(cfg.student_spawn_x[i] >= cfg.student_radius && cfg.student_spawn_x[i] <= width - cfg.student_radius);
        CHECK(cfg.student_spawn_y[i] >= cfg.student_radius && cfg.student_spawn_y[i] <= height - cfg.student_radius);
        CHECK(near((cfg.student_spawn_x[i] - cfg.student_radius) / (width - 2 * cfg.student_radius),
                   (before.student_spawn_x[i] - before.student_radius) / (before.field_w - 2 * before.student_radius)));
        CHECK(near((cfg.student_spawn_y[i] - cfg.student_radius) / (height - 2 * cfg.student_radius),
                   (before.student_spawn_y[i] - before.student_radius) / (before.field_h - 2 * before.student_radius)));
    }
    CHECK(near(cfg.patterns[DEMO_PATTERN_COURSE].lane_spread_px / width,
               before.patterns[DEMO_PATTERN_COURSE].lane_spread_px / before.field_w));
    const PatternConfig *shower = &cfg.patterns[DEMO_PATTERN_SHOWER];
    const float pitch = (width - 4 * cfg.boss_bullet_radius - shower->corridor_width) / shower->shots_per_wave;
    CHECK(shower->shots_per_wave >= 2 * shower->wave_count);
    CHECK(shower->shots_per_wave <= (int32_t)DEMO_MAX_ACTIVE_PLAN_PROJECTILES);
    CHECK(pitch >= 2 * cfg.boss_bullet_radius);
    CHECK(pitch < shower->corridor_width - 2 * cfg.boss_bullet_radius);
    for (int p = 0; p < DEMO_PATTERN_COUNT; ++p) {
        CHECK(cfg.patterns[p].cost == before.patterns[p].cost);
        CHECK(cfg.patterns[p].windup_ticks == before.patterns[p].windup_ticks);
        CHECK(cfg.patterns[p].active_ticks == before.patterns[p].active_ticks);
        CHECK(cfg.patterns[p].bullet_speed == before.patterns[p].bullet_speed);
    }
    if (width == before.field_w && height == before.field_h) CHECK(memcmp(&cfg, &before, sizeof(cfg)) == 0);
    DemoConfig same = cfg;
    CHECK(demo_config_set_field_size(&cfg, width, height));
    CHECK(memcmp(&cfg, &same, sizeof(cfg)) == 0);
    geometry_accepts(&cfg);
}

static void test_invalid(void) {
    DemoConfig cfg, before;
    CHECK(demo_config_init(&cfg));
    const float bad[][2] = {
        {NAN, 720}, {960, NAN}, {INFINITY, 720}, {960, INFINITY},
        {0, 720}, {960, 0}, {-960, 720}, {960, -720},
        {100, 720}, {960, 40}, {100000, 720}
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        before = cfg;
        CHECK(!demo_config_set_field_size(&cfg, bad[i][0], bad[i][1]));
        CHECK(memcmp(&cfg, &before, sizeof(cfg)) == 0);
    }
    CHECK(!demo_config_set_field_size(NULL, 960, 720));
    cfg.patterns[DEMO_PATTERN_SHOWER].corridor_width = 0;
    before = cfg;
    CHECK(!demo_config_set_field_size(&cfg, 1120, 617));
    CHECK(memcmp(&cfg, &before, sizeof(cfg)) == 0);
    CHECK(!demo_config_set_field_size(&cfg, 960, 720));
    CHECK(memcmp(&cfg, &before, sizeof(cfg)) == 0);
}

static void bullet(ProjectilePool *pool, float x, float y) {
    CHECK(pool_spawn(pool, DEMO_FACTION_STUDENT, 100u, 0u, DEMO_PATTERN_RING,
                     x, y, 0, 0, 5, 1, 300, 0u));
}

static void test_pool_bounds(void) {
    ProjectilePool pool, before;
    pool_init(&pool, 10u);
    bullet(&pool, 1100, 300); /* Beyond the old width, legal in a 1280-wide field. */
    pool_advance_in_field(&pool, 1.0f / 60, 1280, 540);
    CHECK(pool.live_count == 1u && pool.items[0].active);
    CHECK(pool.items[0].lifetime_ticks == 299);
    before = pool;
    pool_advance_in_field(&pool, 1.0f / 60, NAN, 540);
    CHECK(memcmp(&pool, &before, sizeof(pool)) == 0);
    pool_advance_in_field(&pool, 1.0f / 60, 1280, 0);
    CHECK(memcmp(&pool, &before, sizeof(pool)) == 0);
    pool_advance_in_field(&pool, NAN, 1280, 540);
    CHECK(memcmp(&pool, &before, sizeof(pool)) == 0);
    pool_advance(&pool, 1.0f / 60);
    CHECK(pool.live_count == 0u); /* Compatibility wrapper still uses 960x720. */

    pool_init(&pool, 10u);
    bullet(&pool, 300, 900); /* Beyond the old height, legal in portrait. */
    pool_advance_in_field(&pool, 1.0f / 60, 650, 1063);
    CHECK(pool.live_count == 1u);
    pool_init(&pool, 10u);
    bullet(&pool, 1280 + 64, 300);
    bullet(&pool, 1280 + 64.125f, 300);
    bullet(&pool, 300, 540 + 64);
    bullet(&pool, 300, 540 + 64.125f);
    bullet(&pool, -64, 300);
    bullet(&pool, -64.125f, 300);
    pool_advance_in_field(&pool, 1.0f / 60, 1280, 540);
    CHECK(pool.live_count == 3u);
    CHECK(pool.items[0].active && !pool.items[1].active);
    CHECK(pool.items[2].active && !pool.items[3].active);
    CHECK(pool.items[4].active && !pool.items[5].active);
}

int main(void) {
    test_field(960, 720);
    test_field(1120, 617);
    test_field(1280, 540);
    test_field(650, 1063);
    test_invalid();
    test_pool_bounds();
    printf("field-size: %u checks, %u failures\n", checks, failures);
    return failures == 0u ? 0 : 1;
}
