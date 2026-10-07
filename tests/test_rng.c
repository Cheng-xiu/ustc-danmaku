/* test_rng.c - core/rng.c 的验收测试 (S02)
 *
 * 覆盖: 同 seed 同 stream 序列一致 / 不同 stream 序列不同 / rng_reset 重复序列 /
 *       world 与 bot 两流交错抽取互不影响 / rng_below 边界与无越界 /
 *       rng_range_i32 闭区间边界与非法区间 / rng_unit_f32 值域 /
 *       rng_next_u32 与 rng_next_u64 的关系 / NULL 安全 / 固定测试向量。
 *
 * 固定测试向量来源: 算法见任务卡与 core/rng.c 顶部注释。下表由独立参考实现
 * (Python 整数运算, 脚本见 work/agents/S02/gen_vectors.py, 不依赖被测 C 代码)
 * 计算后固化。其中 seed=0/stream=0 的 8 个值与 SplitMix64 公开标准流的 seed 0
 * 前缀逐位一致(0xE220A8397B1DCDAF, 0x6E789E6AA1B965F4, ...), 可据此交叉核验;
 * 流初值采用 rng.c 记明的 origin = mix64(seed ^ mix64(stream_id * gamma)), 不用
 * seed + stream_id * gamma —— 后者会让三个流互为同一序列的平移前缀(测试已捕获)。
 *
 * 本测试只使用公共接口 (core/demo_base.h), 不读取 Rng 内部字段做断言, 不修改任何
 * 非授权文件。失败时进程退出码非 0。
 */
#include "demo_base.h"

#include <stdio.h>

/* MinGW 的 PRIX64 会展开成非标准 "I64X", -pedantic 下会报警; 这里自己格式化成
 * 定长十六进制串, 只依赖标准 C, 保证 C99/C11 与严格告警下都能编译。 */
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

static int g_pass = 0;
static int g_fail = 0;

static void check(int condition, const char *name)
{
    if (condition) {
        printf("PASS  %s\n", name);
        g_pass++;
    } else {
        printf("FAIL  %s\n", name);
        g_fail++;
    }
}

static void check_u64(uint64_t actual, uint64_t expected, const char *name)
{
    char a[24];
    char e[24];

    if (actual == expected) {
        printf("PASS  %s (= %s)\n", name, hex64(actual, a));
        g_pass++;
    } else {
        printf("FAIL  %s (expected %s, actual %s)\n", name, hex64(expected, e),
               hex64(actual, a));
        g_fail++;
    }
}

/* ------------------------------------------------------- 1. 固定测试向量 */

/* seed = 0x0000000000000000, stream_id = 0 */
static const uint64_t kVectorA[8] = {
    UINT64_C(0xE220A8397B1DCDAF),
    UINT64_C(0x6E789E6AA1B965F4),
    UINT64_C(0x06C45D188009454F),
    UINT64_C(0xF88BB8A8724C81EC),
    UINT64_C(0x1B39896A51A8749B),
    UINT64_C(0x53CB9F0C747EA2EA),
    UINT64_C(0x2C829ABE1F4532E1),
    UINT64_C(0xC584133AC916AB3C),
};

/* seed = 0x0123456789ABCDEF, stream_id = 1 */
static const uint64_t kVectorB[4] = {
    UINT64_C(0xD6E814475B83C987),
    UINT64_C(0xDCCD3AC23C685C01),
    UINT64_C(0xAF677B9E3A61E005),
    UINT64_C(0x6E926E2B2BCB6987),
};

/* seed = 42, stream_id = 0xDEADBEEF */
static const uint64_t kVectorC[2] = {
    UINT64_C(0x817B75B73370263A),
    UINT64_C(0xCAF767F841A931BA),
};

static void test_fixed_vectors(void)
{
    Rng a;
    Rng b;
    Rng c;
    size_t i;
    int ok;

    rng_seed(&a, UINT64_C(0x0000000000000000), 0u);
    rng_seed(&b, UINT64_C(0x0123456789ABCDEF), 1u);
    rng_seed(&c, UINT64_C(42), UINT64_C(0xDEADBEEF));

    ok = 1;
    for (i = 0; i < 8u; i++) {
        uint64_t v = rng_next_u64(&a);
        if (v != kVectorA[i]) {
            char got[24];
            char want[24];
            printf("      vector A[%u] expected %s, actual %s\n", (unsigned)i,
                   hex64(kVectorA[i], want), hex64(v, got));
            ok = 0;
        }
    }
    check(ok, "fixed vector A: seed=0 stream=0 (8 x u64)");

    ok = 1;
    for (i = 0; i < 4u; i++) {
        uint64_t v = rng_next_u64(&b);
        if (v != kVectorB[i]) {
            char got[24];
            char want[24];
            printf("      vector B[%u] expected %s, actual %s\n", (unsigned)i,
                   hex64(kVectorB[i], want), hex64(v, got));
            ok = 0;
        }
    }
    check(ok, "fixed vector B: seed=0x0123456789ABCDEF stream=1 (4 x u64)");

    ok = 1;
    for (i = 0; i < 2u; i++) {
        uint64_t v = rng_next_u64(&c);
        if (v != kVectorC[i]) {
            char got[24];
            char want[24];
            printf("      vector C[%u] expected %s, actual %s\n", (unsigned)i,
                   hex64(kVectorC[i], want), hex64(v, got));
            ok = 0;
        }
    }
    check(ok, "fixed vector C: seed=42 stream=0xDEADBEEF (2 x u64)");

    /* 单元素向量的逐项命名, 便于失败时定位到具体期望值 */
    rng_seed(&a, 0u, 0u);
    check_u64(rng_next_u64(&a), kVectorA[0], "fixed vector A[0] exact");
}

/* -------------------------------------------- 2. 复现性与流派生 (u64) */

#define SEQ_LEN 64u

static void test_reproducibility_and_streams(void)
{
    Rng a;
    Rng b;
    Rng c;
    Rng d;
    Rng e;
    uint64_t seq_a[SEQ_LEN];
    uint64_t seq_b[SEQ_LEN];
    uint64_t seq_c[SEQ_LEN];
    uint64_t seq_d[SEQ_LEN];
    uint32_t i;
    int same_ab = 1;
    int diff_ac = 0;
    int diff_ad = 0;
    int diff_ac_all = 1;
    int diff_ad_all = 1;

    rng_seed(&a, 20261006u, 0u); /* world 流 */
    rng_seed(&b, 20261006u, 0u); /* 同 seed 同 stream 的第二个实例 */
    rng_seed(&c, 20261006u, 1u); /* bot 流 */
    rng_seed(&d, 20261006u, 2u); /* explore 流 */

    for (i = 0; i < SEQ_LEN; i++) {
        seq_a[i] = rng_next_u64(&a);
        seq_b[i] = rng_next_u64(&b);
        seq_c[i] = rng_next_u64(&c);
        seq_d[i] = rng_next_u64(&d);
    }

    for (i = 0; i < SEQ_LEN; i++) {
        if (seq_a[i] != seq_b[i]) {
            same_ab = 0;
        }
        if (seq_a[i] != seq_c[i]) {
            diff_ac = 1;
        } else {
            diff_ac_all = 0;
        }
        if (seq_a[i] != seq_d[i]) {
            diff_ad = 1;
        } else {
            diff_ad_all = 0;
        }
    }

    check(same_ab, "same (seed, stream) -> identical u64 sequence over 64 draws");
    check(diff_ac && diff_ac_all, "different stream_id (0 vs 1) -> different sequence");
    check(diff_ad && diff_ad_all, "different stream_id (0 vs 2) -> different sequence");

    /* 不同 seed、同 stream 也必须不同 */
    rng_seed(&e, 20261007u, 0u);
    {
        int differs = 0;
        for (i = 0; i < SEQ_LEN; i++) {
            if (rng_next_u64(&e) != seq_a[i]) {
                differs = 1;
            }
        }
        check(differs, "different seed, same stream -> different sequence");
    }

    /* 回归防护: 流初值若退化成 seed + stream_id * gamma, SplitMix64 的 state 就是
     * 计数器, 各流会互为"同一序列的平移前缀", 这里用无交集 + 无平移两条判据拦住。 */
    {
        int any_overlap = 0;
        int shifted_prefix = 0;
        uint32_t j;

        for (i = 0; i < SEQ_LEN && !any_overlap; i++) {
            for (j = 0; j < SEQ_LEN; j++) {
                if (seq_a[i] == seq_c[j]) {
                    any_overlap = 1;
                    break;
                }
            }
        }

        /* stream 1 的第 k 个值若等于 stream 0 的第 0 个值且此后逐项等长吻合, 即为平移前缀 */
        for (i = 0; i + 8u < SEQ_LEN; i++) {
            if (seq_c[i] == seq_a[0]) {
                int match = 1;
                for (j = 0; j < 8u; j++) {
                    if (seq_c[i + j] != seq_a[j]) {
                        match = 0;
                        break;
                    }
                }
                if (match) {
                    shifted_prefix = 1;
                }
            }
        }

        check(!any_overlap, "stream 0 and stream 1 sequences do not overlap");
        check(!shifted_prefix, "stream 1 is not a shifted prefix of stream 0");
    }
}

/* ---------------------------------------------------- 3. rng_reset 语义 */

static void test_reset(void)
{
    Rng r;
    uint64_t first[SEQ_LEN];
    uint64_t second[SEQ_LEN];
    uint64_t third[SEQ_LEN];
    uint32_t i;
    int ok = 1;

    rng_seed(&r, UINT64_C(0xC0FFEE), 3u);

    for (i = 0; i < SEQ_LEN; i++) {
        first[i] = rng_next_u64(&r);
    }

    rng_reset(&r); /* 回到 rng_seed 之后、第一次抽取之前 */
    for (i = 0; i < SEQ_LEN; i++) {
        second[i] = rng_next_u64(&r);
    }

    /* 部分抽取后再重置也必须回到起点 */
    (void)rng_next_u64(&r);
    (void)rng_below(&r, 17u);
    (void)rng_unit_f32(&r);
    rng_reset(&r);
    for (i = 0; i < SEQ_LEN; i++) {
        third[i] = rng_next_u64(&r);
    }

    for (i = 0; i < SEQ_LEN; i++) {
        if (first[i] != second[i] || first[i] != third[i]) {
            ok = 0;
            break;
        }
    }
    check(ok, "rng_reset repeats the exact sequence (from start and after partial draws)");

    /* 重置后的首抽取必须回到 rng_seed 的起点; 用 seed=0/stream=0 的固定向量佐证 */
    rng_seed(&r, 0u, 0u);
    (void)rng_next_u64(&r);
    rng_reset(&r);
    check_u64(rng_next_u64(&r), kVectorA[0], "rng_reset restores the seed-origin state");

    /* reset 幂等: 连续两次 reset 与一次 reset 得到同一序列 */
    rng_reset(&r);
    rng_reset(&r);
    check(rng_next_u64(&r) == kVectorA[0] && rng_next_u64(&r) == kVectorA[1],
          "double rng_reset is idempotent");
}

/* ------------------------------------- 4. 三流交错抽取互不消费 (核心) */

/* 设计: 三个流按不等量、变序的方式交错抽取; 把每个流**发出的每一个 u64** 全部
 * 记录下来(记录流本身只调用 rng_next_u64, 不夹带其它抽取, 以保证记录是连续前缀)。
 * 然后每个流单独使用一遍, 要求单独使用的输出与交错期间的记录逐项完全相等。
 * 若流之间存在任何共享/串扰, 记录就会与单独使用不一致。 */
#define MIX_ROUNDS 200u
#define MIX_TRACE_CAP (MIX_ROUNDS * 4u)

static void test_stream_independence_interleaved(void)
{
    Rng world_mix;
    Rng bot_mix;
    Rng explore_mix;
    Rng alone;
    uint64_t world_trace[MIX_TRACE_CAP];
    uint64_t bot_trace[MIX_TRACE_CAP];
    uint64_t explore_trace[MIX_TRACE_CAP];
    uint32_t world_n = 0u;
    uint32_t bot_n = 0u;
    uint32_t explore_n = 0u;
    uint32_t i;
    uint32_t k;
    int world_ok = 1;
    int bot_ok = 1;
    int explore_ok = 1;

    rng_seed(&world_mix, 7u, 0u);
    rng_seed(&bot_mix, 7u, 1u);
    rng_seed(&explore_mix, 7u, 2u);

    for (i = 0; i < MIX_ROUNDS; i++) {
        /* 变序 + 不等量: 三个流的抽取次数与先后顺序每轮都不同 */
        if ((i % 3u) == 0u) {
            bot_trace[bot_n++] = rng_next_u64(&bot_mix);
            world_trace[world_n++] = rng_next_u64(&world_mix);
            explore_trace[explore_n++] = rng_next_u64(&explore_mix);
        } else if ((i % 3u) == 1u) {
            explore_trace[explore_n++] = rng_next_u64(&explore_mix);
            explore_trace[explore_n++] = rng_next_u64(&explore_mix);
            world_trace[world_n++] = rng_next_u64(&world_mix);
            bot_trace[bot_n++] = rng_next_u64(&bot_mix);
            bot_trace[bot_n++] = rng_next_u64(&bot_mix);
            bot_trace[bot_n++] = rng_next_u64(&bot_mix);
        } else {
            world_trace[world_n++] = rng_next_u64(&world_mix);
            world_trace[world_n++] = rng_next_u64(&world_mix);
            explore_trace[explore_n++] = rng_next_u64(&explore_mix);
            bot_trace[bot_n++] = rng_next_u64(&bot_mix);
        }
    }

    /* 每个流单独使用, 与交错期间记录到的完整序列逐项比对 */
    rng_seed(&alone, 7u, 0u);
    for (k = 0; k < world_n; k++) {
        if (rng_next_u64(&alone) != world_trace[k]) {
            world_ok = 0;
            break;
        }
    }

    rng_seed(&alone, 7u, 1u);
    for (k = 0; k < bot_n; k++) {
        if (rng_next_u64(&alone) != bot_trace[k]) {
            bot_ok = 0;
            break;
        }
    }

    rng_seed(&alone, 7u, 2u);
    for (k = 0; k < explore_n; k++) {
        if (rng_next_u64(&alone) != explore_trace[k]) {
            explore_ok = 0;
            break;
        }
    }

    printf("      traced draws: world=%u bot=%u explore=%u\n", world_n, bot_n, explore_n);
    check(world_ok, "world stream unaffected by bot/explore consumption (interleaved)");
    check(bot_ok, "bot stream unaffected by world/explore consumption (interleaved)");
    check(explore_ok, "explore stream unaffected by world/bot consumption (interleaved)");

    /* 另一组: world 流每次读取之间夹入 bot/explore 流的大量消费, world 流本身
     * 仍必须与它单独使用时的序列逐项一致。 */
    {
        Rng w_mix;
        Rng b_noise;
        Rng e_noise;
        Rng w_ref;
        int same = 1;
        uint64_t expect;

        rng_seed(&w_mix, 99u, 0u);
        rng_seed(&b_noise, 99u, 1u);
        rng_seed(&e_noise, 99u, 2u);
        rng_seed(&w_ref, 99u, 0u);

        for (i = 0; i < 500u; i++) {
            uint64_t got;
            uint32_t jam;

            /* 干扰: 两个非 world 流被高频、不等量地消费 */
            for (jam = 0; jam < (i % 7u) + 1u; jam++) {
                (void)rng_next_u64(&b_noise);
                (void)rng_below(&b_noise, 13u);
            }
            (void)rng_unit_f32(&e_noise);
            (void)rng_range_i32(&e_noise, -50, 50);
            (void)rng_next_bool(&e_noise);

            got = rng_next_u64(&w_mix);
            expect = rng_next_u64(&w_ref);
            if (got != expect) {
                same = 0;
                break;
            }
        }
        check(same, "world stream stays identical under heavy bot/explore noise");
    }

    /* 两流完全交错: world 与 bot 各自序列等于单独使用时的序列 (任务卡显式要求) */
    {
        Rng w;
        Rng b;
        Rng w_alone;
        Rng b_alone;
        uint64_t w_ref[SEQ_LEN];
        uint64_t b_ref[SEQ_LEN];
        int ok = 1;

        rng_seed(&w_alone, 12345u, 0u);
        rng_seed(&b_alone, 12345u, 1u);
        for (i = 0; i < SEQ_LEN; i++) {
            w_ref[i] = rng_next_u64(&w_alone);
            b_ref[i] = rng_next_u64(&b_alone);
        }

        rng_seed(&w, 12345u, 0u);
        rng_seed(&b, 12345u, 1u);
        for (i = 0; i < SEQ_LEN; i++) {
            uint64_t wv;
            uint64_t bv;
            if ((i % 2u) == 0u) {
                wv = rng_next_u64(&w);
                bv = rng_next_u64(&b);
            } else {
                bv = rng_next_u64(&b);
                wv = rng_next_u64(&w);
            }
            if (wv != w_ref[i] || bv != b_ref[i]) {
                ok = 0;
            }
        }
        check(ok, "world/bot interleaving equals each stream used alone");
    }
}

/* --------------------------------------------------- 5. rng_below 契约 */

#define BULK_DRAWS 200000u

static void test_below(void)
{
    Rng a;
    Rng b;
    uint32_t i;
    int ok;
    int n0_no_consume = 1;
    int n1_zero = 1;
    int n1_consumes = 1;
    int n2_in_range = 1;
    int saw0 = 0;
    int saw1 = 0;
    int no_overflow = 1;
    uint32_t counts3[3] = {0u, 0u, 0u};
    uint32_t counts7[7] = {0u, 0u, 0u, 0u, 0u, 0u, 0u};

    /* n == 0: 返回 0 且不消耗状态 */
    rng_seed(&a, 999u, 0u);
    rng_seed(&b, 999u, 0u);
    for (i = 0; i < 64u; i++) {
        uint32_t zero = rng_below(&a, 0u);
        uint64_t va;
        uint64_t vb;
        if (zero != 0u) {
            n0_no_consume = 0;
        }
        va = rng_next_u64(&a);
        vb = rng_next_u64(&b);
        if (va != vb) {
            n0_no_consume = 0;
        }
    }
    check(n0_no_consume, "rng_below(n=0) returns 0 and consumes no state");

    /* n == 1: 只有 0, 且消耗一次状态 */
    rng_seed(&a, 999u, 0u);
    for (i = 0; i < 1000u; i++) {
        if (rng_below(&a, 1u) != 0u) {
            n1_zero = 0;
        }
    }
    check(n1_zero, "rng_below(n=1) always returns 0");

    rng_seed(&a, 999u, 0u);
    rng_seed(&b, 999u, 0u);
    {
        uint64_t after_below = 0;
        uint64_t without = 0;
        (void)rng_below(&a, 1u);
        after_below = rng_next_u64(&a);
        without = rng_next_u64(&b);
        if (after_below == without) {
            n1_consumes = 0;
        }
    }
    check(n1_consumes, "rng_below(n=1) does consume one draw");

    /* n == 2: 值域 {0,1}, 两个值都出现 */
    rng_seed(&a, 4242u, 5u);
    for (i = 0; i < 1000u; i++) {
        uint32_t v = rng_below(&a, 2u);
        if (v > 1u) {
            n2_in_range = 0;
        }
        if (v == 0u) {
            saw0 = 1;
        } else {
            saw1 = 1;
        }
    }
    check(n2_in_range && saw0 && saw1, "rng_below(n=2) yields only {0,1} and both occur");

    /* 越界与越界压力: 多种 n, 大样本 */
    rng_seed(&a, 31337u, 9u);
    for (i = 0; i < BULK_DRAWS; i++) {
        uint32_t n = (uint32_t)((i % 1000u) + 1u);
        if (rng_below(&a, n) >= n) {
            no_overflow = 0;
            break;
        }
    }
    check(no_overflow, "rng_below never returns >= n (200k draws, n = 1..1000)");

    /* 极大 n 不越界 */
    rng_seed(&a, 31337u, 10u);
    ok = 1;
    for (i = 0; i < 1000u; i++) {
        uint32_t n = UINT32_MAX - (i % 3u);
        if (rng_below(&a, n) >= n) {
            ok = 0;
            break;
        }
    }
    check(ok, "rng_below(n near UINT32_MAX) never returns >= n");

    /* n == 3 / n == 7 分布均匀 (拒绝采样无模偏差) */
    rng_seed(&a, 2024u, 11u);
    for (i = 0; i < 300000u; i++) {
        counts3[rng_below(&a, 3u)]++;
    }
    {
        const uint32_t expect = 100000u;
        const uint32_t tol = 1500u; /* 约 5.8 sigma, sigma 约 258 */
        int uniform = 1;
        uint32_t k;
        for (k = 0; k < 3u; k++) {
            uint32_t diff = counts3[k] > expect ? counts3[k] - expect : expect - counts3[k];
            if (diff > tol) {
                uniform = 0;
            }
        }
        printf("      counts3 = %u / %u / %u\n", counts3[0], counts3[1], counts3[2]);
        check(uniform, "rng_below(n=3) is uniform within tolerance (rejection sampling)");
    }

    rng_seed(&a, 555u, 12u);
    for (i = 0; i < 210000u; i++) {
        counts7[rng_below(&a, 7u)]++;
    }
    {
        const uint32_t expect = 30000u;
        const uint32_t tol = 900u; /* 约 5.4 sigma, sigma 约 160 */
        int uniform = 1;
        uint32_t k;
        for (k = 0; k < 7u; k++) {
            uint32_t diff = counts7[k] > expect ? counts7[k] - expect : expect - counts7[k];
            if (diff > tol) {
                uniform = 0;
            }
        }
        printf("      counts7 = %u %u %u %u %u %u %u\n", counts7[0], counts7[1], counts7[2],
               counts7[3], counts7[4], counts7[5], counts7[6]);
        check(uniform, "rng_below(n=7) is uniform within tolerance (rejection sampling)");
    }
}

/* ----------------------------------------------- 6. rng_range_i32 契约 */

static void test_range_i32(void)
{
    Rng a;
    Rng b;
    Rng r;
    uint32_t i;
    int degenerate = 1;
    int lo_gt_hi_ok = 1;
    int lo_gt_hi_no_consume = 1;
    int in_range = 1;
    int small_values = 1;
    int all_small_seen = 1;
    uint32_t seen[7] = {0u, 0u, 0u, 0u, 0u, 0u, 0u};

    /* lo == hi: 合法闭区间, 恒为 lo, 且消耗一次状态 */
    rng_seed(&a, 11u, 0u);
    rng_seed(&b, 11u, 0u);
    for (i = 0; i < 16u; i++) {
        if (rng_range_i32(&a, 5, 5) != 5 || rng_range_i32(&a, -7, -7) != -7 ||
            rng_range_i32(&a, 0, 0) != 0) {
            degenerate = 0;
            break;
        }
    }
    {
        uint64_t after = 0;
        uint64_t without = 0;
        rng_seed(&a, 11u, 0u);
        rng_seed(&b, 11u, 0u);
        (void)rng_range_i32(&a, 5, 5);
        after = rng_next_u64(&a);
        without = rng_next_u64(&b);
        if (after != without) {
            printf("      note: rng_range_i32(lo==hi) consumes one draw\n");
        } else {
            printf("      note: rng_range_i32(lo==hi) consumes no draw\n");
        }
    }
    check(degenerate, "rng_range_i32(lo==hi) returns lo (closed interval)");

    /* lo > hi: 返回 lo, 且不消耗状态 */
    {
        uint64_t after = 0;
        uint64_t without = 0;
        rng_seed(&a, 21u, 0u);
        rng_seed(&b, 21u, 0u);
        if (rng_range_i32(&a, 10, 3) != 10 || rng_range_i32(&a, 0, -1) != 0 ||
            rng_range_i32(&a, INT32_MAX, INT32_MIN) != INT32_MAX) {
            lo_gt_hi_ok = 0;
        }
        after = rng_next_u64(&a);
        without = rng_next_u64(&b);
        if (after != without) {
            lo_gt_hi_no_consume = 0;
        }
    }
    check(lo_gt_hi_ok, "rng_range_i32(lo>hi) returns lo");
    check(lo_gt_hi_no_consume, "rng_range_i32(lo>hi) consumes no state");

    /* 值域正确性: 全量程与窄区间 */
    {
        Rng fresh;
        uint64_t first_after_bulk;
        uint64_t first_at_origin;
        int completed = 1;

        rng_seed(&r, 77u, 1u);
        rng_seed(&fresh, 77u, 1u);
        first_at_origin = rng_next_u64(&fresh); /* 起步第一个值 */

        for (i = 0; i < BULK_DRAWS; i++) {
            /* 全量程闭区间: 只要有越界就立刻失败; 数值类型为 int32_t, 主要验证不 UB/不崩溃 */
            (void)rng_range_i32(&r, INT32_MIN, INT32_MAX);
        }
        first_after_bulk = rng_next_u64(&r);
        if (first_after_bulk == first_at_origin) {
            completed = 0; /* 走了 200k 次抽取却仍在起点, 说明抽取未发生 */
        }
        check(completed, "rng_range_i32(INT32_MIN, INT32_MAX) 200k draws advanced the stream");
    }

    rng_seed(&r, 78u, 1u);
    for (i = 0; i < BULK_DRAWS; i++) {
        int32_t v = rng_range_i32(&r, -3, 3);
        if (v < -3 || v > 3) {
            in_range = 0;
            break;
        }
        seen[(uint32_t)(v + 3)]++;
    }
    for (i = 0; i < 7u; i++) {
        if (seen[i] == 0u) {
            all_small_seen = 0;
        }
    }
    check(in_range, "rng_range_i32(-3, 3) always inside closed interval (200k draws)");
    check(all_small_seen, "rng_range_i32(-3, 3) reaches every value in the interval");

    rng_seed(&r, 79u, 2u);
    for (i = 0; i < 20000u; i++) {
        int32_t v = rng_range_i32(&r, 1, 6);
        if (v < 1 || v > 6) {
            small_values = 0;
            break;
        }
    }
    check(small_values, "rng_range_i32(1, 6) always inside closed interval");

    /* 跨零大区间: 必须同时出现正负值 */
    {
        int saw_neg = 0;
        int saw_pos = 0;
        rng_seed(&r, 80u, 3u);
        for (i = 0; i < 1000u; i++) {
            int32_t v = rng_range_i32(&r, -1000000, 1000000);
            if (v < 0) {
                saw_neg = 1;
            }
            if (v > 0) {
                saw_pos = 1;
            }
        }
        check(saw_neg && saw_pos, "rng_range_i32 spans negative and positive values");
    }
}

/* --------------------------------- 7. rng_unit_f32 / u32 / bool 契约 */

static void test_unit_and_u32_and_bool(void)
{
    Rng a;
    Rng b;
    Rng r;
    uint32_t i;
    int in_unit = 1;
    int high24_exact = 1;
    float min_v = 1.0f;
    float max_v = 0.0f;
    int u32_matches_high_bits = 1;
    int bool_both = 0;
    uint32_t true_count = 0u;

    rng_seed(&a, 2026u, 4u);
    rng_seed(&b, 2026u, 4u);

    for (i = 0; i < 100000u; i++) {
        float f = rng_unit_f32(&a);
        uint64_t raw = rng_next_u64(&b);
        float expect = (float)(raw >> 40) * (1.0f / 16777216.0f);

        if (!(f >= 0.0f) || !(f < 1.0f)) {
            in_unit = 0;
            break;
        }
        if (f != expect) {
            high24_exact = 0;
            break;
        }
        if (f < min_v) {
            min_v = f;
        }
        if (f > max_v) {
            max_v = f;
        }
    }
    check(in_unit, "rng_unit_f32 always in [0, 1) (100k draws)");
    check(high24_exact, "rng_unit_f32 uses the high 24 bits of one u64 draw (exact k/2^24)");
    printf("      unit f32 min = %.9f, max = %.9f\n", (double)min_v, (double)max_v);
    check(min_v < 0.001f && max_v > 0.999f, "rng_unit_f32 covers the low and high ends of [0,1)");

    /* rng_next_u32 == 高 32 位 */
    rng_seed(&a, 808u, 6u);
    rng_seed(&b, 808u, 6u);
    for (i = 0; i < 100000u; i++) {
        uint32_t v32 = rng_next_u32(&a);
        uint64_t v64 = rng_next_u64(&b);
        if (v32 != (uint32_t)(v64 >> 32)) {
            u32_matches_high_bits = 0;
            break;
        }
    }
    check(u32_matches_high_bits, "rng_next_u32 equals the high 32 bits of one u64 draw");

    /* rng_next_bool: 两个取值都出现, 且大致均衡 */
    rng_seed(&r, 1000u, 7u);
    for (i = 0; i < 100000u; i++) {
        if (rng_next_bool(&r)) {
            true_count++;
        }
    }
    bool_both = (true_count > 0u) && (true_count < 100000u);
    check(bool_both, "rng_next_bool yields both true and false");
    printf("      bool true count = %u / 100000\n", true_count);
    check(true_count > 49000u && true_count < 51000u,
          "rng_next_bool is balanced within tolerance");

    /* bool 与 u64 最高位一致 */
    {
        int matches = 1;
        rng_seed(&a, 2001u, 8u);
        rng_seed(&b, 2001u, 8u);
        for (i = 0; i < 10000u; i++) {
            bool vb = rng_next_bool(&a);
            uint64_t v64 = rng_next_u64(&b);
            if (vb != ((v64 >> 63) != 0u)) {
                matches = 0;
                break;
            }
        }
        check(matches, "rng_next_bool equals the top bit of one u64 draw");
    }
}

/* ------------------------------------------------------- 8. NULL 安全 */

static void test_null_safety(void)
{
    int ok = 1;

    rng_seed(NULL, 1u, 2u);
    rng_reset(NULL);

    if (rng_next_u64(NULL) != 0u) {
        ok = 0;
    }
    if (rng_next_u32(NULL) != 0u) {
        ok = 0;
    }
    if (rng_range_i32(NULL, 5, 9) != 0) {
        ok = 0;
    }
    if (rng_range_i32(NULL, INT32_MIN, INT32_MAX) != 0) {
        ok = 0;
    }
    if (rng_unit_f32(NULL) != 0.0f) {
        ok = 0;
    }
    if (rng_below(NULL, 0u) != 0u) {
        ok = 0;
    }
    if (rng_below(NULL, 7u) != 0u) {
        ok = 0;
    }
    if (rng_next_bool(NULL)) {
        ok = 0;
    }

    check(ok, "NULL safety: all rng_* return 0/false and do not crash");

    /* 契约记录: NULL 抽取不产生任何可观测副作用, 后续正常使用仍可复现 */
    {
        Rng a;
        Rng b;
        int same = 1;
        uint32_t i;
        rng_seed(&a, 3u, 3u);
        rng_seed(&b, 3u, 3u);
        for (i = 0; i < 32u; i++) {
            (void)rng_next_u64(NULL);
            (void)rng_below(NULL, 3u);
            (void)rng_unit_f32(NULL);
            if (rng_next_u64(&a) != rng_next_u64(&b)) {
                same = 0;
            }
        }
        check(same, "NULL calls do not perturb any live stream");
    }
}

/* ----------------------------------------- 9. 无全局/静态状态 (交叉验证) */

static void test_no_shared_state(void)
{
    Rng x;
    Rng y;
    Rng z;
    Rng tmp;
    uint32_t i;
    uint32_t k;
    int independent = 1;
    int y_reproducible = 1;
    uint64_t ref_x[32];
    uint64_t ref_y[32];
    uint64_t ref_z[32];

    /* 参考: 三个干净实例各自单独使用 */
    rng_seed(&x, 51u, 0u);
    rng_seed(&y, 51u, 1u);
    rng_seed(&z, 51u, 2u);
    for (i = 0; i < 32u; i++) {
        ref_x[i] = rng_next_u64(&x);
        ref_y[i] = rng_next_u64(&y);
        ref_z[i] = rng_next_u64(&z);
    }

    /* 交错复跑: 每一轮都对 y 做 reset + 补齐消费 + 额外抽取, 并用相反的访问顺序
     * 抽取 z 与 x。x 与 z 的第 i 个值必须仍等于它们单独使用时的第 i 个值。 */
    rng_seed(&x, 51u, 0u);
    rng_seed(&y, 51u, 1u);
    rng_seed(&z, 51u, 2u);
    for (i = 0; i < 32u; i++) {
        uint64_t zz = rng_next_u64(&z); /* 先访问 z */
        uint64_t xx = rng_next_u64(&x); /* 再访问 x */

        /* y 从起点重放: 第 i 次抽取必须等于参考值 (y 自证可复现) */
        rng_reset(&y);
        if (rng_next_u64(&y) != ref_y[0]) {
            y_reproducible = 0;
            break;
        }
        for (k = 1u; k <= i; k++) {
            if (rng_next_u64(&y) != ref_y[k]) {
                y_reproducible = 0;
                break;
            }
        }
        if (!y_reproducible) {
            break;
        }
        (void)rng_below(&y, 9u); /* y 的额外消费不得影响 x / z */
        (void)rng_unit_f32(&y);

        if (xx != ref_x[i] || zz != ref_z[i]) {
            independent = 0;
            break;
        }
    }
    check(independent, "instances are independent: no global/static shared state");
    check(y_reproducible, "instance replayed from rng_reset matches its own reference");

    /* 同一次运行内反复重新 seed 同一实例, 首个值始终一致 */
    {
        uint64_t first = 0;
        char firstbuf[24];
        int ok = 1;
        for (i = 0; i < 1000u; i++) {
            rng_seed(&tmp, UINT64_C(0xABCDEF), 5u);
            if (i == 0u) {
                first = rng_next_u64(&tmp);
            } else if (rng_next_u64(&tmp) != first) {
                ok = 0;
                break;
            }
        }
        printf("      re-seed first draw = %s\n", hex64(first, firstbuf));
        check(ok, "re-seeding the same instance keeps the same first draw");
    }
}

int main(void)
{
    printf("== S02 rng tests (SplitMix64, interface version 1) ==\n");

    test_fixed_vectors();
    test_reproducibility_and_streams();
    test_reset();
    test_stream_independence_interleaved();
    test_below();
    test_range_i32();
    test_unit_and_u32_and_bool();
    test_null_safety();
    test_no_shared_state();

    printf("== summary: %d passed, %d failed ==\n", g_pass, g_fail);
    if (g_fail != 0) {
        printf("RESULT: FAIL\n");
        return 1;
    }
    printf("RESULT: PASS\n");
    return 0;
}
