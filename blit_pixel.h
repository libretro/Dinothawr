/* Dinothawr - pixel format and per-pixel operations.
 *
 * The framebuffer is XRGB8888 throughout: one 32-bit word per pixel,
 * A:24 R:16 G:8 B:0. This was a template parameterised over every field
 * width and shift, but only ever instantiated at that one layout, so the
 * parameters were ceremony around a fixed format. Here it is the format
 * itself, with the operations that used to be member functions.
 *
 * MSVC C89: C comments only, declarations at the top of each block, no
 * C99/C11 features. The header is C++-includable via the extern "C"
 * guard, and is header-only - every operation is small enough to inline
 * at the call site, which is where the blit loops need them.
 */

#ifndef BLIT_PIXEL_H__
#define BLIT_PIXEL_H__

#include <stddef.h>
#include <stdint.h>

#include <retro_inline.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t blit_pixel_t;

#define BLIT_PIXEL_ALPHA_SHIFT 24
#define BLIT_PIXEL_RED_SHIFT   16
#define BLIT_PIXEL_GREEN_SHIFT  8
#define BLIT_PIXEL_BLUE_SHIFT   0

#define BLIT_PIXEL_ALPHA_MASK  0xff000000u

/* Pack 8-bit components. Every field is 8 bits wide here, so the
 * shift-down-then-up dance the template needed for narrower layouts
 * reduces to a shift up. */
static INLINE blit_pixel_t blit_pixel_argb(unsigned a, unsigned r,
      unsigned g, unsigned b)
{
   return ((blit_pixel_t)(a & 0xffu) << BLIT_PIXEL_ALPHA_SHIFT)
        | ((blit_pixel_t)(r & 0xffu) << BLIT_PIXEL_RED_SHIFT)
        | ((blit_pixel_t)(g & 0xffu) << BLIT_PIXEL_GREEN_SHIFT)
        | ((blit_pixel_t)(b & 0xffu) << BLIT_PIXEL_BLUE_SHIFT);
}

/* Overwrite *dst with src if src is not fully transparent. This is the
 * whole of the sprite blit: the game's art is 1-bit alpha, so there is
 * no blending to do on the way to the framebuffer. */
static INLINE void blit_pixel_set_if_alpha(blit_pixel_t *dst,
      blit_pixel_t src)
{
   if (src & BLIT_PIXEL_ALPHA_MASK)
      *dst = src;
}

static INLINE void blit_pixel_set_line_if_alpha(blit_pixel_t *dst,
      const blit_pixel_t *src, size_t pixels)
{
   size_t x;
   for (x = 0; x < pixels; x++)
      blit_pixel_set_if_alpha(&dst[x], src[x]);
}

/* Average two pixels per channel, rounding up. Alpha is not carried:
 * the only caller is the level-preview downscale, which sets alpha to
 * opaque itself once the four samples are combined. */
static INLINE blit_pixel_t blit_pixel_blend(blit_pixel_t a, blit_pixel_t b)
{
   unsigned r = (((a >> BLIT_PIXEL_RED_SHIFT)   & 0xffu)
               + ((b >> BLIT_PIXEL_RED_SHIFT)   & 0xffu) + 1u) >> 1;
   unsigned g = (((a >> BLIT_PIXEL_GREEN_SHIFT) & 0xffu)
               + ((b >> BLIT_PIXEL_GREEN_SHIFT) & 0xffu) + 1u) >> 1;
   unsigned bl= (((a >> BLIT_PIXEL_BLUE_SHIFT)  & 0xffu)
               + ((b >> BLIT_PIXEL_BLUE_SHIFT)  & 0xffu) + 1u) >> 1;

   return ((blit_pixel_t)r  << BLIT_PIXEL_RED_SHIFT)
        | ((blit_pixel_t)g  << BLIT_PIXEL_GREEN_SHIFT)
        | ((blit_pixel_t)bl << BLIT_PIXEL_BLUE_SHIFT);
}

#ifdef __cplusplus
}
#endif

#endif
