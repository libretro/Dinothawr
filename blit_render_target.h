/* Dinothawr - the render target.
 *
 * A pixel buffer with a camera: the framebuffer the game draws into, and
 * the scratch targets the menu previews are rendered through. The camera
 * is the rect's position - a surface is blitted at its own coordinates
 * minus the camera, unless it asks to ignore it.
 *
 * Unlike a surface, these pixels are mutable and unshared. A target owns
 * its buffer outright; nothing else points into it while it is alive.
 *
 * Blit::RenderTarget in surface.hpp is a thin C++ shim over this, for
 * the engine code that still holds one by value. It forwards everything
 * here.
 *
 * MSVC C89.
 */

#ifndef BLIT_RENDER_TARGET_H__
#define BLIT_RENDER_TARGET_H__

#include <stddef.h>

#include <retro_inline.h>

#include "blit_pixel.h"
#include "blit_geom.h"
#include "blit_surface.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
   blit_pixel_t *buffer;
   size_t        count;   /* pixels allocated */
   blit_rect_t   rect;    /* rect.pos is the camera */
} blit_render_target_t;

/* An empty target: no buffer, zero rect. */
void blit_render_target_init(blit_render_target_t *target);

/* A w*h target, zero-filled. Non-zero on success. */
int blit_render_target_init_size(blit_render_target_t *target,
      int w, int h);

void blit_render_target_release(blit_render_target_t *target);

void blit_render_target_clear(blit_render_target_t *target,
      blit_pixel_t fill);

/* Hands the buffer over as shared surface data and leaves the target
 * empty. NULL on allocation failure, in which case the target is
 * unchanged. */
blit_surface_data_t *blit_render_target_to_data(
      blit_render_target_t *target);

/* The pixel at @pos ignoring the camera, or NULL when outside. */
static INLINE blit_pixel_t *blit_render_target_pixel_raw_no_offset(
      const blit_render_target_t *target, blit_pos_t pos)
{
   if (     pos.x < 0 || pos.y < 0
         || pos.x >= target->rect.w || pos.y >= target->rect.h)
      return NULL;

   return &target->buffer[pos.y * target->rect.w + pos.x];
}

/* The pixel at @pos in world coordinates, or NULL when outside. */
static INLINE blit_pixel_t *blit_render_target_pixel_raw(
      const blit_render_target_t *target, blit_pos_t pos)
{
   return blit_render_target_pixel_raw_no_offset(target,
         blit_pos_sub(pos, target->rect.pos));
}

/* Draws @surf, offset by @offset, clipped to @subrect when that is
 * non-empty and to the target either way. */
void blit_render_target_blit_offset(blit_render_target_t *target,
      const blit_surface_t *surf, blit_rect_t subrect, blit_pos_t offset);

void blit_render_target_blit(blit_render_target_t *target,
      const blit_surface_t *surf, blit_rect_t subrect);

/* A sub-rectangle of @src as its own surface, rendered through a scratch
 * target. The caller owns the result; it is empty on failure. Lives here
 * rather than beside the other surface calls because a scratch target is
 * how it is done. */
blit_surface_t blit_surface_sub(const blit_surface_t *src,
      blit_rect_t rect);

#ifdef __cplusplus
}
#endif

#endif
