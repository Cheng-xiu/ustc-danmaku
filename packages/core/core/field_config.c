#include "field_config.h"

#include "pattern_course.h"
#include "pattern_aim.h"
#include "pattern_shower.h"

#include <math.h>
#include <string.h>

static bool legal_axis(float value, float radius, float extent) {
    return isfinite(value) && value >= radius && value <= extent - radius;
}

static float map_axis(float value, float radius, float old_extent, float new_extent) {
    return radius + (value - radius) / (old_extent - 2.0f * radius) *
                    (new_extent - 2.0f * radius);
}

/* Actual generators are the final geometry authority. This disposable RNG is
 * not a world's RNG and its output is never retained or exposed as a plan. */
static bool field_patterns_legal(const DemoConfig *config) {
    PatternRequest request;
    AttackPlan plan;
    Rng rng;
    memset(&request, 0, sizeof(request));
    memset(&plan, 0, sizeof(plan));
    request.origin_x = config->field_w * 0.5f;
    request.origin_y = config->field_h * 0.5f;
    request.target_x = config->student_spawn_x[0];
    request.target_y = config->student_spawn_y[0];
    request.target_id = 100u;
    request.field_w = config->field_w;
    request.field_h = config->field_h;
    request.pattern = DEMO_PATTERN_COURSE;
    rng_seed(&rng, UINT64_C(20261007), UINT64_C(1));
    if (!pattern_course_make_plan(&request, config, &rng, &plan)) return false;
    memset(&plan, 0, sizeof(plan));
    request.pattern = DEMO_PATTERN_SHOWER;
    rng_seed(&rng, UINT64_C(20261007), UINT64_C(1));
    if (!pattern_shower_make_plan(&request, config, &rng, &plan)) return false;
    /* Manual walls/rain use the rectangle's transverse projection, not just
     * its width. Its extrema are min(W,H) on an axis and hypot(W,H) on the
     * diagonal perpendicular to a corner-to-corner vector. Check both axes
     * and that maximum; all intermediate widths lie between these bounds.
     * Actual factories remain the authority for gap, pitch, count and origin
     * legality, so acceptance never silently shrinks a configured corridor. */
    const double diagonal = hypot((double)config->field_w, (double)config->field_h);
    if (!isfinite(diagonal) || diagonal <= 0.0) return false;
    const float directions[3][2] = {
        {1.0f, 0.0f}, {0.0f, 1.0f},
        {(float)(config->field_h / diagonal), (float)(config->field_w / diagonal)}
    };
    request.manual_aim = true;
    request.student_radius = config->student_radius;
    for (unsigned direction = 0u; direction < 3u; ++direction) {
        request.aim_dir_x = directions[direction][0];
        request.aim_dir_y = directions[direction][1];
        request.pattern = DEMO_PATTERN_COURSE;
        if (!pattern_aim_make_plan(&request, config, &plan)) return false;
        request.pattern = DEMO_PATTERN_SHOWER;
        if (!pattern_aim_make_plan(&request, config, &plan)) return false;
    }
    return true;
}

static bool resize_shower(PatternConfig *shower, float old_width, float new_width,
                          float bullet_radius) {
    /* The generator reserves 2r at each edge. Its fixed gap must retain a
     * positive overlap as the gap moves one pitch between adjacent waves. */
    const double old_free = (double)old_width - 4.0 * bullet_radius - shower->corridor_width;
    const double free_span = (double)new_width - 4.0 * bullet_radius - shower->corridor_width;
    const double min_pitch = 2.0 * bullet_radius;
    const double max_pitch = (double)shower->corridor_width - min_pitch;
    if (!(old_free > 0.0) || !(free_span > 0.0) || !(min_pitch > 0.0) ||
        !(max_pitch > min_pitch) || shower->wave_count < 1 ||
        shower->wave_count > 32 || shower->shots_per_wave < 1 ||
        shower->shots_per_wave > (int32_t)DEMO_MAX_ACTIVE_PLAN_PROJECTILES) return false;
    /* Strict pitch < max_pitch requires one more shot at exact division. */
    double minimum = floor(free_span / max_pitch) + 1.0;
    const double scan_minimum = 2.0 * shower->wave_count;
    if (minimum < scan_minimum) minimum = scan_minimum;
    double maximum = floor(free_span / min_pitch);
    if (maximum > DEMO_MAX_ACTIVE_PLAN_PROJECTILES) maximum = DEMO_MAX_ACTIVE_PLAN_PROJECTILES;
    if (minimum > maximum || minimum > DEMO_MAX_ACTIVE_PLAN_PROJECTILES) return false;
    double desired = floor(free_span / old_free * shower->shots_per_wave + 0.5);
    if (desired < minimum) desired = minimum;
    if (desired > maximum) desired = maximum;
    shower->shots_per_wave = (int32_t)desired;
    return true;
}

bool demo_config_set_field_size(DemoConfig *config, float width, float height) {
    if (config == NULL || !isfinite(width) || !isfinite(height) ||
        width <= 0.0f || height <= 0.0f ||
        !demo_config_validate(config, NULL, 0u)) return false;
    const float old_width = config->field_w, old_height = config->field_h;
    const float boss_radius = config->boss_radius, student_radius = config->student_radius;
    if (!isfinite(boss_radius) || !isfinite(student_radius) || boss_radius <= 0.0f ||
        student_radius <= 0.0f || old_width <= 2.0f * boss_radius ||
        old_height <= 2.0f * boss_radius || old_width <= 2.0f * student_radius ||
        old_height <= 2.0f * student_radius || width <= 2.0f * boss_radius ||
        height <= 2.0f * boss_radius || width <= 2.0f * student_radius ||
        height <= 2.0f * student_radius || height < 100.0f ||
        !legal_axis(config->boss_move_min_x, boss_radius, old_width) ||
        !legal_axis(config->boss_move_max_x, boss_radius, old_width) ||
        !legal_axis(config->boss_move_min_y, boss_radius, old_height) ||
        !legal_axis(config->boss_move_max_y, boss_radius, old_height)) return false;
    for (uint32_t i = 0; i < DEMO_MAX_STUDENTS; ++i) {
        if (!legal_axis(config->student_spawn_x[i], student_radius, old_width) ||
            !legal_axis(config->student_spawn_y[i], student_radius, old_height)) return false;
    }
    if (width == old_width && height == old_height) return field_patterns_legal(config);
    DemoConfig candidate = *config;
    candidate.field_w = width;
    candidate.field_h = height;
    candidate.boss_move_min_x = map_axis(config->boss_move_min_x, boss_radius, old_width, width);
    candidate.boss_move_max_x = map_axis(config->boss_move_max_x, boss_radius, old_width, width);
    candidate.boss_move_min_y = map_axis(config->boss_move_min_y, boss_radius, old_height, height);
    candidate.boss_move_max_y = map_axis(config->boss_move_max_y, boss_radius, old_height, height);
    for (uint32_t i = 0; i < DEMO_MAX_STUDENTS; ++i) {
        candidate.student_spawn_x[i] = map_axis(config->student_spawn_x[i], student_radius, old_width, width);
        candidate.student_spawn_y[i] = map_axis(config->student_spawn_y[i], student_radius, old_height, height);
    }
    candidate.patterns[DEMO_PATTERN_COURSE].lane_spread_px *= width / old_width;
    if (!resize_shower(&candidate.patterns[DEMO_PATTERN_SHOWER], old_width, width,
                       candidate.boss_bullet_radius) ||
        !demo_config_validate(&candidate, NULL, 0u) || !field_patterns_legal(&candidate)) return false;
    *config = candidate;
    return true;
}
