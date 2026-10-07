/* v6 mouse-directed attacks. The plan is the complete immutable geometry;
 * previews and real spawns call the same emitter without reading world RNG.
 * u is the firing direction, v=(-u.y,u.x) the transverse axis. Walls and rain
 * sample v, intersect their parallel rays with the rectangle, and enter from
 * the opposite boundary. No individual point is clamped or duplicated. */
#include "pattern_aim.h"

#include <math.h>
#include <string.h>

#define AIM_DEG_TO_RAD 0.01745329251994329577f
#define AIM_RAD_TO_DEG 57.295779513082320876f

static bool positive(float value) { return isfinite(value) && value > 0.0f; }
static bool nonnegative(float value) { return isfinite(value) && value >= 0.0f; }
static int32_t round_tick(float value) {
    if (!nonnegative(value) || value > 1.0e9f) return -1;
    return (int32_t)(value + 0.5f);
}

/* Slab clipping is done in double precision, including nearly axial rays.
 * Insets shrink to half a short chord, so they never jump beyond its exit. */
static bool boundary_origin(const AttackPlan *plan, float transverse, float *x, float *y) {
    const double ux = plan->aim_dir_x, uy = plan->aim_dir_y;
    const double qx = plan->manual_field_w * 0.5 - uy * transverse;
    const double qy = plan->manual_field_h * 0.5 + ux * transverse;
    double enter = -1.0e300, exit = 1.0e300;
    const double q[2] = {qx, qy}, u[2] = {ux, uy};
    const double extent[2] = {plan->manual_field_w, plan->manual_field_h};
    for (int axis = 0; axis < 2; ++axis) {
        if (fabs(u[axis]) < 1.0e-12) {
            if (q[axis] < 0.0 || q[axis] > extent[axis]) return false;
        } else {
            double a = -q[axis] / u[axis], b = (extent[axis] - q[axis]) / u[axis];
            if (a > b) { const double swap = a; a = b; b = swap; }
            if (a > enter) enter = a;
            if (b < exit) exit = b;
        }
    }
    if (!(exit > enter) || !isfinite(enter) || !isfinite(exit)) return false;
    double inset = plan->manual_entry_inset_px;
    if (inset > (exit - enter) * 0.5) inset = (exit - enter) * 0.5;
    const double t = enter + inset;
    *x = (float)(qx + ux * t); *y = (float)(qy + uy * t);
    return isfinite(*x) && isfinite(*y) && *x >= -0.001f && *y >= -0.001f &&
           *x <= plan->manual_field_w + 0.001f && *y <= plan->manual_field_h + 0.001f;
}

static bool shot_geometry(const AttackPlan *plan, int32_t wave, int32_t shot,
                          float *x, float *y, float *dx, float *dy) {
    *dx = plan->aim_dir_x; *dy = plan->aim_dir_y;
    if (plan->pattern == DEMO_PATTERN_RING || plan->pattern == DEMO_PATTERN_MINE) {
        const float fraction = plan->shots_per_wave == 1 ? 0.0f :
            (float)shot / (float)(plan->shots_per_wave - 1) - 0.5f;
        const float angle = fraction * plan->gap_span_deg * AIM_DEG_TO_RAD;
        const float c = cosf(angle), s = sinf(angle);
        *dx = plan->aim_dir_x * c - plan->aim_dir_y * s;
        *dy = plan->aim_dir_x * s + plan->aim_dir_y * c;
        *x = plan->origin_x; *y = plan->origin_y;
        return true;
    }
    float transverse;
    if (plan->pattern == DEMO_PATTERN_COURSE) {
        const int32_t channel = ((int32_t)plan->wave_offset + wave) % 3;
        const int32_t first_count = (plan->shots_per_wave + 1) / 2;
        const int32_t group = shot < first_count ? 0 : 1;
        const int32_t index = group == 0 ? shot : shot - first_count;
        const int32_t count = group == 0 ? first_count : plan->shots_per_wave - first_count;
        int32_t column = group;
        if (column >= channel) ++column;
        const float width = (plan->manual_transverse_max - plan->manual_transverse_min) / 3.0f;
        const float center = plan->manual_transverse_min + ((float)column + 0.5f) * width;
        transverse = center + (count == 1 ? 0.0f :
            ((float)index / (float)(count - 1) * 2.0f - 1.0f) * plan->lane_spread_px);
    } else if (plan->pattern == DEMO_PATTERN_SHOWER) {
        const int32_t gap_index = (int32_t)plan->wave_offset + wave;
        transverse = plan->manual_transverse_min + ((float)shot + 0.5f) * plan->manual_scan_step_px;
        if (shot >= gap_index) transverse += plan->corridor_width;
    } else return false;
    return boundary_origin(plan, transverse, x, y);
}

bool pattern_aim_make_plan(const PatternRequest *request, const DemoConfig *config,
                           AttackPlan *out) {
    if (request == NULL || config == NULL || out == NULL || !request->manual_aim ||
        (int)request->pattern < 0 || request->pattern >= DEMO_PATTERN_COUNT ||
        !isfinite(request->aim_dir_x) || !isfinite(request->aim_dir_y) ||
        !isfinite(request->origin_x) || !isfinite(request->origin_y) ||
        request->start_tick < 0 || request->student_count > DEMO_MAX_STUDENTS ||
        !positive(request->student_radius) || !positive(config->field_w) ||
        !positive(config->field_h) || request->origin_x < 0.0f || request->origin_y < 0.0f ||
        request->origin_x > config->field_w || request->origin_y > config->field_h ||
        !positive(config->boss_bullet_radius) || !positive(config->boss_bullet_damage) ||
        config->boss_bullet_lifetime_ticks <= 0) return false;
    const double length = hypot((double)request->aim_dir_x, (double)request->aim_dir_y);
    if (!isfinite(length) || length < 1.0e-6) return false;
    const PatternConfig *pc = &config->patterns[(int)request->pattern];
    if (!positive(pc->bullet_speed) || pc->windup_ticks < 0 || pc->active_ticks <= 0 ||
        pc->wave_count < 1 || pc->wave_count > 32 || pc->manual_shots_per_wave < 1 ||
        pc->manual_shots_per_wave > (int32_t)DEMO_MAX_ACTIVE_PLAN_PROJECTILES ||
        !nonnegative(pc->first_spawn_sec) || !nonnegative(pc->wave_interval_sec) ||
        !nonnegative(pc->manual_arc_span_deg) || pc->manual_arc_span_deg >= 180.0f ||
        !nonnegative(pc->manual_entry_inset_px)) return false;
    AttackPlan plan;
    memset(&plan, 0, sizeof(plan));
    plan.active = true; plan.manual_aim = true; plan.pattern = request->pattern;
    /* plan_id is assigned by the accepting caller; no target or random draw. */
    plan.origin_x = request->origin_x; plan.origin_y = request->origin_y;
    plan.aim_dir_x = (float)(request->aim_dir_x / length);
    plan.aim_dir_y = (float)(request->aim_dir_y / length);
    plan.aim_x = plan.origin_x + plan.aim_dir_x;
    plan.aim_y = plan.origin_y + plan.aim_dir_y;
    plan.lock_speed = pc->bullet_speed; plan.start_tick = request->start_tick;
    plan.windup_ticks = pc->windup_ticks; plan.active_ticks = pc->active_ticks;
    plan.wave_count = pc->wave_count; plan.shots_per_wave = pc->manual_shots_per_wave;
    plan.gap_angle_deg = atan2f(plan.aim_dir_y, plan.aim_dir_x) * AIM_RAD_TO_DEG;
    plan.gap_span_deg = pc->manual_arc_span_deg; plan.corridor_width = pc->corridor_width;
    plan.manual_field_w = config->field_w; plan.manual_field_h = config->field_h;
    plan.manual_entry_inset_px = pc->manual_entry_inset_px;
    plan.manual_bullet_radius = config->boss_bullet_radius;
    plan.manual_bullet_damage = config->boss_bullet_damage;
    plan.manual_bullet_lifetime_ticks = config->boss_bullet_lifetime_ticks;
    const float half_span = (fabsf(plan.aim_dir_y) * config->field_w +
                             fabsf(plan.aim_dir_x) * config->field_h) * 0.5f;
    if (!positive(half_span)) return false;
    plan.manual_transverse_min = -half_span; plan.manual_transverse_max = half_span;
    for (int32_t wave = 0; wave < plan.wave_count; ++wave) {
        const float at = (pc->first_spawn_sec + wave * pc->wave_interval_sec) * DEMO_TICKS_PER_SECOND;
        const int32_t tick = round_tick(at);
        if (tick < 0 || tick >= plan.active_ticks) return false;
        plan.wave_tick[wave] = at;
    }
    if (plan.pattern == DEMO_PATTERN_RING || plan.pattern == DEMO_PATTERN_MINE) {
        if (!positive(plan.gap_span_deg)) return false;
        const float safety = plan.pattern == DEMO_PATTERN_MINE ? pc->spawn_safety_radius :
                             request->student_radius;
        if (!nonnegative(safety)) return false;
        for (uint32_t i = 0; i < request->student_count; ++i) {
            if (!request->student_alive[i]) continue;
            if (!isfinite(request->student_x[i]) || !isfinite(request->student_y[i]) ||
                hypot((double)request->student_x[i] - plan.origin_x,
                      (double)request->student_y[i] - plan.origin_y) < safety) return false;
        }
    } else if (plan.pattern == DEMO_PATTERN_COURSE) {
        const float width = 2.0f * half_span / 3.0f;
        if (plan.shots_per_wave < 2 || !positive(pc->lane_spread_px) ||
            !positive(pc->corridor_width)) return false;
        plan.lane_spread_px = pc->lane_spread_px / (config->field_w / 3.0f) * width;
        const float maximum = width * 0.5f - plan.manual_bullet_radius;
        if (plan.lane_spread_px > maximum) plan.lane_spread_px = maximum;
        if (!positive(plan.lane_spread_px) || width < plan.corridor_width) return false;
        const float boss_transverse = -plan.aim_dir_y * (plan.origin_x - config->field_w * 0.5f) +
                                      plan.aim_dir_x * (plan.origin_y - config->field_h * 0.5f);
        int32_t column = (int32_t)((boss_transverse + half_span) / width);
        if (column < 0) column = 0;
        if (column > 2) column = 2;
        plan.wave_offset = (float)column;
    } else {
        const double radius = plan.manual_bullet_radius;
        const double gap = pc->corridor_width;
        const double free_span = 2.0 * half_span - 4.0 * radius - gap;
        const double min_pitch = 2.0 * radius, max_pitch = gap - min_pitch;
        if (!positive(pc->manual_density_pitch_px) || !isfinite(gap) || gap < 100.0 ||
            !(free_span > 0.0) || !(max_pitch > min_pitch)) return false;
        double minimum = floor(free_span / max_pitch) + 1.0;
        if (minimum < 2.0 * plan.wave_count) minimum = 2.0 * plan.wave_count;
        double maximum = floor(free_span / min_pitch);
        if (maximum > DEMO_MAX_ACTIVE_PLAN_PROJECTILES) maximum = DEMO_MAX_ACTIVE_PLAN_PROJECTILES;
        if (minimum > maximum || minimum > DEMO_MAX_ACTIVE_PLAN_PROJECTILES) return false;
        double desired = floor(free_span / pc->manual_density_pitch_px + 0.5);
        if (desired < minimum) desired = minimum;
        if (desired > maximum) desired = maximum;
        plan.shots_per_wave = (int32_t)desired;
        plan.manual_transverse_min += (float)(2.0 * radius);
        plan.manual_transverse_max -= (float)(2.0 * radius);
        plan.manual_scan_step_px = (float)(free_span / plan.shots_per_wave);
        /* Integer gap index advances one pitch. Both sides always retain rays. */
        plan.wave_offset = (float)((plan.shots_per_wave - (plan.wave_count - 1)) / 2);
        if (plan.wave_offset < 1.0f || plan.wave_offset + plan.wave_count - 1 >= plan.shots_per_wave)
            return false;
    }
    /* Verify every planned origin before committing. This also handles unusual
     * aspect ratios/diagonal short chords and coincident-wave output capacity. */
    for (int32_t wave = 0; wave < plan.wave_count; ++wave) {
        int32_t simultaneous = 0;
        for (int32_t other = 0; other < plan.wave_count; ++other)
            if (round_tick(plan.wave_tick[wave]) == round_tick(plan.wave_tick[other])) ++simultaneous;
        if (simultaneous * plan.shots_per_wave > (int32_t)DEMO_MAX_ACTIVE_PLAN_PROJECTILES) return false;
        for (int32_t shot = 0; shot < plan.shots_per_wave; ++shot) {
            float x, y, dx, dy;
            if (!shot_geometry(&plan, wave, shot, &x, &y, &dx, &dy)) return false;
        }
    }
    plan.lock_checked_at_spawn = true;
    *out = plan;
    return true;
}

bool pattern_aim_emit(const AttackPlan *plan, const DemoConfig *config,
                     uint32_t attack_tick, ProjectileSpawnBuffer *out) {
    (void)config; /* Configuration changes cannot move a locked plan's rays. */
    if (plan == NULL || out == NULL || !plan->active || !plan->manual_aim ||
        (int)plan->pattern < 0 || plan->pattern >= DEMO_PATTERN_COUNT ||
        plan->wave_count < 1 || plan->wave_count > 32 || plan->shots_per_wave < 1 ||
        plan->shots_per_wave > (int32_t)DEMO_MAX_ACTIVE_PLAN_PROJECTILES ||
        !positive(plan->lock_speed) || !positive(plan->manual_bullet_radius) ||
        !positive(plan->manual_field_w) || !positive(plan->manual_field_h)) return false;
    bool emitted = false;
    for (int32_t wave = 0; wave < plan->wave_count; ++wave) {
        const int32_t due = round_tick(plan->wave_tick[wave]);
        if (due < 0 || attack_tick != (uint32_t)due) continue;
        for (int32_t shot = 0; shot < plan->shots_per_wave; ++shot) {
            float x, y, dx, dy;
            if (!shot_geometry(plan, wave, shot, &x, &y, &dx, &dy)) return false;
            Projectile spec;
            memset(&spec, 0, sizeof(spec));
            spec.faction = DEMO_FACTION_BOSS; spec.source_id = 1u;
            spec.plan_id = plan->plan_id; spec.source_pattern = plan->pattern;
            spec.x = spec.px = x; spec.y = spec.py = y;
            spec.vx = dx * plan->lock_speed; spec.vy = dy * plan->lock_speed;
            spec.radius = plan->manual_bullet_radius; spec.damage = plan->manual_bullet_damage;
            spec.lifetime_ticks = plan->manual_bullet_lifetime_ticks;
            (void)spawn_buffer_push(out, &spec);
        }
        emitted = true;
    }
    return emitted;
}
