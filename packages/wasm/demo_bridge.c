/* ABI v5: explicit manual aim and an independent, pure C preview buffer. */
#include "demo_bridge.h"
#include "attack.h"
#include "field_config.h"
#include "patterns.h"
#include "world.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#define HEADER_WORDS 64u
#define ACTOR_WORDS 10u
#define BULLET_WORDS 10u
#define WARNING_WORDS 8u
#define EVENT_WORDS 9u
#define WARNING_CAPACITY (32u * DEMO_MAX_ACTIVE_PLAN_PROJECTILES)
#define PREVIEW_HEADER_WORDS 16u
#define PREVIEW_WORDS (PREVIEW_HEADER_WORDS + WARNING_CAPACITY * WARNING_WORDS)
#define SNAPSHOT_WORDS (HEADER_WORDS + (DEMO_MAX_STUDENTS + 1u) * ACTOR_WORDS + \
    DEMO_MAX_PROJECTILES * BULLET_WORDS + WARNING_CAPACITY * WARNING_WORDS + \
    DEMO_MAX_STEP_EVENTS * EVENT_WORDS + DEMO_MAX_STUDENTS * 2u)

typedef struct PublicRay {
    float x, y, vx, vy, radius;
    int32_t spawn_tick;
    uint32_t wave, pattern;
} PublicRay;

_Static_assert(sizeof(float) == sizeof(uint32_t), "ABI requires 32-bit float");
static World g_world;
static int g_ready;
static uint8_t g_snapshot[SNAPSHOT_WORDS * 4u];
static uint32_t g_snapshot_size;
static uint8_t g_preview[PREVIEW_WORDS * 4u];
static uint32_t g_preview_size;
static PublicRay g_preview_rays[WARNING_CAPACITY];
static PublicRay g_rays[WARNING_CAPACITY];
static uint32_t g_ray_count;
static uint64_t g_ray_plan;
static int g_ray_valid;

static void write_u32(uint8_t *buffer, uint32_t word, uint32_t value) {
    uint8_t *p = buffer + word * 4u;
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void put_u32(uint32_t word, uint32_t value) { write_u32(g_snapshot, word, value); }

static void write_f32(uint8_t *buffer, uint32_t word, float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    write_u32(buffer, word, bits);
}

static void put_f32(uint32_t word, float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    put_u32(word, bits);
}

static void put_u64(uint32_t word, uint64_t value) {
    put_u32(word, (uint32_t)value);
    put_u32(word + 1u, (uint32_t)(value >> 32));
}

/* Real emit functions read the accepted immutable plan and consume no RNG.
 * Geometry is cached once per plan, never reconstructed in TypeScript. */
static int project_plan_rays(const AttackPlan *plan, const DemoConfig *config,
                             PublicRay *rays, uint32_t *ray_count) {
    *ray_count = 0u;
    if (plan == NULL || config == NULL || rays == NULL || !plan->active ||
        pattern_vtable(plan->pattern) == NULL || plan->wave_count < 1 || plan->wave_count > 32) return 0;
    for (int32_t wave = 0; wave < plan->wave_count; ++wave) {
        const float due_f = plan->wave_tick[wave];
        if (!isfinite(due_f) || due_f < 0.0f || due_f >= (float)INT32_MAX) return 0;
        const int32_t due = (int32_t)lroundf(due_f);
        /* Multiple waves scheduled at one tick are emitted by one call. */
        int duplicate = 0;
        for (int32_t earlier = 0; earlier < wave; ++earlier)
            if ((int32_t)lroundf(plan->wave_tick[earlier]) == due) duplicate = 1;
        if (duplicate) continue;
        ProjectileSpawnBuffer output;
        spawn_buffer_init(&output);
        const int emitted = pattern_emit(plan, config, (uint32_t)due, NULL, &output);
        if (output.overflow != 0u || output.count > DEMO_MAX_ACTIVE_PLAN_PROJECTILES ||
            *ray_count > WARNING_CAPACITY - output.count) return 0;
        if (!emitted || output.count == 0u) return 0;
        const int64_t absolute = (int64_t)plan->start_tick + plan->windup_ticks + due;
        if (absolute < 0 || absolute > INT32_MAX) return 0;
        for (uint32_t i = 0u; i < output.count; ++i) {
            const Projectile *p = &output.spec[i];
            if (!isfinite(p->x) || !isfinite(p->y) || !isfinite(p->vx) || !isfinite(p->vy) ||
                !isfinite(p->radius) || p->radius <= 0.0f ||
                ((double)p->vx * p->vx + (double)p->vy * p->vy) <= 0.0) return 0;
            if (plan->manual_aim && (p->x < -64.0f || p->y < -64.0f ||
                p->x > plan->manual_field_w + 64.0f || p->y > plan->manual_field_h + 64.0f)) return 0;
            PublicRay *ray = &rays[(*ray_count)++];
            ray->x = p->x; ray->y = p->y; ray->vx = p->vx; ray->vy = p->vy;
            ray->radius = p->radius; ray->spawn_tick = (int32_t)absolute;
            ray->wave = (uint32_t)wave; ray->pattern = (uint32_t)plan->pattern;
        }
    }
    return 1;
}

static int project_rays(void) {
    const AttackPlan *plan = &g_world.plan;
    if (!plan->active) {
        g_ray_count = 0u; g_ray_plan = 0u; g_ray_valid = 0;
        return 1;
    }
    if (g_ray_plan == plan->plan_id && g_ray_valid) return 1;
    g_ray_plan = plan->plan_id; g_ray_valid = 0;
    if (!project_plan_rays(plan, &g_world.cfg, g_rays, &g_ray_count)) return 0;
    g_ray_valid = 1;
    return 1;
}

static int normalize_aim(float *x, float *y) {
    if (!isfinite(*x) || !isfinite(*y)) return 0;
    const double length = hypot((double)*x, (double)*y);
    if (!isfinite(length) || length < 1.0e-6) return 0;
    *x = (float)(*x / length); *y = (float)(*y / length);
    return 1;
}

int demo_reset(uint32_t seed_lo, uint32_t seed_hi, uint32_t students) {
    return demo_reset_sized(seed_lo, seed_hi, students, DEMO_FIELD_WIDTH, DEMO_FIELD_HEIGHT);
}

int demo_reset_sized(uint32_t seed_lo, uint32_t seed_hi, uint32_t students,
                     float field_width, float field_height) {
    DemoConfig config;
    if (students == 0u) students = 3u;
    if (students > DEMO_MAX_STUDENTS || !demo_config_init(&config)) return 0;
    config.student_count = students;
    if (!demo_config_set_field_size(&config, field_width, field_height)) return 0;
    const uint64_t seed = ((uint64_t)seed_hi << 32) | seed_lo;
    /* A rejected window size must leave the current match usable. */
    demo_dispose();
    g_ready = world_reset(&g_world, &config, seed) ? 1 : 0;
    return g_ready;
}

static int step_input(float move_x, float move_y, int pointer_valid, float pointer_x,
                      float pointer_y, uint32_t attack_mask, int manual_aim,
                      float aim_dir_x, float aim_dir_y) {
    if (!g_ready || g_world.status != DEMO_STATUS_RUNNING || g_world.truncated ||
        !isfinite(move_x) || !isfinite(move_y) || !isfinite(pointer_x) ||
        !isfinite(pointer_y) || (attack_mask & ~15u) != 0u) return 0;
    BossInput input;
    boss_input_clear(&input);
    input.move_x = move_x; input.move_y = move_y;
    input.pointer_valid = pointer_valid != 0;
    input.pointer_x = pointer_x; input.pointer_y = pointer_y;
    input.manual_aim = manual_aim != 0;
    input.aim_dir_x = aim_dir_x; input.aim_dir_y = aim_dir_y;
    for (uint32_t p = 0u; p < DEMO_PATTERN_COUNT; ++p)
        input.attack_requested[p] = (attack_mask & (1u << p)) != 0u;
    world_step(&g_world, &input);
    g_snapshot_size = 0u;
    g_preview_size = 0u;
    return 1;
}

int demo_step(float move_x, float move_y, int pointer_valid, float pointer_x,
              float pointer_y, uint32_t attack_mask) {
    return step_input(move_x, move_y, pointer_valid, pointer_x, pointer_y, attack_mask, 0, 0.0f, 0.0f);
}

int demo_step_aim(float move_x, float move_y, int pointer_valid, float pointer_x,
                  float pointer_y, uint32_t attack_mask, float aim_dir_x, float aim_dir_y) {
    if (!normalize_aim(&aim_dir_x, &aim_dir_y)) return 0;
    return step_input(move_x, move_y, pointer_valid, pointer_x, pointer_y, attack_mask,
                      1, aim_dir_x, aim_dir_y);
}

const uint8_t *demo_preview(uint32_t pattern, float aim_dir_x, float aim_dir_y) {
    g_preview_size = 0u;
    if (!g_ready || pattern >= DEMO_PATTERN_COUNT || !normalize_aim(&aim_dir_x, &aim_dir_y)) return NULL;
    AttackPlan plan;
    memset(&plan, 0, sizeof(plan));
    DemoRejectReason reason = DEMO_REJECT_NONE;
    uint32_t count = 0u;
    const int geometry_valid = attack_build_aim_plan(&g_world, (DemoPattern)pattern,
                                                    aim_dir_x, aim_dir_y, &plan, &reason);
    if (geometry_valid) {
        if (!project_plan_rays(&plan, &g_world.cfg, g_preview_rays, &count)) return NULL;
    }
    const uint32_t bytes = (PREVIEW_HEADER_WORDS + count * WARNING_WORDS) * 4u;
    memset(g_preview, 0, PREVIEW_HEADER_WORDS * 4u);
    write_u32(g_preview, 0u, 0x55445031u); write_u32(g_preview, 1u, 1u);
    write_u32(g_preview, 2u, bytes); write_u32(g_preview, 3u, pattern);
    write_u32(g_preview, 4u, geometry_valid && reason == DEMO_REJECT_NONE ? 1u : 0u);
    write_u32(g_preview, 5u, (uint32_t)reason);
    write_f32(g_preview, 6u, g_world.boss.x); write_f32(g_preview, 7u, g_world.boss.y);
    write_f32(g_preview, 8u, geometry_valid ? plan.aim_dir_x : aim_dir_x);
    write_f32(g_preview, 9u, geometry_valid ? plan.aim_dir_y : aim_dir_y);
    write_u32(g_preview, 10u, count); write_u32(g_preview, 11u, PREVIEW_HEADER_WORDS * 4u);
    write_u32(g_preview, 12u, (uint32_t)g_world.tick);
    write_f32(g_preview, 13u, g_world.cfg.field_w); write_f32(g_preview, 14u, g_world.cfg.field_h);
    uint32_t cursor = PREVIEW_HEADER_WORDS;
    for (uint32_t i = 0u; i < count; ++i) {
        const PublicRay *ray = &g_preview_rays[i];
        write_f32(g_preview, cursor, ray->x); write_f32(g_preview, cursor + 1u, ray->y);
        write_f32(g_preview, cursor + 2u, ray->vx); write_f32(g_preview, cursor + 3u, ray->vy);
        write_f32(g_preview, cursor + 4u, ray->radius);
        write_u32(g_preview, cursor + 5u, (uint32_t)ray->spawn_tick);
        write_u32(g_preview, cursor + 6u, ray->wave); write_u32(g_preview, cursor + 7u, ray->pattern);
        cursor += WARNING_WORDS;
    }
    g_preview_size = bytes;
    return g_preview;
}

uint32_t demo_preview_size(void) { return g_preview_size; }

static uint32_t write_actor(uint32_t cursor, const Actor *actor, uint32_t faction) {
    put_u32(cursor, actor->id); put_u32(cursor + 1u, actor->alive ? 1u : 0u);
    put_f32(cursor + 2u, actor->x); put_f32(cursor + 3u, actor->y);
    put_f32(cursor + 4u, actor->radius); put_u32(cursor + 5u, (uint32_t)actor->hp);
    put_u32(cursor + 6u, (uint32_t)actor->hp_max);
    put_u32(cursor + 7u, (uint32_t)actor->invuln_ticks);
    put_u32(cursor + 8u, faction); put_u32(cursor + 9u, 0u);
    return cursor + ACTOR_WORDS;
}

const uint8_t *demo_snapshot(void) {
    g_snapshot_size = 0u;
    if (!g_ready || g_world.student_count > DEMO_MAX_STUDENTS ||
        g_world.pool.capacity > DEMO_MAX_PROJECTILES ||
        g_world.events.count > DEMO_MAX_STEP_EVENTS ||
        g_world.spawn_preview_count > DEMO_MAX_STUDENTS || !project_rays()) return NULL;
    memset(g_snapshot, 0, HEADER_WORDS * 4u);
    WorldView view;
    world_make_view(&g_world, &view);
    uint32_t cursor = HEADER_WORDS;
    put_u32(40u, cursor * 4u);
    cursor = write_actor(cursor, &g_world.boss, DEMO_FACTION_BOSS);
    for (uint32_t i = 0u; i < g_world.student_count; ++i)
        cursor = write_actor(cursor, &g_world.students[i], DEMO_FACTION_STUDENT);
    put_u32(41u, cursor * 4u);
    uint32_t bullets = 0u;
    for (uint32_t i = 0u; i < g_world.pool.capacity; ++i) {
        const Projectile *p = &g_world.pool.items[i];
        if (!p->active) continue;
        ++bullets;
        put_u64(cursor, p->id); put_u32(cursor + 2u, (uint32_t)p->faction);
        put_u32(cursor + 3u, (uint32_t)p->source_pattern);
        put_f32(cursor + 4u, p->x); put_f32(cursor + 5u, p->y);
        put_f32(cursor + 6u, p->vx); put_f32(cursor + 7u, p->vy);
        put_f32(cursor + 8u, p->radius); put_u32(cursor + 9u, p->source_id);
        cursor += BULLET_WORDS;
    }
    if (bullets != g_world.pool.live_count) return NULL;
    put_u32(42u, cursor * 4u);
    for (uint32_t i = 0u; i < g_ray_count; ++i) {
        const PublicRay *ray = &g_rays[i];
        put_f32(cursor, ray->x); put_f32(cursor + 1u, ray->y);
        put_f32(cursor + 2u, ray->vx); put_f32(cursor + 3u, ray->vy);
        put_f32(cursor + 4u, ray->radius);
        put_u32(cursor + 5u, (uint32_t)ray->spawn_tick);
        put_u32(cursor + 6u, ray->wave); put_u32(cursor + 7u, ray->pattern);
        cursor += WARNING_WORDS;
    }
    put_u32(43u, cursor * 4u);
    for (uint32_t i = 0u; i < g_world.events.count; ++i) {
        const StepEvent *event = &g_world.events.items[i];
        put_u32(cursor, (uint32_t)event->type); put_u32(cursor + 1u, (uint32_t)event->tick);
        put_u32(cursor + 2u, event->source_id); put_u32(cursor + 3u, event->target_id);
        put_u32(cursor + 4u, (uint32_t)event->pattern);
        put_u32(cursor + 5u, (uint32_t)event->reject);
        put_u32(cursor + 6u, (uint32_t)event->amount);
        put_f32(cursor + 7u, event->x); put_f32(cursor + 8u, event->y);
        cursor += EVENT_WORDS;
    }
    put_u32(54u, cursor * 4u);
    for (uint32_t i = 0u; i < g_world.spawn_preview_count; ++i) {
        put_f32(cursor, g_world.spawn_preview[i].x);
        put_f32(cursor + 1u, g_world.spawn_preview[i].y);
        cursor += 2u;
    }
    put_u32(0u, 0x55444331u); put_u32(1u, 5u); put_u32(2u, cursor * 4u);
    put_u32(3u, (uint32_t)g_world.tick); put_u32(4u, (uint32_t)g_world.status);
    put_u32(5u, g_world.cfg.version); put_u32(6u, g_world.student_count);
    put_u32(7u, bullets); put_u32(8u, g_ray_count); put_u32(9u, g_world.events.count);
    put_u32(10u, (uint32_t)g_world.energy); put_u32(11u, (uint32_t)g_world.cfg.energy_max);
    put_u32(12u, (uint32_t)g_world.attack_state);
    put_u32(13u, (uint32_t)g_world.plan.pattern); put_u32(14u, g_world.plan.target_id);
    put_u32(15u, (uint32_t)g_world.plan.start_tick);
    put_u32(16u, (uint32_t)g_world.plan.windup_ticks);
    put_u32(17u, (uint32_t)g_world.plan.active_ticks);
    put_u64(18u, g_world.plan.plan_id); put_u64(20u, g_world.seed);
    put_u32(22u, g_world.attack_accept_count); put_u32(23u, g_world.attack_reject_count);
    put_u32(24u, g_world.boss_hits_taken); put_u32(25u, g_world.student_hits_taken);
    put_u32(26u, g_world.boss_bullets_spawned); put_u32(27u, g_world.student_bullets_spawned);
    put_u32(28u, g_world.spawn_overflow_count);
    put_f32(29u, g_world.cfg.field_w); put_f32(30u, g_world.cfg.field_h);
    put_u32(31u, view.marked_target);
    for (uint32_t p = 0u; p < DEMO_PATTERN_COUNT; ++p) {
        put_u32(32u + p, (uint32_t)g_world.cfg.patterns[p].cost);
        put_u32(36u + p, view.pattern_available[p] ? 1u : 0u);
    }
    put_u32(44u, g_world.events.dropped);
    put_u32(45u, g_world.wave_index); put_u32(46u, g_world.waves_cleared);
    put_u32(47u, (uint32_t)g_world.wave_phase); put_u32(48u, g_world.next_wave_students);
    put_u32(49u, (uint32_t)g_world.wave_spawn_tick);
    put_u32(50u, (uint32_t)g_world.gpa_hundredths);
    put_u32(51u, g_world.students_defeated); put_u32(52u, g_world.students_deployed);
    put_u32(53u, g_world.spawn_preview_count);
    put_u32(55u, g_world.cfg.gpa_half_saturation_kills);
    put_u32(56u, (uint32_t)g_world.cfg.gpa_max_hundredths);
    if (g_world.plan.active && g_world.plan.manual_aim) {
        put_f32(57u, g_world.plan.aim_dir_x); put_f32(58u, g_world.plan.aim_dir_y);
        put_u32(59u, 1u);
    }
    g_snapshot_size = cursor * 4u;
    return g_snapshot;
}

uint32_t demo_snapshot_size(void) { return g_snapshot_size; }

void demo_dispose(void) {
    g_ready = 0; g_snapshot_size = 0u; g_preview_size = 0u;
    g_ray_count = 0u; g_ray_plan = 0u; g_ray_valid = 0;
    memset(&g_world, 0, sizeof(g_world));
}
