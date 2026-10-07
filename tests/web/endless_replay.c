/* Finite public-observation scripted probe for the shipped v4 endless game.
 * Only real world_step resolves gameplay. No bullets, HP, actors, config or
 * RNG state are injected/changed. The controller sees current public actors,
 * on-screen student bullets and HUD availability; it receives no World.
 * Optional ENDLESS_SNAPSHOT_TRACE replays the chosen legal inputs through the
 * exact native Wasm bridge and writes length-prefixed public ABI snapshots.
 */
#include "world.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef ENDLESS_SNAPSHOT_TRACE
#include "demo_bridge.h"
#endif

#define TICK_BUDGET 9000u
#define CONTROLLER_LIMIT 6u
#define CPU_BUDGET_SECONDS 60.0

typedef struct VisibleStudent { float x, y; bool alive; } VisibleStudent;
typedef struct VisibleBullet { float x, y, vx, vy, radius; } VisibleBullet;
typedef struct PublicState {
    int32_t tick;
    float boss_x, boss_y;
    VisibleStudent students[DEMO_MAX_STUDENTS];
    uint32_t student_count;
    VisibleBullet bullets[DEMO_MAX_PROJECTILES];
    uint32_t bullet_count;
    bool available[DEMO_PATTERN_COUNT];
} PublicState;

typedef struct Script {
    const char *name;
    unsigned movement; /* 1 chase, 2 clockwise orbit, 3 counterclockwise orbit */
    float distance, avoidance;
} Script;

typedef struct PublicResult {
    int32_t status, tick, boss_hp, gpa_hundredths, energy;
    uint32_t boss_hits, student_hits, students_defeated, students_deployed;
    uint32_t wave_index, student_count, waves_cleared, wave_phase;
    uint32_t accepted, rejected, boss_bullets, student_bullets;
    bool truncated;
} PublicResult;

typedef struct Run {
    const Script *script;
    BossInput *inputs;
    PublicResult *milestones;
    unsigned ticks, milestone_count;
    bool reached_second_wave;
    PublicResult result;
} Run;

static const float directions[9][2] = {
    {0,0}, {1,0}, {0.70710677f,0.70710677f}, {0,1}, {-0.70710677f,0.70710677f},
    {-1,0}, {-0.70710677f,-0.70710677f}, {0,-1}, {0.70710677f,-0.70710677f}
};

/* The decision observation allowlist: no future spawn/warning geometry,
 * hidden bot state, RNG, phase timers or predicted copied World. */
static void observe_public(const WorldView *view, PublicState *out) {
    memset(out, 0, sizeof(*out));
    out->tick = view->tick;
    out->boss_x = view->boss->x;
    out->boss_y = view->boss->y;
    out->student_count = view->student_count;
    for (uint32_t i = 0; i < out->student_count; ++i) {
        out->students[i].x = view->students[i].x;
        out->students[i].y = view->students[i].y;
        out->students[i].alive = view->students[i].alive;
    }
    for (uint32_t i = 0; i < view->projectile_capacity; ++i) {
        const Projectile *p = &view->projectiles[i];
        if (!p->active || p->faction != DEMO_FACTION_STUDENT ||
            p->x < 0 || p->x > 960 || p->y < 0 || p->y > 720) continue;
        VisibleBullet *b = &out->bullets[out->bullet_count++];
        b->x = p->x; b->y = p->y;
        b->vx = p->vx; b->vy = p->vy; b->radius = p->radius;
    }
    for (unsigned p = 0; p < DEMO_PATTERN_COUNT; ++p)
        out->available[p] = view->pattern_available[p];
}

/* Short straight-line estimate uses only visible launched bullets and public
 * Boss speed 270; it selects keyboard input, not collision or future spawns. */
static float visible_risk(const PublicState *s, float mx, float my) {
    float score = 0;
    for (uint32_t i = 0; i < s->bullet_count; ++i) {
        const VisibleBullet *b = &s->bullets[i];
        float rx = b->x - s->boss_x, ry = b->y - s->boss_y;
        float vx = b->vx - mx * 270, vy = b->vy - my * 270;
        float vv = vx * vx + vy * vy;
        float t = vv > 0 ? -(rx * vx + ry * vy) / vv : 0;
        if (t < 0) t = 0;
        if (t > 0.45f) t = 0.45f;
        float distance = hypotf(rx + vx * t, ry + vy * t);
        float safe = 22 + b->radius + 10;
        if (distance < safe) score += 1 + (safe - distance) / safe;
    }
    return score;
}

static void choose_legal_input(const PublicState *s, const Script *script,
                               int32_t *last_request, BossInput *in) {
    boss_input_clear(in);
    int target = -1;
    float distance_sq = 0;
    for (uint32_t i = 0; i < s->student_count; ++i) {
        if (!s->students[i].alive) continue;
        float dx = s->students[i].x - s->boss_x, dy = s->students[i].y - s->boss_y;
        float d = dx * dx + dy * dy;
        if (target < 0 || d < distance_sq) { target = (int)i; distance_sq = d; }
    }
    if (target < 0) return; /* During the wave gap no hidden upcoming actor is targeted. */
    float distance = sqrtf(distance_sq);
    float ux = distance > 0 ? (s->students[target].x - s->boss_x) / distance : 0;
    float uy = distance > 0 ? (s->students[target].y - s->boss_y) / distance : 0;
    float desired_x = 0, desired_y = 0;
    if (script->movement == 1) {
        if (distance > script->distance + 8) { desired_x = ux; desired_y = uy; }
        else if (distance < script->distance - 8) { desired_x = -ux; desired_y = -uy; }
    } else {
        float radial = (distance - script->distance) / 80;
        if (radial < -1) radial = -1;
        if (radial > 1) radial = 1;
        float sign = script->movement == 2 ? 1.0f : -1.0f;
        desired_x = ux * radial - uy * sign;
        desired_y = uy * radial + ux * sign;
    }
    float desired_len = hypotf(desired_x, desired_y);
    if (desired_len > 0) { desired_x /= desired_len; desired_y /= desired_len; }
    float best_score = 1.0e30f;
    unsigned best = 0;
    for (unsigned candidate = 0; candidate < 9; ++candidate) {
        float mx = directions[candidate][0], my = directions[candidate][1];
        float ex = mx - desired_x, ey = my - desired_y;
        float score = ex * ex + ey * ey + script->avoidance * visible_risk(s, mx, my);
        float x = s->boss_x + mx * 270 * 0.35f, y = s->boss_y + my * 270 * 0.35f;
        if (x < 22 || x > 938 || y < 22 || y > 698) score += 8;
        if (score < best_score) { best = candidate; best_score = score; }
    }
    in->move_x = directions[best][0];
    in->move_y = directions[best][1];
    if (s->tick - *last_request >= 12 && distance >= 35 && s->available[0]) {
        in->attack_requested[DEMO_PATTERN_RING] = true;
        *last_request = s->tick;
    }
}

static PublicResult result_public(const WorldView *v) {
    PublicResult result;
    memset(&result, 0, sizeof(result));
    result.status = (int32_t)v->status; result.tick = v->tick; result.boss_hp = v->boss->hp;
    result.gpa_hundredths = v->gpa_hundredths; result.energy = v->energy;
    result.boss_hits = v->boss_hits_taken; result.student_hits = v->student_hits_taken;
    result.students_defeated = v->students_defeated; result.students_deployed = v->students_deployed;
    result.wave_index = v->wave_index; result.student_count = v->student_count;
    result.waves_cleared = v->waves_cleared; result.wave_phase = v->wave_phase;
    result.accepted = v->attack_accept_count; result.rejected = v->attack_reject_count;
    result.truncated = v->truncated;
    return result;
}

static Run run_public_script(const DemoConfig *cfg, const Script *script) {
    Run run;
    memset(&run, 0, sizeof(run));
    run.script = script;
    run.inputs = calloc(TICK_BUDGET, sizeof(*run.inputs));
    run.milestones = calloc(TICK_BUDGET + 1, sizeof(*run.milestones));
    World world;
    WorldView view;
    PublicState public_state;
    int32_t last_request = -12;
    if (!run.inputs || !run.milestones || !world_reset(&world, cfg, cfg->seed_default)) exit(2);
    world_make_view(&world, &view);
    run.milestones[run.milestone_count++] = result_public(&view);
    for (; run.ticks < TICK_BUDGET; ++run.ticks) {
        if (view.status != DEMO_STATUS_RUNNING || view.truncated) break;
        uint32_t prior_wave = view.wave_index, prior_kills = view.students_defeated;
        DemoStudentWavePhase prior_phase = view.wave_phase;
        observe_public(&view, &public_state);
        choose_legal_input(&public_state, script, &last_request, &run.inputs[run.ticks]);
        world_step(&world, &run.inputs[run.ticks]);
        world_make_view(&world, &view);
        if (view.wave_index >= 2 && view.student_count >= 4 && view.students_defeated >= 3)
            run.reached_second_wave = true;
        if (prior_wave != view.wave_index || prior_phase != view.wave_phase ||
            prior_kills != view.students_defeated || view.tick % 300 == 0 ||
            view.status != DEMO_STATUS_RUNNING || run.ticks + 1 == TICK_BUDGET)
            run.milestones[run.milestone_count++] = result_public(&view);
    }
    run.result = result_public(&view);
    /* These counters are reporting only, never passed to the decision controller. */
    run.result.boss_bullets = world.boss_bullets_spawned;
    run.result.student_bullets = world.student_bullets_spawned;
    printf("%s: tick=%d status=%d wave=%u students=%u kills=%u gpa=%d hp=%d clear=%u\n",
        script->name, run.result.tick, run.result.status, run.result.wave_index,
        run.result.student_count, run.result.students_defeated, run.result.gpa_hundredths,
        run.result.boss_hp, run.result.waves_cleared);
    return run;
}

static void free_run(Run *run) { free(run->inputs); free(run->milestones); }

static unsigned attack_mask(const BossInput *input) {
    unsigned mask = 0;
    for (unsigned p = 0; p < DEMO_PATTERN_COUNT; ++p)
        if (input->attack_requested[p]) mask |= 1u << p;
    return mask;
}

static void write_result(FILE *file, const PublicResult *r) {
    fprintf(file, "{\"status\":%d,\"tick\":%d,\"boss_hp\":%d,\"boss_hits\":%u,"
        "\"student_hits\":%u,\"kills\":%u,\"students_defeated\":%u,"
        "\"students_deployed\":%u,\"gpa_hundredths\":%d,\"wave_index\":%u,"
        "\"student_count\":%u,\"waves_cleared\":%u,\"wave_phase\":%u,"
        "\"energy\":%d,\"accepted\":%u,\"rejected\":%u,\"truncated\":%s}",
        r->status, r->tick, r->boss_hp, r->boss_hits, r->student_hits,
        r->students_defeated, r->students_defeated, r->students_deployed,
        r->gpa_hundredths, r->wave_index, r->student_count, r->waves_cleared,
        r->wave_phase, r->energy, r->accepted, r->rejected, r->truncated ? "true" : "false");
}

static void write_files(const DemoConfig *cfg, const Run *chosen, const PublicResult *attempts,
                         const Script *scripts, unsigned count, double cpu_seconds,
                         const char *inputs_path, const char *result_path) {
    FILE *file = fopen(inputs_path, "wb");
    if (!file) { fprintf(stderr, "cannot open %s\n", inputs_path); exit(3); }
    fprintf(file, "{\n\"schema_version\":1,\"mode\":\"endless\",\"config_version\":%u,"
        "\"seed_lo\":%u,\"seed_hi\":0,\"students\":%u,\"script\":\"%s\","
        "\"tick_budget\":%u,\"expected\":", cfg->version, cfg->seed_default,
        cfg->student_count, chosen->script->name, TICK_BUDGET);
    write_result(file, &chosen->result);
    fprintf(file, ",\n\"inputs\":[\n");
    for (unsigned tick = 0; tick < chosen->ticks; ++tick) {
        const BossInput *input = &chosen->inputs[tick];
        fprintf(file, "%s{\"tick\":%u,\"move_x\":%.9g,\"move_y\":%.9g,"
            "\"pointer_valid\":0,\"pointer_x\":0,\"pointer_y\":0,\"attack_mask\":%u}",
            tick ? ",\n" : "", tick, input->move_x, input->move_y, attack_mask(input));
    }
    fprintf(file, "\n]}\n");
    if (fclose(file)) exit(3);
    file = fopen(result_path, "wb");
    if (!file) { fprintf(stderr, "cannot open %s\n", result_path); exit(3); }
    fprintf(file, "{\n\"config_version\":%u,\"seed_lo\":%u,\"seed_hi\":0,"
        "\"initial_students\":%u,\"controller\":\"%s\",\"sampling_type\":"
        "\"finite_public_observation_script_not_ml_or_human\",\"script_count\":%u,"
        "\"tick_budget_per_script\":%u,\"cpu_seconds\":%.3f,"
        "\"reached_second_wave\":%s,\"budget_unfinished\":%s,\"expected\":",
        cfg->version, cfg->seed_default, cfg->student_count, chosen->script->name,
        count, TICK_BUDGET, cpu_seconds, chosen->reached_second_wave ? "true" : "false",
        chosen->result.status == DEMO_STATUS_RUNNING ? "true" : "false");
    write_result(file, &chosen->result);
    fprintf(file, ",\n\"milestones\":[");
    for (unsigned i = 0; i < chosen->milestone_count; ++i) {
        if (i) fprintf(file, ",\n");
        write_result(file, &chosen->milestones[i]);
    }
    fprintf(file, "],\n\"attempts\":[");
    for (unsigned i = 0; i < count; ++i) {
        fprintf(file, "%s{\"controller\":\"%s\",\"result\":", i ? ",\n" : "", scripts[i].name);
        write_result(file, &attempts[i]);
        fprintf(file, "}");
    }
    fprintf(file, "]\n}\n");
    if (fclose(file)) exit(3);
}

#ifdef ENDLESS_SNAPSHOT_TRACE
static void save_bridge_trace(const DemoConfig *cfg, const Run *run, const char *path) {
    FILE *file = fopen(path, "wb");
    if (!file || !demo_reset(cfg->seed_default, 0, cfg->student_count)) exit(4);
    for (unsigned tick = 0; tick <= run->ticks; ++tick) {
        if (tick > 0) {
            const BossInput *in = &run->inputs[tick - 1];
            if (!demo_step(in->move_x, in->move_y, 0, 0, 0, attack_mask(in))) exit(4);
        }
        const uint8_t *snapshot = demo_snapshot();
        uint32_t length = demo_snapshot_size();
        if (!snapshot || length == 0 || fwrite(&length, 4, 1, file) != 1 ||
            fwrite(snapshot, 1, length, file) != length) exit(4);
    }
    demo_dispose();
    if (fclose(file)) exit(4);
    printf("native ABI trace saved: %s (%u snapshots)\n", path, run->ticks + 1);
}
#endif

int main(int argc, char **argv) {
    DemoConfig cfg;
    if (!demo_config_init(&cfg) || cfg.version != 4 || !cfg.endless_mode ||
        cfg.student_count != 3 || cfg.seed_default != 20261006u || cfg.max_ticks != 0 ||
        cfg.patterns[DEMO_PATTERN_SHOWER].cost != 100 || cfg.student_hp != 3 || cfg.boss_hp != 6) {
        fprintf(stderr, "requires shipped v4 default endless config\n"); return 2;
    }
    const char *inputs_path = argc > 1 ? argv[1] : "build/web-validation/endless-inputs.json";
    const char *result_path = argc > 2 ? argv[2] : "build/web-validation/endless-native-result.json";
    const Script scripts[CONTROLLER_LIMIT] = {
        {"chase-d150-avoid20-ring", 1, 150, 20},
        {"chase-d150-avoid4-ring", 1, 150, 4},
        {"chase-d120-avoid20-ring", 1, 120, 20},
        {"chase-d180-avoid20-ring", 1, 180, 20},
        {"orbit-clockwise-d150-avoid4-ring", 2, 150, 4},
        {"chase-d48-avoid20-ring-regression", 1, 48, 20}
    };
    clock_t start = clock();
    PublicResult attempts[CONTROLLER_LIMIT];
    Run chosen;
    memset(&chosen, 0, sizeof(chosen));
    unsigned count = 0;
    for (unsigned i = 0; i < CONTROLLER_LIMIT; ++i) {
        if (i > 0 && (double)(clock() - start) / CLOCKS_PER_SEC >= CPU_BUDGET_SECONDS) break;
        Run run = run_public_script(&cfg, &scripts[i]);
        attempts[count++] = run.result;
        bool better = !chosen.inputs || run.reached_second_wave ||
            run.result.wave_index > chosen.result.wave_index ||
            (run.result.wave_index == chosen.result.wave_index &&
             run.result.students_defeated > chosen.result.students_defeated);
        if (better) { free_run(&chosen); chosen = run; }
        else free_run(&run);
        if (chosen.reached_second_wave) break;
    }
    double seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
    write_files(&cfg, &chosen, attempts, scripts, count, seconds, inputs_path, result_path);
#ifdef ENDLESS_SNAPSHOT_TRACE
    if (argc > 3) save_bridge_trace(&cfg, &chosen, argv[3]);
#endif
    fprintf(stderr, "finite probe: scripts=%u cpu_seconds=%.3f reached_second_wave=%s\n",
        count, seconds, chosen.reached_second_wave ? "true" : "false");
    free_run(&chosen);
    return 0;
}
