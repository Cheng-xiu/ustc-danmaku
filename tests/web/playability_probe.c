/* Finite scripted playability probe. Default config is never modified.
 * The controller accepts only PublicState, populated from visible current
 * actors, on-screen student bullets, HUD availability and the logical tick.
 * No World pointer, RNG, bot state, AttackPlan or future warning enters it.
 * Output inputs are legal eight-direction keyboard axes and isolated attack
 * edges (at least 12 ticks apart). This is scripted sampling, not training.
 */
#include "world.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PROBE_TICK_BUDGET 7200
#define PROBE_CPU_BUDGET_SEC 120.0

typedef struct VisibleStudent { float x, y; int hp; bool alive; } VisibleStudent;
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

/* This function is the complete decision-observation allowlist. */
static void public_observe(const WorldView *view, PublicState *out) {
    memset(out, 0, sizeof(*out));
    out->tick = view->tick;
    out->boss_x = view->boss->x;
    out->boss_y = view->boss->y;
    out->student_count = view->student_count;
    for (uint32_t i = 0; i < out->student_count; ++i) {
        const Actor *student = &view->students[i];
        out->students[i].x = student->x;
        out->students[i].y = student->y;
        out->students[i].hp = student->hp;
        out->students[i].alive = student->alive;
    }
    for (uint32_t i = 0; i < view->projectile_capacity; ++i) {
        const Projectile *p = &view->projectiles[i];
        if (!p->active || p->faction != DEMO_FACTION_STUDENT) continue;
        if (p->x < 0 || p->x > 960 || p->y < 0 || p->y > 720) continue;
        VisibleBullet *bullet = &out->bullets[out->bullet_count++];
        bullet->x = p->x; bullet->y = p->y;
        bullet->vx = p->vx; bullet->vy = p->vy; bullet->radius = p->radius;
    }
    for (int i = 0; i < DEMO_PATTERN_COUNT; ++i) out->available[i] = view->pattern_available[i];
}

/* Movement: 0=stand, 1=chase, 2=clockwise orbit, 3=counterclockwise orbit,
 * 4=perimeter waypoints. Attack: 0..3=one spell, 4=visible-context mix,
 * 5=explicit request cycle. Parameters describe controllers, not world rules. */
typedef struct Script { char name[80]; int movement, attack; float distance, dodge; } Script;
typedef struct Controller { int32_t last_request; unsigned cycle, waypoint; } Controller;

static const float directions[9][2] = {
    {0,0}, {1,0}, {0.70710677f,0.70710677f}, {0,1}, {-0.70710677f,0.70710677f},
    {-1,0}, {-0.70710677f,-0.70710677f}, {0,-1}, {0.70710677f,-0.70710677f}
};

static int nearest_student(const PublicState *state) {
    int best = -1;
    float best_distance = 0;
    for (uint32_t i = 0; i < state->student_count; ++i) {
        if (!state->students[i].alive) continue;
        float dx = state->students[i].x - state->boss_x;
        float dy = state->students[i].y - state->boss_y;
        float d = dx * dx + dy * dy;
        if (best < 0 || d < best_distance) { best = (int)i; best_distance = d; }
    }
    return best;
}

/* Straight-line risk estimate from currently on-screen bullets only. It does
 * not step a copied world or generate future shots; 270 is the published Boss
 * speed. This score selects an input; the real C core alone resolves hits. */
static float visible_bullet_risk(const PublicState *state, float mx, float my) {
    float score = 0;
    for (uint32_t i = 0; i < state->bullet_count; ++i) {
        const VisibleBullet *p = &state->bullets[i];
        float rx = p->x - state->boss_x, ry = p->y - state->boss_y;
        float vx = p->vx - mx * 270, vy = p->vy - my * 270;
        float vv = vx * vx + vy * vy;
        float t = vv > 0 ? -(rx * vx + ry * vy) / vv : 0;
        if (t < 0) t = 0;
        if (t > 0.45f) t = 0.45f;
        float d = hypotf(rx + vx * t, ry + vy * t);
        float safe = 22 + p->radius + 10;
        if (d < safe) score += 1 + (safe - d) / safe;
    }
    return score;
}

static void choose_input(const PublicState *state, const Script *script,
                         Controller *controller, BossInput *input) {
    boss_input_clear(input);
    int target = nearest_student(state);
    if (target < 0) return;
    float dx = state->students[target].x - state->boss_x;
    float dy = state->students[target].y - state->boss_y;
    float distance = hypotf(dx, dy);
    float ux = distance > 0 ? dx / distance : 0;
    float uy = distance > 0 ? dy / distance : 0;
    float desired_x = 0, desired_y = 0;
    if (script->movement == 1) {
        if (distance > script->distance + 8) { desired_x = ux; desired_y = uy; }
        else if (distance < script->distance - 8) { desired_x = -ux; desired_y = -uy; }
    } else if (script->movement == 2 || script->movement == 3) {
        float radial = (distance - script->distance) / 80;
        if (radial < -1) radial = -1;
        if (radial > 1) radial = 1;
        float sign = script->movement == 2 ? 1.0f : -1.0f;
        desired_x = ux * radial - uy * sign;
        desired_y = uy * radial + ux * sign;
    } else if (script->movement == 4) {
        static const float points[4][2] = {{80,640},{80,80},{880,80},{880,640}};
        dx = points[controller->waypoint % 4][0] - state->boss_x;
        dy = points[controller->waypoint % 4][1] - state->boss_y;
        if (hypotf(dx,dy) < 30) {
            ++controller->waypoint;
            dx = points[controller->waypoint % 4][0] - state->boss_x;
            dy = points[controller->waypoint % 4][1] - state->boss_y;
        }
        desired_x = dx; desired_y = dy;
    }
    float desired_len = hypotf(desired_x, desired_y);
    if (desired_len > 0) { desired_x /= desired_len; desired_y /= desired_len; }
    int best = 0;
    float best_score = 1.0e30f;
    for (int candidate = 0; candidate < 9; ++candidate) {
        float mx = directions[candidate][0], my = directions[candidate][1];
        if (script->movement == 0 && candidate != 0) continue;
        float ex = mx - desired_x, ey = my - desired_y;
        float score = ex * ex + ey * ey + script->dodge * visible_bullet_risk(state, mx, my);
        float future_x = state->boss_x + mx * 270 * 0.35f;
        float future_y = state->boss_y + my * 270 * 0.35f;
        if (future_x < 22 || future_x > 938 || future_y < 22 || future_y > 698) score += 8;
        if (score < best_score) { best = candidate; best_score = score; }
    }
    input->move_x = directions[best][0];
    input->move_y = directions[best][1];
    if (state->tick - controller->last_request < 12) return;
    int spell = script->attack;
    if (spell == 4) {
        unsigned clustered = 0;
        for (uint32_t i = 0; i < state->student_count; ++i) {
            if (!state->students[i].alive) continue;
            float sx = state->students[i].x - state->students[target].x;
            float sy = state->students[i].y - state->students[target].y;
            if (hypotf(sx,sy) < 220) ++clustered;
        }
        if (distance >= 35 && distance < 160 && state->available[0]) spell = 0;
        else if (clustered >= 2 && state->available[3]) spell = 3;
        else if (state->available[2]) spell = 2;
        else if (state->available[1]) spell = 1;
        else spell = 0;
    } else if (spell == 5) {
        spell = (int)(controller->cycle % 4);
    }
    if (spell == 0 && distance < 35) return;
    if (spell >= 0 && spell < 4 && state->available[spell]) {
        input->attack_requested[spell] = true;
        controller->last_request = state->tick;
        if (script->attack == 5) ++controller->cycle;
    }
}

static void write_winner(const char *path, const DemoConfig *cfg, const Script *script,
                         const World *world, const BossInput *inputs, unsigned ticks) {
    FILE *file = fopen(path, "wb");
    if (!file) { fprintf(stderr, "cannot write winning inputs: %s\n", path); exit(3); }
    fprintf(file, "{\n\"schema_version\":1,\"config_version\":%u,\"seed_lo\":%u,"
            "\"seed_hi\":0,\"students\":%u,\"script\":\"%s\","
            "\"expected\":{\"status\":%d,\"tick\":%d,\"boss_hp\":%d,"
            "\"boss_hits\":%u,\"student_hits\":%u},\n\"inputs\":[\n",
            cfg->version, cfg->seed_default, cfg->student_count, script->name,
            (int)world->status, world->tick, world->boss.hp,
            world->boss_hits_taken, world->student_hits_taken);
    for (unsigned tick = 0; tick < ticks; ++tick) {
        unsigned mask = 0;
        for (int p = 0; p < 4; ++p) if (inputs[tick].attack_requested[p]) mask |= 1u << p;
        fprintf(file, "%s{\"tick\":%u,\"move_x\":%.9g,\"move_y\":%.9g,"
                "\"pointer_valid\":0,\"pointer_x\":0,\"pointer_y\":0,\"attack_mask\":%u}",
                tick ? ",\n" : "", tick, inputs[tick].move_x, inputs[tick].move_y, mask);
    }
    fprintf(file, "\n]}\n");
    if (fclose(file)) { fprintf(stderr, "winning inputs close failed\n"); exit(3); }
}

static bool run_script(const DemoConfig *cfg, const Script *script, const char *winner_path,
                       bool winner_already_saved) {
    World world;
    WorldView view;
    PublicState state;
    Controller controller = {-12, 0, 0};
    BossInput *trace = calloc(PROBE_TICK_BUDGET, sizeof(*trace));
    unsigned accepted[4] = {0}, rejected = 0;
    if (!trace || !world_reset(&world, cfg, cfg->seed_default)) exit(2);
    unsigned ticks = 0;
    for (; ticks < PROBE_TICK_BUDGET; ++ticks) {
        world_make_view(&world, &view);
        if (view.status != DEMO_STATUS_RUNNING || view.truncated) break;
        public_observe(&view, &state);
        choose_input(&state, script, &controller, &trace[ticks]);
        world_step(&world, &trace[ticks]);
        for (uint32_t i = 0; i < world.events.count; ++i) {
            const StepEvent *event = &world.events.items[i];
            if (event->type == DEMO_EVENT_ATTACK_ACCEPTED) ++accepted[event->pattern];
            if (event->type == DEMO_EVENT_ATTACK_REJECTED) ++rejected;
        }
    }
    unsigned alive = 0, hp = 0;
    for (uint32_t i = 0; i < world.student_count; ++i) {
        alive += world.students[i].alive ? 1 : 0;
        hp += (unsigned)world.students[i].hp;
    }
    const char *status = world.status == DEMO_STATUS_BOSS_WIN ? "BOSS_WIN" :
        world.status == DEMO_STATUS_BOSS_LOSE ? "BOSS_LOSE" : "BUDGET_UNFINISHED";
    printf("%s,%s,%d,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n", script->name,
        status, world.tick, world.boss.hp, alive, hp, world.boss_hits_taken,
        world.student_hits_taken, accepted[0], accepted[1], accepted[2], accepted[3],
        rejected, world.boss_bullets_spawned, world.student_bullets_spawned);
    bool won = world.status == DEMO_STATUS_BOSS_WIN;
    if (won && !winner_already_saved) {
        write_winner(winner_path, cfg, script, &world, trace, ticks);
        fprintf(stderr, "winning inputs saved: %s (%s, tick %d)\n", winner_path,
                script->name, world.tick);
    }
    free(trace);
    return won;
}

int main(int argc, char **argv) {
    DemoConfig cfg;
    if (!demo_config_init(&cfg) || cfg.version != 2 || cfg.student_count != 3 ||
        cfg.seed_default != 20261006u) {
        fprintf(stderr, "probe requires unmodified default config v2, 3 students, seed 20261006\n");
        return 2;
    }
    const char *winner_path = argc > 1 ? argv[1] : "build/web-validation/winning-inputs.json";
    clock_t start = clock();
    unsigned scenarios = 0, wins = 0;
    printf("script,status,ticks,boss_hp,students_alive,student_hp_sum,boss_hits,student_hits,"
           "ring,course,mine,shower,rejections,boss_bullets,student_bullets\n");
    for (int attack = 0; attack <= 5; ++attack) {
        Script script;
        snprintf(script.name, sizeof(script.name), "stand-spell%d", attack);
        script.movement = 0; script.attack = attack; script.distance = 0; script.dodge = 0;
        if (run_script(&cfg, &script, winner_path, wins > 0)) ++wins;
        ++scenarios;
    }
    const float distances[] = {48, 100, 180};
    const float dodges[] = {0, 4, 20};
    const int attacks[] = {0, 2, 4};
    for (int movement = 1; movement <= 4; ++movement) {
        for (unsigned range = 0; range < 3; ++range) {
            if (movement == 4 && range > 0) continue;
            for (unsigned dodge = 0; dodge < 3; ++dodge) {
                for (unsigned attack = 0; attack < 3; ++attack) {
                    if ((double)(clock() - start) / CLOCKS_PER_SEC >= PROBE_CPU_BUDGET_SEC)
                        goto done;
                    Script script;
                    snprintf(script.name, sizeof(script.name), "move%d-d%.0f-avoid%.0f-spell%d",
                        movement, distances[range], dodges[dodge], attacks[attack]);
                    script.movement = movement; script.attack = attacks[attack];
                    script.distance = distances[range]; script.dodge = dodges[dodge];
                    if (run_script(&cfg, &script, winner_path, wins > 0)) ++wins;
                    ++scenarios;
                }
            }
        }
    }
done:
    fprintf(stderr, "probe summary: scenarios=%u wins=%u cpu_seconds=%.3f tick_budget=%d\n",
        scenarios, wins, (double)(clock() - start) / CLOCKS_PER_SEC, PROBE_TICK_BUDGET);
    return 0;
}
