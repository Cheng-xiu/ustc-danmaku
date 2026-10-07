#ifndef WEB_DEMO_BRIDGE_H
#define WEB_DEMO_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int demo_reset(uint32_t seed_lo, uint32_t seed_hi, uint32_t students);
int demo_reset_sized(uint32_t seed_lo, uint32_t seed_hi, uint32_t students,
                     float field_width, float field_height);
int demo_step(float move_x, float move_y, int pointer_valid, float pointer_x,
              float pointer_y, uint32_t attack_mask);
const uint8_t *demo_snapshot(void);
uint32_t demo_snapshot_size(void);
void demo_dispose(void);

#ifdef __cplusplus
}
#endif
#endif
