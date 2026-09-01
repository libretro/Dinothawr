/* Dinothawr - a cluster of surfaces drawn together.
 *
 * One tilemap layer: the tiles that make it up, each with its own offset
 * from the cluster's origin, drawn in insertion order. The game finds
 * individual elements by position - the block the player is pushing, the
 * goal squares - and mutates them in place, so elements are addressed by
 * pointer and stay put for the cluster's lifetime.
 *
 * The cluster owns a reference on each element's surface. Blit::
 * SurfaceCluster in surface.hpp wraps one so the engine's Renderable
 * dispatch still works; the elements themselves are this struct's, not
 * the wrapper's.
 *
 * MSVC C89.
 */

#ifndef BLIT_SURFACE_CLUSTER_H__
#define BLIT_SURFACE_CLUSTER_H__

#include <stddef.h>

#include "blit_geom.h"
#include "blit_surface.h"
#include "blit_render_target.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
   blit_surface_t surf;
   blit_pos_t     offset;
   unsigned       tag;
} blit_cluster_elem_t;

typedef struct
{
   blit_cluster_elem_t *elems;
   size_t               count;
   size_t               capacity;
} blit_surface_cluster_t;

void blit_surface_cluster_init(blit_surface_cluster_t *cluster);
void blit_surface_cluster_release(blit_surface_cluster_t *cluster);

/* Appends a copy of @surf at @offset, taking its own reference on the
 * surface's tables. Non-zero on success.
 *
 * Note that this may move earlier elements: a pointer taken from the
 * cluster is only good until the next add. Nothing adds after a level
 * has loaded, so in practice the pointers the game holds are stable. */
int blit_surface_cluster_add(blit_surface_cluster_t *cluster,
      const blit_surface_t *surf, blit_pos_t offset);

/* Draws every element at @origin plus its own offset. */
void blit_surface_cluster_render(const blit_surface_cluster_t *cluster,
      blit_render_target_t *target, blit_pos_t origin);

/* The element whose surface sits at @offset, or NULL. */
blit_cluster_elem_t *blit_surface_cluster_find(
      const blit_surface_cluster_t *cluster, blit_pos_t offset);

#ifdef __cplusplus
}
#endif

#endif
