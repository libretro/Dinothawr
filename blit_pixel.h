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

/* SSE2 is baseline on x86-64 and on any i686 build that enables it;
 * NEON likewise on aarch64. Both are compile-time here - no runtime
 * dispatch - so a target without either simply takes the scalar loop. */
#ifndef BLIT_PIXEL_NO_SIMD
#if defined(__SSE2__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2) || defined(_M_X64) || defined(_M_AMD64)
#include <emmintrin.h>
#define BLIT_PIXEL_SSE2 1
#elif defined(__aarch64__) || defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define BLIT_PIXEL_NEON 1
#endif
#endif

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

/* The sprite blit's inner loop, and the hottest code in the renderer.
 *
 * The scalar form branches once per pixel on the alpha test, which the
 * predictor gets wrong at every edge of every sprite. The vector forms
 * do the same work as a masked select over four pixels at a time, with
 * no branch and no data-dependent timing: they write dst unconditionally
 * (storing back what was already there where the source is transparent),
 * which is sound because dst is the render target's own buffer and src
 * is immutable surface data, so the two never alias. */
static INLINE void blit_pixel_set_line_if_alpha(blit_pixel_t *dst,
      const blit_pixel_t *src, size_t pixels)
{
   size_t x = 0;

#if defined(BLIT_PIXEL_SSE2)
   {
      const __m128i alpha = _mm_set1_epi32((int)BLIT_PIXEL_ALPHA_MASK);
      const __m128i zero  = _mm_setzero_si128();

      for (; x + 4 <= pixels; x += 4)
      {
         __m128i s    = _mm_loadu_si128((const __m128i*)(src + x));
         __m128i d    = _mm_loadu_si128((const __m128i*)(dst + x));
         /* All-ones lanes where the source is fully transparent, i.e.
          * where the destination is kept. */
         __m128i keep = _mm_cmpeq_epi32(_mm_and_si128(s, alpha), zero);

         _mm_storeu_si128((__m128i*)(dst + x),
               _mm_or_si128(_mm_and_si128(keep, d),
                            _mm_andnot_si128(keep, s)));
      }
   }
#elif defined(BLIT_PIXEL_NEON)
   {
      const uint32x4_t alpha = vdupq_n_u32(BLIT_PIXEL_ALPHA_MASK);

      for (; x + 4 <= pixels; x += 4)
      {
         uint32x4_t s    = vld1q_u32(src + x);
         uint32x4_t d    = vld1q_u32(dst + x);
         /* All-ones lanes where the source has any alpha bit set. */
         uint32x4_t take = vtstq_u32(s, alpha);

         vst1q_u32(dst + x, vbslq_u32(take, s, d));
      }
   }
#endif

   for (; x < pixels; x++)
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
