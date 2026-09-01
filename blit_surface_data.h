/* Dinothawr - shared surface pixel data.
 *
 * The immutable pixel buffer behind a Surface: a tileset face, a sprite
 * frame, a glyph sheet. One buffer is shared by every Surface showing
 * it - the session cache hands the same data to every level that uses a
 * tile - so it is reference counted and freed on the last release.
 *
 * The buffer is immutable once created. Nothing writes through a
 * blit_surface_data_t: refill_color builds a fresh one and swaps, and
 * the only mutable pixels in the engine are the render target's own.
 * That is what makes sharing safe without copying.
 *
 * The count is a plain int, not an atomic. Every surface load, blit and
 * release happens on the thread that calls retro_load_game and
 * retro_run; the audio decode jobs are the core's only other threads and
 * they never touch a surface.
 *
 * MSVC C89.
 */

#ifndef BLIT_SURFACE_DATA_H__
#define BLIT_SURFACE_DATA_H__

#include <stddef.h>

#include <retro_inline.h>

#include "blit_pixel.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct blit_surface_data blit_surface_data_t;

struct blit_surface_data
{
   blit_pixel_t *pixels;
   int           w;
   int           h;
   int           refcount;
};

/* Takes ownership of @pixels, which must be w*h entries from malloc and
 * is freed on the last release. Returns NULL on allocation failure, in
 * which case @pixels is freed too - the caller has handed it over
 * either way. */
blit_surface_data_t *blit_surface_data_new(blit_pixel_t *pixels,
      int w, int h);

/* A w*h buffer filled with one colour. NULL on allocation failure. */
blit_surface_data_t *blit_surface_data_new_filled(blit_pixel_t fill,
      int w, int h);

/* Inline: a Surface copy happens per blit, and this is an increment.
 * The release path is not inline - it ends in two free() calls, and
 * pasting those into every destruction site costs more in code size
 * than the call saves. */
static INLINE blit_surface_data_t *blit_surface_data_ref(
      blit_surface_data_t *data)
{
   if (data)
      data->refcount++;
   return data;
}

/* Not inline. Measured: inlining the whole thing costs 8% (two free()s
 * pasted into a hundred thousand release sites), and inlining just the
 * decrement with an out-of-line destroy costs 3.6% over a single call
 * instruction at each site. */
void blit_surface_data_unref(blit_surface_data_t *data);

#ifdef __cplusplus
}
#endif

#endif
