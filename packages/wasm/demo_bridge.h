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
int demo_step_aim(float move_x, float move_y, int pointer_valid, float pointer_x,
                  float pointer_y, uint32_t attack_mask, float aim_dir_x, float aim_dir_y);
/* Pure C preview; its independent buffer never replaces the accepted plan.
 * Readiness is separate from geometry, so CD/energy refusal can retain rays. */
const uint8_t *demo_preview(uint32_t pattern, float aim_dir_x, float aim_dir_y);
uint32_t demo_preview_size(void);
const uint8_t *demo_snapshot(void);
uint32_t demo_snapshot_size(void);
void demo_dispose(void);

#ifdef __cplusplus
}
#endif
#endif
