/* world.c - 逻辑世界主循环（母代理独占维护）
 *
 * 接口版本: 1    配置版本: 1
 * 本文件是 demo 唯一的 tick 顺序实现。graphics 端与 sim 端都只调用 world_step。
 *
 * tick 顺序（契约，见 docs/demo-interfaces.md 3.2）:
 *   1) 消费一个出招请求（同 tick 最多一个，招式 ID 小者优先）
 *   2) attack_step: 预警/攻击推进 + 波次生成 + 攻击结束清弹
 *   3) 保存本 tick 移动前坐标（扫掠用），再应用 Boss 移动
 *   4) 学生观测、AI 决策调度（动作保持）与移动
 *   5) 学生反击冷却与发射（瞄准发射瞬间的 Boss 位置）
 *   6) pool_advance: 保存本 tick 起点并积分，处理寿命与出界
 *   7) 碰撞结算（相对运动扫掠、最早交点、稳定 ID 破平局、命中即移除）
 *   8) 伤害与击倒（无敌、倒下只发一次、倒下后不再行动）
 *   9) 终局裁决（同 tick 双倒按 outcome_rule，配置 1 = Boss 胜）
 *  10) 能量恢复、tick 递增、max_ticks 截断
 *
 * 移动输入语义（已批准，见 docs/demo-rules.md 1.2）:
 *   BossInput.move_x/move_y 是"朝鼠标指针方向的单位向量 × 幅度"，幅度 <= 1。
 *   鼠标模式（florr/digdig 风格）由平台层计算:
 *     dx = pointer_x - boss_x;  len = |(dx,dy)|
 *     len <= 死区            -> (0,0)            角色停住
 *     死区 < len < 饱和距离  -> 方向单位向量 × (len - 死区)/(饱和 - 死区)
 *     len >= 饱和距离        -> 方向单位向量 × 1   全速
 *   因此指针离角色越近移动越慢，指针处停住；键盘模式产生八方向单位向量。
 *   核心只做归一化移动与边界夹紧，不读鼠标或键盘。
 */
#include "world.h"

#include <math.h>
#include <string.h>

#include "attack.h"
#include "patterns.h"
#include "student_bot.h"
#include "student_fire.h"

#define DEMO_RNG_STREAM_WORLD 1u
#define DEMO_RNG_STREAM_BOT 2u
#define DEMO_RNG_STREAM_EXPLORE 3u

#define DEMO_BOSS_START_X 480.0f
#define DEMO_BOSS_START_Y 620.0f
#define DEMO_BOSS_STARTUP_INVULN_TICKS 60

/* ---------------------------------------------------------------- 小工具 */

static bool is_finite_f(float v) { return isfinite(v) != 0; }

static float vec_len(float x, float y) { return sqrtf(x * x + y * y); }

static void event_reset(StepEvents *events) {
    events->count = 0u;
    events->dropped = 0u;
    events->capacity = DEMO_MAX_STEP_EVENTS;
}

static void push_event(World *world, DemoEventType type, DemoEntityId source_id,
                       DemoEntityId target_id, int32_t amount, float x, float y,
                       DemoPattern pattern, DemoRejectReason reject) {
    StepEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.tick = world->tick;
    ev.source_id = source_id;
    ev.target_id = target_id;
    ev.amount = amount;
    ev.x = x;
    ev.y = y;
    ev.pattern = pattern;
    ev.reject = reject;
    if (!events_push(&world->events, &ev)) {
        world->events.dropped++;
    }
}

static void remember_positions(World *world) {
    world->boss_prev_x = world->boss.x;
    world->boss_prev_y = world->boss.y;
    for (uint32_t i = 0u; i < world->student_count; ++i) {
        world->student_prev_x[i] = world->students[i].x;
        world->student_prev_y[i] = world->students[i].y;
    }
}

void boss_input_clear(BossInput *in) {
    if (in == NULL) {
        return;
    }
    memset(in, 0, sizeof(*in));
}

void events_init(StepEvents *events) {
    if (events == NULL) {
        return;
    }
    memset(events, 0, sizeof(*events));
    events->capacity = DEMO_MAX_STEP_EVENTS;
}

bool events_push(StepEvents *events, const StepEvent *event) {
    if (events == NULL || event == NULL) {
        return false;
    }
    if (events->capacity == 0u) {
        events->capacity = DEMO_MAX_STEP_EVENTS;
    }
    if (events->count >= events->capacity || events->count >= DEMO_MAX_STEP_EVENTS) {
        events->dropped++;
        return false;
    }
    events->items[events->count] = *event;
    events->count++;
    return true;
}

/* ---------------------------------------------------------------- 重置 */

bool world_reset(World *world, const DemoConfig *config, uint64_t seed) {
    if (world == NULL || config == NULL) {
        return false;
    }
    char err[128];
    if (!demo_config_validate(config, err, sizeof(err))) {
        return false;
    }

    memset(world, 0, sizeof(*world));
    world->cfg = *config;
    world->seed = seed;
    world->tick = 0;
    world->status = DEMO_STATUS_RUNNING;
    world->truncated = false;

    rng_seed(&world->world_rng, seed, DEMO_RNG_STREAM_WORLD);
    rng_seed(&world->bot_rng, seed, DEMO_RNG_STREAM_BOT);
    rng_seed(&world->explore_rng, seed, DEMO_RNG_STREAM_EXPLORE);

    world->boss.id = 1u;
    world->boss.alive = true;
    world->boss.x = DEMO_BOSS_START_X;
    world->boss.y = DEMO_BOSS_START_Y;
    world->boss.radius = config->boss_radius;
    world->boss.hp = config->boss_hp;
    world->boss.hp_max = config->boss_hp;
    world->boss.invuln_ticks = DEMO_BOSS_STARTUP_INVULN_TICKS;

    world->student_count = config->student_count;
    for (uint32_t i = 0u; i < world->student_count; ++i) {
        Actor *s = &world->students[i];
        s->id = 100u + i;
        s->alive = true;
        s->x = config->student_spawn_x[i];
        s->y = config->student_spawn_y[i];
        s->radius = config->student_radius;
        s->hp = config->student_hp;
        s->hp_max = config->student_hp;
        s->invuln_ticks = 0;
        student_bot_init(&world->bot[i], (uint32_t)(seed & 0xFFFFFFFFu) ^ (i * 2654435761u));
        memset(&world->obs[i], 0, sizeof(world->obs[i]));
        memset(&world->action[i], 0, sizeof(world->action[i]));
    }

    pool_init(&world->pool, config->projectile_cap);

    world->energy = config->energy_start;
    world->energy_regen_accum = 0.0f;
    world->attack_state = DEMO_ATTACK_IDLE;
    memset(&world->plan, 0, sizeof(world->plan));
    world->next_plan_id = 1u;
    world->student_since_decision = 0u;
    world->pending_count = 0u;
    memset(world->pending, 0, sizeof(world->pending));

    remember_positions(world);
    event_reset(&world->events);
    memset(&world->last_result, 0, sizeof(world->last_result));
    return true;
}

/* ---------------------------------------------------------------- 查询 */

bool world_nearest_student(const World *world, DemoEntityId *out_id, float *out_x,
                           float *out_y) {
    if (world == NULL) {
        return false;
    }
    bool found = false;
    float best_d2 = 0.0f;
    DemoEntityId best_id = 0u;
    float best_x = 0.0f;
    float best_y = 0.0f;
    for (uint32_t i = 0u; i < world->student_count; ++i) {
        const Actor *s = &world->students[i];
        if (!s->alive) {
            continue;
        }
        float dx = s->x - world->boss.x;
        float dy = s->y - world->boss.y;
        float d2 = dx * dx + dy * dy;
        /* 同距按稳定 ID 小者优先 */
        if (!found || d2 < best_d2 || (d2 == best_d2 && s->id < best_id)) {
            found = true;
            best_d2 = d2;
            best_id = s->id;
            best_x = s->x;
            best_y = s->y;
        }
    }
    if (!found) {
        return false;
    }
    if (out_id != NULL) {
        *out_id = best_id;
    }
    if (out_x != NULL) {
        *out_x = best_x;
    }
    if (out_y != NULL) {
        *out_y = best_y;
    }
    return true;
}

bool world_pattern_available(const World *world, DemoPattern pattern) {
    if (world == NULL) {
        return false;
    }
    if ((int)pattern < 0 || (int)pattern >= (int)DEMO_PATTERN_COUNT) {
        return false;
    }
    if (world->status != DEMO_STATUS_RUNNING) {
        return false;
    }
    if (world->attack_state != DEMO_ATTACK_IDLE) {
        return false;
    }
    if (world->energy < world->cfg.patterns[(int)pattern].cost) {
        return false;
    }
    DemoEntityId id = 0u;
    float x = 0.0f;
    float y = 0.0f;
    return world_nearest_student(world, &id, &x, &y);
}

const StudentObservation *world_student_observation(const World *world, uint32_t index) {
    if (world == NULL || index >= world->student_count) {
        return NULL;
    }
    return &world->obs[index];
}

/* ---------------------------------------------------------------- 学生观测 */

static void build_observations(World *world) {
    PatternWarning warning;
    memset(&warning, 0, sizeof(warning));
    if (world->attack_state != DEMO_ATTACK_IDLE && world->plan.active) {
        pattern_warning(&world->plan, &world->cfg, &warning);
    }

    for (uint32_t i = 0u; i < world->student_count; ++i) {
        Actor *s = &world->students[i];
        StudentObservation *obs = &world->obs[i];
        memset(obs, 0, sizeof(*obs));
        obs->tick = world->tick;
        obs->self_id = s->id;
        obs->self_x = s->x;
        obs->self_y = s->y;
        obs->self_radius = s->radius;
        obs->self_hp = s->hp;
        obs->self_alive = s->alive;
        obs->field_w = world->cfg.field_w;
        obs->field_h = world->cfg.field_h;
        obs->target_x = world->boss.x;
        obs->target_y = world->boss.y;
        {
            float dx = world->boss.x - s->x;
            float dy = world->boss.y - s->y;
            float len = vec_len(dx, dy);
            if (len > 1e-6f) {
                obs->aim_dir_x = dx / len;
                obs->aim_dir_y = dy / len;
            }
        }
        obs->can_fire = student_fire_ready(s, (int32_t)world->bot[i].fire_cooldown_ticks);
        obs->warning = warning;

        /* 只暴露当前已生成的弹（不含未来波次与随机状态） */
        uint32_t n = 0u;
        for (uint32_t k = 0u; k < world->pool.capacity && n < 64u; ++k) {
            const Projectile *p = &world->pool.items[k];
            if (!p->active) {
                continue;
            }
            obs->projectiles[n].x = p->x;
            obs->projectiles[n].y = p->y;
            obs->projectiles[n].vx = p->vx;
            obs->projectiles[n].vy = p->vy;
            obs->projectiles[n].radius = p->radius;
            n++;
        }
        obs->projectile_count = n;
    }
}

/* ---------------------------------------------------------------- 学生调度 */

static void students_think_and_move(World *world) {
    const DemoConfig *cfg = &world->cfg;

    bool decide = (world->student_since_decision == 0u);
    if (decide) {
        world->student_since_decision = (uint32_t)cfg->student_decision_ticks;
    }
    world->student_since_decision--;

    for (uint32_t i = 0u; i < world->student_count; ++i) {
        Actor *s = &world->students[i];
        if (!s->alive) {
            continue; /* 倒下学生退出后续行动 */
        }
        if (decide) {
            StudentAction action;
            memset(&action, 0, sizeof(action));
            student_bot_choose(&world->obs[i], &world->bot[i], &action);
            world->action[i] = action;
        }
        float mx = world->action[i].move_x;
        float my = world->action[i].move_y;
        if (!is_finite_f(mx) || !is_finite_f(my)) {
            mx = 0.0f;
            my = 0.0f;
        }
        actor_apply_move(s, mx, my, cfg->student_speed, 1.0f / (float)DEMO_TICKS_PER_SECOND,
                         cfg->student_radius, cfg->field_w - cfg->student_radius,
                         cfg->student_radius, cfg->field_h - cfg->student_radius);
    }
}

static void students_fire(World *world) {
    const DemoConfig *cfg = &world->cfg;
    for (uint32_t i = 0u; i < world->student_count; ++i) {
        Actor *s = &world->students[i];
        if (!s->alive) {
            continue;
        }
        if (!student_fire_ready(s, world->bot[i].fire_cooldown_ticks)) {
            continue; /* 冷却在 student_fire_tick_cooldown 中统一推进 */
        }
        DemoEntityId source = 0u;
        /* 用 uint32_t 中转字段调用冻结签名, 避免 (uint32_t *)&int32_t 的严格别名违规 */
        world->fire_cooldown_scratch[i] = (uint32_t)world->bot[i].fire_cooldown_ticks;
        if (!student_fire_try(cfg, s, world->boss.x, world->boss.y, &world->pool, world->tick,
                              &world->fire_cooldown_scratch[i], &source)) {
            world->fire_cooldown_scratch[i] = (uint32_t)world->bot[i].fire_cooldown_ticks;
            continue;
        }
        world->bot[i].fire_cooldown_ticks = (int32_t)world->fire_cooldown_scratch[i];
        world->student_bullets_spawned++;
        push_event(world, DEMO_EVENT_STUDENT_FIRE, s->id, world->boss.id, 1, s->x, s->y,
                   (DemoPattern)0, DEMO_REJECT_NONE);
    }
}

/* ---------------------------------------------------------------- Boss 控制 */

static void boss_control(World *world, const BossInput *input) {
    const DemoConfig *cfg = &world->cfg;
    float dx = 0.0f;
    float dy = 0.0f;

    if (input != NULL && input->pointer_valid && cfg->pointer_to_position) {
        float deadzone = (input->pointer_deadzone > 0.0f) ? input->pointer_deadzone
                                                          : cfg->pointer_deadzone;
        float saturate = (input->pointer_saturate > 0.0f) ? input->pointer_saturate
                                                          : cfg->pointer_saturate;
        if (saturate <= deadzone) {
            saturate = deadzone + 1.0f;
        }
        float to_x = input->pointer_x - world->boss.x;
        float to_y = input->pointer_y - world->boss.y;
        float len = vec_len(to_x, to_y);
        if (len > deadzone && is_finite_f(len)) {
            float mag = (len - deadzone) / (saturate - deadzone);
            if (mag > 1.0f) {
                mag = 1.0f;
            }
            dx = (to_x / len) * mag;
            dy = (to_y / len) * mag;
        }
    } else if (input != NULL) {
        dx = input->move_x;
        dy = input->move_y;
    }

    if (!is_finite_f(dx) || !is_finite_f(dy)) {
        dx = 0.0f;
        dy = 0.0f;
    }
    /* 幅度超过 1 时夹紧, 不让平台层错误输入造成超速 */
    float len = vec_len(dx, dy);
    if (len > 1.0f) {
        dx /= len;
        dy /= len;
    }
    actor_apply_move(&world->boss, dx, dy, cfg->boss_speed,
                     1.0f / (float)DEMO_TICKS_PER_SECOND, cfg->boss_move_min_x,
                     cfg->boss_move_max_x, cfg->boss_move_min_y, cfg->boss_move_max_y);
}

/* ---------------------------------------------------------------- 出招请求 */

static DemoRejectReason request_attack(World *world, DemoPattern pattern) {
    DemoRejectReason reason = DEMO_REJECT_NONE;
    if (!attack_try_request(world, pattern, &reason)) {
        world->attack_reject_count++;
        push_event(world, DEMO_EVENT_ATTACK_REJECTED, world->boss.id, 0u, 0, world->boss.x,
                   world->boss.y, pattern, reason);
        world->last_result.last_reject = reason;
        return reason;
    }
    world->attack_accept_count++;
    push_event(world, DEMO_EVENT_ATTACK_ACCEPTED, world->boss.id, world->plan.target_id, 0,
               world->plan.origin_x, world->plan.origin_y, pattern, DEMO_REJECT_NONE);
    world->last_result.attack_accepted = true;
    return DEMO_REJECT_NONE;
}

static void consume_requests(World *world, const BossInput *input) {
    if (input == NULL || world->status != DEMO_STATUS_RUNNING) {
        return;
    }
    /* 同 tick 多个请求: 招式 ID 小者优先, 只接受一个 */
    for (int p = 0; p < (int)DEMO_PATTERN_COUNT; ++p) {
        if (input->attack_requested[p]) {
            (void)request_attack(world, (DemoPattern)p);
            return;
        }
    }
}

/* ---------------------------------------------------------------- 碰撞 */

typedef struct HitCandidate {
    bool valid;
    float t_min;
    DemoEntityId target_id;
    uint32_t target_index;
} HitCandidate;

static bool actor_bullet_hit(const Actor *actor, float prev_x, float prev_y,
                             const Projectile *p, float *out_t, float *out_dist) {
    Segment a;
    a.ax = prev_x;
    a.ay = prev_y;
    a.bx = actor->x;
    a.by = actor->y;
    Segment b;
    b.ax = p->px;
    b.ay = p->py;
    b.bx = p->x;
    b.by = p->y;
    return sweep_hit(&a, &b, actor->radius + p->radius, out_t, out_dist);
}

static void resolve_collisions(World *world) {
    for (uint32_t i = 0u; i < world->pool.capacity; ++i) {
        Projectile *p = &world->pool.items[i];
        if (!p->active) {
            continue;
        }
        if (p->faction == DEMO_FACTION_BOSS) {
            HitCandidate best;
            memset(&best, 0, sizeof(best));
            for (uint32_t k = 0u; k < world->student_count; ++k) {
                Actor *s = &world->students[k];
                if (!s->alive) {
                    continue;
                }
                float t = 0.0f;
                float dist = 0.0f;
                if (!actor_bullet_hit(s, world->student_prev_x[k], world->student_prev_y[k], p,
                                      &t, &dist)) {
                    continue;
                }
                if (!best.valid || t < best.t_min ||
                    (t == best.t_min && s->id < best.target_id)) {
                    best.valid = true;
                    best.t_min = t;
                    best.target_id = s->id;
                    best.target_index = k;
                }
            }
            if (best.valid) {
                Actor *s = &world->students[best.target_index];
                DemoPattern src_pattern = p->source_pattern;
                if (actor_apply_damage(s, (int32_t)p->damage)) {
                    /* 有效受击成功后由 world 统一赋予无敌时间(actor_apply_damage 是纯函数, 不改无敌)。
                     * 这样同一 tick 内的后续弹会被 invuln_ticks 挡住, 不会重复扣血。 */
                    s->invuln_ticks = world->cfg.student_hurt_invuln_ticks;
                    world->student_hits_taken++;
                    push_event(world, DEMO_EVENT_HIT, world->boss.id, s->id,
                               (int32_t)p->damage, p->x, p->y, src_pattern, DEMO_REJECT_NONE);
                    if (!s->alive) {
                        push_event(world, DEMO_EVENT_KNOCKDOWN, world->boss.id, s->id, 0, s->x,
                                   s->y, src_pattern, DEMO_REJECT_NONE);
                    }
                }
                p->active = false;
                if (world->pool.live_count > 0u) {
                    world->pool.live_count--;
                }
            }
        } else if (p->faction == DEMO_FACTION_STUDENT) {
            float t = 0.0f;
            float dist = 0.0f;
            if (actor_bullet_hit(&world->boss, world->boss_prev_x, world->boss_prev_y, p, &t,
                                 &dist)) {
                if (actor_apply_damage(&world->boss, (int32_t)p->damage)) {
                    world->boss.invuln_ticks = world->cfg.boss_hurt_invuln_ticks;
                    world->boss_hits_taken++;
                    push_event(world, DEMO_EVENT_HIT, p->source_id, world->boss.id,
                               (int32_t)p->damage, p->x, p->y, (DemoPattern)0,
                               DEMO_REJECT_NONE);
                    if (!world->boss.alive) {
                        push_event(world, DEMO_EVENT_KNOCKDOWN, p->source_id, world->boss.id, 0,
                                   world->boss.x, world->boss.y, (DemoPattern)0,
                                   DEMO_REJECT_NONE);
                    }
                }
                p->active = false;
                if (world->pool.live_count > 0u) {
                    world->pool.live_count--;
                }
            }
        }
    }
}

/* ---------------------------------------------------------------- 终局 */

static void finish(World *world, DemoWorldStatus status) {
    world->status = status;
    push_event(world, DEMO_EVENT_GAME_OVER, world->boss.id, 0u, (int32_t)status, world->boss.x,
               world->boss.y, (DemoPattern)0, DEMO_REJECT_NONE);
}

static void evaluate_outcome(World *world) {
    bool all_down = true;
    for (uint32_t i = 0u; i < world->student_count; ++i) {
        if (world->students[i].alive) {
            all_down = false;
            break;
        }
    }
    bool boss_down = !world->boss.alive;

    if (all_down && boss_down) {
        switch (world->cfg.outcome_rule) {
            case DEMO_OUTCOME_DRAW:
                finish(world, DEMO_STATUS_DRAW);
                break;
            case DEMO_OUTCOME_BOSS_LOSE:
                finish(world, DEMO_STATUS_BOSS_LOSE);
                break;
            case DEMO_OUTCOME_BOSS_WIN:
            default:
                finish(world, DEMO_STATUS_BOSS_WIN);
                break;
        }
        return;
    }
    if (all_down) {
        finish(world, DEMO_STATUS_BOSS_WIN);
        return;
    }
    if (boss_down) {
        finish(world, DEMO_STATUS_BOSS_LOSE);
    }
}

/* ---------------------------------------------------------------- 主循环 */

void world_step(World *world, const BossInput *input) {
    if (world == NULL) {
        return;
    }
    event_reset(&world->events);
    memset(&world->last_result, 0, sizeof(world->last_result));

    if (world->status != DEMO_STATUS_RUNNING) {
        /* 终局或已截断: 逻辑停止, 不推进 tick, 不产生新事件 */
        world->last_result.event_count = world->events.count;
        return;
    }

    /* 1) 出招请求 */
    consume_requests(world, input);

    /* 2) 攻击状态机与波次生成 */
    attack_step(world);

    /* 3) 保存移动前坐标, 再应用 Boss 移动（扫掠需要本 tick 位移段） */
    remember_positions(world);
    boss_control(world, input);

    /* 4) 学生观测、决策与移动 */
    build_observations(world);
    students_think_and_move(world);

    /* 5) 学生反击冷却与发射 */
    students_fire(world);

    /* 6) 弹运动 */
    pool_advance(&world->pool, 1.0f / (float)DEMO_TICKS_PER_SECOND);

    /* 7) 碰撞结算 */
    resolve_collisions(world);

    /* 8) 计时器与能量 */
    actor_tick_timers(&world->boss);
    for (uint32_t i = 0u; i < world->student_count; ++i) {
        actor_tick_timers(&world->students[i]);
        if (world->students[i].alive) {
            student_fire_tick_cooldown(&world->bot[i].fire_cooldown_ticks);
        }
    }
    world->energy_regen_accum += world->cfg.energy_regen_per_sec / (float)DEMO_TICKS_PER_SECOND;
    while (world->energy_regen_accum >= 1.0f) {
        world->energy_regen_accum -= 1.0f;
        if (world->energy < world->cfg.energy_max) {
            world->energy++;
        }
    }

    /* 9) 终局 */
    evaluate_outcome(world);

    /* 10) tick 递增与实验截断 */
    world->tick++;
    if (world->cfg.max_ticks > 0 && world->tick >= world->cfg.max_ticks &&
        world->status == DEMO_STATUS_RUNNING) {
        world->truncated = true;
        push_event(world, DEMO_EVENT_TRUNCATED, 0u, 0u, world->tick, 0.0f, 0.0f,
                   (DemoPattern)0, DEMO_REJECT_NONE);
    }

    world->last_result.event_count = world->events.count;
}

/* ---------------------------------------------------------------- 视图 */

void world_make_view(const World *world, WorldView *out) {
    if (world == NULL || out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->tick = world->tick;
    out->status = world->status;
    out->truncated = world->truncated;
    out->paused = false; /* 暂停由外层状态机管理, 核心不表达暂停 */

    out->boss = &world->boss;
    out->students = world->students;
    out->student_count = world->student_count;

    out->projectiles = world->pool.items;
    out->projectile_capacity = world->pool.capacity;
    out->projectile_live = world->pool.live_count;

    out->energy = world->energy;
    out->energy_max = world->cfg.energy_max;

    out->attack_state = world->attack_state;
    out->plan_active = world->plan.active;
    out->plan_pattern = world->plan.pattern;
    out->plan_target = world->plan.target_id;
    out->plan_start_tick = world->plan.start_tick;
    out->plan_windup_ticks = world->plan.windup_ticks;
    out->plan_active_ticks = world->plan.active_ticks;
    out->plan_id = world->plan.plan_id;
    if (world->plan.active) {
        pattern_warning(&world->plan, &world->cfg, &out->warning);
    }

    out->world_seed = world->seed;
    out->attack_accept_count = world->attack_accept_count;
    out->attack_reject_count = world->attack_reject_count;
    out->boss_hits_taken = world->boss_hits_taken;
    out->student_hits_taken = world->student_hits_taken;

    out->events = world->events.items;
    out->event_count = world->events.count;

    {
        DemoEntityId id = 0u;
        float x = 0.0f;
        float y = 0.0f;
        out->marked_target = world_nearest_student(world, &id, &x, &y) ? id : 0u;
    }
    for (int p = 0; p < (int)DEMO_PATTERN_COUNT; ++p) {
        out->pattern_available[p] = world_pattern_available(world, (DemoPattern)p);
    }
}
