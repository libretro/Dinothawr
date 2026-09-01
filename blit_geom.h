/* Dinothawr - integer geometry.
 *
 * A point and an axis-aligned rectangle, in whole pixels. These are the
 * currency of the whole blit path: a Surface carries a rect, a
 * RenderTarget blits into one, and the camera is a Pos.
 *
 * MSVC C89: C comments only, declarations at the top of each block, no
 * C99/C11 features. Header-only - every operation here is a few
 * arithmetic ops and wants to inline into the blit loops.
 *
 * Both types are plain aggregates, so C++ callers can still brace-init
 * them and pass them by value; blit.hpp layers the operator sugar the
 * remaining C++ engine code uses on top, without touching the layout.
 */

#ifndef BLIT_GEOM_H__
#define BLIT_GEOM_H__

#include <stdint.h>

#include <retro_inline.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
   int x;
   int y;
} blit_pos_t;

typedef struct
{
   blit_pos_t pos;
   int        w;
   int        h;
} blit_rect_t;

static INLINE blit_pos_t blit_pos(int x, int y)
{
   blit_pos_t p;
   p.x = x;
   p.y = y;
   return p;
}

static INLINE blit_pos_t blit_pos_zero(void)
{
   return blit_pos(0, 0);
}

static INLINE blit_pos_t blit_pos_add(blit_pos_t a, blit_pos_t b)
{
   return blit_pos(a.x + b.x, a.y + b.y);
}

static INLINE blit_pos_t blit_pos_sub(blit_pos_t a, blit_pos_t b)
{
   return blit_pos(a.x - b.x, a.y - b.y);
}

static INLINE blit_pos_t blit_pos_mul(blit_pos_t a, blit_pos_t b)
{
   return blit_pos(a.x * b.x, a.y * b.y);
}

static INLINE blit_pos_t blit_pos_scale(int s, blit_pos_t a)
{
   return blit_pos(s * a.x, s * a.y);
}

static INLINE blit_pos_t blit_pos_div(blit_pos_t a, int d)
{
   return blit_pos(a.x / d, a.y / d);
}

static INLINE blit_pos_t blit_pos_neg(blit_pos_t a)
{
   return blit_pos(-a.x, -a.y);
}

static INLINE int blit_pos_equal(blit_pos_t a, blit_pos_t b)
{
   return a.x == b.x && a.y == b.y;
}

/* Total order over positions, so a Pos can key an ordered container.
 * Packs the pair into one 64-bit value the way the C++ comparator did,
 * with x above y; the casts through unsigned keep negative coordinates
 * ordering consistently rather than by sign-extended bit pattern. */
static INLINE int blit_pos_less(blit_pos_t a, blit_pos_t b)
{
   uint64_t lhs = ((uint64_t)(uint32_t)a.x << 32) | (uint32_t)a.y;
   uint64_t rhs = ((uint64_t)(uint32_t)b.x << 32) | (uint32_t)b.y;
   return lhs < rhs;
}

static INLINE blit_rect_t blit_rect(blit_pos_t pos, int w, int h)
{
   blit_rect_t r;
   r.pos = pos;
   r.w   = w;
   r.h   = h;
   return r;
}

static INLINE blit_rect_t blit_rect_wh(int w, int h)
{
   return blit_rect(blit_pos_zero(), w, h);
}

static INLINE blit_rect_t blit_rect_zero(void)
{
   return blit_rect(blit_pos_zero(), 0, 0);
}

static INLINE blit_rect_t blit_rect_offset(blit_rect_t r, blit_pos_t off)
{
   return blit_rect(blit_pos_add(r.pos, off), r.w, r.h);
}

/* Non-empty, i.e. worth blitting. */
static INLINE int blit_rect_valid(blit_rect_t r)
{
   return r.w > 0 && r.h > 0;
}

/* Intersection, or an empty rect when they do not overlap. */
static INLINE blit_rect_t blit_rect_intersect(blit_rect_t a, blit_rect_t b)
{
   int x_left   = (a.pos.x > b.pos.x) ? a.pos.x : b.pos.x;
   int a_right  = a.pos.x + a.w;
   int b_right  = b.pos.x + b.w;
   int x_right  = (a_right < b_right) ? a_right : b_right;
   int width    = x_right - x_left;

   int y_top    = (a.pos.y > b.pos.y) ? a.pos.y : b.pos.y;
   int a_bottom = a.pos.y + a.h;
   int b_bottom = b.pos.y + b.h;
   int y_bottom = (a_bottom < b_bottom) ? a_bottom : b_bottom;
   int height   = y_bottom - y_top;

   if (width <= 0 || height <= 0)
      return blit_rect_zero();

   return blit_rect(blit_pos(x_left, y_top), width, height);
}

#ifdef __cplusplus
}
#endif

#endif
