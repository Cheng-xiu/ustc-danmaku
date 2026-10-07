#ifndef DEMO_PROJECTILES_H
#define DEMO_PROJECTILES_H

#include "demo_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Advance against the owning world's frozen logical field. Invalid dimensions
 * or dt leave the pool unchanged. pool_advance remains the default-size wrapper. */
void pool_advance_in_field(ProjectilePool *pool, float dt, float width, float height);

#ifdef __cplusplus
}
#endif
#endif
