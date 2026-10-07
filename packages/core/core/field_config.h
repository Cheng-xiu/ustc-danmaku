#ifndef DEMO_FIELD_CONFIG_H
#define DEMO_FIELD_CONFIG_H

#include "demo_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Resize a reset configuration, never a live World/accepted attack plan.
 * Keep speeds, radii, costs and timing fixed. Map legal spawn/bounds coordinates,
 * scale the course row span and preserve shower spacing within its hard limits.
 * On failure every byte of config remains unchanged. An unchanged field is a no-op. */
bool demo_config_set_field_size(DemoConfig *config, float width, float height);

#ifdef __cplusplus
}
#endif
#endif
