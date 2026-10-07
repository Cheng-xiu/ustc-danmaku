/* pattern_course.c - 课表华容道: 顶部出弹, 三列中封两列、留一列。
 * 每波通道列 (初始列 + 波次) % 3；初始列只在接受请求时抽取一次。
 * 每个封锁列内有 center + {-spread, 0, spread} 三条竖直弹线，
 * spread 来自 PatternConfig.lane_spread_px，在不可变 AttackPlan 中锁定。
 * v4 默认 spread=96 px；弹体始终完全留在原封锁列。
 * 最紧通道的弹心空档是 1.5*column_width - spread = 384 px，
 * 大于要求的 130 px；扣掉两侧学生与弹半径仍有 332 px 净宽。
 * 每发出生 y=0..100，速度 (0, lock_speed)；预警从同一计划 emit 读取。
 * 发数在两列间平均分配，奇数多一发给第一列；请求拒绝不改计划/RNG。
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

/* 列内三条弹线的偏移在接受请求时锁定, 不读取之后改写的 config。 */
static float course_lane_offset(int32_t k, float spread) {
    int32_t slot = k % 3; /* k >= 0, 结果 0/1/2 */
    return ((float)slot - 1.0f) * spread;
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

    if (!isfinite(pc->lane_spread_px) || pc->lane_spread_px < 0.0f ||
        pc->lane_spread_px > colw * 0.5f - config->boss_bullet_radius) {
        return false; /* 弹体必须完全留在各自被封锁列, 不侵入通道列。 */
    }

    /* 通道无弹区可行性: 最紧的一种通道位置(边缘列)也必须留出 corridor_width。
     * 边缘列通道时弹心空档 = 1.5 * colw - lane_spread_px。 */
    if (!(colw * 1.5f - pc->lane_spread_px >= corridor)) {
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
    out->lane_spread_px = pc->lane_spread_px;
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
    if (!isfinite(plan->corridor_width) || !isfinite(plan->wave_offset) ||
        !isfinite(plan->lane_spread_px) || plan->lane_spread_px < 0.0f) {
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
    if (plan->lane_spread_px > colw * 0.5f - config->boss_bullet_radius ||
        colw * 1.5f - plan->lane_spread_px < plan->corridor_width) {
        return false;
    }

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
                float x = cx + course_lane_offset(k, plan->lane_spread_px);
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
