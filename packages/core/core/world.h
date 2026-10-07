/* world.h - 逻辑世界: 固定步进、状态机、终局、只读视图
 * 接口版本: 1
 */
#ifndef DEMO_WORLD_H
#define DEMO_WORLD_H

#include "demo_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* AttackPlan 的容量上限: world 内部保存一份当前计划 */
#define DEMO_ATTACK_CAPACITY 4u

typedef struct PendingRequest {
    bool used;
    DemoPattern pattern;
} PendingRequest;

typedef struct World {
    DemoConfig cfg;
    Rng world_rng;
    Rng bot_rng;
    Rng explore_rng; /* 平局/同分裁决专用流, 与 world 流分离 */

    int32_t tick;
    uint64_t seed;
    DemoWorldStatus status;
    bool truncated;

    Actor boss;
    Actor students[DEMO_MAX_STUDENTS];
    uint32_t student_count;

    uint32_t wave_index;       /* 已派出的波次: 初始为 1 */
    uint32_t waves_cleared;
    DemoStudentWavePhase wave_phase;
    uint32_t next_wave_students;
    int32_t wave_spawn_tick;   /* 冻结出生预告的绝对 tick */
    uint32_t spawn_preview_count;
    Vec2 spawn_preview[DEMO_MAX_STUDENTS];
    DemoEntityId next_student_id; /* 跨波学生身份持续递增 */
    int32_t gpa_hundredths;
    uint32_t students_defeated;
    uint32_t students_deployed;

    /* 本 tick 移动前坐标: 扫掠碰撞需要位移段 */
    float boss_prev_x;
    float boss_prev_y;
    float student_prev_x[DEMO_MAX_STUDENTS];
    float student_prev_y[DEMO_MAX_STUDENTS];

    StudentBotState bot[DEMO_MAX_STUDENTS];
    StudentObservation obs[DEMO_MAX_STUDENTS];
    StudentAction action[DEMO_MAX_STUDENTS];
    /* student_fire_try 的 inout_cooldown 入参是 uint32_t*（接口 v2 冻结），
     * 而 StudentBotState.fire_cooldown_ticks 是 int32_t。
     * 这里保留一个 uint32_t 中转字段并在同一 tick 内同步，避免 `(uint32_t *)&int32_t` 强转
     * 触发的严格别名违规（S20 验收 P1）。 */
    uint32_t fire_cooldown_scratch[DEMO_MAX_STUDENTS];

    ProjectilePool pool;

    int32_t energy;
    float energy_regen_accum;

    DemoAttackState attack_state;
    AttackPlan plan;
    uint64_t next_plan_id;

    uint32_t student_since_decision; /* 决策调度: 由 world 统一协调, 不由各端自己决定 */
    PendingRequest pending[DEMO_PATTERN_COUNT];
    uint32_t pending_count;

    uint32_t boss_hits_taken;
    uint32_t student_hits_taken;
    uint32_t boss_bullets_spawned;
    uint32_t student_bullets_spawned;
    uint32_t spawn_overflow_count;
    uint32_t attack_accept_count;
    uint32_t attack_reject_count;

    StepEvents events;
    StepResult last_result;
} World;

/* 初始化/重置世界。同 config 同 seed 必须得到同初始状态。
 * 返回 false 表示指针为空或配置非法。重置清除: 弹幕、计划、目标、能量、AI 状态、
 * 无敌计时、待处理请求与全部计数。 */
bool world_reset(World *world, const DemoConfig *config, uint64_t seed);

/* 仅由累计真实击倒数决定 GPA: floor(max*n/(n+k)), 单位为 0.01。
 * 使用 64 位整数计算, 不累计逐次舍入, 有限 n 不会达到正渐近上限。
 * 传入空配置、非正上限或 k=0 时返回 0。 */
int32_t world_gpa_for_kills(const DemoConfig *config, uint32_t kills);

/* 推进一个逻辑 tick。input 可为 NULL(视为无输入)。 */
void world_step(World *world, const BossInput *input);

/* 只读视图: 渲染、AI 与日志共用。不暴露未来波次与隐藏随机状态。 */
typedef struct WorldView {
    int32_t tick;
    DemoWorldStatus status;
    bool truncated;
    bool paused;

    const Actor *boss;
    const Actor *students;
    uint32_t student_count;

    uint32_t wave_index;
    uint32_t waves_cleared;
    DemoStudentWavePhase wave_phase;
    uint32_t next_wave_students;
    int32_t wave_spawn_tick;
    uint32_t spawn_preview_count;
    Vec2 spawn_preview[DEMO_MAX_STUDENTS]; /* 当前已公开, 不属于隐藏未来几何 */
    int32_t gpa_hundredths;
    uint32_t students_defeated;
    uint32_t students_deployed;

    const Projectile *projectiles;
    uint32_t projectile_capacity;
    uint32_t projectile_live;

    int32_t energy;
    int32_t energy_max;

    DemoAttackState attack_state;
    bool plan_active;
    DemoPattern plan_pattern;
    DemoEntityId plan_target;
    int32_t plan_start_tick;
    int32_t plan_windup_ticks;
    int32_t plan_active_ticks;
    PatternWarning warning;

    uint64_t plan_id;
    uint64_t world_seed;
    uint32_t attack_accept_count;
    uint32_t attack_reject_count;
    uint32_t boss_hits_taken;
    uint32_t student_hits_taken;

    const StepEvent *events;
    uint32_t event_count;

    /* 渲染只读派生量 */
    DemoEntityId marked_target; /* 最近存活学生; 无目标为 0 */
    bool pattern_available[DEMO_PATTERN_COUNT];
} WorldView;

void world_make_view(const World *world, WorldView *out);

/* 便捷查询: 最近存活学生(同距按稳定 ID 小者优先); 无则返回 false。 */
bool world_nearest_student(const World *world, DemoEntityId *out_id, float *out_x,
                           float *out_y);
/* 该招式当前是否可请求(能量/忙碌/目标检查), 不改状态。 */
bool world_pattern_available(const World *world, DemoPattern pattern);
/* 当前刻的 student 观测(只读, 供渲染/诊断)。 */
const StudentObservation *world_student_observation(const World *world, uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* DEMO_WORLD_H */
