/* Standalone native ABI/real-wave probe, no graphics or wall clock. */
#include "demo_bridge.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t u32(const uint8_t *bytes, uint32_t word) {
    const uint8_t *p = bytes + word * 4u;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "bridge probe failed, line %d: %s\n", __LINE__, #condition); \
    return EXIT_FAILURE; } } while (0)

int main(void) {
    CHECK(demo_reset(0xffffffffu, 0x12345678u, 8u) == 1);
    const uint8_t *bytes = demo_snapshot();
    CHECK(bytes != NULL && u32(bytes, 6u) == 8u);
    CHECK(u32(bytes, 20u) == 0xffffffffu && u32(bytes, 21u) == 0x12345678u);
    demo_dispose();
    CHECK(demo_snapshot() == NULL && demo_snapshot_size() == 0u);
    CHECK(demo_step(0, 0, 0, 0, 0, 0) == 0);

    for (uint32_t pattern = 0u; pattern < 4u; ++pattern) {
        CHECK(demo_reset(12345u, 0u, 3u) == 1);
        const uint32_t start_tick = pattern == 3u ? 240u : 120u;
        for (uint32_t tick = 0u; tick < start_tick; ++tick) {
            const uint32_t edge = (tick / 90u) % 4u;
            CHECK(demo_step(edge == 0u ? 1 : edge == 2u ? -1 : 0,
                           edge == 1u ? -1 : edge == 3u ? 1 : 0, 0, 0, 0, 0) == 1);
        }
        CHECK(demo_step(0, 0, 0, 0, 0, 1u << pattern) == 1);
        bytes = demo_snapshot();
        CHECK(bytes != NULL && u32(bytes, 22u) == 1u && u32(bytes, 8u) > 0u);
        CHECK(u32(bytes, 0u) == 0x55444331u && u32(bytes, 1u) == 4u);
        CHECK(u32(bytes, 2u) == demo_snapshot_size() && u32(bytes, 15u) == start_tick);
        uint32_t predicted[512] = {0};
        uint32_t count = u32(bytes, 8u), offset = u32(bytes, 42u) / 4u;
        for (uint32_t ray = 0u; ray < count; ++ray) {
            uint32_t spawn = u32(bytes, offset + ray * 8u + 5u);
            CHECK(spawn >= start_tick && spawn - start_tick < 512u);
            predicted[spawn - start_tick]++;
        }
        const uint32_t end = start_tick + u32(bytes, 16u) + u32(bytes, 17u);
        uint32_t actual = 0u, waves = 0u;
        while (u32(bytes, 3u) <= end + 1u && u32(bytes, 4u) == 0u) {
            const uint32_t edge = (u32(bytes, 3u) / 90u) % 4u;
            CHECK(demo_step(edge == 0u ? 1 : edge == 2u ? -1 : 0,
                           edge == 1u ? -1 : edge == 3u ? 1 : 0, 0, 0, 0, 0) == 1);
            bytes = demo_snapshot();
            CHECK(bytes != NULL && u32(bytes, 44u) == 0u && u32(bytes, 28u) == 0u);
            const uint32_t events = u32(bytes, 9u), event_offset = u32(bytes, 43u) / 4u;
            for (uint32_t ev = 0u; ev < events; ++ev) {
                const uint32_t word = event_offset + ev * 9u;
                if (u32(bytes, word) == 5u) {
                    const uint32_t tick = u32(bytes, word + 1u);
                    const uint32_t spawned = u32(bytes, word + 6u);
                    CHECK(tick >= start_tick && tick - start_tick < 512u);
                    CHECK(spawned == predicted[tick - start_tick]);
                    actual += spawned; waves++;
                }
            }
        }
        CHECK(waves > 0u && actual <= count);
        /* Death or a wave clear legitimately cancels the remaining plan. */
        if (u32(bytes, 4u) == 0u && u32(bytes, 46u) == 0u) CHECK(actual == count);
        printf("pattern=%u warnings=%u real_spawn=%u waves=%u late_start=%u OK\n",
               pattern, count, actual, waves, start_tick);
    }
    CHECK(demo_reset(12345u, 0u, 3u) == 1);
    bytes = demo_snapshot();
    CHECK(bytes != NULL && u32(bytes, 3u) == 0u && u32(bytes, 8u) == 0u && u32(bytes, 22u) == 0u);
    CHECK(demo_step(1, 0, 0, 0, 0, 0) == 1);
    bytes = demo_snapshot();
    const uint32_t old_tick = u32(bytes, 3u), old_seed = u32(bytes, 20u);
    CHECK(demo_reset_sized(999u, 0u, 3u, NAN, 720.0f) == 0);
    CHECK(demo_reset_sized(999u, 0u, 3u, 1600.0f, 60.0f) == 0);
    bytes = demo_snapshot();
    CHECK(bytes != NULL && u32(bytes, 3u) == old_tick && u32(bytes, 20u) == old_seed);
    CHECK(demo_step(0, 0, 0, 0, 0, 0) == 1);
    puts("native bridge lifecycle, seed64, 8 students, all warnings vs real waves PASS");
    return EXIT_SUCCESS;
}
