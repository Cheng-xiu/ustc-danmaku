/* pattern_course.c - S08 招 1 选课系统·课表华容道: 封路弹带与连续可达通道
 *
 * 接口版本: 1 (core/demo_base.h + core/pattern_course.h 已冻结; 本文件不改任何头文件)
 * 配置版本: 1 (demo-config-v1: cost 35 / windup 72 / active 180 / bullet_speed 220 px/s
 *             / wave_count 3 / shots_per_wave 24 / corridor_width 130 px
 *             / first_spawn_sec 0 / wave_interval_sec 0.6)
 * 用途档位: 中消耗·封路 (消耗 35)
 *
 * 职责与边界:
 *   - 只实现 pattern_course_{make_plan,emit,warning} 三个冻结函数。
 *   - 不改注册表/配置/其他招式; 不写日志; 不读墙钟; 不使用平台 rand();
 *     不使用 EasyX/Windows API/C++ 容器; 纯 C11。
 *   - emit 内**不调用任何 rng**: 冻结签名没有 Rng 入参, 全部几何在 make_plan
 *     固化进 AttackPlan, emit 只读计划(本文件 emit/warning 无任何 rng_* 调用)。
 *
 * ---------------------------------------------------------------- 已批准几何
 *
 *   1. 战场按宽度三等分: 列宽 colw = field_w / 3, 第 j 列中心 cx[j] = colw * (j + 0.5)。
 *      默认场宽 960 px => colw = 320 px, cx = {160, 480, 800}。
 *   2. 每一波封锁 3 列中的 2 列(各一条竖直下落弹带), 保留 1 列作为通道列。
 *   3. 通道列逐波推进到相邻列, 保证"旧通道可达新通道":
 *          channel[i] = (channel[0] + i) % 3
 *      channel[0] 在 make_plan 用 rng_below(rng, 3) 抽一次(唯一一次 rng 消耗),
 *      之后完全确定 —— 相邻两波的通道列必然相邻(模 3 差 1), 学生只需横向平移
 *      一个列宽即可跟上, 不会被两侧弹带夹死。
 *   4. 计划内的编码(头文件不改, 复用通用字段, 报告同步记录):
 *          plan->wave_offset   = (float)channel[0]     <- 首波通道列号, 取值 0/1/2
 *          plan->geometry_seed = (uint64_t)channel[0]  <- 同一初值的整型存档
 *      emit 里按 channel(i) = (wave_offset + i) % 3 重算, 几何因此完全确定。
 *
 * ---------------------------------------------------------------- 通道无弹区保证
 *
 *   每发的 x 只允许落在本列中心 ± COURSE_LANE_JITTER(固定常量 8 px, 与 rng 无关):
 *          x = cx[col] + ((k % 3) - 1) * 8        =>  x ∈ [cx-8, cx+8]
 *   于是任一被封锁列的弹心 x 都严格落在本列内部, 相邻列之间没有任何弹心。三种通道
 *   情形下"无弹心区"宽度分别为:
 *          channel 在边缘列(0 或 2): 1.5 * colw - 8  >= corridor_width
 *          channel 在中间列(1)     : 2.0 * colw - 16 >= corridor_width
 *   make_plan 用最紧的一条 (1.5 * colw - 8 >= corridor_width) 作为硬性可行性判据;
 *   默认配置下无弹心区宽 472 px, 远大于 corridor_width 130 px(结论: 通道列不会
 *   被任何弹覆盖, 且相邻被封锁列的弹带也各向内收缩了 8 px)。因为弹竖直下落
 *   (vx = 0), 这条无弹区在整条下落路径上都不变, 不会在飞行途中被封死。
 *   语义说明: corridor_width 是"弹心空档"宽度; 实际可通过净宽还要扣掉
 *   2 * (boss_bullet_radius + student_radius), 由渲染/AI 自行解释, 本模块不放大。
 *
 * ---------------------------------------------------------------- 弹带排布
 *
 *   被封锁列每列发数 per_column = shots_per_wave / 2 (整数除法), 两列合计
 *   2 * per_column; 若 shots_per_wave 为奇数, 差 extra = shots - 2*per_column
 *   (0 或 1)补到第一条被封锁列, 因此每波总发数恰好为 shots_per_wave, 绝不超过。
 *   每列竖直排布: 全部出生点位于已批准的顶部 y=0..100 带，紧凑排列:
 *          y_k = 100 * k / (n_column - 1), 单发时 y=100。
 *   速度恒为 (vx, vy) = (0, lock_speed): 竖直下落, |v| == lock_speed。
 *
 * ---------------------------------------------------------------- 出生安全距离
 *
 *   本招全部弹从场地顶部 y = 0..100 出生并竖直落下，不再按场高铺满。
 *   顶部生成带是已批准几何，生成点不随学生或 Boss 移动迁移，
 *   与"环弹/金矿"的 spawn_safety_radius 语义无关。因此即使 req 中某个存活学生与
 *   某一列中心的距离 < student_radius, 也**照常允许生成请求**(不因此返回 false),
 *   且 plan->lock_checked_at_spawn 恒为 false, 如实说明没有做生成时复查。
 *
 * ---------------------------------------------------------------- 参数合法性
 *
 *   make_plan 在**写 out 之前**完成全部校验, 任一不满足即返回 false 且不写 out
 *   (调用方据此回滚能量与状态)。被拒的情形:
 *     - request/config/rng/out 任一为空指针;
 *     - 场宽/场高/弹速/时长/发数/波次/波次时刻非法(<=0、非有限);
 *     - corridor_width 非有限或 < 110 px(硬性下限, 不静默加宽);
 *     - start_tick < 0;
 *     - 波次数量超过 wave_tick[] 容量(DEMO_PATTERN_COUNT * 8 == 32);
 *     - 几何上无法留出 corridor_width 无弹区(1.5 * colw - 8 < corridor_width);
 *     - 某波时刻非有限/超范围, 或晚于 active_ticks(计划无法"每波各生成一次"兑现);
 *     - Boss 弹半径/伤害/寿命非法。
 *
 *   plan_id 由调用方(core/attack.c 的 next_plan_id)在 make_plan 之后赋值:
 *   本模块按任务要求清零 out, 既不保留也不生成 plan_id(不影响调用方流程)。
 */
#include "pattern_course.h"

#include <math.h>
#include <string.h>

/* 战场等分列数(选课系统固定 3 列)。 */
#define COURSE_COLUMNS 3

/* docs/demo-rules.md: 课表保留 >= 110 px 连续通道(硬性下限), 配置默认 130 px。 */
#define COURSE_MIN_CORRIDOR_WIDTH 110.0f

/* Boss 弹的 source_id: world 里 Boss 的实体 ID 固定为 1 (core/world.c)。 */
#define COURSE_BOSS_SOURCE_ID 1u

/* wave_tick[] 的数组长度: AttackPlan 里是 DEMO_PATTERN_COUNT * 8 == 32。 */
#define COURSE_WAVE_TICK_CAP (DEMO_PATTERN_COUNT * 8)

/* 每条弹相对本列中心的固定横向抖动(px): 确定性常量, 不使用 rng。 */
#define COURSE_LANE_JITTER 8.0f

/* 基线曾按场高铺到 y=660，违背顶部出生；只修出生带，不改其余几何。 */
#define COURSE_TOP_SPAWN_Y 100.0f

/* 四舍五入到最近整数 tick。避免依赖 libm 的 lroundf; 只处理 v >= 0 的已校验输入。
 * 非有限或超出 int32 表示范围时返回 INT32_MIN, 表示"该波次时刻不可用"。 */
static int32_t course_round_tick(float v) {
    int32_t base;
    float frac;

    if (!isfinite(v)) {
        return INT32_MIN;
    }
    if (v < 0.0f || v > 1.0e9f) {
        return INT32_MIN;
    }
    base = (int32_t)v; /* 截断 */
    frac = v - (float)base;
    if (frac >= 0.5f) {
        base += 1;
    }
    return base;
}

/* 第 wave 波的通道列 = (首波通道列 + wave) % 3 (见文件头第 3/4 条)。 */
static int32_t course_channel_of(const AttackPlan *plan, int32_t wave) {
    int32_t col;
    int32_t c;

    if (!isfinite(plan->wave_offset)) {
        return 0;
    }
    col = (int32_t)plan->wave_offset; /* make_plan 只写 0/1/2 的整值 */
    c = (col + wave) % COURSE_COLUMNS;
    if (c < 0) {
        c += COURSE_COLUMNS;
    }
    return c;
}

/* 第 k 发相对本列中心的固定横向偏移: {-8, 0, +8} 循环, 与随机数无关。 */
static float course_lane_offset(int32_t k) {
    int32_t slot = k % 3; /* k >= 0, 结果 0/1/2 */
    return ((float)slot - 1.0f) * COURSE_LANE_JITTER;
}

bool pattern_course_make_plan(const PatternRequest *request, const DemoConfig *config, Rng *rng,
                              AttackPlan *out) {
    const PatternConfig *pc;
    float field_w;
    float field_h;
    float colw;
    float corridor;
    float first;
    float step;
    float dx;
    float dy;
    float len;
    int32_t waves;
    int32_t shots;
    int32_t initial_col;
    int32_t i;

    if (request == NULL || config == NULL || rng == NULL || out == NULL) {
        return false;
    }

    pc = &config->patterns[DEMO_PATTERN_COURSE];

    /* ---- 1) 参数校验: 全部在写 out 之前完成 ---- */
    field_w = config->field_w;
    field_h = config->field_h;
    if (!isfinite(field_w) || !isfinite(field_h) || field_w <= 0.0f || field_h <= 0.0f) {
        return false;
    }
    if (!isfinite(request->origin_x) || !isfinite(request->origin_y) ||
        !isfinite(request->target_x) || !isfinite(request->target_y)) {
        return false;
    }
    if (request->start_tick < 0) {
        return false;
    }
    if (!isfinite(pc->bullet_speed) || pc->bullet_speed <= 0.0f) {
        return false;
    }
    if (pc->windup_ticks < 0 || pc->active_ticks <= 0) {
        return false;
    }
    if (pc->wave_count <= 0 || pc->wave_count > COURSE_WAVE_TICK_CAP) {
        return false;
    }
    if (pc->shots_per_wave <= 0 ||
        pc->shots_per_wave > (int32_t)DEMO_MAX_ACTIVE_PLAN_PROJECTILES) {
        return false;
    }
    if (!isfinite(pc->corridor_width) || pc->corridor_width < COURSE_MIN_CORRIDOR_WIDTH) {
        return false; /* 硬性下限 110 px; 不静默加宽, 否则预警与几何会不一致 */
    }
    if (!isfinite(pc->first_spawn_sec) || pc->first_spawn_sec < 0.0f) {
        return false;
    }
    if (!isfinite(pc->wave_interval_sec) || pc->wave_interval_sec < 0.0f) {
        return false;
    }
    if (!isfinite(config->boss_bullet_radius) || config->boss_bullet_radius < 0.0f) {
        return false;
    }
    if (!isfinite(config->boss_bullet_damage)) {
        return false;
    }
    if (config->boss_bullet_lifetime_ticks <= 0) {
        return false;
    }

    waves = pc->wave_count;
    shots = pc->shots_per_wave;
    corridor = pc->corridor_width;
    colw = field_w / (float)COURSE_COLUMNS;

    /* 通道无弹区可行性: 最紧的一种通道位置(边缘列)也必须留出 corridor_width。
     * 见文件头"通道无弹区保证": 边缘列通道时无弹区 = 1.5 * colw - JITTER。 */
    if (!(colw * 1.5f - COURSE_LANE_JITTER >= corridor)) {
        return false;
    }

    /* 波次时刻必须有限且能在攻击时长内兑现。 */
    for (i = 0; i < waves; ++i) {
        float v = pc->first_spawn_sec * (float)DEMO_TICKS_PER_SECOND +
                  (float)i * pc->wave_interval_sec * (float)DEMO_TICKS_PER_SECOND;
        int32_t t = course_round_tick(v);

        if (t == INT32_MIN || t > pc->active_ticks) {
            return false;
        }
    }

    /* ---- 2) 唯一的随机量: 首波通道列 (0/1/2) ---- */
    initial_col = (int32_t)rng_below(rng, (uint32_t)COURSE_COLUMNS);

    /* ---- 3) 一次写满完整不可变计划 ---- */
    memset(out, 0, sizeof(*out));

    out->active = true;
    out->pattern = DEMO_PATTERN_COURSE;
    out->target_id = request->target_id;
    out->origin_x = request->origin_x; /* 锁定时的 Boss 位置 */
    out->origin_y = request->origin_y;
    out->aim_x = request->target_x;
    out->aim_y = request->target_y;

    /* 锁定方向: origin -> aim 的单位向量; 退化(重合)时退化为竖直向下 (0, 1)。 */
    dx = request->target_x - request->origin_x;
    dy = request->target_y - request->origin_y;
    len = sqrtf(dx * dx + dy * dy);
    if (isfinite(len) && len > 0.0f) {
        out->aim_dir_x = dx / len;
        out->aim_dir_y = dy / len;
    } else {
        out->aim_dir_x = 0.0f;
        out->aim_dir_y = 1.0f;
    }

    out->lock_speed = pc->bullet_speed;
    out->start_tick = request->start_tick;
    out->windup_ticks = pc->windup_ticks;
    out->active_ticks = pc->active_ticks;

    out->gap_angle_deg = 0.0f;          /* 课表无角度缺口概念 */
    out->gap_span_deg = 0.0f;
    out->gap_drift_deg_per_wave = 0.0f; /* 通道推进走 wave_offset 编码, 不用角度漂移 */
    out->corridor_width = corridor;
    out->wave_count = waves;
    out->shots_per_wave = shots;

    first = pc->first_spawn_sec * (float)DEMO_TICKS_PER_SECOND;
    step = pc->wave_interval_sec * (float)DEMO_TICKS_PER_SECOND;
    for (i = 0; i < COURSE_WAVE_TICK_CAP; ++i) {
        if (i < waves) {
            out->wave_tick[i] = first + step * (float)i;
        } else {
            out->wave_tick[i] = 0.0f; /* 未使用的下标显式清零, 不留未初始化数据 */
        }
    }

    /* 计划编码(见文件头第 4 条): wave_offset = 首波通道列号, geometry_seed 同值存档。 */
    out->wave_offset = (float)initial_col;
    out->geometry_seed = (uint64_t)initial_col;
    out->lock_checked_at_spawn = false; /* 顶部下落弹: 本招不要求出生安全距离 */
    return true;
}

bool pattern_course_emit(const AttackPlan *plan, const DemoConfig *config, uint32_t attack_tick,
                         ProjectileSpawnBuffer *out) {
    float colw;
    int64_t elapsed;
    int32_t waves;
    int32_t shots;
    int32_t per_column;
    int32_t extra;
    int32_t i;
    int32_t col;
    int32_t blocked;
    bool matched = false;

    if (plan == NULL || config == NULL || out == NULL) {
        return false;
    }
    if (!plan->active) {
        return false;
    }

    if (plan->wave_count <= 0 || plan->shots_per_wave <= 0) {
        return false;
    }
    if (plan->active_ticks <= 0) {
        return false;
    }
    if (!isfinite(plan->lock_speed) || plan->lock_speed <= 0.0f) {
        return false;
    }
    if (!isfinite(plan->corridor_width) || !isfinite(plan->wave_offset)) {
        return false;
    }
    if (!isfinite(config->field_w) || config->field_w <= 0.0f) {
        return false;
    }
    if (!isfinite(config->field_h) || config->field_h <= 0.0f) {
        return false;
    }
    if (!isfinite(config->boss_bullet_radius) || config->boss_bullet_radius < 0.0f) {
        return false;
    }
    if (!isfinite(config->boss_bullet_damage)) {
        return false;
    }
    if (config->boss_bullet_lifetime_ticks <= 0) {
        return false;
    }

    /* attack_tick 是预警结束后的相对 tick，与 wave_tick 使用同一零点。 */
    elapsed = (int64_t)attack_tick;
    if (elapsed < 0 || elapsed > (int64_t)plan->active_ticks) {
        return false;
    }

    waves = plan->wave_count;
    if (waves > COURSE_WAVE_TICK_CAP) {
        waves = COURSE_WAVE_TICK_CAP; /* 数组边界防御, 不越界读 wave_tick[] */
    }
    shots = plan->shots_per_wave;

    /* 整数除法分到两条被封锁列; 奇数发数时把不足的 1 发补给第一条被封锁列,
     * 保证每波总发数恰好 == shots_per_wave 且绝不超过它。 */
    per_column = shots / 2;
    if (per_column < 1) {
        per_column = 1;
    }
    extra = shots - per_column * 2;
    if (extra < 0) {
        extra = 0;
    }

    colw = config->field_w / (float)COURSE_COLUMNS;

    for (i = 0; i < waves; ++i) {
        int32_t due = course_round_tick(plan->wave_tick[i]);

        if (due == INT32_MIN) {
            continue; /* 波次时刻非法: 该波不生成, 不放宽成"补发密弹" */
        }
        if (elapsed != (int64_t)due) {
            continue; /* 本 tick 不是第 i 波的生成 tick */
        }

        matched = true;
        blocked = 0;
        for (col = 0; col < COURSE_COLUMNS; ++col) {
            float cx;
            float y_step;
            int32_t n;
            int32_t k;

            if (col == course_channel_of(plan, i)) {
                continue; /* 通道列不生成任何弹 */
            }

            n = per_column + ((blocked == 0) ? extra : 0);
            if (n <= 0) {
                blocked += 1;
                continue;
            }
            cx = colw * ((float)col + 0.5f);     /* 本列中心 x */
            y_step = (n > 1) ? COURSE_TOP_SPAWN_Y / (float)(n - 1) : 0.0f;

            for (k = 0; k < n; ++k) {
                Projectile spec;
                float x = cx + course_lane_offset(k); /* 固定常量抖动, 不用 rng */
                float y = (n > 1) ? y_step * (float)k : COURSE_TOP_SPAWN_Y;

                memset(&spec, 0, sizeof(spec)); /* id/generation/active 由 pool_spawn 赋值 */
                spec.faction = DEMO_FACTION_BOSS;
                spec.source_id = COURSE_BOSS_SOURCE_ID;
                spec.plan_id = plan->plan_id;
                spec.source_pattern = DEMO_PATTERN_COURSE;
                spec.px = x; /* 起点 == 终点: 弹池按速度推进 */
                spec.py = y;
                spec.x = x;
                spec.y = y;
                spec.vx = 0.0f; /* 竖直下落: vx 恒为 0 => |v| == lock_speed */
                spec.vy = plan->lock_speed;
                spec.radius = config->boss_bullet_radius;
                spec.damage = config->boss_bullet_damage;
                spec.lifetime_ticks = config->boss_bullet_lifetime_ticks;

                (void)spawn_buffer_push(out, &spec); /* 容量不足时 push 自己记 overflow */
            }
            blocked += 1;
        }
        break; /* 同一 tick 至多命中一波 */
    }

    return matched;
}

void pattern_course_warning(const AttackPlan *plan, const DemoConfig *config,
                            PatternWarning *out) {
    (void)config; /* 预警只读计划; 签名保留 config 以匹配注册表 */

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (plan == NULL || !plan->active) {
        out->valid = false;
        return;
    }

    out->valid = true;
    out->pattern = DEMO_PATTERN_COURSE;
    out->target_id = plan->target_id;
    out->origin_x = plan->origin_x;
    out->origin_y = plan->origin_y;
    out->aim_x = plan->aim_x;
    out->aim_y = plan->aim_y;
    out->radius_hint = 0.0f; /* 课表无环形/扇面半径 */
    out->corridor_width = plan->corridor_width;
    out->wave_count = plan->wave_count;
}
