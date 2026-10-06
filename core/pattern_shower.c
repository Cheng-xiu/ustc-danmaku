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
 *   0. 记号: 顶部落点带 [X_LO, X_HI] = [2r, field_w - 2r] (r = Boss 弹半径);
 *      缝隙目标宽度 W = config->patterns[SHOWER].corridor_width;
 *      弹间距 p = ((X_HI - X_LO) - W) / shots_per_wave (整格间距)。
 *
 *   1. 扫描方向 dir: make_plan 用 rng 抽一次布尔量, 向右(+x) 或 向左(-x)。
 *      编码进 plan->gap_angle_deg (该字段对环弹是缺口中心角, 对淋浴复用为扫描方向标记):
 *          gap_angle_deg = +90.0f  => 扫描方向为 +x (向右)
 *          gap_angle_deg = -90.0f  => 扫描方向为 -x (向左)
 *      注释与报告都记录该编码; 头文件不改。
 *
 *   2. 弹位全在同一条间距为 p 的均匀栅格上, 缝隙是把栅格中连续若干格"挖掉"形成的:
 *          x_k = X_LO + (k + 0.5) * p                  (k <  nLeft, 缝隙左侧)
 *          x_k = X_LO + (k + 0.5) * p + W              (k >= nLeft, 缝隙右侧整体让开 W)
 *          缝隙区间 = [X_LO + nLeft * p,  X_LO + nLeft * p + W]      (宽度恰为 W)
 *      nLeft 是整数格下标, 表示缝隙左侧保留几发。于是对**任意一波**都有精确结论
 *      (不依赖 nLeft、不依赖种子、不依赖浮点运气):
 *        - 缝隙区间内没有任何弹的 x;
 *        - 缝隙左侧最近一发中心到缝左沿 = p/2, 右侧最近一发中心到缝右沿 = p/2;
 *        - 弹心之间空档 = W + p; 排除两侧弹体半径后的净空档 = W + p - 2r >= W
 *          (由 p >= 2r 的校验保证);
 *        - 缝隙的**净空档中心恰好等于缝隙中心 c_i** (见下一条), 所以"缝隙中心沿扫描
 *          方向平移"是精确成立的观测。
 *      最右一发 k = shots-1 位于 X_LO + (shots - 0.5) * p + W = X_HI - 0.5 * p,
 *      最左一发位于 X_LO + 0.5 * p, 因此**全部弹都在落点带内**, 无需夹紧。
 *
 *   3. 起始缝隙(第 0 波)中心 x: 用 rng 抽一次 [0,1) 在合法整数下标区间内取 nLeft_0,
 *      记 c_0 = X_LO + nLeft_0 * p + W/2, 固化进 plan->wave_offset。
 *      合法区间 nLeft_0 ∈ [1 + (waves-1), shots_per_wave - 1 - (waves-1)]:
 *      下界保证整个扫描过程中左侧至少还有一发弹, 上界保证右侧至少还有一发弹 ——
 *      这条缝隙永远是"两列弹之间"的真实竖向通道, 不会退化成贴边的角落。
 *
 *   4. 每波缝隙中心: c_i = c_0 + dir * i * p, 即 step = 恰好一个弹间距 p。
 *      (实现上由 c_i 反推整数 nLeft_i = nLeft_0 + dir * i, 因此始终是整数格。)
 *      取 step = p 的三个理由:
 *        a) 相邻两波**净空档交集恒为 (W + p - 2r) - p = W - 2r**, 默认 120 - 12 = 108 px,
 *           与种子和浮点无关, 严格为正 => "每波平移但不突变封死", 且相邻波缝隙可达;
 *        b) step = p < W + p - 2r => 缝隙不会一步跨过自身净宽;
 *        c) step = p 恰好一格, 每波弹的 x 集合真的平移一位, 扫描在战场上可见。
 *      扫描速率 = p / wave_interval_sec, 默认 51 / 0.4 = 127.5 px/s, 低于弹速 240 px/s,
 *      满足原规范"扫描速率限定"。
 *
 *   5. 每发 vx = 0, vy = lock_speed, 起点 (x, 100): 弹竖直下落 => 该 x 空档在下落全程
 *      都是一条固定竖向缝隙, 不需要重算, 也不会在飞行中封死。
 *      同时保证 |v| == lock_speed (因为 vx 恒为 0)。
 *
 * 不伪造强度: 每波发数 == shots_per_wave (默认 16, 远小于满屏); 弹间距 p >= 2r
 * (默认 51 px >> 12 px, 弹体不重叠); 且恒保留净空档 >= W 的竖向缝隙。
 *
 * ---------------------------------------------------------------- 参数合法性
 *
 * make_plan 在**写 out 之前**完成全部校验, 任一不满足即返回 false 且不写 out
 * (调用方据此回滚能量与状态)。被拒的情形:
 *   - request/config/rng/out 任一为空指针 (rng 为空时无法按规则抽取扫描几何);
 *   - 场宽/弹速/波次/发数/时长/波次间距/弹半径/伤害/寿命非法 (<=0 或非有限);
 *   - corridor_width 非有限或 < 100 px (demo-rules 的硬性下限; 本模块选择"拒绝并让
 *     配置错误可见", 不静默加宽 —— 否则预警与实际几何会与配置不一致);
 *   - corridor_width <= 2 * bullet_radius (净空档会非正, 缝隙形同虚设);
 *   - 自由段放不下弹 (free_span <= 0), 或弹间距 p < 2 * bullet_radius (无缝密弹);
 *   - 弹间距 p >= corridor_width - 2 * bullet_radius (相邻波缝隙交集会 <= 0);
 *   - shots_per_wave < 2 * wave_count (无法保证缝隙两侧在扫描全程都有弹);
 *   - start_tick < 0;
 *   - 最后波次时刻超出 active_ticks (计划无法按"每波各生成一次"兑现);
 *   - wave_tick 非有限或 > 1e9 (会溢出 tick 运算)。
 *
 * plan_id 由调用方(core/attack.c 的 next_plan_id)分配: 本模块既不生成也不清零,
 * 保持入参 out 里已有的 plan_id 原值 (若调用方在 make_plan 之后填写, 同样不受影响)。
 *
 * 缝隙宽度 W 是"弹的 x 坐标"层面的空档: 弹半径 r 的实体在空档内的可通过净宽为
 * W - 2r (默认 120 - 12 = 108 px), 仍 >= 100 px 的硬性下限。世界侧判碰撞用的是
 * 弹心到学生心的距离 <= r + student_radius, 因此渲染/AI 可用缝心中线区间配合
 * 净宽 W - 2r 判断"站在哪里安全"。测试输出把弹心空档与净空档都打印出来。
 */
#include "pattern_shower.h"

#include <math.h>
#include <string.h>

/* 所有顶部弹从 y = 100 附近进入场地 (原规范 3.4)。 */
#define SHOWER_TOP_SPAWN_Y 100.0f

/* docs/demo-rules.md 2.5: 淋浴保留 >= 100 px 竖向缝隙(硬性), 配置默认 120 px。 */
#define SHOWER_MIN_CORRIDOR_WIDTH 100.0f

/* 扫描方向在 plan->gap_angle_deg 中的编码(见文件头第 1 条)。 */
#define SHOWER_SCAN_RIGHT_DEG 90.0f
#define SHOWER_SCAN_LEFT_DEG (-90.0f)

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
 * 非有限或超出可表示范围时返回 INT32_MIN, 表示"该波次时刻不可用"。 */
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

/* 四舍五入到最近 int32, 用于从固化的 wave_offset 反推整数格下标。 */
static int32_t shower_round_i32(float v) {
    int32_t base;

    if (!isfinite(v)) {
        return INT32_MIN;
    }
    base = (int32_t)v;
    if ((v - (float)base) >= 0.5f) {
        base += 1;
    } else if ((v - (float)base) <= -0.5f) {
        base -= 1;
    }
    return base;
}

/* 由计划固化量反推第 wave 波的整数格下标 nLeft_i = nLeft_0 + dir * i。
 * 不消耗随机数; 与 make_plan 的 c_0 = X_LO + nLeft_0 * p + W/2 互为逆运算。 */
static int32_t shower_gap_index(const AttackPlan *plan, const DemoConfig *config, int32_t wave,
                                float pitch) {
    float x_lo = shower_side_margin(config);
    float dir = (plan->gap_angle_deg >= 0.0f) ? 1.0f : -1.0f;
    float c = plan->wave_offset + dir * (float)wave * pitch;

    return shower_round_i32((c - x_lo - plan->corridor_width * 0.5f) / pitch);
}

/* 第 k 发在均匀栅格上的 x; nLeft 为缝隙起始格下标。见文件头第 2 条。 */
static float shower_grid_x(float x_lo, float pitch, float corridor, int32_t nLeft, int32_t k) {
    float x = x_lo + ((float)k + 0.5f) * pitch;

    if (k >= nLeft) {
        x += corridor; /* 右侧整体让开缝隙宽度 W 后继续按同一间距铺开 */
    }
    return x;
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
    float c0;
    float dx;
    float dy;
    float len;
    float v;
    float u;
    float n_span;
    int32_t n_min;
    int32_t n_max;
    int32_t n0;
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

    /* 净空档 W - 2r 必须为正, 否则"缝隙"里站不住任何东西。 */
    if (!(corridor > 2.0f * config->boss_bullet_radius)) {
        return false;
    }
    /* 自由段必须放得下 shots 发弹, 且弹体不重叠。 */
    if (!(x_hi - x_lo > corridor)) {
        return false;
    }
    free_span = (x_hi - x_lo) - corridor;
    pitch = free_span / (float)shots;
    if (!(pitch >= 2.0f * config->boss_bullet_radius)) {
        return false; /* 否则就是无缝密弹, 属于被禁止的伪造强度 */
    }
    /* 相邻波净空档交集 = (W - 2r) - p 必须严格为正(缝隙可达、不突变封死)。 */
    if (!(pitch < corridor - 2.0f * config->boss_bullet_radius)) {
        return false;
    }
    /* 缝隙两侧在扫描全程都要有弹: 需要至少 2*waves 发。 */
    if (shots < 2 * waves) {
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

    /* ---- 2) 抽取并固化随机几何(顺序固定, 便于跨机核验) ----
     * 抽取顺序: (1) 扫描方向 (2) 起始缝隙格下标比例 (3) geometry_seed 存档。
     * 全部校验都在此之前完成, 所以失败路径不消耗任何随机数。 */
    scan_right = rng_next_bool(rng);
    u = rng_unit_f32(rng); /* [0, 1) */
    geometry_seed = rng_next_u64(rng);
    if (!isfinite(u) || u < 0.0f) {
        u = 0.0f;
    }
    if (u > 1.0f) {
        u = 1.0f;
    }

    /* 起始格下标区间(两个方向相同, 因此与抽到的方向无关)。 */
    n_min = 1 + (waves - 1);
    n_max = shots - 1 - (waves - 1);
    n_span = (float)(n_max - n_min + 1);
    n0 = n_min + (int32_t)(u * n_span);
    if (n0 > n_max) {
        n0 = n_max; /* 防御: u == 1 时夹紧, 不越出合法格范围 */
    }
    if (n0 < n_min) {
        n0 = n_min;
    }
    c0 = x_lo + (float)n0 * pitch + corridor * 0.5f;

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

    /* 扫描方向编码(见文件头第 1 条): +90 = 向右(+x), -90 = 向左(-x)。 */
    out->gap_angle_deg = scan_right ? SHOWER_SCAN_RIGHT_DEG : SHOWER_SCAN_LEFT_DEG;
    out->gap_span_deg = 0.0f;           /* 淋浴无缺口跨度概念 */
    out->gap_drift_deg_per_wave = 0.0f; /* 淋浴用 wave_offset + 整数格平移, 不用角度漂移 */
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
    float margin;
    float x_lo;
    float x_hi;
    float pitch;
    int32_t waves;
    int32_t shots;
    int32_t i;
    int32_t k;
    uint32_t pushed = 0u;

    if (plan == NULL || config == NULL || out == NULL) {
        return false;
    }
    if (plan->wave_count <= 0 || plan->wave_count > SHOWER_WAVE_TICK_CAP) {
        return false;
    }
    if (plan->shots_per_wave <= 0 ||
        plan->shots_per_wave > (int32_t)DEMO_MAX_ACTIVE_PLAN_PROJECTILES) {
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
    shots = plan->shots_per_wave;
    margin = shower_side_margin(config);
    x_lo = margin;
    x_hi = config->field_w - margin;
    if (!(x_hi - x_lo > plan->corridor_width)) {
        return false;
    }
    pitch = ((x_hi - x_lo) - plan->corridor_width) / (float)shots;
    if (!isfinite(pitch) || !(pitch > 0.0f)) {
        return false;
    }

    for (i = 0; i < waves; ++i) {
        int32_t due = shower_round_tick(plan->wave_tick[i]);
        int32_t n_left;

        if (due == INT32_MIN) {
            continue; /* 波次时刻非法: 该波不生成, 不放宽成"补发密弹" */
        }
        if (elapsed != (int64_t)due) {
            continue; /* 本 tick 不是第 i 波的生成 tick */
        }

        n_left = shower_gap_index(plan, config, i, pitch);
        if (n_left == INT32_MIN) {
            continue;
        }
        /* 缝隙两侧都必须有弹, 否则这一波不是"两列弹之间的通道", 判为不可兑现。 */
        if (n_left < 1 || n_left > shots - 1) {
            continue;
        }

        for (k = 0; k < shots; ++k) {
            Projectile spec;
            float x = shower_grid_x(x_lo, pitch, plan->corridor_width, n_left, k);

            if (!isfinite(x) || x < x_lo || x > x_hi) {
                continue; /* 几何不可兑现: 少发也不发越界/非法坐标弹 */
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
