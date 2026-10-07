/* demo_base.h - ustc-danmaku 最小可玩 Boss demo 公共基础类型
 *
 * 接口版本: 1
 * 本文件必须同时可被 C99/C11 与 C++ 包含。
 * 核心规则: 纯 C, 不包含 EasyX / Windows API / 墙钟 / C++ 容器。
 * 所有时间量都是整数逻辑 tick, 1 秒 = DEMO_TICKS_PER_SECOND 个 tick。
 */
#ifndef DEMO_BASE_H
#define DEMO_BASE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- 常量 */

#define DEMO_TICKS_PER_SECOND 60
#define DEMO_MAX_STUDENTS 8u
#define DEMO_MAX_PROJECTILES 800u
#define DEMO_MAX_ACTIVE_PLAN_PROJECTILES 256u
#define DEMO_MAX_STEP_EVENTS 256u
#define DEMO_MAX_ATTACK_PLAN_POINTS 512u

/* 场景: 画面 1280x720, 左侧逻辑战场 960x720, 右侧 320 px HUD */
#define DEMO_FIELD_WIDTH 960.0f
#define DEMO_FIELD_HEIGHT 720.0f

/* 学生数量上限之外的实体一律不存在; 学生槽位索引稳定, 不因倒下而重排 */
typedef uint32_t DemoEntityId;

typedef enum DemoWorldStatus {
    DEMO_STATUS_RUNNING = 0,
    DEMO_STATUS_BOSS_WIN,
    DEMO_STATUS_BOSS_LOSE,
    DEMO_STATUS_DRAW
} DemoWorldStatus;

/* 无尽模式: 清波后只进入公开出生预告, 不产生胜利终局。 */
typedef enum DemoStudentWavePhase {
    DEMO_STUDENT_WAVE_ACTIVE = 0,
    DEMO_STUDENT_WAVE_PREVIEW = 1
} DemoStudentWavePhase;

typedef enum DemoFaction {
    DEMO_FACTION_NONE = 0,
    DEMO_FACTION_BOSS,    /* Boss 弹: 伤害学生 */
    DEMO_FACTION_STUDENT  /* 学生弹: 伤害 Boss */
} DemoFaction;

typedef enum DemoAttackState {
    DEMO_ATTACK_IDLE = 0,
    DEMO_ATTACK_WINDUP, /* 预警: 只显示, 不生成弹 */
    DEMO_ATTACK_ACTIVE  /* 攻击: 按计划生成弹 */
} DemoAttackState;

/* 四招: 动作 ID 与招式一一对应 */
typedef enum DemoPattern {
    DEMO_PATTERN_RING = 0,   /* 桃李苑·绿色圆圈好辣 - 中耗周边压力 */
    DEMO_PATTERN_COURSE = 1, /* 选课系统·课表华容道 - 中耗封路 */
    DEMO_PATTERN_MINE = 2,   /* 一教金矿·绩点淘金   - 低耗追击 */
    DEMO_PATTERN_SHOWER = 3, /* 期末总评·绩点淋浴   - 高耗多目标压制 */
    DEMO_PATTERN_COUNT = 4
} DemoPattern;

/* 出招请求被拒绝的原因; 拒绝不扣能量, 不排队 */
typedef enum DemoRejectReason {
    DEMO_REJECT_NONE = 0,
    DEMO_REJECT_NO_ENERGY,
    DEMO_REJECT_BUSY,
    DEMO_REJECT_NO_TARGET,
    DEMO_REJECT_NO_REQUEST,
    DEMO_REJECT_COUNT
} DemoRejectReason;

typedef enum DemoBotPolicy {
    DEMO_BOT_PATROL = 0, /* 简单往返脚本 */
    DEMO_BOT_DODGE = 1   /* 基于可见弹/公开预警的有限躲避 */
} DemoBotPolicy;

/* ---------------------------------------------------------------- 数学 */

typedef struct Vec2 {
    float x;
    float y;
} Vec2;

/* ---------------------------------------------------------------- RNG */

/* SplitMix64 派生流。状态只由 seed 与 stream_id 决定, 不受绘制/日志影响。 */
typedef struct Rng {
    uint64_t state;
    uint64_t stream_id;
} Rng;

void rng_seed(Rng *rng, uint64_t seed, uint64_t stream_id);
void rng_reset(Rng *rng); /* 恢复到 rng_seed 之后、第一次抽取之前的状态 */
uint64_t rng_next_u64(Rng *rng);
uint32_t rng_next_u32(Rng *rng);
int32_t rng_range_i32(Rng *rng, int32_t lo, int32_t hi); /* [lo, hi] 闭区间 */
float rng_unit_f32(Rng *rng);                            /* [0, 1) */
/* 在 [0, n) 内均匀取整数; n == 0 时返回 0 且不消耗状态 */
uint32_t rng_below(Rng *rng, uint32_t n);
bool rng_next_bool(Rng *rng);

/* ---------------------------------------------------------------- 配置 */

typedef struct PatternConfig {
    int32_t cost;             /* 共享能量消耗 */
    int32_t windup_ticks;     /* 预警时长 */
    int32_t active_ticks;     /* 攻击时长 */
    float bullet_speed;       /* px/s */
    int32_t wave_count;       /* 波次数量 */
    int32_t shots_per_wave;   /* 每波基础发数(扇面/环/列共用, 具体含义见招式模块) */
    float gap_span_deg;       /* 环弹: 缺口总跨度; 其余招式保留 */
    float corridor_width;     /* 课表: 通道宽度 px; 淋浴: 竖向缝隙 px */
    float lane_spread_px;     /* 课表: 列内横向弹线偏移, px */
    float spawn_safety_radius;/* 环弹/金矿: 生成点与学生安全距离 */
    float first_spawn_sec;    /* 第一波生成时刻(相对攻击开始) */
    float wave_interval_sec;  /* 相邻波次间隔 */
} PatternConfig;

typedef enum DemoOutcomeRule {
    DEMO_OUTCOME_DRAW = 0,       /* 同 tick 双方倒下记平局 */
    DEMO_OUTCOME_BOSS_WIN = 1,   /* 同 tick 双方倒下记 Boss 胜(已批准) */
    DEMO_OUTCOME_BOSS_LOSE = 2   /* 同 tick 双方倒下记 Boss 败 */
} DemoOutcomeRule;

typedef struct DemoConfig {
    uint32_t version; /* 配置版本号; 变更规则/数值时必须递增 */
    uint32_t seed_default;

    /* 场地与角色 */
    float field_w;
    float field_h;
    float boss_move_min_x;
    float boss_move_max_x;
    float boss_move_min_y;
    float boss_move_max_y;
    float boss_radius;
    float boss_hit_radius;
    bool boss_hit_radius_equals_body; /* true 时命中半径取 boss_radius */
    int32_t boss_hp;
    int32_t boss_hurt_invuln_ticks;
    float boss_speed;     /* 复现移动使用(脚本 AI / 回放) */
    bool pointer_to_position; /* true=指针到位置(鼠标摇杆); false=仅使用轴向量 */
    float pointer_deadzone;   /* 指针死区(战场像素) */
    float pointer_saturate;   /* 指针达到全速的距离(战场像素) */

    /* 学生 */
    uint32_t student_count;
    float student_radius;
    float student_speed;
    int32_t student_hp;
    int32_t student_hurt_invuln_ticks;
    float student_spawn_x[DEMO_MAX_STUDENTS];
    float student_spawn_y[DEMO_MAX_STUDENTS];
    int32_t student_decision_ticks; /* AI 决策周期 */
    int32_t student_bot_policy;     /* DemoBotPolicy */
    int32_t student_fire_interval_ticks;
    int32_t student_fire_damage;
    float student_bullet_speed;
    float student_bullet_radius;
    int32_t student_bullet_lifetime_ticks;
    float student_fire_min_range;   /* 学生与该距离内不发射(避免零距离) */

    /* 共享能量 */
    int32_t energy_max;
    int32_t energy_start;
    float energy_regen_per_sec;

    /* 弹池与弹 */
    uint32_t projectile_cap;
    float boss_bullet_radius;
    float boss_bullet_damage;   /* 每发命中扣学生多少血 */
    int32_t boss_bullet_lifetime_ticks;
    int32_t boss_bullet_clear_on_attack_end; /* >0: 攻击结束清本招剩余 Boss 弹 */
    bool boss_bullet_straight;               /* true=直线基础运动(已批准) */

    /* 四招 */
    PatternConfig patterns[DEMO_PATTERN_COUNT];

    /* 裁决与实验截断 */
    bool endless_mode;    /* 产品默认 true; false 仅保留历史有限局回归 */
    int32_t wave_gap_ticks; /* 无尽清波后的公开出生预告, 至少 120 tick */
    uint32_t gpa_half_saturation_kills; /* GPA = max*n/(n+k); 默认 k=20, 必须 >0 */
    int32_t gpa_max_hundredths;         /* 默认 430 = 4.30 */
    int32_t outcome_rule; /* DemoOutcomeRule */
    int32_t max_ticks;    /* sim 防卡截断; 截断记为 TRUNCATED, 不判胜负 */
} DemoConfig;

/* 填默认配置(含已批准规则)。返回 false 表示指针为空或 cfg 内指针非法。 */
bool demo_config_init(DemoConfig *cfg);
/* 校验配置。错误原因写入 err(可为 NULL), err_cap 为缓冲区大小。 */
bool demo_config_validate(const DemoConfig *cfg, char *err, size_t err_cap);
/* 配置版本字符串, 便于日志与 HUD */
const char *demo_config_version_string(void);

/* ---------------------------------------------------------------- 输入 */

/* 平台层每帧收集的原始输入。核心不读取键盘/鼠标, 只消费本结构。 */
typedef struct RawInput {
    float mouse_x;        /* 指针在窗口内的像素坐标 */
    float mouse_y;
    bool mouse_inside;    /* 指针是否在窗口内且窗口有焦点 */
    float joy_x;          /* 手柄/键盘合成的归一化轴, [-1,1] */
    float joy_y;
    bool attack_pressed[DEMO_PATTERN_COUNT]; /* 本帧按下边沿(Down) */
    bool attack_held[DEMO_PATTERN_COUNT];    /* 本帧是否按住 */
    bool pause_pressed;
    bool restart_pressed;
    bool quit_pressed;
} RawInput;

/* 核心每 tick 消费的移动与出招意图。同一逻辑 tick 最多接受一个请求。
 *
 * 移动语义（已批准，见 docs/demo-rules.md 1.2）:
 *   - 指针到位置（鼠标摇杆, florr/digdig 风格）: 平台层把鼠标坐标换算到战场坐标,
 *     填入 pointer_x/pointer_y 并把 pointer_valid 置 true; 核心自己计算
 *     "朝指针方向、距离越近越慢、死区内停住"。
 *   - 键盘/手柄: 平台层填 move_x/move_y（八方向单位向量, 斜向归一化）。
 *   - 两者同时存在时的优先级由平台层决定（当前: 指针在窗口内且超出死区时用指针）。 */
typedef struct BossInput {
    float move_x; /* [-1,1] 键盘/手柄轴 */
    float move_y;
    bool pointer_valid;      /* 指针是否用于移动 */
    float pointer_x;         /* 指针在战场坐标系中的位置 */
    float pointer_y;
    float pointer_deadzone;  /* 死区（战场像素）; <=0 时用核心默认值 */
    float pointer_saturate;  /* 达到全速的距离; <=0 时用核心默认值 */
    bool attack_requested[DEMO_PATTERN_COUNT];
    bool pause_requested;   /* 由外层状态机消费, 核心不处理暂停 */
    bool restart_requested;
    bool quit_requested;
} BossInput;

void boss_input_clear(BossInput *in);

/* ---------------------------------------------------------------- 角色 */

typedef struct Actor {
    DemoEntityId id;
    bool alive;
    float x;
    float y;
    float radius;
    int32_t hp;
    int32_t hp_max;
    int32_t invuln_ticks; /* 剩余无敌 tick */
} Actor;

/* 按批准范围移动, 归一化斜向速度, 夹紧边界。world 只调用本函数, 不重复实现。 */
void actor_apply_move(Actor *a, float dir_x, float dir_y, float speed, float dt,
                      float min_x, float max_x, float min_y, float max_y);
/* 朝目标点移动: 指针到位置语义。距离 deadzone 内不移动, 接近时不越过目标。 */
void actor_move_toward(Actor *a, float target_x, float target_y, float speed, float dt,
                       float deadzone, float min_x, float max_x, float min_y, float max_y);
/* 扣血入口。无敌中返回 false 且不扣血; 血量大等于 0 后 clamp 到 0。 */
bool actor_apply_damage(Actor *a, int32_t damage);
void actor_tick_timers(Actor *a);

/* ---------------------------------------------------------------- 弹 */

typedef struct Projectile {
    uint64_t id;          /* 整局唯一: (pool_index, generation, tick) 组合, 见 projectiles.c */
    uint32_t generation;  /* 同一池位复用的世代号, 不复用下标充当整局 ID */
    bool active;
    DemoFaction faction;
    DemoEntityId source_id;
    uint64_t plan_id;     /* 属于哪次攻击计划; 学生弹为 0 */
    DemoPattern source_pattern; /* 由哪一招放出; 学生弹为 0 且无意义 */
    float px;
    float py;             /* 本 tick 起点 */
    float x;
    float y;              /* 本 tick 终点 */
    float vx;
    float vy;
    float radius;
    float damage;
    int32_t lifetime_ticks;
} Projectile;

typedef struct ProjectilePool {
    Projectile items[DEMO_MAX_PROJECTILES];
    uint32_t capacity;
    uint32_t live_count;
    uint32_t next_generation;
    uint32_t overflow_events; /* 容量不足次数; 供诊断, 不许静默丢失 */
} ProjectilePool;

void pool_init(ProjectilePool *pool, uint32_t capacity);
/* 生成一发弹。返回 false 表示容量不足, 不越界写。 */
bool pool_spawn(ProjectilePool *pool, DemoFaction faction, DemoEntityId source_id,
                uint64_t plan_id, DemoPattern source_pattern, float x, float y, float vx,
                float vy, float radius, float damage, int32_t lifetime_ticks,
                uint64_t id_seed_tick);
void pool_advance(ProjectilePool *pool, float dt); /* 位置积分 + 寿命推进 + 出界移除 */
uint32_t pool_clear_plan(ProjectilePool *pool, uint64_t plan_id); /* 返回清除数量 */
uint32_t pool_count_faction(const ProjectilePool *pool, DemoFaction faction);

/* pattern_emit 的固定容量输出缓冲 */
typedef struct ProjectileSpawnBuffer {
    uint32_t count;
    uint32_t capacity;
    uint32_t overflow;
    Projectile spec[DEMO_MAX_ACTIVE_PLAN_PROJECTILES];
} ProjectileSpawnBuffer;

void spawn_buffer_init(ProjectileSpawnBuffer *buf);
bool spawn_buffer_push(ProjectileSpawnBuffer *buf, const Projectile *spec);

/* ---------------------------------------------------------------- 碰撞 */

typedef struct Segment {
    float ax, ay;
    float bx, by;
} Segment;

/* 相对运动扫掠: 判断线段 A(本 tick) 与线段 B(本 tick) 的最小距离是否 <= radius_sum,
 * 并输出最早命中参数 t_min ∈ [0,1] 与最小距离。零相对运动分支不能除零。
 * 返回值: true 表示在 t_min 处命中。 */
bool sweep_hit(const Segment *a, const Segment *b, float radius_sum, float *t_min,
               float *min_distance);
/* 只求最小距离, 不判命中; 用于擦弹等非判定用途。 */
float sweep_min_distance(const Segment *a, const Segment *b, float *t_min);
/* 线段是否与轴对齐矩形相交; 用于出界与场景查询。 */
bool segment_hits_aabb(const Segment *s, float min_x, float min_y, float max_x, float max_y);
/* 线段是否与圆相交; 用于出生安全检查。 */
bool segment_hits_circle(const Segment *s, float cx, float cy, float radius);

/* ---------------------------------------------------------------- 攻击计划 */

/* 计划在"接受请求"时一次生成, 之后不可变: 预警与攻击读取同一份数据。 */
typedef struct AttackPlan {
    bool active;
    uint64_t plan_id;      /* 单调递增, 整局唯一 */
    DemoPattern pattern;
    DemoEntityId target_id;
    float origin_x;        /* 锁定时的 Boss 位置 */
    float origin_y;
    float aim_x;           /* 锁定时的目标位置 */
    float aim_y;
    float aim_dir_x;       /* 归一化锁定方向(未命中时用瞄准方向) */
    float aim_dir_y;
    float lock_speed;      /* 锁定时的弹速, 与配置一致 */
    int32_t start_tick;    /* 接受请求的 tick */
    int32_t windup_ticks;
    int32_t active_ticks;
    float gap_angle_deg;   /* 环弹缺口中心角 */
    float gap_span_deg;
    float gap_drift_deg_per_wave; /* 每波缺口旋转量 */
    float corridor_width;         /* 课表/淋浴: 通道或缝隙宽度 */
    float lane_spread_px;         /* 接受时锁定的课表弹线偏移 */
    int32_t wave_count;
    int32_t shots_per_wave;
    float wave_tick[DEMO_PATTERN_COUNT * 8]; /* 每波相对 tick：预警结束、攻击开始为 0 */
    float wave_offset;             /* 通用偏移(列位置/扫描起点) */
    uint64_t geometry_seed;        /* 从 world RNG 抽取的几何种子, 保存在计划内 */
    bool lock_checked_at_spawn;    /* 历史名称：接受时检查安全几何；不表示生成时复查 */
} AttackPlan;

/* 生成计划所需的输入; 由 core/attack.c 组装, 招式模块只读。 */
typedef struct PatternRequest {
    DemoPattern pattern;
    DemoEntityId target_id;
    float origin_x;
    float origin_y;
    float target_x;
    float target_y;
    int32_t start_tick;
    uint32_t student_count;
    float student_x[DEMO_MAX_STUDENTS];
    float student_y[DEMO_MAX_STUDENTS];
    bool student_alive[DEMO_MAX_STUDENTS];
    float student_radius;
    float field_w;
    float field_h;
} PatternRequest;

/* 生成完整计划。返回 false 表示请求非法(指针为空/招式非法/几何无法满足),
 * 不产生半成品计划, 由调用方回滚能量与状态。 */
bool pattern_make_plan(const PatternRequest *request, const DemoConfig *config, Rng *rng,
                       AttackPlan *out);
/* 按计划在 attack_tick(预警结束后的攻击阶段相对 tick) 生成该 tick 应生成的弹。
 * 四招 emit 与 wave_tick[] 都采用此零点；不得再加减 plan.start_tick。
 * 返回是否生成了波次。 */
bool pattern_emit(const AttackPlan *plan, const DemoConfig *config, uint32_t attack_tick,
                  Rng *rng, ProjectileSpawnBuffer *out);
/* 只读: 适用于渲染/AI 的公开预警几何。 */
typedef struct PatternWarning {
    bool valid;
    DemoPattern pattern;
    DemoEntityId target_id;
    float origin_x, origin_y;
    float aim_x, aim_y;
    float radius_hint;      /* 环弹半径/扇面半径等提示, 0 表示无 */
    float gap_angle_deg;
    float gap_span_deg;
    float corridor_width;
    int32_t wave_count;
} PatternWarning;

void pattern_warning(const AttackPlan *plan, const DemoConfig *config, PatternWarning *out);

/* ---------------------------------------------------------------- 事件 */

typedef enum DemoEventType {
    DEMO_EVENT_NONE = 0,
    DEMO_EVENT_ATTACK_ACCEPTED,
    DEMO_EVENT_ATTACK_REJECTED,
    DEMO_EVENT_WINDUP_START,
    DEMO_EVENT_ATTACK_START,
    DEMO_EVENT_WAVE_SPAWN,
    DEMO_EVENT_SPAWN_OVERFLOW,
    DEMO_EVENT_HIT,
    DEMO_EVENT_KNOCKDOWN,
    DEMO_EVENT_ENERGY_SPENT,
    DEMO_EVENT_STUDENT_FIRE,
    DEMO_EVENT_GAME_OVER,
    DEMO_EVENT_TRUNCATED,
    DEMO_EVENT_STUDENT_WAVE_CLEAR,
    DEMO_EVENT_STUDENT_WAVE_PREVIEW,
    DEMO_EVENT_STUDENT_WAVE_BEGIN
} DemoEventType;

typedef struct StepEvent {
    DemoEventType type;
    int32_t tick;
    DemoEntityId source_id;
    DemoEntityId target_id;
    DemoPattern pattern;
    DemoRejectReason reject;
    int32_t amount; /* 伤害/能量/生成数等负重 */
    float x, y;
} StepEvent;

typedef struct StepEvents {
    uint32_t count;
    uint32_t capacity;
    uint32_t dropped; /* 事件缓冲不足时计数, 不静默丢弃 */
    StepEvent items[DEMO_MAX_STEP_EVENTS];
} StepEvents;

void events_init(StepEvents *events);
bool events_push(StepEvents *events, const StepEvent *event);

/* ---------------------------------------------------------------- 学生 AI */

/* 学生在某 tick 可见的信息: 只包含公开数据, 不含未来波次与 RNG。 */
typedef struct StudentObservation {
    int32_t tick;
    DemoEntityId self_id;
    float self_x, self_y;
    float self_radius;
    int32_t self_hp;
    bool self_alive;
    float field_w, field_h;
    float target_x, target_y; /* 当前锁定目标(通常是 Boss) */
    float aim_dir_x, aim_dir_y;
    bool can_fire;
    uint32_t projectile_count;
    struct VisibleProjectile {
        float x, y;
        float vx, vy;
        float radius;
    } projectiles[64];
    PatternWarning warning; /* 当前公开预警(可能 valid=false) */
} StudentObservation;

typedef struct StudentBotState {
    int32_t decision_cooldown_ticks;
    float move_x, move_y;
    int32_t fire_cooldown_ticks;
    uint32_t patrol_phase;
    float patrol_dir_x, patrol_dir_y;
    uint64_t choice_serial;
} StudentBotState;

typedef struct StudentAction {
    float move_x, move_y; /* 归一化移动意图, [-1,1] */
    bool request_fire;    /* 是否请求本 tick 反击; 由核心裁决 */
} StudentAction;

/* 产出意图, 不写世界。同观测同状态必须得到同结果。 */
void student_bot_choose(const StudentObservation *obs, StudentBotState *bot,
                        StudentAction *out);
void student_bot_init(StudentBotState *bot, uint32_t serial_seed);

/* ---------------------------------------------------------------- 世界 */

typedef struct StepResult {
    uint32_t event_count;
    bool emitted_projectile;
    bool attack_accepted;
    DemoRejectReason last_reject;
} StepResult;

/* 不透明世界句柄由 core/world.h 提供; world 内部布局不属于公共接口。 */

#ifdef __cplusplus
}
#endif

#endif /* DEMO_BASE_H */
