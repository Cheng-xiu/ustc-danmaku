/* pattern_shower.c - S10 招 3 期末总评·绩点淋浴: 锁定方向的扫描弹雨
 *
 * 接口版本: 2 (core/demo_base.h + core/pattern_shower.h 已冻结; 本文件不改任何头文件)
 * 配置版本: 1 (demo-config-v1, 见 docs/demo-rules.md 2.5 第 3 行)
 * 用途档位: 高消耗·多目标压制 (消耗 60)
 *
 * 职责与边界:
 *   - 只实现 pattern_shower_{make_plan,emit,warning} 三个冻结函数。
 *   - 不改注册表/配置/其他招式; 不写日志; 不读墙钟; 不使用平台 rand();
 *     不使用 EasyX/Windows API/C++ 容器; 纯 C11。
 *   - emit 内**不调用任何 rng**: 冻结签名没有 Rng 入参, 全部几何在 make_plan
 *     固化进 AttackPlan, emit 只读计划(本文件内无任何 rng_* 调用)。
 *
 * ---------------------------------------------------------------- 已批准几何
 *
 * 依据 docs/demo-rules.md 2.5 与 docs/project-spec-v3.1.md 3.4:
 *   - 预警 72 tick, 攻击 300 tick, 弹速 240 px/s, 5 波 (间隔 0.4 s), 每波 16 发;
 *   - 保留 >= 120 px 竖向缝隙 (硬性下限 100 px);
 *   - 上方弹雨依次向左或右扫描, 逼玩家换向与上下调整, 扫描速率限定;
 *   - 所有顶部弹从 y = 100 附近进入场地。
 *
 * 本模块的确定性构造 (全部写进 AttackPlan, 之后不可变):
 *
 *   1. 顶部落点带: 可用 x 区间 [X_LO, X_HI] = [2r, field_w - 2r] (r = Boss 弹半径)。
 *   2. 扫描方向 dir: make_plan 用 rng 抽一次布尔量, 向右(+x) 或 向左(-x)。
 *      编码进 plan->gap_angle_deg (该字段对环弹是缺口中心角, 对淋浴复用为扫描方向标记):
 *          gap_angle_deg = +90.0f  => 扫描方向为 +x (向右)
 *          gap_angle_deg = -90.0f  => 扫描方向为 -x (向左)
 *      注释与报告都记录该编码; 头文件不改。
 *   3. 起始缝隙中心 x: plan->wave_offset (再用 rng 抽一次 [0,1) 在该方向合法区间内插值)。
 *   4. 每波缝隙中心: c_i = wave_offset + dir * i * step,
 *      step = max(corridor_width * 0.5, pitch) —— 由配置与实际弹间距导出, 不额外消耗随机数。
 *      取这两个下界的理由:
 *        a) step >= corridor_width * 0.5 => 相邻波缝隙交集宽度 = corridor_width - step
 *           >= corridor_width * 0.5 > 0, 满足"每波平移但不突变/相邻波缝隙重叠(可达)";
 *        b) step >= pitch (波内弹间距) => 每推进一波至少有一发弹跨越缝隙, 弹的 x 集合
 *           真的改变, "扫描"在战场上可见, 而不是只挪一个看不见的标记;
 *        c) step < corridor_width (下面的 pitch < corridor_width 校验保证) => 缝隙不会
 *           一步跨过自身宽度, 相邻两波必留交集。
 *      扫描速率 = step / wave_interval_sec, 默认 60 / 0.4 = 150 px/s, 低于弹速 240 px/s,
 *      满足原规范"扫描速率限定"。
 *   5. 每波 16 发在"可用 x 区间去掉缝隙后的两条自由段"上**等间距均匀**铺开:
 *          p = ((X_HI - X_LO) - W) / shots_per_wave
 *          第 k 发沿拼接后的自由长度取 (k + 0.5) * p, 落在缝隙左侧则 x = X_LO + o,
 *          落在右侧则 x = (c_i + W/2) + (o - left_len)。
 *      因此波内任意相邻两发中心距恒为 p (整体均匀), 而缝隙处相邻两发跨越缝隙,
 *      其间距为 W + p —— **缝隙内绝无弹的 x**, 实际空档宽度 >= W。
 *      缝隙两侧必定都有弹 (c 的取值范围保证 left_len >= p/2 且 x_hi - c_hi >= p/2),
 *      所以这条缝隙永远是"两列弹之间"的真实竖向通道, 不是贴边的角落:
 *        - 左右都有弹: 空档 = W + p >= W;
 *        - 全在右侧  : 左侧空档 = W + 0.5p >= W  (仅当 pitch 与边距退化时才可能, 已被
 *          c 的范围排除, 这里只列出以防后人放宽 c 范围)。
 *   6. 每发 vx = 0, vy = lock_speed, 起点 (x, 100): 弹竖直下落 => 该 x 空档在下落全程
 *      都是一条固定竖向缝隙, 不需要重算, 也不会在飞行中封死。
 *      同时保证 |v| == lock_speed (因为 vx 恒为 0)。
 *
 * 不伪造强度: 每波发数 == shots_per_wave (默认 16, 远小于满屏), 弹间距 p 必须
 * >= 2r (默认 51 px >> 12 px, 弹不重叠), 且恒保留 >= corridor_width 的缝隙。
 *
 * ---------------------------------------------------------------- 参数合法性
 *
 * make_plan 在**写 out 之前**完成全部校验, 任一不满足即返回 false 且不写 out
 * (调用方据此回滚能量与状态)。被拒的情形:
 *   - request/config/rng/out 任一为空指针 (rng 为空时无法按规则抽取扫描几何);
 *   - 场宽/弹速/波次/发数/时长/波次间距非法 (<=0、非有限);
 *   - corridor_width 非有限或 < 100 px (demo-rules 的硬性下限; 本模块选择"拒绝并让
 *     配置错误可见", 不静默加宽 —— 否则预警与实际几何会与配置不一致);
 *   - start_tick < 0;
 *   - 缝隙吃掉可用宽度, 或按不等式算出的弹间距 < 2 * 弹半径 (会变成无缝密弹);
 *   - 5 波扫描后缝隙会跑出场内, 即 (wave_count - 1) * step > 合法中心区间长度;
 *   - 最后波次时刻超出 active_ticks (计划无法按"每波各生成一次"兑现);
 *   - wave_tick 非有限或 > 1e9 (会溢出 tick 运算)。
 *
 * plan_id 由调用方(core/attack.c 的 next_plan_id)分配: 本模块既不生成也不清零,
 * 保持入参 out 里已有的 plan_id 原值 (若调用方在 make_plan 之后填写, 同样不受影响)。
 *
 * 缝隙宽度 W 是"弹的 x 坐标"层面的空档: 弹半径 r 的实体在空档内的可通过宽度为
 * W - 2r (默认 120 - 12 = 108 px), 仍 >= 100 px 的硬性下限。世界侧判碰撞用的是
 * 弹心到学生心的距离 <= r + student_radius, 因此建议渲染/AI 用 W 作为缝心中线区间,
 * 用 W - 2r 作为"缝隙内可安全通过"的净宽, 两者都记录在本报告与测试输出里。
 */
#include "pattern_shower.h"

#include <math.h>
#include <string.h>

/* 所有顶部弹从 y = 100 附近进入场地 (原规范 3.4)。 */
#define SHOWER_TOP_SPAWN_Y 100.0f

/* docs/demo-rules.md 2.5: 淋浴保留 >= 100 px 竖向缝隙(硬性), 配置默认 120 px。 */
#define SHOWER_MIN_CORRIDOR_WIDTH 100.0f

/* 扫描方向在 plan->gap_angle_deg 中的编码(见文件头第 2 条)。 */
#define SHOWER_SCAN_RIGHT_DEG 90.0f
#define SHOWER_SCAN_LEFT_DEG (-90.0f)

/* 每波缝隙中心相对上一波平移的比例(见文件头第 4 条)。 */
#define SHOWER_SCAN_STEP_FACTOR 0.5f

/* Boss 弹的 source_id: world 里 Boss 的实体 ID 固定为 1 (core/world.c)。 */
#define SHOWER_BOSS_SOURCE_ID 1u

/* wave_tick[] 的数组长度: AttackPlan 里是 DEMO_PATTERN_COUNT * 8 == 32。 */
#define SHOWER_WAVE_TICK_CAP (DEMO_PATTERN_COUNT * 8)

/* 可用落点带的左右留边 = 2 * 弹半径, 保证圆心(而不是弹的边缘)在场地内。 */
static float shower_side_margin(const DemoConfig *config) {
    float m = config->boss_bullet_radius * 2.0f;
    if (!(m >= 0.0f)) {
        m = 0.0f; /* NaN 或不合理负值一律归 0, 不把非法边距带进几何 */
    }
    return m;
}

/* 四舍五入到最近整数 tick。避免依赖 libm 的 lroundf; 只处理 v >= 0 的已校验输入。
 * 非有限或超出 int32 表示范围时返回 INT32_MIN, 表示"该波次时刻不可用"。 */
static int32_t shower_round_tick(float v) {
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

/* 第 wave 波缝隙中心 x (由计划固化量推导, 不消耗随机数)。 */
static float shower_wave_center(const AttackPlan *plan, int32_t wave) {
    float step = plan->corridor_width * SHOWER_SCAN_STEP_FACTOR;
    float dir = (plan->gap_angle_deg >= 0.0f) ? 1.0f : -1.0f;

    return plan->wave_offset + dir * (float)wave * step;
}

/* 第 wave 波第 k 发子弹的 x。返回 false 表示该计划几何不可兑现(不生成弹)。 */
static bool shower_lane_x(const AttackPlan *plan, const DemoConfig *config, int32_t wave,
                          int32_t k, int32_t shots, float *out_x) {
    float margin = shower_side_margin(config);
    float x_lo = margin;
    float x_hi = config->field_w - margin;
    float half = plan->corridor_width * 0.5f;
    float c = shower_wave_center(plan, wave);
    float c_lo = c - half;
    float c_hi = c + half;
    float left_len;
    float total;
    float pitch;
    float off;

    if (out_x == NULL) {
        return false;
    }
    /* make_plan 已保证缝隙全程在可用落点带内; 这里只容忍浮点尾差(1e-3 px),
     * 超出容忍量的计划一律判为不可兑现, 不悄悄把缝隙挪回场内。 */
    if (c_lo < x_lo - 1.0e-3f || c_hi > x_hi + 1.0e-3f || c_hi < c_lo) {
        return false;
    }
    if (c_lo < x_lo) {
        c_lo = x_lo;
    }
    if (c_hi > x_hi) {
        c_hi = x_hi;
    }
    left_len = c_lo - x_lo;
    total = (x_hi - x_lo) - (c_hi - c_lo);
    if (!(total > 0.0f)) {
        return false;
    }
    pitch = total / (float)shots;
    off = ((float)k + 0.5f) * pitch;
    if (off <= left_len) {
        *out_x = x_lo + off;
        if (*out_x > c_lo) {
            *out_x = c_lo; /* 夹紧到缝隙左沿: 浮点误差不得侵占缝隙 */
        }
    } else {
        *out_x = c_hi + (off - left_len);
        if (*out_x < c_hi) {
            *out_x = c_hi; /* 夹紧到缝隙右沿: 浮点误差不得侵占缝隙 */
        }
    }
    if (*out_x > x_hi) {
        *out_x = x_hi; /* 浮点保底: 不留越界坐标 */
    }
    if (*out_x < x_lo) {
        *out_x = x_lo;
    }
    return true;
}

bool pattern_shower_make_plan(const PatternRequest *request, const DemoConfig *config, Rng *rng,
                              AttackPlan *out) {
    const PatternConfig *pc;
    float margin;
    float x_lo;
    float x_hi;
    float corridor;
    float free_span;
    float pitch;
    float step;
    float c_min;
    float c_max;
    float span;
    float lo;
    float hi;
    float u;
    float c0;
    float dx;
    float dy;
    float len;
    float v;
    bool scan_right;
    int32_t waves;
    int32_t shots;
    int32_t i;
    uint64_t keep_plan_id;
    uint64_t geometry_seed;

    if (request == NULL || config == NULL || rng == NULL || out == NULL) {
        return false;
    }

    pc = &config->patterns[DEMO_PATTERN_SHOWER];

    /* ---- 1) 参数校验: 全部在写 out 之前完成 ---- */
    if (!isfinite(config->field_w) || config->field_w <= 0.0f) {
        return false;
    }
    if (!isfinite(config->field_h) || config->field_h <= 0.0f) {
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
    if (!isfinite(pc->corridor_width) || pc->corridor_width < SHOWER_MIN_CORRIDOR_WIDTH) {
        return false; /* 硬性下限 100 px; 不静默加宽 */
    }
    if (pc->windup_ticks < 0 || pc->active_ticks <= 0) {
        return false;
    }
    if (pc->wave_count <= 0 || pc->wave_count > SHOWER_WAVE_TICK_CAP) {
        return false;
    }
    if (pc->shots_per_wave <= 0 ||
        pc->shots_per_wave > (int32_t)DEMO_MAX_ACTIVE_PLAN_PROJECTILES) {
        return false;
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
    margin = shower_side_margin(config);
    x_lo = margin;
    x_hi = config->field_w - margin;

    /* 缝隙必须能放进可用落点带, 且留出足够空间让弹不重叠。 */
    if (!(x_hi - x_lo > corridor)) {
        return false;
    }
    free_span = (x_hi - x_lo) - corridor;
    pitch = free_span / (float)shots;
    if (!(pitch >= 2.0f * config->boss_bullet_radius)) {
        return false; /* 否则就是无缝密弹, 属于被禁止的伪造强度 */
    }
    if (!(pitch < corridor)) {
        return false; /* 扫描步长上界依赖 pitch < corridor(保证相邻波缝隙必有交集) */
    }

    /* 缝隙在扫描全程都必须在场内, 且缝隙左右两侧各自至少要放得下一发弹
     * (left_len >= pitch/2 且 right_len >= pitch/2), 这样缝隙永远是"两列弹之间"
     * 的真实竖向通道, 不会退化成贴边的角落。 */
    step = corridor * SHOWER_SCAN_STEP_FACTOR;
    if (pitch > step) {
        step = pitch; /* 每推进一波至少有一发弹跨越缝隙, 扫描在战场上可见 */
    }
    if (!(step > 0.0f) || !(step < corridor)) {
        return false;
    }
    c_min = margin + corridor * 0.5f + pitch * 0.5f;
    c_max = config->field_w - margin - corridor * 0.5f - pitch * 0.5f;
    if (!(c_max >= c_min)) {
        return false;
    }
    /* 两个方向的合法起点区间长度相同; 先判可行性, 再抽方向, 保证 rng 消耗与结果无关。 */
    if ((float)(waves - 1) * step > c_max - c_min) {
        return false;
    }

    /* 波次时刻必须能在攻击时长内兑现。 */
    for (i = 0; i < waves; ++i) {
        v = pc->first_spawn_sec * (float)DEMO_TICKS_PER_SECOND +
            (float)i * pc->wave_interval_sec * (float)DEMO_TICKS_PER_SECOND;
        if (!isfinite(v) || v < 0.0f || v > 1.0e9f) {
            return false;
        }
        if ((float)shower_round_tick(v) > (float)pc->active_ticks) {
            return false;
        }
    }

    /* ---- 2) 抽取并固化随机几何(顺序固定, 便于跨机核验) ---- */
    /* 抽取顺序: (1) 扫描方向 (2) 起始缝隙中心比例 (3) geometry_seed 存档。 */
    scan_right = rng_next_bool(rng);
    u = rng_unit_f32(rng); /* [0, 1); rng 契约保证有限 */
    if (!isfinite(u) || u < 0.0f) {
        u = 0.0f;
    }
    if (u > 1.0f) {
        u = 1.0f;
    }
    geometry_seed = rng_next_u64(rng);

    if (scan_right) {
        lo = c_min;
        hi = c_max - (float)(waves - 1) * step;
    } else {
        lo = c_min + (float)(waves - 1) * step;
        hi = c_max;
    }
    span = hi - lo;
    if (!(span >= 0.0f)) {
        return false; /* 理论不可达: 上面已判过可行性, 这里只做防御 */
    }
    c0 = lo + span * u;

    /* ---- 3) 一次写满完整不可变计划 ---- */
    keep_plan_id = out->plan_id; /* plan_id 由 attack.c 分配, 本模块不生成/不清零 */

    out->active = true;
    out->pattern = DEMO_PATTERN_SHOWER;
    out->target_id = request->target_id;
    out->origin_x = request->origin_x;
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

    /* 扫描方向编码(见文件头第 2 条): +90 = 向右(+x), -90 = 向左(-x)。 */
    out->gap_angle_deg = scan_right ? SHOWER_SCAN_RIGHT_DEG : SHOWER_SCAN_LEFT_DEG;
    out->gap_span_deg = 0.0f;               /* 淋浴无缺口跨度概念 */
    out->gap_drift_deg_per_wave = 0.0f;     /* 淋浴无角度漂移, 平移用 wave_offset+step */
    out->corridor_width = corridor;
    out->wave_count = waves;
    out->shots_per_wave = shots;

    for (i = 0; i < SHOWER_WAVE_TICK_CAP; ++i) {
        if (i < waves) {
            out->wave_tick[i] = pc->first_spawn_sec * (float)DEMO_TICKS_PER_SECOND +
                                (float)i * pc->wave_interval_sec * (float)DEMO_TICKS_PER_SECOND;
        } else {
            out->wave_tick[i] = 0.0f; /* 未使用的下标显式清零, 不留未初始化数据 */
        }
    }

    out->wave_offset = c0; /* 第 0 波缝隙中心 x (起始扫描位置) */
    out->geometry_seed = geometry_seed;
    out->lock_checked_at_spawn = false; /* 淋浴不生成在目标身边, 无出生安全距离要求 */
    out->plan_id = keep_plan_id;
    return true;
}

bool pattern_shower_emit(const AttackPlan *plan, const DemoConfig *config, uint32_t attack_tick,
                         ProjectileSpawnBuffer *out) {
    int64_t start;
    int64_t now;
    int64_t elapsed;
    int32_t waves;
    int32_t shots;
    int32_t i;
    int32_t k;
    uint32_t pushed = 0u;

    if (plan == NULL || config == NULL || out == NULL) {
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
    if (!isfinite(plan->corridor_width) || !(plan->corridor_width > 0.0f)) {
        return false;
    }
    if (!isfinite(plan->wave_offset) || !isfinite(plan->gap_angle_deg)) {
        return false;
    }
    if (!isfinite(config->field_w) || config->field_w <= 0.0f) {
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

    /* 超出攻击时长或早于计划起点: 一律不生成。 */
    start = (int64_t)plan->start_tick;
    now = (int64_t)attack_tick;
    elapsed = now - start;
    if (elapsed < 0) {
        return false;
    }
    if (elapsed > (int64_t)plan->active_ticks) {
        return false;
    }

    waves = plan->wave_count;
    if (waves > SHOWER_WAVE_TICK_CAP) {
        waves = SHOWER_WAVE_TICK_CAP; /* 数组边界防御, 不越界读 wave_tick[] */
    }
    shots = plan->shots_per_wave;

    for (i = 0; i < waves; ++i) {
        int32_t due = shower_round_tick(plan->wave_tick[i]);

        if (due == INT32_MIN) {
            continue; /* 波次时刻非法: 该波不生成, 不放宽成"补发密弹" */
        }
        if (elapsed != (int64_t)due) {
            continue; /* 本 tick 不是第 i 波的生成 tick */
        }

        for (k = 0; k < shots; ++k) {
            Projectile spec;
            float x = 0.0f;

            if (!shower_lane_x(plan, config, i, k, shots, &x)) {
                continue; /* 几何不可兑现: 少发也不发越界/非法坐标弹 */
            }
            if (!isfinite(x)) {
                continue;
            }

            memset(&spec, 0, sizeof(spec)); /* id/generation/active 由 pool_spawn 赋值 */
            spec.faction = DEMO_FACTION_BOSS;
            spec.source_id = SHOWER_BOSS_SOURCE_ID;
            spec.plan_id = plan->plan_id;
            spec.source_pattern = DEMO_PATTERN_SHOWER;
            /* 起点: 顶部区域, 与"所有顶部弹从 y = 100 附近进入场地"一致 */
            spec.px = x;
            spec.py = SHOWER_TOP_SPAWN_Y;
            spec.x = x;
            spec.y = SHOWER_TOP_SPAWN_Y;
            /* 竖直下落: vx 恒为 0 => |v| == lock_speed 且缝隙在下落全程保持不变 */
            spec.vx = 0.0f;
            spec.vy = plan->lock_speed;
            spec.radius = config->boss_bullet_radius;
            spec.damage = config->boss_bullet_damage;
            spec.lifetime_ticks = config->boss_bullet_lifetime_ticks;

            if (spawn_buffer_push(out, &spec)) {
                pushed += 1u; /* 容量不足时 push 自己记 overflow, 不越界 */
            }
        }
    }

    return pushed > 0u;
}

void pattern_shower_warning(const AttackPlan *plan, const DemoConfig *config,
                            PatternWarning *out) {
    (void)config; /* 预警只读计划; 签名保留 config 以匹配注册表 */

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (plan == NULL) {
        out->valid = false;
        return;
    }

    out->valid = true;
    out->pattern = DEMO_PATTERN_SHOWER;
    out->target_id = plan->target_id;
    out->origin_x = plan->origin_x;
    out->origin_y = plan->origin_y;
    out->aim_x = plan->aim_x;
    out->aim_y = plan->aim_y;
    out->radius_hint = 0.0f; /* 淋浴无环形/扇面半径 */
    /* 扫描方向编码沿用计划字段: +90 = 向右(+x), -90 = 向左(-x)。 */
    out->gap_angle_deg = plan->gap_angle_deg;
    out->gap_span_deg = 0.0f;
    out->corridor_width = plan->corridor_width;
    out->wave_count = plan->wave_count;
}
