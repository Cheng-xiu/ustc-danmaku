/* rng.c - SplitMix64 确定性随机数: 独立流派生、状态重置
 *
 * 接口版本: 1 (core/demo_base.h 冻结, 本实现不修改任何公共头文件)
 * 算法: SplitMix64 计数器模式
 *     state += 0x9E3779B97F4A7C15
 *     z  = state
 *     z  = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9
 *     z  = (z ^ (z >> 27)) * 0x94D049BB133111EB
 *     return z ^ (z >> 31)
 *
 * 纯 C, 无墙钟/无平台 rand()/无全局可变状态/无静态缓存/不写日志。
 * 同 (seed, stream_id) 在任何机器、任何编译选项下产生同一序列(只用 64 位无符号整数
 * 运算, 溢出按 C 标准取模, 不涉及浮点); 渲染与日志不能影响本文件。
 *
 * 流隔离: world / bot / explore 各持有一个独立 Rng 实例并按各自的 stream_id 派生,
 * 某个流抽取多少次都不会改变另一个流的序列。
 *
 * 初始状态的派生(重要): 不能直接用 state = seed + stream_id * gamma。SplitMix64 的
 * state 本身就是递增计数器, 用 gamma 的整数倍做初值只会得到"同一条序列的平移前缀":
 * stream_id=1 的序列就是 stream_id=0 序列从第 2 个值开始的后缀, 三个流高度重叠,
 * 失去流派生意义(本任务测试一开始就是这样抓到该缺陷的)。因此按任务卡允许的
 * "先跑一个混合函数"方案, 用 SplitMix64 混合函数对 (seed, stream_id) 做雪崩:
 *     origin = mix64( seed ^ mix64(stream_id * gamma) )
 * mix64 是双射且雪崩, 不同 stream_id 的初值在 64 位空间里互不相关, 不再相差 gamma 的
 * 整数倍。该式满足 (seed, stream_id) = (0, 0) 时 origin = 0, 因此 seed=0/stream=0 仍与
 * SplitMix64 公开标准流的 seed 0 前缀逐位一致, 便于交叉核验。
 *
 * 状态字段契约(在不改公共头文件的前提下满足 rng_reset):
 *   - rng->state     当前 SplitMix64 状态, 每次抽取前进一个 gamma 步;
 *   - rng->stream_id 保存该流的**初始 state**(由 seed 与 stream_id 派生),
 *                    rng_reset 用它把 state 回卷到 rng_seed 之后、第一次抽取之前。
 * 若后续需要在该字段里额外保留原始流编号, 需要公共头文件新增一个字段(接口变更,
 * 由母代理决定), 本任务不擅自改头文件。
 *
 * 空指针契约: 全部 rng_* 函数对 NULL 安全。rng_seed/rng_reset 为无操作;
 * 抽取函数返回 0(浮点为 0.0f, 布尔为 false)且不消耗任何状态。
 * 退化输入契约: rng_below(n == 0) 返回 0 且不消耗状态; rng_range_i32(lo > hi)
 * 返回 lo 且不消耗状态; rng_range_i32(lo == hi) 为合法闭区间, 返回 lo 并消耗一次抽取。
 */
#include "demo_base.h"

#define RNG_GOLDEN_GAMMA UINT64_C(0x9E3779B97F4A7C15)
#define RNG_MIX_MUL_1 UINT64_C(0xBF58476D1CE4E5B9)
#define RNG_MIX_MUL_2 UINT64_C(0x94D049BB133111EB)

/* 2^24, rng_unit_f32 的定标常数(高 24 位 -> [0, 1)) */
#define RNG_UNIT_SCALE (1.0f / 16777216.0f)

/* SplitMix64 混合函数: 只用 64 位无符号整数运算, 双射且雪崩。 */
static uint64_t rng_mix64(uint64_t z)
{
    z = (z ^ (z >> 30)) * RNG_MIX_MUL_1;
    z = (z ^ (z >> 27)) * RNG_MIX_MUL_2;
    return z ^ (z >> 31);
}

/* 该流的初始 state: 由 seed 与 stream_id 做雪崩派生。
 * stream_id 的乘法在无符号类型上溢出即取模, 行为由 C 标准定义, 不是未定义行为。
 * seed = 0 且 stream_id = 0 时结果为 0, 保留 SplitMix64 标准 seed 0 前缀;
 * 只要 seed 或 stream_id 非零, 初值就与 0 无固定倍数关系, 三个流不重叠。 */
static uint64_t rng_stream_origin(uint64_t seed, uint64_t stream_id)
{
    return rng_mix64(seed ^ rng_mix64(stream_id * RNG_GOLDEN_GAMMA));
}

void rng_seed(Rng *rng, uint64_t seed, uint64_t stream_id)
{
    uint64_t origin;

    if (rng == NULL) {
        return;
    }

    origin = rng_stream_origin(seed, stream_id);
    rng->state = origin;
    rng->stream_id = origin; /* 初始 state 副本, 供 rng_reset 回卷 */
}

void rng_reset(Rng *rng)
{
    if (rng == NULL) {
        return;
    }

    /* 回到 rng_seed 之后、第一次抽取之前 */
    rng->state = rng->stream_id;
}

/* 前进一个 gamma 步并做 SplitMix64 混合。调用方保证 rng 非空。 */
static uint64_t rng_next_state(Rng *rng)
{
    rng->state += RNG_GOLDEN_GAMMA;
    return rng_mix64(rng->state);
}

uint64_t rng_next_u64(Rng *rng)
{
    if (rng == NULL) {
        return 0u;
    }

    return rng_next_state(rng);
}

uint32_t rng_next_u32(Rng *rng)
{
    if (rng == NULL) {
        return 0u;
    }

    /* 取高 32 位: 高位的混合质量优于低位 */
    return (uint32_t)(rng_next_state(rng) >> 32);
}

/* 在 [0, n) 内均匀取数, n >= 1 由调用方保证; 用拒绝采样消除取模偏差。 */
static uint64_t rng_below_u64(Rng *rng, uint64_t n)
{
    /* threshold = (2^64 - n) % n: 落在 [0, threshold) 的样本会使取模结果偏多, 丢弃 */
    uint64_t threshold = (UINT64_MAX - n + 1u) % n;
    uint64_t sample;

    do {
        sample = rng_next_state(rng);
    } while (sample < threshold);

    return sample % n;
}

uint32_t rng_below(Rng *rng, uint32_t n)
{
    if (rng == NULL) {
        return 0u;
    }

    if (n == 0u) {
        return 0u; /* 契约: n == 0 返回 0 且不消耗状态 */
    }

    return (uint32_t)rng_below_u64(rng, (uint64_t)n);
}

int32_t rng_range_i32(Rng *rng, int32_t lo, int32_t hi)
{
    uint64_t span;
    uint64_t offset;

    if (rng == NULL) {
        return 0;
    }

    if (lo > hi) {
        return lo; /* 契约: 闭区间非法时返回 lo, 不消耗状态 */
    }

    /* span = hi - lo + 1, 最大 2^32(lo = INT32_MIN, hi = INT32_MAX), 不溢出 */
    span = (uint64_t)((int64_t)hi - (int64_t)lo) + 1u;
    offset = rng_below_u64(rng, span);

    return (int32_t)((int64_t)lo + (int64_t)offset);
}

float rng_unit_f32(Rng *rng)
{
    uint64_t z;

    if (rng == NULL) {
        return 0.0f;
    }

    /* 用高 24 位构造, 结果精确落在 {k / 2^24 | 0 <= k < 2^24} 内, 即 [0, 1) */
    z = rng_next_state(rng);
    return (float)(z >> 40) * RNG_UNIT_SCALE;
}

bool rng_next_bool(Rng *rng)
{
    /* rng_next_u64(NULL) 返回 0, 因此 NULL 自然得到 false */
    return (rng_next_u64(rng) >> 63) != 0u;
}
