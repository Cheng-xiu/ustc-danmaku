/* Geometry fixtures use actual emit output. No world RNG, future enemy path,
 * or injected projectile participates in a preview or accepted plan. */
#include "demo_base.h"
#include "field_config.h"
#include "pattern_aim.h"
#include "patterns.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures; \
    fprintf(stderr, "line %d: %s\n", __LINE__, #expr); } } while (0)
static bool near(float a, float b) { return fabsf(a - b) < .006f; }
static PatternRequest request(const DemoConfig *cfg) {
    PatternRequest r; memset(&r, 0, sizeof(r));
    r.manual_aim = true; r.aim_dir_x = 0; r.aim_dir_y = 1;
    r.origin_x = cfg->field_w * .5f; r.origin_y = cfg->field_h * .5f;
    r.start_tick = 1013; r.target_id = 999; r.target_x = NAN; r.target_y = NAN;
    r.field_w = cfg->field_w; r.field_h = cfg->field_h;
    r.student_radius = cfg->student_radius;
    return r;
}
static float transverse(const AttackPlan *p, const Projectile *s) {
    return -p->aim_dir_y * (s->x - p->manual_field_w * .5f) +
           p->aim_dir_x * (s->y - p->manual_field_h * .5f);
}
static void check_rows(const AttackPlan *p, int wave, const ProjectileSpawnBuffer *b,
                       float *rain_gap_center) {
    float previous = -FLT_MAX, maximum_gap = 0, gap_center = 0;
    const float width = (p->manual_transverse_max - p->manual_transverse_min) / 3;
    const int channel = ((int)p->wave_offset + wave) % 3;
    for (unsigned i = 0; i < b->count; ++i) {
        const Projectile *s = &b->spec[i];
        const float t = transverse(p, s);
        CHECK(near(s->vx, p->aim_dir_x * p->lock_speed));
        CHECK(near(s->vy, p->aim_dir_y * p->lock_speed));
        CHECK(t > previous);
        if (p->pattern == DEMO_PATTERN_COURSE) {
            const int column = (int)((t - p->manual_transverse_min) / width);
            CHECK(column >= 0 && column <= 2 && column != channel);
            CHECK(t - s->radius >= p->manual_transverse_min + column * width - .006f);
            CHECK(t + s->radius <= p->manual_transverse_min + (column + 1) * width + .006f);
        }
        if (i > 0 && t - previous > maximum_gap) {
            maximum_gap = t - previous; gap_center = (t + previous) * .5f;
        }
        if (i > 0) CHECK(t - previous >= 2 * s->radius - .006f);
        previous = t;
        /* Walking backwards by the locked inset reaches the opposite edge,
         * except a very short diagonal chord whose origin is its midpoint. */
        const float back_x = s->x - p->aim_dir_x * p->manual_entry_inset_px;
        const float back_y = s->y - p->aim_dir_y * p->manual_entry_inset_px;
        const bool entry_or_short = near(back_x, 0) || near(back_x, p->manual_field_w) ||
            near(back_y, 0) || near(back_y, p->manual_field_h) ||
            back_x < 0 || back_x > p->manual_field_w || back_y < 0 || back_y > p->manual_field_h;
        CHECK(entry_or_short);
    }
    if (p->pattern == DEMO_PATTERN_SHOWER) {
        CHECK(near(maximum_gap, p->corridor_width + p->manual_scan_step_px));
        CHECK(maximum_gap - 2 * p->manual_bullet_radius >= p->corridor_width - .006f);
        if (wave > 0) CHECK(near(gap_center - *rain_gap_center, p->manual_scan_step_px));
        *rain_gap_center = gap_center;
    }
}
static void check_plan(const DemoConfig *cfg, const PatternRequest *r) {
    AttackPlan p;
    memset(&p, 0, sizeof(p));
    CHECK(pattern_aim_make_plan(r, cfg, &p));
    if (!p.active) return;
    CHECK(p.manual_aim && p.target_id == 0 && p.geometry_seed == 0);
    CHECK(near(hypotf(p.aim_dir_x, p.aim_dir_y), 1));
    CHECK(near(p.origin_x, r->origin_x) && near(p.origin_y, r->origin_y));
    CHECK(p.start_tick == r->start_tick);
    CHECK(p.windup_ticks == cfg->patterns[r->pattern].windup_ticks);
    CHECK(p.active_ticks == cfg->patterns[r->pattern].active_ticks);
    CHECK(near(p.lock_speed, cfg->patterns[r->pattern].bullet_speed));
    p.plan_id = UINT64_C(555123);
    const AttackPlan before = p;
    unsigned total = 0; float rain_gap_center = 0;
    for (int wave = 0; wave < p.wave_count; ++wave) {
        ProjectileSpawnBuffer b, repeated, altered;
        spawn_buffer_init(&b); spawn_buffer_init(&repeated); spawn_buffer_init(&altered);
        CHECK(pattern_aim_emit(&p, cfg, (unsigned)lroundf(p.wave_tick[wave]), &b));
        CHECK(b.count == (unsigned)p.shots_per_wave && b.overflow == 0);
        CHECK(pattern_aim_emit(&p, cfg, (unsigned)lroundf(p.wave_tick[wave]), &repeated));
        CHECK(memcmp(&b, &repeated, sizeof(b)) == 0);
        DemoConfig changed = *cfg;
        changed.field_w = 100; changed.field_h = 200;
        changed.boss_bullet_radius = 77; changed.boss_bullet_damage = 33;
        changed.boss_bullet_lifetime_ticks = 7;
        memset(changed.patterns, 0, sizeof(changed.patterns));
        Rng rng, rng_before; rng_seed(&rng, 43, 7); rng_before = rng;
        CHECK(pattern_emit(&p, &changed, (unsigned)lroundf(p.wave_tick[wave]), &rng, &altered));
        CHECK(memcmp(&b, &altered, sizeof(b)) == 0);
        CHECK(memcmp(&rng, &rng_before, sizeof(rng)) == 0);
        for (unsigned i = 0; i < b.count; ++i) {
            const Projectile *s = &b.spec[i];
            CHECK(s->plan_id == p.plan_id && s->source_pattern == p.pattern);
            CHECK(s->faction == DEMO_FACTION_BOSS && s->source_id == 1);
            CHECK(s->x >= -.001f && s->x <= cfg->field_w + .001f &&
                  s->y >= -.001f && s->y <= cfg->field_h + .001f);
            CHECK(near(s->x, s->px) && near(s->y, s->py));
            CHECK(near(hypotf(s->vx, s->vy), p.lock_speed));
            CHECK(s->vx * p.aim_dir_x + s->vy * p.aim_dir_y > 0);
            CHECK(near(s->radius, cfg->boss_bullet_radius) && near(s->damage, cfg->boss_bullet_damage));
            CHECK(s->lifetime_ticks == cfg->boss_bullet_lifetime_ticks);
            if (p.pattern == DEMO_PATTERN_RING || p.pattern == DEMO_PATTERN_MINE) {
                CHECK(near(s->x, r->origin_x) && near(s->y, r->origin_y));
                const float cosine = (s->vx * p.aim_dir_x + s->vy * p.aim_dir_y) / p.lock_speed;
                CHECK(cosine >= cosf(p.gap_span_deg * .5f * .01745329252f) - .0001f);
            }
        }
        if (p.pattern == DEMO_PATTERN_COURSE || p.pattern == DEMO_PATTERN_SHOWER)
            check_rows(&p, wave, &b, &rain_gap_center);
        total += b.count;
    }
    CHECK(total == (unsigned)(p.wave_count * p.shots_per_wave));
    CHECK(memcmp(&p, &before, sizeof(p)) == 0);
    ProjectileSpawnBuffer not_due; spawn_buffer_init(&not_due);
    CHECK(!pattern_aim_emit(&p, cfg, (unsigned)p.active_ticks, &not_due));
    CHECK(not_due.count == 0);
    PatternWarning warning;
    pattern_warning(&p, cfg, &warning);
    CHECK(warning.valid && warning.target_id == 0 && warning.pattern == p.pattern);
    CHECK(near(warning.aim_x, p.aim_x) && near(warning.aim_y, p.aim_y));
}
static void rejection(const PatternRequest *r, const DemoConfig *cfg) {
    AttackPlan untouched, p; memset(&p, 0xa5, sizeof(p)); untouched = p;
    CHECK(!pattern_aim_make_plan(r, cfg, &p));
    CHECK(memcmp(&p, &untouched, sizeof(p)) == 0);
}
static void test_rejections_and_safety(const DemoConfig *base) {
    PatternRequest r = request(base); r.pattern = DEMO_PATTERN_MINE;
    const float bad[][2] = {{0, 0}, {NAN, 1}, {INFINITY, 1}, {1, -INFINITY}, {1e-8f, 0}};
    for (unsigned i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        r.aim_dir_x = bad[i][0]; r.aim_dir_y = bad[i][1]; rejection(&r, base);
    }
    r = request(base); r.pattern = DEMO_PATTERN_MINE;
    r.student_count = 2; r.student_alive[0] = true; r.student_alive[1] = true;
    r.student_x[0] = r.origin_x + 120; r.student_y[0] = r.origin_y;
    r.student_x[1] = r.origin_x - 119; r.student_y[1] = r.origin_y;
    rejection(&r, base);
    r.student_alive[1] = false;
    AttackPlan p; CHECK(pattern_aim_make_plan(&r, base, &p));
    r.student_alive[1] = true; r.student_x[1] = r.origin_x - 120;
    CHECK(pattern_aim_make_plan(&r, base, &p));
    r.pattern = DEMO_PATTERN_RING; r.student_x[1] = r.origin_x - 48;
    CHECK(pattern_aim_make_plan(&r, base, &p));
    r.student_x[1] = r.origin_x - 19; rejection(&r, base);
    r.student_alive[1] = false; r.student_x[1] = NAN;
    CHECK(pattern_aim_make_plan(&r, base, &p));
    r.aim_dir_x = FLT_MAX; r.aim_dir_y = FLT_MAX;
    CHECK(pattern_aim_make_plan(&r, base, &p)); CHECK(near(p.aim_dir_x, p.aim_dir_y));
    r = request(base); r.pattern = DEMO_PATTERN_RING; r.origin_x = -1; rejection(&r, base);
    r = request(base); r.pattern = DEMO_PATTERN_MINE; r.manual_aim = false; rejection(&r, base);
    r = request(base); r.pattern = DEMO_PATTERN_SHOWER;
    DemoConfig cfg = *base; cfg.patterns[DEMO_PATTERN_SHOWER].manual_density_pitch_px = NAN;
    rejection(&r, &cfg);
    cfg = *base; cfg.patterns[DEMO_PATTERN_SHOWER].wave_interval_sec = 0;
    cfg.patterns[DEMO_PATTERN_SHOWER].manual_density_pitch_px = 1;
    rejection(&r, &cfg);
    cfg = *base; cfg.patterns[DEMO_PATTERN_RING].manual_arc_span_deg = 180;
    CHECK(!demo_config_validate(&cfg, NULL, 0));
    cfg = *base; cfg.patterns[DEMO_PATTERN_MINE].manual_shots_per_wave = 0;
    CHECK(!demo_config_validate(&cfg, NULL, 0));
    cfg = *base; cfg.patterns[DEMO_PATTERN_COURSE].manual_entry_inset_px = INFINITY;
    CHECK(!demo_config_validate(&cfg, NULL, 0));
}
int main(void) {
    DemoConfig base; CHECK(demo_config_init(&base)); CHECK(base.version == 6);
    CHECK(demo_config_validate(&base, NULL, 0));
    CHECK(base.patterns[0].cost == 25 && base.patterns[1].cost == 50 &&
          base.patterns[2].cost == 20 && base.patterns[3].cost == 100);
    const float sizes[][2] = {{960,720}, {1280,540}, {650,1063.3846f}};
    const float directions[][2] = {{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}};
    for (unsigned size = 0; size < sizeof(sizes)/sizeof(sizes[0]); ++size) {
        DemoConfig cfg = base; CHECK(demo_config_set_field_size(&cfg, sizes[size][0], sizes[size][1]));
        for (unsigned dir = 0; dir < 8; ++dir) for (int skill = 0; skill < 4; ++skill) {
            PatternRequest r = request(&cfg); r.pattern = (DemoPattern)skill;
            r.aim_dir_x = directions[dir][0]; r.aim_dir_y = directions[dir][1];
            for (int pos = 0; pos < 5; ++pos) {
                if (pos > 0) {
                    r.origin_x = pos == 1 || pos == 3 ? 22 : cfg.field_w - 22;
                    r.origin_y = pos <= 2 ? 22 : cfg.field_h - 22;
                }
                check_plan(&cfg, &r);
            }
        }
    }
    test_rejections_and_safety(&base);
    printf("manual aim: %u checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
