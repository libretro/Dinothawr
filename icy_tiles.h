/* Dinothawr - finding tiles by attribute.
 *
 * The game asks a layer for the tiles carrying an attribute: the goal
 * squares, the goal blocks. Answers are element pointers into the
 * layer's cluster, which are stable as long as nothing is added - and
 * nothing is, after a level has loaded.
 *
 * MSVC C89.
 */

#ifndef ICY_TILES_H__
#define ICY_TILES_H__

#include <stddef.h>

#include "blit_surface_cluster.h"
#include "blit_tilemap.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fills @out with the elements of @layer_name whose @attr is @value,
 * and returns how many there are. A NULL or empty @value matches any
 * tile that has the attribute at all.
 *
 * Writes at most @max entries but counts them all, so a caller that
 * cares can size a buffer from a first pass with @out NULL. */
size_t icy_tiles_with_attr(const blit_tilemap_t *map,
      const char *layer_name, const char *attr, const char *value,
      blit_cluster_elem_t **out, size_t max);

#ifdef __cplusplus
}
#endif

#endif
