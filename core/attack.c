/* Attack transaction and single-plan state machine.
 * Legacy requests choose a stable nearest target; manual requests accept a
 * direction. The pure manual factory also serves pre-aim without world/RNG
 * mutation. Accepted geometry, cost and timing are frozen for the full plan.
 */
#include "attack.h"

#include <string.h>
#include <math.h>

#include "patterns.h"
#include "pattern_aim.h"

/* 最近存活学�? 同距按稳�?ID 小者优先。返�?false 表示无存活目标�?*/
static bool find_nearest_student(const World *world, DemoEntityId *out_id, float *out_x,
                                 float *out_y) {
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

/* 事件缓冲不足时计�?dropped, 不静默丢弃�?*/
static void attack_push_event(World *world, DemoEventType type, DemoEntityId target_id,
                              int32_t amount, float x, float y, DemoPattern pattern) {
    StepEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.tick = world->tick;
    ev.source_id = world->boss.id;
    ev.target_id = target_id;
    ev.amount = amount;
    ev.x = x;
    ev.y = y;
    ev.pattern = pattern;
    ev.reject = DEMO_REJECT_NONE;
    if (!events_push(&world->events, &ev)) {
        world->events.dropped++;
    }
}

bool attack_build_aim_plan(const World *world, DemoPattern pattern, float dir_x, float dir_y,
                           AttackPlan *out_plan, DemoRejectReason *out_reason) {
    if (out_reason) *out_reason = DEMO_REJECT_NONE;
    if (!world || !out_plan || (int)pattern < 0 || (int)pattern >= DEMO_PATTERN_COUNT ||
        !isfinite(dir_x) || !isfinite(dir_y)) return false;
    const double length = hypot((double)dir_x, (double)dir_y);
    if (!isfinite(length) || length < 1.0e-6f) return false;
    dir_x = (float)(dir_x / length); dir_y = (float)(dir_y / length);
    bool has_students = false;
    for (uint32_t i = 0u; i < world->student_count && i < DEMO_MAX_STUDENTS; ++i)
        if (world->students[i].alive) has_students = true;
    DemoRejectReason reason = DEMO_REJECT_NONE;
    if (world->status != DEMO_STATUS_RUNNING || world->truncated || !has_students)
        reason = DEMO_REJECT_NO_TARGET;
    else if (world->attack_state != DEMO_ATTACK_IDLE) reason = DEMO_REJECT_BUSY;
    else if (world->energy < world->cfg.patterns[(int)pattern].cost)
        reason = DEMO_REJECT_NO_ENERGY;
    if (out_reason) *out_reason = reason;
    PatternRequest request;
    memset(&request, 0, sizeof(request));
    request.pattern = pattern; request.manual_aim = true;
    request.aim_dir_x = dir_x; request.aim_dir_y = dir_y;
    request.origin_x = world->boss.x; request.origin_y = world->boss.y;
    request.target_x = world->boss.x + dir_x * 100.0f;
    request.target_y = world->boss.y + dir_y * 100.0f;
    request.start_tick = world->tick;
    request.student_count = world->student_count;
    request.student_radius = world->cfg.student_radius;
    request.field_w = world->cfg.field_w; request.field_h = world->cfg.field_h;
    for (uint32_t i = 0u; i < world->student_count && i < DEMO_MAX_STUDENTS; ++i) {
        request.student_x[i] = world->students[i].x;
        request.student_y[i] = world->students[i].y;
        request.student_alive[i] = world->students[i].alive;
    }
    return pattern_aim_make_plan(&request, &world->cfg, out_plan);
}

bool attack_try_request_aim(World *world, DemoPattern pattern, float dir_x, float dir_y,
                            DemoRejectReason *out_reason) {
    DemoRejectReason reason = DEMO_REJECT_NONE;
    AttackPlan plan;
    memset(&plan, 0, sizeof(plan));
    const bool geometry_valid = attack_build_aim_plan(world, pattern, dir_x, dir_y, &plan, &reason);
    if (out_reason) *out_reason = reason;
    if (!geometry_valid || reason != DEMO_REJECT_NONE) return false;
    world->energy -= world->cfg.patterns[(int)pattern].cost;
    plan.plan_id = world->next_plan_id++;
    plan.active = true;
    world->plan = plan;
    world->attack_state = DEMO_ATTACK_WINDUP;
    attack_push_event(world, DEMO_EVENT_WINDUP_START, 0u, world->cfg.patterns[(int)pattern].cost,
                      plan.origin_x, plan.origin_y, pattern);
    return true;
}

bool attack_try_request(World *world, DemoPattern pattern, DemoRejectReason *out_reason) {
    if (out_reason != NULL) {
        *out_reason = DEMO_REJECT_NONE;
    }
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
        if (out_reason != NULL) {
            *out_reason = DEMO_REJECT_BUSY;
        }
        return false;
    }

    const PatternConfig *pc = &world->cfg.patterns[(int)pattern];

    /* 1) 目标: 最近存活学�? 无目标直接拒绝�?*/
    DemoEntityId target_id = 0u;
    float target_x = 0.0f;
    float target_y = 0.0f;
    if (!find_nearest_student(world, &target_id, &target_x, &target_y)) {
        if (out_reason != NULL) {
            *out_reason = DEMO_REJECT_NO_TARGET;
        }
        return false;
    }

    /* 2) 能量: 共享能量�? 不足则拒绝且不扣�?*/
    if (world->energy < pc->cost) {
        if (out_reason != NULL) {
            *out_reason = DEMO_REJECT_NO_ENERGY;
        }
        return false;
    }

    const PatternVTable *vt = pattern_vtable(pattern);
    if (vt == NULL || vt->make_plan == NULL) {
        return false; /* 招式非法: 不扣能量、不改变状�?*/
    }

    PatternRequest req;
    memset(&req, 0, sizeof(req));
    req.pattern = pattern;
    req.target_id = target_id;
    req.origin_x = world->boss.x;
    req.origin_y = world->boss.y;
    req.target_x = target_x;
    req.target_y = target_y;
    req.start_tick = world->tick;
    req.student_count = world->student_count;
    req.student_radius = world->cfg.student_radius;
    req.field_w = world->cfg.field_w;
    req.field_h = world->cfg.field_h;
    for (uint32_t i = 0u; i < world->student_count; ++i) {
        req.student_x[i] = world->students[i].x;
        req.student_y[i] = world->students[i].y;
        req.student_alive[i] = world->students[i].alive;
    }

    /* 先生成到临时计划: 失败�?world 完全不动�?*/
    AttackPlan plan;
    memset(&plan, 0, sizeof(plan));
    if (!vt->make_plan(&req, &world->cfg, &world->world_rng, &plan)) {
        if (out_reason != NULL) {
            *out_reason = DEMO_REJECT_NONE;
        }
        return false;
    }

    /* 原子提交: 只扣一次能�? 保存计划, 进入预警�?*/
    world->energy -= pc->cost;
    plan.plan_id = world->next_plan_id++;
    plan.active = true;
    world->plan = plan;
    world->attack_state = DEMO_ATTACK_WINDUP;

    attack_push_event(world, DEMO_EVENT_WINDUP_START, plan.target_id, pc->cost, plan.origin_x,
                      plan.origin_y, pattern);
    return true;
}

void attack_step(World *world) {
    if (world == NULL) {
        return;
    }
    if (world->attack_state == DEMO_ATTACK_IDLE) {
        return;
    }
    if (!world->plan.active) {
        /* 计划已被外部清除: 只回收状�? 不生成任何波次�?*/
        world->attack_state = DEMO_ATTACK_IDLE;
        return;
    }

    const int32_t elapsed = world->tick - world->plan.start_tick;

    if (world->attack_state == DEMO_ATTACK_WINDUP) {
        if (elapsed < world->plan.windup_ticks) {
            return; /* 仍在预警: 只显�? 不生成弹 */
        }
        /* 边界: elapsed == windup_ticks 恰好切换�?         * 同一 tick 继续走下面的 ACTIVE 分支, 保证"攻击开始这一�?应生成的
         * 第一波不被预警切换吞�?否则会少生成一�?�?*/
        world->attack_state = DEMO_ATTACK_ACTIVE;
        attack_push_event(world, DEMO_EVENT_ATTACK_START, world->plan.target_id, 0,
                          world->plan.origin_x, world->plan.origin_y, world->plan.pattern);
    }

    /* ACTIVE: 先判结束, 结束后同一 tick 不再 emit�?*/
    if (elapsed >= world->plan.windup_ticks + world->plan.active_ticks) {
        if (world->cfg.boss_bullet_clear_on_attack_end > 0) {
            (void)pool_clear_plan(&world->pool, world->plan.plan_id);
        }
        world->attack_state = DEMO_ATTACK_IDLE;
        world->plan.active = false;
        return;
    }

    const PatternVTable *vt = pattern_vtable(world->plan.pattern);
    if (vt == NULL || vt->emit == NULL) {
        return;
    }

    ProjectileSpawnBuffer buf;
    spawn_buffer_init(&buf);
    /* 波次时刻契约（docs/demo-interfaces.md 3.4，接�?v2 冻结，勿改）:
     *   plan->wave_tick[i] = first_spawn_sec*60 + i*wave_interval_sec*60�?     *   语义�?**预警结束、进入攻击之�?* �?i 波的相对 tick"�?     *   因此第三个参数必须传 (elapsed - windup_ticks)，不是绝�?world->tick�?     *
     * 传绝�?tick 的后果：默认配置 windup=72、wave_tick=[0, 42, 84]�?     * 绝对 tick 起步就是 72 且只增不减，�?1 �?t=0)与第 2 �?t=42)永不匹配�?     * 只有�?3 �?t=84)能生�?—�?预警与实际生成不一致，玩家看到 3 波预警只收到 1 波�?     * 实测证据�?docs/demo-findings.md；回归哨兵为
     * `build/sim/sim.exe --seed 12345 --script mixed --max-ticks 7200`（应四招都被接受�?boss_bullets 非零）�?*/
    const uint32_t active_tick = (uint32_t)(elapsed - world->plan.windup_ticks);
    const bool emitted = world->plan.manual_aim
        ? pattern_aim_emit(&world->plan, &world->cfg, active_tick, &buf)
        : vt->emit(&world->plan, &world->cfg, active_tick, &buf);
    if (!emitted) {
        return; /* �?tick 不生成波�?*/
    }

    uint32_t spawned = 0u;
    for (uint32_t i = 0u; i < buf.count; ++i) {
        const Projectile *spec = &buf.spec[i];
        if (pool_spawn(&world->pool, spec->faction, spec->source_id, spec->plan_id,
                       spec->source_pattern, spec->x, spec->y, spec->vx, spec->vy, spec->radius,
                       spec->damage, spec->lifetime_ticks, (uint64_t)world->tick)) {
            spawned++;
        } else {
            world->spawn_overflow_count++;
            attack_push_event(world, DEMO_EVENT_SPAWN_OVERFLOW, 0, 1, spec->x, spec->y,
                              world->plan.pattern);
        }
    }
    world->boss_bullets_spawned += spawned;

    attack_push_event(world, DEMO_EVENT_WAVE_SPAWN, world->plan.target_id, (int32_t)spawned,
                      world->plan.origin_x, world->plan.origin_y, world->plan.pattern);
}

float attack_progress(const World *world) {
    if (world == NULL || world->attack_state == DEMO_ATTACK_IDLE) {
        return 0.0f;
    }

    const float elapsed = (float)(world->tick - world->plan.start_tick);
    const float windup = (float)world->plan.windup_ticks;

    if (world->attack_state == DEMO_ATTACK_WINDUP) {
        if (windup <= 0.0f) {
            return 1.0f;
        }
        float t = elapsed / windup;
        if (t < 0.0f) {
            t = 0.0f;
        }
        if (t > 1.0f) {
            t = 1.0f;
        }
        return t;
    }

    /* ACTIVE: 1 + 攻击阶段进度, 取�?[1, 2] */
    const float active = (float)world->plan.active_ticks;
    if (active <= 0.0f) {
        return 2.0f;
    }
    float t = (elapsed - windup) / active;
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    return 1.0f + t;
}
