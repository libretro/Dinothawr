/* Dinothawr - the camera.
 *
 * Where the view sits over a level. A map small enough to fit the view
 * is centred once and stays put; a larger one follows the player and
 * stops at the edges, so the view never shows past the map.
 *
 * The camera is the render target's rect position, and this writes it.
 * It reads the followed rect and the map size fresh each call, so a
 * level whose player moves needs no other bookkeeping.
 *
 * MSVC C89.
 */

#ifndef ICY_CAMERA_H__
#define ICY_CAMERA_H__

#include "blit_geom.h"
#include "blit_render_target.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Points @target's camera at @follow within a map of @map_size. */
void icy_camera_update(blit_render_target_t *target,
      blit_rect_t follow, blit_pos_t map_size);

#ifdef __cplusplus
}
#endif

#endif
