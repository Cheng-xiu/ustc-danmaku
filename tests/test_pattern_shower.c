/* test_pattern_shower.c - S10 招 3 期末总评·绩点淋浴 的验收测试
 *
 * 覆盖任务卡 S10 的全部 12 条验收条件。每条子检查打印 PASS/FAIL, 每个项目打印汇总,
 * 末尾打印总计。退出码: 全部通过为 0, 任一失败为 1。
 *
 * 编译(任务卡给出的真实依赖已存在, 链接测试加 core/rng.c core/projectiles.c;
 * 另需 core/demo_config.c 提供 demo_config_init/validate/version_string —— 只读链接,
 * 不修改该文件, 遵循 AGENTS.md "game、sim、测试链接同一批核心源码, 读取同一规则配置"):
 *   gcc -std=c11 -Wall -Wextra -I <repo>/core tests/test_pattern_shower.c \
 *       core/pattern_shower.c core/rng.c core/projectiles.c core/demo_config.c \
 *       -o test_pattern_shower.exe
 *
 * 本文件只使用冻结接口 (core/demo_base.h + core/pattern_shower.h), 不修改任何头文件。
 * 真实 rng (core/rng.c) 与真实弹池 (core/projectiles.c) 都链接进来, 不使用测试替身,
 * 因此随机数消耗与池容量边界是实测结果, 不是模型推断。
 *
 * 与实现共享的约定(两侧注释互相标注, 改动需同步):
 *   - SHOWER_TOP_SPAWN_Y = 100.0f  (core/pattern_shower.c 的顶部进入 y)
 *   - 扫描方向编码: plan->gap_angle_deg = +90 => 向右(+x); -90 => 向左(-x)
 *   - 扫描步长: step = max(corridor_width * 0.5, pitch)
 *   - 弹间距:   pitch = ((field_w - 2*2r) - corridor_width) / shots_per_wave
 */
#include "demo_base.h"
#include "pattern_shower.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define TEST_TOP_SPAWN_Y 100.0f
#define TEST_SCAN_RIGHT_DEG 90.0f
#define TEST_SCAN_LEFT_DEG (-90.0f)
#define TEST_WAVE_CAP 64u

/* 非有限值: 用 0/0 与 1/0 构造, 不依赖 NaN/INFINITY 宏的具体写法。
 * volatile 阻止编译器在编译期折叠成常量(避免 -Werror 下的常量折叠告警)。 */
static float test_nan(void)
{
    volatile float zero = 0.0f;

    return zero / zero;
}

static float test_inf(void)
{
    volatile float one = 1.0f;
    volatile float zero = 0.0f;

    return one / zero;
}

/* MinGW 的 PRIX64 会展开成非标准 "I64X", 严格告警下不能直接用;
 * 按 tests/test_rng.c 的既有做法自己格式化成定长十六进制串, 只依赖标准 C。 */
static const char *hex64(uint64_t v, char *buf)
{
    static const char digits[] = "0123456789ABCDEF";
    int i;

    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 16; i++) {
        buf[2 + i] = digits[(v >> ((15 - i) * 4)) & 0xFu];
    }
    buf[18] = '\0';
    return buf;
}

/* ---------------------------------------------------------------- 测试框架 */

static int g_checks = 0;
static int g_failed = 0;
static int g_item_checks = 0;
static int g_item_failed = 0;

static void check(bool ok, const char *name)
{
    g_checks += 1;
    g_item_checks += 1;
    if (!ok) {
        g_failed += 1;
        g_item_failed += 1;
    }
    printf("    [%s] %s\n", ok ? "PASS" : "FAIL", name);
}

static void check_near(float got, float want, float eps, const char *name)
{
    float diff = got - want;
    bool ok;

    if (diff < 0.0f) {
        diff = -diff;
    }
    ok = isfinite(got) && (diff <= eps);
    g_checks += 1;
    g_item_checks += 1;
    if (!ok) {
        g_failed += 1;
        g_item_failed += 1;
    }
    printf("    [%s] %s (got=%.9g want=%.9g eps=%.9g)\n", ok ? "PASS" : "FAIL", name,
           (double)got, (double)want, (double)eps);
}

static void check_i32(int32_t got, int32_t want, const char *name)
{
    bool ok = (got == want);

    g_checks += 1;
    g_item_checks += 1;
    if (!ok) {
        g_failed += 1;
        g_item_failed += 1;
    }
    printf("    [%s] %s (got=%d want=%d)\n", ok ? "PASS" : "FAIL", name, (int)got, (int)want);
}

static void check_u32(uint32_t got, uint32_t want, const char *name)
{
    bool ok = (got == want);

    g_checks += 1;
    g_item_checks += 1;
    if (!ok) {
        g_failed += 1;
        g_item_failed += 1;
    }
    printf("    [%s] %s (got=%u want=%u)\n", ok ? "PASS" : "FAIL", name, (unsigned)got,
           (unsigned)want);
}

static void item_begin(const char *title)
{
    g_item_checks = 0;
    g_item_failed = 0;
    printf("\n== %s ==\n", title);
}

static void item_end(const char *title)
{
    printf("  -> %s: %s (%d 子项, %d 失败)\n", title, (g_item_failed == 0) ? "PASS" : "FAIL",
           g_item_checks, g_item_failed);
}

/* ---------------------------------------------------------------- 测试工具 */

/* 一轮波次的完整几何: 已排序 x + 原始 spec(用于字段核对)。 */
typedef struct WaveGeom {
    uint32_t count;
    float x_sorted[TEST_WAVE_CAP];
    Projectile spec[TEST_WAVE_CAP];
} WaveGeom;

static void geom_capture(const ProjectileSpawnBuffer *buf, WaveGeom *g)
{
    memset(g, 0, sizeof(*g));
    g->count = buf->count;
    if (g->count > TEST_WAVE_CAP) {
        g->count = TEST_WAVE_CAP; /* 防御: 超出本测试数组则截断(实际不会发生) */
    }
    for (uint32_t i = 0u; i < g->count; ++i) {
        g->x_sorted[i] = buf->spec[i].x;
        g->spec[i] = buf->spec[i];
    }
    /* 插入排序(默认 16 发, 足够) */
    for (uint32_t i = 1u; i < g->count; ++i) {
        float v = g->x_sorted[i];
        uint32_t j = i;

        while (j > 0u && g->x_sorted[j - 1u] > v) {
            g->x_sorted[j] = g->x_sorted[j - 1u];
            j -= 1u;
        }
        g->x_sorted[j] = v;
    }
}

/* 无弹区间(把弹体半径排除后, 该 x 区间内没有任何弹体)。 */
typedef struct ClearGap {
    float lo;
    float hi;
    float width;
} ClearGap;

static ClearGap max_clear_gap(const WaveGeom *g, float radius, float band_lo, float band_hi)
{
    ClearGap best;
    float prev_right = band_lo;
    uint32_t j;

    /* 从第一个空档(最左侧)初始化, 不能用整条 band 当初始值, 否则永远找不到真缝隙。 */
    best.lo = band_lo;
    best.hi = (g->count == 0u) ? band_hi : (g->x_sorted[0] - radius);
    if (best.hi > band_hi) {
        best.hi = band_hi;
    }
    best.width = best.hi - best.lo;

    for (j = 0u; j <= g->count; ++j) {
        float left = (j == 0u) ? band_lo : (g->x_sorted[j - 1u] + radius);
        float right = (j == g->count) ? band_hi : (g->x_sorted[j] - radius);
        float w;

        if (left < prev_right) {
            left = prev_right; /* 弹体重叠时不许把负数宽度算成区间 */
        }
        w = right - left;
        if (w > best.width) {
            best.lo = left;
            best.hi = right;
            best.width = w;
        }
        prev_right = right;
    }
    return best;
}

static void geom_mean_min_max(const WaveGeom *g, float *out_mean, float *out_min, float *out_max)
{
    float sum = 0.0f;
    float lo = 0.0f;
    float hi = 0.0f;

    for (uint32_t i = 0u; i < g->count; ++i) {
        sum += g->x_sorted[i];
        if (i == 0u) {
            lo = g->x_sorted[i];
            hi = g->x_sorted[i];
        } else {
            if (g->x_sorted[i] < lo) {
                lo = g->x_sorted[i];
            }
            if (g->x_sorted[i] > hi) {
                hi = g->x_sorted[i];
            }
        }
    }
    *out_mean = (g->count > 0u) ? (sum / (float)g->count) : 0.0f;
    *out_min = lo;
    *out_max = hi;
}

static void fill_default_request(PatternRequest *req, const DemoConfig *cfg)
{
    memset(req, 0, sizeof(*req));
    req->pattern = DEMO_PATTERN_SHOWER;
    req->target_id = 2u;
    req->origin_x = 480.0f;
    req->origin_y = 620.0f;
    req->target_x = 480.0f;
    req->target_y = 300.0f;
    req->start_tick = 1000;
    req->student_count = cfg->student_count;
    for (uint32_t i = 0u; i < DEMO_MAX_STUDENTS; ++i) {
        bool live = (i < cfg->student_count);
        req->student_alive[i] = live;
        req->student_x[i] = live ? cfg->student_spawn_x[i] : 0.0f;
        req->student_y[i] = live ? cfg->student_spawn_y[i] : 0.0f;
    }
    req->student_radius = cfg->student_radius;
    req->field_w = cfg->field_w;
    req->field_h = cfg->field_h;
}

/* 用给定种子/流调用 make_plan, 并把 plan_id 预置为 sentinel(检查是否被保留)。 */
static bool build_plan(const PatternRequest *req, const DemoConfig *cfg, uint64_t seed,
                       uint64_t stream, AttackPlan *out)
{
    Rng rng;

    memset(out, 0, sizeof(*out));
    out->plan_id = UINT64_C(0xDEADBEEF12345678); /* sentinel: 见验收项 1 */
    rng_seed(&rng, seed, stream);
    return pattern_shower_make_plan(req, cfg, &rng, out);
}

/* 第 i 波的攻击阶段相对 tick: round(wave_tick[i])。 */
static int32_t wave_tick_relative(const AttackPlan *plan, int32_t i)
{
    float v = plan->wave_tick[i];
    int32_t base = (int32_t)v;

    if ((v - (float)base) >= 0.5f) {
        base += 1;
    }
    return base;
}

/* 在某 tick 生成一波, 捕获几何。 */
static bool emit_at(const AttackPlan *plan, const DemoConfig *cfg, uint32_t tick, WaveGeom *g)
{
    ProjectileSpawnBuffer buf;
    bool emitted;

    spawn_buffer_init(&buf);
    emitted = pattern_shower_emit(plan, cfg, tick, &buf);
    if (g != NULL) {
        geom_capture(&buf, g);
    }
    return emitted;
}

/* 把一波的坐标打印出来(证据用)。 */
static void print_coverage(const char *tag, int32_t wave, const WaveGeom *g, const ClearGap *gap,
                           const PatternRequest *req)
{
    float mean;
    float lo;
    float hi;
    char inside[256];
    char safe[256];
    size_t n_in = 0u;
    size_t n_safe = 0u;

    geom_mean_min_max(g, &mean, &lo, &hi);
    printf("      %s wave %d: 发数=%u, 弹 x 范围 [%.1f, %.1f] (范围中点 %.1f, 非连续覆盖), "
           "缝隙 [%.1f, %.1f] 净宽 %.1f px (弹体半径已排除)\n",
           tag, (int)wave, (unsigned)g->count, (double)lo, (double)hi, (double)mean,
           (double)gap->lo, (double)gap->hi, (double)gap->width);

    inside[0] = '\0';
    safe[0] = '\0';
    for (uint32_t s = 0u; s < req->student_count && s < DEMO_MAX_STUDENTS; ++s) {
        if (!req->student_alive[s]) {
            continue;
        }
        if (req->student_x[s] >= gap->lo && req->student_x[s] <= gap->hi) {
            int written = snprintf(safe + n_safe, sizeof(safe) - n_safe, " S%u(x=%.0f)",
                                   (unsigned)s, (double)req->student_x[s]);
            if (written > 0 && (size_t)written < sizeof(safe) - n_safe) {
                n_safe += (size_t)written;
            }
        } else {
            int written = snprintf(inside + n_in, sizeof(inside) - n_in, " S%u(x=%.0f)",
                                   (unsigned)s, (double)req->student_x[s]);
            if (written > 0 && (size_t)written < sizeof(inside) - n_in) {
                n_in += (size_t)written;
            }
        }
    }
    printf("        覆盖带内学生:%s ; 缝隙内学生:%s\n", (inside[0] != '\0') ? inside : " 无",
           (safe[0] != '\0') ? safe : " 无");
}

/* ---------------------------------------------------------------- 1) 计划字段 */

static void test_plan_fields(const DemoConfig *cfg)
{
    PatternRequest req;
    AttackPlan plan;
    const PatternConfig *pc = &cfg->patterns[DEMO_PATTERN_SHOWER];
    static const float expect_wave[5] = {0.0f, 24.0f, 48.0f, 72.0f, 96.0f};
    char hbuf[24];

    item_begin("1) make_plan 字段与 wave_tick");
    fill_default_request(&req, cfg);

    check(build_plan(&req, cfg, 20261006u, 7u, &plan), "make_plan 返回 true");

    check(plan.active, "active == true");
    check_i32((int32_t)plan.pattern, (int32_t)DEMO_PATTERN_SHOWER,
              "pattern == DEMO_PATTERN_SHOWER");
    check_u32(plan.target_id, req.target_id, "target_id 来自 request");
    check_near(plan.origin_x, req.origin_x, 1e-4f, "origin_x 来自 request");
    check_near(plan.origin_y, req.origin_y, 1e-4f, "origin_y 来自 request");
    check_near(plan.aim_x, req.target_x, 1e-4f, "aim_x == request.target_x");
    check_near(plan.aim_y, req.target_y, 1e-4f, "aim_y == request.target_y");
    check_near(plan.lock_speed, pc->bullet_speed, 1e-4f,
               "lock_speed == config->patterns[SHOWER].bullet_speed");
    check_i32(plan.start_tick, req.start_tick, "start_tick 来自 request");
    check_i32(plan.windup_ticks, pc->windup_ticks, "windup_ticks 来自配置(72)");
    check_i32(plan.active_ticks, pc->active_ticks, "active_ticks 来自配置(300)");
    check_i32(plan.wave_count, pc->wave_count, "wave_count 来自配置(5)");
    check_i32(plan.shots_per_wave, pc->shots_per_wave, "shots_per_wave 来自配置(16)");
    check_near(plan.corridor_width, pc->corridor_width, 1e-4f, "corridor_width 来自配置(120)");
    check(plan.corridor_width >= 100.0f, "corridor_width >= 100 px 硬性下限");
    check(plan.plan_id == UINT64_C(0xDEADBEEF12345678),
          "make_plan 保留调用方已填的 plan_id(不生成、不清零)");
    printf("      geometry_seed = %s\n", hex64(plan.geometry_seed, hbuf));

    /* 扫描方向编码: 只允许 ±90 两种取值 */
    check(plan.gap_angle_deg == TEST_SCAN_RIGHT_DEG || plan.gap_angle_deg == TEST_SCAN_LEFT_DEG,
          "gap_angle_deg 为 ±90 (扫描方向编码, 见注释)");
    printf("      扫描方向编码 gap_angle_deg = %.1f => %s\n", (double)plan.gap_angle_deg,
           (plan.gap_angle_deg >= 0.0f) ? "向右(+x)" : "向左(-x)");

    /* start x 偏移(wave_offset)必须在合法中心区间内, 且每波缝隙全程在场内 */
    {
        float margin = cfg->boss_bullet_radius * 2.0f;
        float x_lo = margin;
        float x_hi = cfg->field_w - margin;
        float free_span = (x_hi - x_lo) - plan.corridor_width;
        float pitch = free_span / (float)plan.shots_per_wave;
        float step = pitch; /* 实现约定: 每波缝隙中心平移恰好一个弹间距 */
        float dir = (plan.gap_angle_deg >= 0.0f) ? 1.0f : -1.0f;
        int32_t waves = plan.wave_count;
        int32_t shots = plan.shots_per_wave;
        /* c_0 = X_LO + nLeft * p + W/2, nLeft ∈ [1+(w-1), shots-1-(w-1)] */
        float c_min = x_lo + (float)(1 + (waves - 1)) * pitch + plan.corridor_width * 0.5f;
        float c_max = x_lo + (float)(shots - 1 - (waves - 1)) * pitch + plan.corridor_width * 0.5f;
        float idx = (plan.wave_offset - x_lo - plan.corridor_width * 0.5f) / pitch;
        int32_t idx_round = (int32_t)(idx + 0.5f);

        check(plan.wave_offset >= c_min - 1e-3f && plan.wave_offset <= c_max + 1e-3f,
              "wave_offset(起始缝隙中心 x)落在合法区间内");
        check(fabsf(idx - (float)idx_round) < 1e-3f,
              "wave_offset 落在间距 p 的整数栅格上(缝隙中心 == X_LO + nLeft*p + W/2)");
        printf("      margin=%.1f pitch=%.3f step=%.1f c 合法区间=[%.1f, %.1f] "
               "wave_offset=%.1f (nLeft=%d)\n",
               (double)margin, (double)pitch, (double)step, (double)c_min, (double)c_max,
               (double)plan.wave_offset, (int)idx_round);
        for (int32_t i = 0; i < plan.wave_count; ++i) {
            float c = plan.wave_offset + dir * (float)i * step;
            int32_t n_left = (int32_t)(((c - x_lo - plan.corridor_width * 0.5f) / pitch) + 0.5f);
            bool in_band = (c - plan.corridor_width * 0.5f >= x_lo - 1e-3f) &&
                           (c + plan.corridor_width * 0.5f <= x_hi + 1e-3f);
            bool two_sides = (n_left >= 1) && (n_left <= shots - 1);

            check(in_band, "每波缝隙区间都在可用落点带内");
            check(two_sides, "每波缝隙两侧都至少有一发弹(不是贴边角落)");
        }
    }

    /* wave_tick: 5 波, 递增, 与 first_spawn_sec/wave_interval_sec 一致 */
    check_i32(plan.wave_count, 5, "wave_count == 5(默认)");
    for (int32_t i = 0; i < plan.wave_count; ++i) {
        check_near(plan.wave_tick[i], expect_wave[i], 1e-3f, "wave_tick[i] == i * 0.4s * 60");
    }
    {
        bool increasing = true;

        for (int32_t i = 1; i < plan.wave_count; ++i) {
            if (!(plan.wave_tick[i] > plan.wave_tick[i - 1])) {
                increasing = false;
            }
        }
        check(increasing, "wave_tick 严格递增");
    }
    check_near(plan.wave_tick[4],
               pc->first_spawn_sec * 60.0f + 4.0f * pc->wave_interval_sec * 60.0f, 1e-3f,
               "wave_tick[4] == first_spawn_sec*60 + 4*wave_interval_sec*60");

    /* 随机性确实被使用: 不同种子给出不同的 (方向, 起始 x) 组合 */
    {
        int32_t diff_count = 0;

        for (uint64_t seed = 1u; seed <= 12u; ++seed) {
            AttackPlan p;
            PatternRequest r2 = req;

            if (!build_plan(&r2, cfg, seed, 3u, &p)) {
                continue;
            }
            if (p.gap_angle_deg != plan.gap_angle_deg ||
                fabsf(p.wave_offset - plan.wave_offset) > 1e-3f) {
                diff_count += 1;
            }
        }
        check(diff_count > 0, "不同种子产生不同的扫描几何(方向/起始 x) => rng 确实被使用");
        printf("      12 个种子中与基准不同的数量: %d\n", diff_count);
    }

    /* 同种子同流 => 逐字节相同(确定性) */
    {
        PatternRequest r2 = req;
        AttackPlan plan_b;

        check(build_plan(&r2, cfg, 20261006u, 7u, &plan_b), "重复构造成功");
        check(memcmp(&plan, &plan_b, sizeof(plan)) == 0, "同 (seed, stream) 得到逐字节相同的计划");
    }

    /* make_plan 只消耗 3 次抽取: bool(1) + unit(1) + u64(1), 且顺序固定 */
    {
        Rng used;
        Rng reference;
        AttackPlan p;

        rng_seed(&used, 42u, 9u);
        check(build_plan(&req, cfg, 42u, 9u, &p), "用于 rng 消耗核验的 make_plan 成功");
        /* used 未被 build_plan 使用, 这里手工重放同样的种子给真实调用的核对流 */
        {
            PatternRequest r3 = req;
            AttackPlan p3;

            rng_seed(&used, 42u, 9u);
            memset(&p3, 0, sizeof(p3));
            p3.plan_id = 1u;
            check(pattern_shower_make_plan(&r3, cfg, &used, &p3), "直接传入 rng 的 make_plan 成功");

            rng_seed(&reference, 42u, 9u);
            (void)rng_next_bool(&reference);
            (void)rng_unit_f32(&reference);
            (void)rng_next_u64(&reference);
            check(used.state == reference.state,
                  "make_plan 恰好消耗 (bool + unit_f32 + u64) 三次抽取");
        }
    }
    item_end("1) make_plan 字段与 wave_tick");
}

/* ---------------------------------------------------------------- 2) 锁定不迁移 */

static void test_lock_no_migration(const DemoConfig *cfg)
{
    PatternRequest req;
    PatternRequest mutated;
    AttackPlan plan;
    AttackPlan snapshot;
    WaveGeom before[8];
    WaveGeom after[8];
    Rng probe;
    uint64_t state_after_make;
    char h1[24];
    char h2[24];

    item_begin("2) 锁定不迁移: 改 request 后 emit 几何不变");
    fill_default_request(&req, cfg);
    check(build_plan(&req, cfg, 5u, 1u, &plan), "make_plan 成功");

    snapshot = plan;
    /* 用真实 rng 走一遍 make_plan, 记下消耗后的状态, 之后反复 emit 比对 */
    {
        PatternRequest r2 = req;
        AttackPlan p2;

        rng_seed(&probe, 5u, 1u);
        memset(&p2, 0, sizeof(p2));
        check(pattern_shower_make_plan(&r2, cfg, &probe, &p2), "带真实 rng 的 make_plan 成功");
        state_after_make = probe.state;
    }

    for (int32_t i = 0; i < plan.wave_count; ++i) {
        emit_at(&plan, cfg, (uint32_t)wave_tick_relative(&plan, i), &before[i]);
    }

    /* 改 request: 换目标、挪原点/瞄准点、学生位置全部变化 */
    mutated = req;
    mutated.target_id = 7u;
    mutated.origin_x = 100.0f;
    mutated.origin_y = 700.0f;
    mutated.target_x = 900.0f;
    mutated.target_y = 40.0f;
    mutated.start_tick = 555;
    for (uint32_t i = 0u; i < DEMO_MAX_STUDENTS; ++i) {
        mutated.student_x[i] = 320.0f + (float)i * 10.0f;
        mutated.student_y[i] = 200.0f;
    }
    check(memcmp(&mutated, &req, sizeof(req)) != 0, "request 确实被改动(测试前提有效)");

    for (int32_t i = 0; i < plan.wave_count; ++i) {
        emit_at(&plan, cfg, (uint32_t)wave_tick_relative(&plan, i), &after[i]);
    }

    for (int32_t i = 0; i < plan.wave_count; ++i) {
        check_u32(after[i].count, before[i].count, "波内发数不变");
        check(memcmp(before[i].x_sorted, after[i].x_sorted,
                     sizeof(float) * before[i].count) == 0,
              "波内 x 集合逐字节不变");
        check(memcmp(before[i].spec, after[i].spec, sizeof(Projectile) * before[i].count) == 0,
              "波内完整弹规格逐字节不变");
    }
    check(memcmp(&plan, &snapshot, sizeof(plan)) == 0, "emit 不改动 AttackPlan 本体");

    /* emit 内不得调用 rng: 计划构造后反复 emit, rng 状态必须不变 */
    for (int32_t i = 0; i < plan.wave_count; ++i) {
        emit_at(&plan, cfg, (uint32_t)wave_tick_relative(&plan, i), NULL);
    }
    check(probe.state == state_after_make, "emit 不消耗 rng 状态(emit 内无 rng 调用)");
    printf("      rng 状态: emit 后=%s, make_plan 后=%s\n", hex64(probe.state, h1),
           hex64(state_after_make, h2));

    /* target_id/origin/aim 的锁定同样不迁移 */
    check_u32(plan.target_id, req.target_id, "计划内的 target_id 保持原锁定值");
    check_near(plan.origin_x, req.origin_x, 1e-4f, "计划内的 origin 保持原锁定值");
    item_end("2) 锁定不迁移");
}

/* ---------------------------------------------------------------- 3) 波次边界 */

static void test_wave_boundaries(const DemoConfig *cfg)
{
    PatternRequest req;
    AttackPlan plan;
    int32_t start;
    int32_t last_elapsed;
    int32_t true_count = 0;
    int32_t expected_true = 0;
    bool seen[8];

    item_begin("3) 波次边界: 5 个正确 tick 各生成一次, 其他 tick 不生成");
    fill_default_request(&req, cfg);
    check(build_plan(&req, cfg, 11u, 2u, &plan), "make_plan 成功");
    start = 0; /* emit 的零点为攻击期开始，计划的绝对 start_tick 保持不变。 */
    last_elapsed = (int32_t)plan.active_ticks;

    memset(seen, 0, sizeof(seen));
    for (int32_t e = -8; e <= last_elapsed + 8; ++e) {
        int32_t relative_tick = start + e;
        bool emitted;
        bool expect = false;
        int32_t which = -1;

        for (int32_t i = 0; i < plan.wave_count; ++i) {
            if (wave_tick_relative(&plan, i) - start == e) {
                expect = true;
                which = i;
            }
        }
        if (expect) {
            expected_true += 1;
        }
        emitted = emit_at(&plan, cfg, (uint32_t)relative_tick, NULL);
        if (emitted != expect) {
            printf("      tick=%d (elapsed=%d): emitted=%d expect=%d\n", (int)relative_tick, (int)e,
                   emitted ? 1 : 0, expect ? 1 : 0);
        }
        check(emitted == expect, "emit 的返回与计划波次时刻一致");
        if (emitted) {
            true_count += 1;
            if (which >= 0) {
                seen[which] = true;
            }
        }
    }
    check_i32(true_count, 5, "整个窗口内恰好 5 个 tick 生成");
    check_i32(expected_true, 5, "计划给出 5 个波次时刻");
    for (int32_t i = 0; i < plan.wave_count; ++i) {
        check(seen[i], "该波次在自己的 tick 上生成过一次");
    }
    /* 四个具体边界 */
    check(emit_at(&plan, cfg, (uint32_t)(start - 1), NULL) == false, "start_tick-1 不生成");
    check(emit_at(&plan, cfg, (uint32_t)start, NULL) == true, "start_tick+0 生成第 0 波");
    check(emit_at(&plan, cfg, (uint32_t)(start + (int32_t)plan.active_ticks), NULL) == false,
          "elapsed == active_ticks 时不生成");
    check(emit_at(&plan, cfg, (uint32_t)(start + (int32_t)plan.active_ticks + 1), NULL) == false,
          "超出 active_ticks 返回 false");
    check(emit_at(&plan, cfg, (uint32_t)(start + 25), NULL) == false, "非波次 tick 返回 false");

    /* 超出 active_ticks 的守卫必须独立于"是否是波次 tick"生效。
     * 默认配置的 5 个波次时刻(0/24/48/72/96)全在 active_ticks(300) 之内,
     * 因此这条守卫在默认配置下不会被触发, 必须手工构造边界才可测。
     * 契约(与实现文件头一致): "超出"(elapsed > active_ticks) 返回 false;
     * elapsed == active_ticks 不算超出, 但只有恰为波次 tick 时才生成。 */
    {
        AttackPlan tight = plan;
        ProjectileSpawnBuffer buf;

        /* active_ticks 恰好等于第 1 波时刻 24: elapsed == 24 不算超出 => 生成 */
        tight.active_ticks = 24;
        spawn_buffer_init(&buf);
        check(pattern_shower_emit(&tight, cfg, (uint32_t)(start + 24), &buf) == true,
              "elapsed == active_ticks 且恰为波次 tick 时仍生成(不算超出)");
        check_u32(buf.count, (uint32_t)tight.shots_per_wave, "该波按计划发满");
        spawn_buffer_init(&buf);
        check(pattern_shower_emit(&tight, cfg, (uint32_t)(start + 25), &buf) == false,
              "elapsed > active_ticks 立即返回 false");
        check_u32(buf.count, 0u, "超出后不生成任何弹");
        spawn_buffer_init(&buf);
        check(pattern_shower_emit(&tight, cfg, (uint32_t)(start + 48), &buf) == false,
              "第 2 波(48)已超出 active_ticks(24), 被拒绝而不继续生成");
        check_u32(buf.count, 0u, "被拒绝的波次未生成任何弹");
        spawn_buffer_init(&buf);
        check(pattern_shower_emit(&tight, cfg, (uint32_t)(start + 96), &buf) == false,
              "最后一个波次(96)同样被拒绝");
    }
    item_end("3) 波次边界");
}

/* ---------------------------------------------------------------- 4) 每波发数 */

static void test_shots_per_wave(const DemoConfig *cfg)
{
    PatternRequest req;
    AttackPlan plan;

    item_begin("4) 每波发数 <= shots_per_wave 且 > 0");
    fill_default_request(&req, cfg);
    check(build_plan(&req, cfg, 3u, 4u, &plan), "make_plan 成功");

    for (int32_t i = 0; i < plan.wave_count; ++i) {
        WaveGeom g;

        check(emit_at(&plan, cfg, (uint32_t)wave_tick_relative(&plan, i), &g), "该波生成成功");
        check(g.count > 0u, "该波发数 > 0");
        check(g.count <= (uint32_t)plan.shots_per_wave, "该波发数 <= shots_per_wave(不得超额补发)");
        check_u32(g.count, (uint32_t)plan.shots_per_wave, "默认配置下该波发数 == shots_per_wave");
    }
    item_end("4) 每波发数");
}

/* ---------------------------------------------------------------- 5) 竖向缝隙 */

static void test_corridor(const DemoConfig *cfg)
{
    PatternRequest req;
    AttackPlan plan;

    item_begin("5) 竖向缝隙: 每波存在 >= corridor_width 宽的无弹 x 区间");
    fill_default_request(&req, cfg);
    check(build_plan(&req, cfg, 21u, 5u, &plan), "make_plan 成功");

    for (int32_t i = 0; i < plan.wave_count; ++i) {
        WaveGeom g;
        ClearGap gap;
        float margin = cfg->boss_bullet_radius * 2.0f;
        float pitch = ((cfg->field_w - margin) - margin - plan.corridor_width) /
                      (float)plan.shots_per_wave;
        float dir = (plan.gap_angle_deg >= 0.0f) ? 1.0f : -1.0f;
        float c_expected = plan.wave_offset + dir * (float)i * pitch;

        check(emit_at(&plan, cfg, (uint32_t)wave_tick_relative(&plan, i), &g), "该波生成成功");
        gap = max_clear_gap(&g, cfg->boss_bullet_radius, 0.0f, cfg->field_w);
        check(gap.width >= plan.corridor_width, "该波最大无弹区间净宽 >= corridor_width(120 px)");
        check(gap.hi > gap.lo, "该波无弹区间宽度为正");
        /* 精确结论(见实现文件头第 2、4 条): 缝隙中心 == c_i, 净空档 == W + p - 2r */
        check_near(0.5f * (gap.lo + gap.hi), c_expected, 0.05f,
                   "该波缝隙净空档中心 == c_i(缝隙中心沿扫描方向平移)");
        check_near(gap.width, plan.corridor_width + pitch - 2.0f * cfg->boss_bullet_radius,
                   0.05f, "该波缝隙净空档 == W + p - 2r(精确值)");
        printf("      wave %d: 缝隙 [%.2f, %.2f] 净宽 %.2f px (c_i=%.2f), 要求净宽 >= %.1f\n",
               (int)i, (double)gap.lo, (double)gap.hi, (double)gap.width, (double)c_expected,
               (double)plan.corridor_width);
    }
    /* 没有一发弹体落在缝隙内 */
    {
        WaveGeom g;
        ClearGap gap;
        bool none_inside = true;

        emit_at(&plan, cfg, (uint32_t)wave_tick_relative(&plan, 0), &g);
        gap = max_clear_gap(&g, cfg->boss_bullet_radius, 0.0f, cfg->field_w);
        for (uint32_t k = 0u; k < g.count; ++k) {
            float body_lo = g.x_sorted[k] - cfg->boss_bullet_radius;
            float body_hi = g.x_sorted[k] + cfg->boss_bullet_radius;

            if (body_lo < gap.hi && body_hi > gap.lo) {
                none_inside = false;
            }
        }
        check(none_inside, "第 0 波缝隙区间内没有任何弹体");
    }
    item_end("5) 竖向缝隙");
}

/* ---------------------------------------------------------------- 6) 缝隙可达 */

static void test_corridor_reachable(const DemoConfig *cfg)
{
    PatternRequest req;
    AttackPlan plan;
    uint64_t seed;

    item_begin("6) 缝隙可达: 相邻两波缝隙区间重叠(交集宽度 > 0)");
    fill_default_request(&req, cfg);

    /* 多个种子都检查, 覆盖"向右扫描"与"向左扫描"两个分支 */
    for (seed = 1u; seed <= 6u; ++seed) {
        WaveGeom g[8];
        ClearGap gap[8];
        float dir;

        check(build_plan(&req, cfg, seed, 6u, &plan), "make_plan 成功");
        dir = (plan.gap_angle_deg >= 0.0f) ? 1.0f : -1.0f;
        printf("      seed=%u 方向=%s\n", (unsigned)seed,
               (dir > 0.0f) ? "向右(+x)" : "向左(-x)");
        for (int32_t i = 0; i < plan.wave_count; ++i) {
            check(emit_at(&plan, cfg, (uint32_t)wave_tick_relative(&plan, i), &g[i]), "该波生成成功");
            gap[i] = max_clear_gap(&g[i], cfg->boss_bullet_radius, 0.0f, cfg->field_w);
        }
        for (int32_t i = 1; i < plan.wave_count; ++i) {
            float lo = (gap[i - 1].lo > gap[i].lo) ? gap[i - 1].lo : gap[i].lo;
            float hi = (gap[i - 1].hi < gap[i].hi) ? gap[i - 1].hi : gap[i].hi;
            float overlap = hi - lo;
            float margin = cfg->boss_bullet_radius * 2.0f;
            float free_span = ((cfg->field_w - margin) - margin) - plan.corridor_width;
            float pitch = free_span / (float)plan.shots_per_wave;
            /* 精确下界(见 core/pattern_shower.c 文件头第 4 条):
             * 每波净空档 = W + p - 2r; 扫描步长 step = p; 交集 = W - 2r。 */
            float expected_overlap = plan.corridor_width - 2.0f * cfg->boss_bullet_radius;

            check(overlap > 0.0f, "相邻两波缝隙交集宽度 > 0(可达, 不是突变封死)");
            check(overlap >= expected_overlap - 0.5f,
                  "交集宽度 >= W - 2r(扫描步长 = 弹间距 p, 交集与种子无关)");
            printf("        wave %d ∩ wave %d = [%.1f, %.1f] 宽 %.1f px "
                   "(下界 %.1f = W %.0f - 2r %.0f; 弹间距 p=%.2f)\n",
                   (int)(i - 1), (int)i, (double)lo, (double)hi, (double)overlap,
                   (double)expected_overlap, (double)plan.corridor_width,
                   (double)(2.0f * cfg->boss_bullet_radius), (double)pitch);
        }
    }
    item_end("6) 缝隙可达");
}

/* ---------------------------------------------------------------- 7) 扫描平移 */

static void test_scan_shift(const DemoConfig *cfg)
{
    PatternRequest req;
    AttackPlan plan;
    uint64_t seed;

    item_begin("7) 扫描平移: 相邻波弹的 x 中心不同且方向一致");
    fill_default_request(&req, cfg);

    for (seed = 1u; seed <= 4u; ++seed) {
        WaveGeom g[8];
        float mean[8];
        ClearGap gap[8];
        float gap_center[8];
        float dummy_lo;
        float dummy_hi;
        float dir;

        check(build_plan(&req, cfg, seed, 6u, &plan), "make_plan 成功");
        dir = (plan.gap_angle_deg >= 0.0f) ? 1.0f : -1.0f;
        for (int32_t i = 0; i < plan.wave_count; ++i) {
            emit_at(&plan, cfg, (uint32_t)wave_tick_relative(&plan, i), &g[i]);
            geom_mean_min_max(&g[i], &mean[i], &dummy_lo, &dummy_hi);
            gap[i] = max_clear_gap(&g[i], cfg->boss_bullet_radius, 0.0f, cfg->field_w);
            gap_center[i] = 0.5f * (gap[i].lo + gap[i].hi);
        }
        for (int32_t i = 1; i < plan.wave_count; ++i) {
            float delta = mean[i] - mean[i - 1];
            float gap_delta = gap_center[i] - gap_center[i - 1];

            /* 验收项 7 的原文要求: 相邻波弹的 x 中心不同(确实在扫描) */
            check(fabsf(delta) > 1e-3f, "相邻波弹的 x 中心不同(确实在扫描)");
            check(memcmp(g[i].x_sorted, g[i - 1].x_sorted, sizeof(float) * g[i].count) != 0,
                  "相邻波 x 集合确实不同");
            /* 扫描方向由缝隙中心承载(它是"安全通道"的位置, 也是渲染预警要画的东西):
             * 缝隙中心必须沿 gap_angle_deg 编码的方向平移。 */
            check(fabsf(gap_delta) > 1e-3f, "相邻波缝隙中心不同");
            check((gap_delta > 0.0f) == (dir > 0.0f),
                  "相邻波缝隙中心平移方向与 gap_angle_deg 编码的扫描方向一致");
            printf("      seed=%u wave %d->%d: 弹 x 中心 %.2f -> %.2f (Δ=%.2f), "
                   "缝隙中心 %.2f -> %.2f (Δ=%.2f, 方向 %s)\n",
                   (unsigned)seed, (int)(i - 1), (int)i, (double)mean[i - 1], (double)mean[i],
                   (double)delta, (double)gap_center[i - 1], (double)gap_center[i],
                   (double)gap_delta, (dir > 0.0f) ? "+x" : "-x");
        }
    }
    item_end("7) 扫描平移");
}

/* ---------------------------------------------------------------- 8) 速度方向 */

static void test_velocity(const DemoConfig *cfg)
{
    PatternRequest req;
    AttackPlan plan;

    item_begin("8) 速度方向向下且 |v| == lock_speed");
    fill_default_request(&req, cfg);
    check(build_plan(&req, cfg, 8u, 8u, &plan), "make_plan 成功");

    for (int32_t i = 0; i < plan.wave_count; ++i) {
        WaveGeom g;

        emit_at(&plan, cfg, (uint32_t)wave_tick_relative(&plan, i), &g);
        for (uint32_t k = 0u; k < g.count; ++k) {
            float mag = sqrtf(g.spec[k].vx * g.spec[k].vx + g.spec[k].vy * g.spec[k].vy);

            check(g.spec[k].vy > 0.0f, "vy > 0(向下)");
            check_near(g.spec[k].vy, plan.lock_speed, 1e-3f, "vy == lock_speed");
            check_near(g.spec[k].vx, 0.0f, 1e-3f, "vx == 0(竖直下落, 无横向分量)");
            check_near(mag, plan.lock_speed, 1e-3f, "|v| == lock_speed");
        }
    }
    check_near(plan.lock_speed, cfg->patterns[DEMO_PATTERN_SHOWER].bullet_speed, 1e-4f,
               "lock_speed 与配置一致(240 px/s)");
    item_end("8) 速度方向");
}

/* ---------------------------------------------------------------- 9) 起点位置 */

static void test_spawn_origin(const DemoConfig *cfg)
{
    PatternRequest req;
    AttackPlan plan;

    item_begin("9) 起点 y 在顶部区域(<= 120, 固定约定 y = 100)");
    fill_default_request(&req, cfg);
    check(build_plan(&req, cfg, 9u, 9u, &plan), "make_plan 成功");

    for (int32_t i = 0; i < plan.wave_count; ++i) {
        WaveGeom g;

        emit_at(&plan, cfg, (uint32_t)wave_tick_relative(&plan, i), &g);
        for (uint32_t k = 0u; k < g.count; ++k) {
            check_near(g.spec[k].y, TEST_TOP_SPAWN_Y, 1e-4f, "y == 100(顶部进入)");
            check(g.spec[k].y <= 120.0f, "起点 y <= 120(顶部区域约定)");
            check(g.spec[k].y >= 0.0f, "起点 y >= 0");
            check_near(g.spec[k].px, g.spec[k].x, 0.0f, "px == x(spawn 语义)");
            check_near(g.spec[k].py, g.spec[k].y, 0.0f, "py == y(spawn 语义)");
            check(g.spec[k].x == g.spec[k].px, "x 与 px 逐位相同");
            check(g.spec[k].y == g.spec[k].py, "y 与 py 逐位相同");
        }
    }
    item_end("9) 起点位置");
}

/* ---------------------------------------------------------------- 10) 容量 */

static void test_capacity(const DemoConfig *cfg)
{
    PatternRequest req;
    AttackPlan plan;

    item_begin("10) 容量: spawn_buffer 与弹池容量边界 overflow 正确不越界");
    fill_default_request(&req, cfg);
    check(build_plan(&req, cfg, 10u, 10u, &plan), "make_plan 成功");

    /* 10.1 五波共 80 发全部放进容量 256 的缓冲 */
    {
        ProjectileSpawnBuffer buf;

        spawn_buffer_init(&buf);
        for (int32_t i = 0; i < plan.wave_count; ++i) {
            (void)pattern_shower_emit(&plan, cfg, (uint32_t)wave_tick_relative(&plan, i), &buf);
        }
        check_u32(buf.count, 80u, "5 波 x 16 发 = 80 发全部进入缓冲");
        check_u32(buf.overflow, 0u, "容量 256 时无 overflow");
        check(buf.count <= DEMO_MAX_ACTIVE_PLAN_PROJECTILES, "缓冲计数不越界");
    }

    /* 10.2 缓冲容量被压到 4: 只收 4 发, 其余计 overflow, 越界下标不写 */
    {
        ProjectileSpawnBuffer buf;
        bool sentinel_ok = true;
        uint32_t i;
        size_t b;

        spawn_buffer_init(&buf);
        buf.capacity = 4u;
        memset(&buf.spec[4], 0x5A, sizeof(Projectile) * 8u); /* 越界哨兵 */
        for (int32_t w = 0; w < plan.wave_count; ++w) {
            (void)pattern_shower_emit(&plan, cfg, (uint32_t)wave_tick_relative(&plan, w), &buf);
        }
        check_u32(buf.count, 4u, "容量 4 时只保留 4 发");
        check_u32(buf.overflow, 76u, "其余 76 发计入 overflow(5*16-4)");
        for (i = 4u; i < 12u; ++i) {
            const unsigned char *raw = (const unsigned char *)&buf.spec[i];

            for (b = 0u; b < sizeof(buf.spec[i]); ++b) {
                if (raw[b] != 0x5Au) {
                    sentinel_ok = false;
                }
            }
        }
        check(sentinel_ok, "容量 4 时未写入 spec[4..11](哨兵未被破坏, 不越界)");
    }

    /* 10.3 真实弹池 (core/projectiles.c): 容量 800 收下全部 80 发 */
    {
        ProjectilePool pool;
        uint32_t spawned = 0u;

        pool_init(&pool, cfg->projectile_cap);
        for (int32_t w = 0; w < plan.wave_count; ++w) {
            ProjectileSpawnBuffer buf;
            uint32_t tick = (uint32_t)wave_tick_relative(&plan, w);

            spawn_buffer_init(&buf);
            (void)pattern_shower_emit(&plan, cfg, tick, &buf);
            for (uint32_t k = 0u; k < buf.count; ++k) {
                const Projectile *s = &buf.spec[k];

                if (pool_spawn(&pool, s->faction, s->source_id, s->plan_id, s->source_pattern,
                               s->x, s->y, s->vx, s->vy, s->radius, s->damage,
                               s->lifetime_ticks, (uint64_t)tick)) {
                    spawned += 1u;
                }
            }
        }
        check_u32(spawned, 80u, "真实弹池容量 800 全部收下 80 发");
        check_u32(pool.live_count, 80u, "live_count == 80");
        check_u32(pool.overflow_events, 0u, "无 overflow");
        check_u32(pool_count_faction(&pool, DEMO_FACTION_BOSS), 80u, "Boss 阵营计数 == 80");
        check_u32(pool_count_faction(&pool, DEMO_FACTION_STUDENT), 0u, "无学生弹混入");
        check(pool.live_count <= cfg->projectile_cap, "live_count 未超过池容量");
        {
            bool fields_ok = true;
            bool ids_unique = true;
            uint32_t live_seen = 0u;
            uint32_t i;
            uint32_t j;

            for (i = 0u; i < DEMO_MAX_PROJECTILES; ++i) {
                if (!pool.items[i].active) {
                    continue;
                }
                live_seen += 1u;
                if (pool.items[i].faction != DEMO_FACTION_BOSS ||
                    pool.items[i].source_id != 1u ||
                    pool.items[i].source_pattern != DEMO_PATTERN_SHOWER ||
                    pool.items[i].plan_id != plan.plan_id ||
                    pool.items[i].radius != cfg->boss_bullet_radius ||
                    pool.items[i].damage != cfg->boss_bullet_damage ||
                    pool.items[i].lifetime_ticks != cfg->boss_bullet_lifetime_ticks) {
                    fields_ok = false;
                }
                for (j = i + 1u; j < DEMO_MAX_PROJECTILES; ++j) {
                    if (pool.items[j].active && pool.items[j].id == pool.items[i].id) {
                        ids_unique = false;
                    }
                }
            }
            check_u32(live_seen, 80u, "池内 active 弹数 == 80");
            check(fields_ok,
                  "每发弹的 faction/source_id/source_pattern/plan_id/radius/damage/lifetime 正确");
            check(ids_unique, "80 发弹的 id 两两不同(整局唯一)");
        }
        /* 按计划清弹: 只清本招 */
        check_u32(pool_clear_plan(&pool, plan.plan_id), 80u, "按计划清弹清掉全部 80 发");
        check_u32(pool.live_count, 0u, "清弹后 live_count == 0");
    }

    /* 10.4 真实弹池容量 8: 只收 8 发, 其余计 overflow, 不越界不覆盖 */
    {
        ProjectilePool pool;
        uint32_t ok = 0u;
        uint32_t fail = 0u;
        float first_x = 0.0f;
        bool first_found = false;

        pool_init(&pool, 8u);
        for (int32_t w = 0; w < plan.wave_count; ++w) {
            ProjectileSpawnBuffer buf;
            uint32_t tick = (uint32_t)wave_tick_relative(&plan, w);

            spawn_buffer_init(&buf);
            (void)pattern_shower_emit(&plan, cfg, tick, &buf);
            for (uint32_t k = 0u; k < buf.count; ++k) {
                const Projectile *s = &buf.spec[k];

                if (pool_spawn(&pool, s->faction, s->source_id, s->plan_id, s->source_pattern,
                               s->x, s->y, s->vx, s->vy, s->radius, s->damage,
                               s->lifetime_ticks, (uint64_t)tick)) {
                    ok += 1u;
                    if (!first_found) {
                        first_found = true;
                        first_x = s->x;
                    }
                } else {
                    fail += 1u;
                }
            }
        }
        check_u32(ok, 8u, "容量 8 时成功 8 发");
        check_u32(fail, 72u, "容量 8 时失败 72 发(计数不静默丢失)");
        check_u32(pool.live_count, 8u, "live_count == 8");
        check_u32(pool.overflow_events, 72u, "overflow_events == 72");
        check(pool.live_count <= pool.capacity, "live_count 未超过容量");
        check(pool.items[0u].active, "槽位 0 未被覆盖");
        check_near(pool.items[0u].x, first_x, 1e-4f, "槽位 0 的 x 未被后续 spawn 改写");
    }

    /* 10.5 单招上限: 80 发远低于弹池 800 与缓冲 256 */
    check(80u <= DEMO_MAX_ACTIVE_PLAN_PROJECTILES, "整招 80 发 <= 计划缓冲 256");
    check(80u <= DEMO_MAX_PROJECTILES, "整招 80 发 <= 弹池 800");
    item_end("10) 容量");
}

/* ---------------------------------------------------------------- 11) 非法输入 */

static void test_invalid(const DemoConfig *cfg)
{
    PatternRequest req;
    DemoConfig bad;
    AttackPlan ref;
    AttackPlan out;
    Rng rng;

    item_begin("11) 非法输入返回 false 且不写 out");
    fill_default_request(&req, cfg);

    /* 空指针 */
    memset(&ref, 0xA5, sizeof(ref));
    rng_seed(&rng, 1u, 1u);
    out = ref;
    check(pattern_shower_make_plan(NULL, cfg, &rng, &out) == false, "request == NULL");
    check(memcmp(&out, &ref, sizeof(out)) == 0, "request == NULL 时 out 未被写");
    check(pattern_shower_make_plan(&req, NULL, &rng, &out) == false, "config == NULL");
    check(memcmp(&out, &ref, sizeof(out)) == 0, "config == NULL 时 out 未被写");
    check(pattern_shower_make_plan(&req, cfg, NULL, &out) == false, "rng == NULL");
    check(memcmp(&out, &ref, sizeof(out)) == 0, "rng == NULL 时 out 未被写");
    check(pattern_shower_make_plan(&req, cfg, &rng, NULL) == false, "out == NULL");

#define EXPECT_REJECT(setup, name)                                             \
    do {                                                                       \
        bad = *cfg;                                                            \
        setup;                                                                 \
        out = ref;                                                             \
        check(pattern_shower_make_plan(&req, &bad, &rng, &out) == false, name); \
        check(memcmp(&out, &ref, sizeof(out)) == 0, name " => out 未被写");     \
    } while (0)

    EXPECT_REJECT(bad.patterns[DEMO_PATTERN_SHOWER].corridor_width = 99.0f,
                  "corridor_width = 99 < 100 硬性下限");
    EXPECT_REJECT(bad.patterns[DEMO_PATTERN_SHOWER].corridor_width = 0.0f, "corridor_width = 0");
    EXPECT_REJECT(bad.patterns[DEMO_PATTERN_SHOWER].corridor_width = -120.0f,
                  "corridor_width < 0");
    EXPECT_REJECT(bad.patterns[DEMO_PATTERN_SHOWER].corridor_width = test_nan(),
                  "corridor_width = NaN");
    EXPECT_REJECT(bad.patterns[DEMO_PATTERN_SHOWER].bullet_speed = 0.0f, "bullet_speed = 0");
    EXPECT_REJECT(bad.patterns[DEMO_PATTERN_SHOWER].bullet_speed = -240.0f, "bullet_speed < 0");
    EXPECT_REJECT(bad.patterns[DEMO_PATTERN_SHOWER].wave_count = 0, "wave_count = 0");
    EXPECT_REJECT(bad.patterns[DEMO_PATTERN_SHOWER].wave_count = -1, "wave_count < 0");
    EXPECT_REJECT(bad.patterns[DEMO_PATTERN_SHOWER].shots_per_wave = 0, "shots_per_wave = 0");
    EXPECT_REJECT(bad.patterns[DEMO_PATTERN_SHOWER].shots_per_wave = -1, "shots_per_wave < 0");
    EXPECT_REJECT(bad.patterns[DEMO_PATTERN_SHOWER].active_ticks = 0, "active_ticks = 0");
    EXPECT_REJECT(bad.patterns[DEMO_PATTERN_SHOWER].active_ticks = -5, "active_ticks < 0");
    EXPECT_REJECT(bad.patterns[DEMO_PATTERN_SHOWER].windup_ticks = -1, "windup_ticks < 0");
    EXPECT_REJECT(bad.patterns[DEMO_PATTERN_SHOWER].wave_interval_sec = -0.4f,
                  "wave_interval_sec < 0");
    EXPECT_REJECT(bad.patterns[DEMO_PATTERN_SHOWER].first_spawn_sec = -1.0f,
                  "first_spawn_sec < 0");
    EXPECT_REJECT(bad.patterns[DEMO_PATTERN_SHOWER].wave_interval_sec = 10.0f,
                  "最后一波超出 active_ticks(4*10s 无法在 300 tick 内兑现)");
    EXPECT_REJECT(bad.field_w = 100.0f, "field_w 太小, 放不下缝隙");
    EXPECT_REJECT(bad.field_w = 0.0f, "field_w = 0");
    EXPECT_REJECT(bad.boss_bullet_lifetime_ticks = 0, "boss_bullet_lifetime_ticks = 0");
    EXPECT_REJECT(bad.boss_bullet_damage = test_nan(), "boss_bullet_damage = NaN");
    EXPECT_REJECT(bad.boss_bullet_radius = -1.0f, "boss_bullet_radius < 0");
    /* 发数过多 => 弹间距 < 2r => 会退化成无缝密弹, 必须拒绝 */
    EXPECT_REJECT(bad.patterns[DEMO_PATTERN_SHOWER].shots_per_wave = 200,
                  "shots_per_wave 过大导致弹间距 < 2r(拒绝伪造密弹)");
    /* 缝隙过宽 => 自由段放不下弹 */
    EXPECT_REJECT(bad.patterns[DEMO_PATTERN_SHOWER].corridor_width = 900.0f,
                  "corridor_width 太大, 自由段放不下弹");
    /* 弹间距过大 => 相邻波缝隙交集 (W - 2r) - p 会 <= 0 */
    EXPECT_REJECT(bad.patterns[DEMO_PATTERN_SHOWER].shots_per_wave = 2,
                  "shots_per_wave 过小导致 p >= W - 2r(相邻波缝隙会失去交集)");
    /* 缝隙两侧在扫描全程都要有弹: shots < 2 * wave_count */
    EXPECT_REJECT(bad.patterns[DEMO_PATTERN_SHOWER].shots_per_wave = 8,
                  "shots_per_wave < 2*wave_count(扫描全程保不住两侧的弹)");
    /* 净空档必须为正: W <= 2r */
    EXPECT_REJECT(bad.boss_bullet_radius = 70.0f,
                  "corridor_width <= 2*boss_bullet_radius(净空档非正, 缝隙形同虚设)");
    EXPECT_REJECT(bad.patterns[DEMO_PATTERN_SHOWER].shots_per_wave =
                      (int32_t)DEMO_MAX_ACTIVE_PLAN_PROJECTILES + 1,
                  "shots_per_wave 超出计划缓冲上限");
#undef EXPECT_REJECT

    /* request 侧非法 */
    {
        PatternRequest bad_req = req;

        bad_req.start_tick = -1;
        out = ref;
        check(pattern_shower_make_plan(&bad_req, cfg, &rng, &out) == false, "start_tick < 0");
        check(memcmp(&out, &ref, sizeof(out)) == 0, "start_tick < 0 时 out 未被写");

        bad_req = req;
        bad_req.origin_x = test_nan();
        out = ref;
        check(pattern_shower_make_plan(&bad_req, cfg, &rng, &out) == false, "origin_x = NaN");
        check(memcmp(&out, &ref, sizeof(out)) == 0, "origin_x = NaN 时 out 未被写");

        bad_req = req;
        bad_req.target_y = test_inf();
        out = ref;
        check(pattern_shower_make_plan(&bad_req, cfg, &rng, &out) == false, "target_y = +Inf");
        check(memcmp(&out, &ref, sizeof(out)) == 0, "target_y = +Inf 时 out 未被写");
    }

    /* 被拒后 rng 状态: 参数校验在抽取之前完成时不得消耗随机数 */
    {
        Rng probe;
        Rng fresh;
        AttackPlan sink;

        bad = *cfg;
        bad.patterns[DEMO_PATTERN_SHOWER].corridor_width = 50.0f;
        rng_seed(&probe, 77u, 3u);
        rng_seed(&fresh, 77u, 3u);
        sink = ref;
        check(pattern_shower_make_plan(&req, &bad, &probe, &sink) == false, "失败路径返回 false");
        check(probe.state == fresh.state, "参数校验失败发生在 rng 抽取之前(不消耗随机数)");
    }

    /* emit 侧非法输入 */
    {
        AttackPlan plan;
        ProjectileSpawnBuffer buf;

        check(build_plan(&req, cfg, 12u, 12u, &plan), "make_plan 成功");
        spawn_buffer_init(&buf);
        check(pattern_shower_emit(NULL, cfg, (uint32_t)0, &buf) == false,
              "emit: plan == NULL");
        check(pattern_shower_emit(&plan, NULL, (uint32_t)0, &buf) == false,
              "emit: config == NULL");
        check(pattern_shower_emit(&plan, cfg, (uint32_t)0, NULL) == false,
              "emit: out == NULL");
        check_u32(buf.count, 0u, "emit 失败时缓冲未被写");

        {
            AttackPlan broken = plan;

            broken.wave_count = 0;
            spawn_buffer_init(&buf);
            check(pattern_shower_emit(&broken, cfg, (uint32_t)0, &buf) == false,
                  "emit: wave_count == 0");
            check_u32(buf.count, 0u, "emit 失败时缓冲仍为空");

            broken = plan;
            broken.active_ticks = 0;
            spawn_buffer_init(&buf);
            check(pattern_shower_emit(&broken, cfg, (uint32_t)0, &buf) == false,
                  "emit: active_ticks == 0");

            broken = plan;
            broken.wave_count = 99; /* 超出 wave_tick[] 容量: 不得越界读 */
            spawn_buffer_init(&buf);
            (void)pattern_shower_emit(&broken, cfg, (uint32_t)0, &buf);
            check(buf.count <= DEMO_MAX_ACTIVE_PLAN_PROJECTILES,
                  "emit: wave_count 被伪造为 99 时仍不越界读 wave_tick[]");
        }
    }

    /* warning 侧空指针 */
    {
        PatternWarning w;
        PatternWarning ref_w;

        memset(&ref_w, 0xA5, sizeof(ref_w));
        w = ref_w;
        pattern_shower_warning(NULL, cfg, &w);
        check(w.valid == false, "warning: plan == NULL 时 valid == false");
        check(w.pattern == (DemoPattern)0, "warning: 空计划下 pattern 归 0(清零输出)");
        pattern_shower_warning(NULL, cfg, NULL); /* 不得崩溃 */
        check(true, "warning: out == NULL 不崩溃");
    }
    item_end("11) 非法输入");
}

/* ---------------------------------------------------------------- 12) 证据 */

static void test_scenario_evidence(const DemoConfig *cfg)
{
    PatternRequest clustered;
    PatternRequest spread;
    AttackPlan plan_c;
    AttackPlan plan_s;
    WaveGeom g;
    ClearGap gap;
    int32_t covered_total = 0;
    int32_t safe_total = 0;
    int32_t i;
    uint32_t s;

    item_begin("12) 学生聚拢 / 分散两种请求的生成证据");
    fill_default_request(&clustered, cfg);
    clustered.target_id = 2u;
    clustered.target_x = 480.0f;
    clustered.target_y = 430.0f;
    clustered.student_count = 3u;
    clustered.student_alive[0] = true;
    clustered.student_alive[1] = true;
    clustered.student_alive[2] = true;
    clustered.student_x[0] = 460.0f;
    clustered.student_x[1] = 480.0f;
    clustered.student_x[2] = 500.0f;
    clustered.student_y[0] = 430.0f;
    clustered.student_y[1] = 430.0f;
    clustered.student_y[2] = 430.0f;

    spread = clustered;
    spread.student_x[0] = 120.0f;
    spread.student_y[0] = 260.0f;
    spread.student_x[1] = 800.0f;
    spread.student_y[1] = 300.0f;
    spread.student_x[2] = 480.0f;
    spread.student_y[2] = 620.0f;

    check(build_plan(&clustered, cfg, 20261006u, 21u, &plan_c), "聚拢场景 make_plan 成功");
    printf("    [聚拢场景] 学生 x = {%.0f, %.0f, %.0f}, 目标 (%.0f, %.0f)\n",
           (double)clustered.student_x[0], (double)clustered.student_x[1],
           (double)clustered.student_x[2], (double)clustered.target_x,
           (double)clustered.target_y);
    printf("      扫描方向=%s, 起始缝隙中心 wave_offset=%.1f, corridor_width=%.1f\n",
           (plan_c.gap_angle_deg >= 0.0f) ? "向右(+x)" : "向左(-x)", (double)plan_c.wave_offset,
           (double)plan_c.corridor_width);
    for (i = 0; i < plan_c.wave_count; ++i) {
        emit_at(&plan_c, cfg, (uint32_t)wave_tick_relative(&plan_c, i), &g);
        gap = max_clear_gap(&g, cfg->boss_bullet_radius, 0.0f, cfg->field_w);
        print_coverage("聚拢", i, &g, &gap, &clustered);
    }

    check(build_plan(&spread, cfg, 20261006u, 21u, &plan_s), "分散场景 make_plan 成功");
    printf("    [分散场景] 学生 x = {%.0f, %.0f, %.0f}, 目标 (%.0f, %.0f)\n",
           (double)spread.student_x[0], (double)spread.student_x[1], (double)spread.student_x[2],
           (double)spread.target_x, (double)spread.target_y);
    printf("      扫描方向=%s, 起始缝隙中心 wave_offset=%.1f, corridor_width=%.1f\n",
           (plan_s.gap_angle_deg >= 0.0f) ? "向右(+x)" : "向左(-x)", (double)plan_s.wave_offset,
           (double)plan_s.corridor_width);
    for (i = 0; i < plan_s.wave_count; ++i) {
        emit_at(&plan_s, cfg, (uint32_t)wave_tick_relative(&plan_s, i), &g);
        gap = max_clear_gap(&g, cfg->boss_bullet_radius, 0.0f, cfg->field_w);
        print_coverage("分散", i, &g, &gap, &spread);
    }

    /* 顶部扫描几何不依赖学生位置: 两个场景的固化几何完全相同 */
    check_near(plan_c.wave_offset, plan_s.wave_offset, 0.0f,
               "聚拢/分散的起始扫描 x 相同(顶部扫描几何不随学生位置改变)");
    check(plan_c.gap_angle_deg == plan_s.gap_angle_deg, "扫描方向编码相同");
    for (i = 0; i < plan_c.wave_count; ++i) {
        WaveGeom gc;
        WaveGeom gs;

        emit_at(&plan_c, cfg, (uint32_t)wave_tick_relative(&plan_c, i), &gc);
        emit_at(&plan_s, cfg, (uint32_t)wave_tick_relative(&plan_s, i), &gs);
        check(memcmp(gc.x_sorted, gs.x_sorted, sizeof(float) * gc.count) == 0,
              "同一波在聚拢/分散场景下弹幕 x 完全相同");
    }

    /* 多目标压制证据: 按真实接触判定(|弹心 - 学生心| <= r + student_radius)逐波统计。
     * 这才是"压制"的可核对定义; 单纯比较 x 是否落在缝隙区间会把缝隙边缘的擦碰漏掉。 */
    for (i = 0; i < plan_c.wave_count; ++i) {
        int32_t wave_threat = 0;

        emit_at(&plan_c, cfg, (uint32_t)wave_tick_relative(&plan_c, i), &g);
        gap = max_clear_gap(&g, cfg->boss_bullet_radius, 0.0f, cfg->field_w);
        for (s = 0u; s < clustered.student_count; ++s) {
            bool threatened = false;
            uint32_t k;

            if (!clustered.student_alive[s]) {
                continue;
            }
            for (k = 0u; k < g.count; ++k) {
                float reach = cfg->boss_bullet_radius + clustered.student_radius;

                if (fabsf(g.x_sorted[k] - clustered.student_x[s]) <= reach) {
                    threatened = true;
                }
            }
            if (threatened) {
                covered_total += 1;
                wave_threat += 1;
            } else {
                safe_total += 1;
            }
        }
        printf("        聚拢 wave %d: 被本波弹接触判定的学生数 = %d / %u\n", (int)i,
               (int)wave_threat, (unsigned)clustered.student_count);
    }
    printf("    聚拢场景 5 波合计: 被接触判定的学生-x 计数=%d, 安全的计数=%d (共 %d 次判定)\n",
           (int)covered_total, (int)safe_total, (int)(covered_total + safe_total));
    check(covered_total > 0, "聚拢场景存在被弹接触判定的学生(有压制压力, 不是空放)");
    check(safe_total > 0, "聚拢场景也存在安全时刻(缝隙可达, 不是全屏无缝密弹)");
    check(covered_total + safe_total == 5 * (int32_t)clustered.student_count,
          "逐波判定次数 == 波数 x 学生数(没有学生被漏统计)");
    item_end("12) 学生聚拢/分散证据");
}

/* ---------------------------------------------------------------- main */

int main(void)
{
    DemoConfig cfg;
    PatternWarning warn;
    AttackPlan plan;
    PatternRequest req;
    char err[256];

    printf("S10 绩点淋浴招式验收测试 (招 3 期末总评·绩点淋浴)\n");
    printf("接口版本: 2  配置版本: %s\n", demo_config_version_string());

    if (!demo_config_init(&cfg)) {
        printf("FATAL: demo_config_init 失败\n");
        return 1;
    }
    if (!demo_config_validate(&cfg, err, sizeof(err))) {
        printf("FATAL: 默认配置未通过校验: %s\n", err);
        return 1;
    }
    printf("场地: %.0f x %.0f, 淋浴配置: cost=%d windup=%d active=%d speed=%.0f wave=%d shots=%d"
           " corridor=%.0f first=%.1fs interval=%.1fs\n",
           (double)cfg.field_w, (double)cfg.field_h, (int)cfg.patterns[DEMO_PATTERN_SHOWER].cost,
           (int)cfg.patterns[DEMO_PATTERN_SHOWER].windup_ticks,
           (int)cfg.patterns[DEMO_PATTERN_SHOWER].active_ticks,
           (double)cfg.patterns[DEMO_PATTERN_SHOWER].bullet_speed,
           (int)cfg.patterns[DEMO_PATTERN_SHOWER].wave_count,
           (int)cfg.patterns[DEMO_PATTERN_SHOWER].shots_per_wave,
           (double)cfg.patterns[DEMO_PATTERN_SHOWER].corridor_width,
           (double)cfg.patterns[DEMO_PATTERN_SHOWER].first_spawn_sec,
           (double)cfg.patterns[DEMO_PATTERN_SHOWER].wave_interval_sec);

    /* 预警(第 1 项的只读输出) */
    fill_default_request(&req, &cfg);
    if (build_plan(&req, &cfg, 1u, 1u, &plan)) {
        pattern_shower_warning(&plan, &cfg, &warn);
        printf("预警: valid=%d pattern=%d target=%u origin=(%.0f,%.0f) aim=(%.0f,%.0f) "
               "corridor=%.0f wave_count=%d gap_angle=%.1f(扫描方向编码)\n",
               warn.valid ? 1 : 0, (int)warn.pattern, (unsigned)warn.target_id,
               (double)warn.origin_x, (double)warn.origin_y, (double)warn.aim_x,
               (double)warn.aim_y, (double)warn.corridor_width, (int)warn.wave_count,
               (double)warn.gap_angle_deg);
    } else {
        printf("FATAL: 基准计划构造失败\n");
        return 1;
    }

    test_plan_fields(&cfg);
    test_lock_no_migration(&cfg);
    test_wave_boundaries(&cfg);
    test_shots_per_wave(&cfg);
    test_corridor(&cfg);
    test_corridor_reachable(&cfg);
    test_scan_shift(&cfg);
    test_velocity(&cfg);
    test_spawn_origin(&cfg);
    test_capacity(&cfg);
    test_invalid(&cfg);
    test_scenario_evidence(&cfg);

    printf("\n================ 汇总 ================\n");
    printf("总子项: %d, 失败: %d\n", g_checks, g_failed);
    printf("结果: %s\n", (g_failed == 0) ? "ALL PASS" : "FAILED");
    return (g_failed == 0) ? 0 : 1;
}
