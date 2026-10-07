/* Public snapshot replay fixture; links the exact same C bridge/core as Wasm. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "demo_bridge.h"

static int write_snapshot(FILE *file) {
    const uint8_t *data = demo_snapshot();
    uint32_t length = demo_snapshot_size();
    return data && length && fwrite(&length, 4, 1, file) == 1 && fwrite(data, 1, length, file) == length;
}
int main(int argc, char **argv) {
    if (argc != 5) { fprintf(stderr, "usage: native_replay path seedLo seedHi students\n"); return 2; }
    FILE *file = fopen(argv[1], "wb");
    if (!file) return 3;
    uint32_t lo = (uint32_t)strtoul(argv[2], NULL, 10);
    uint32_t hi = (uint32_t)strtoul(argv[3], NULL, 10);
    uint32_t students = (uint32_t)strtoul(argv[4], NULL, 10);
    if (!demo_reset(lo, hi, students) || !write_snapshot(file)) return 4;
    for (uint32_t tick = 0; tick < 3600; tick++) {
        uint32_t segment = (tick / 120) % 4;
        float mx = segment == 0 ? 1.0f : segment == 2 ? -1.0f : 0.0f;
        float my = segment == 1 ? -1.0f : segment == 3 ? 1.0f : 0.0f;
        uint32_t attacks = tick % 240 == 0 ? 1u << ((tick / 240) % 4) : 0;
        /* Include refused requests, pointer input, all patterns and 64-bit seeds. */
        if (tick % 240 == 10) attacks = 8;
        int pointer = (tick / 300) % 3 == 1;
        float px = tick % 600 < 300 ? 260.0f : 720.0f;
        float py = tick % 480 < 240 ? 540.0f : 180.0f;
        demo_step(mx, my, pointer, px, py, attacks);
        if (!write_snapshot(file)) return 5;
    }
    demo_dispose();
    fclose(file);
    return 0;
}
