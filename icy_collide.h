/* Dinothawr - grid collision.
 *
 * Whether a surface may move one step. Everything the game moves is a
 * tile: the player, the blocks, the goal squares. So a move is checked
 * against the tiles the surface would land on, and against nothing else.
 *
 * The surface's own tile never counts as a collision - a thing cannot
 * block itself - which is why the current tile is passed rather than
 * derived from the destination.
 *
 * MSVC C89.
 */

#ifndef ICY_COLLIDE_H__
#define ICY_COLLIDE_H__

#include "blit_geom.h"
#include "blit_tilemap.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Non-zero when @surf_rect moved by @offset tiles would land on a
 * blocked tile.
 *
 * The check assumes @surf_rect starts aligned to the grid; a sprite
 * wider than a tile is still treated as tile-sized, which is what lets
 * the dino be slightly larger than 16x16 without confusing the grid.
 * Call icy_collide_aligned first if that is not already known. */
int icy_collide_offset(const blit_tilemap_t *map, blit_rect_t surf_rect,
      blit_pos_t offset);

/* Non-zero when @surf_rect sits on the tile grid. The collision check
 * has no meaning otherwise, and the caller decides whether that is a
 * broken level or a bug. */
int icy_collide_aligned(const blit_tilemap_t *map, blit_rect_t surf_rect);

#ifdef __cplusplus
}
#endif

#endif
