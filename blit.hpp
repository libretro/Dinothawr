#ifndef BLIT_HPP__
#define BLIT_HPP__

#include "blit_pixel.h"
#include "blit_geom.h"
#include <algorithm>
#include <stdint.h>
#include <vector>

#include <climits>

#if defined(__SSE2__) && defined(USE_SIMD)
#include <emmintrin.h>
#endif

/* Operator sugar over the C aggregates in blit_geom.h. These hold no
 * state, add no members and change no layout - a caller that converts to
 * C drops the sugar and calls the blit_pos_ and blit_rect_ functions
 * directly, and the whole block goes once the last C++ caller does.
 *
 * Global scope, not namespace Blit: the types themselves are declared at
 * global scope by a C header, so this is where argument-dependent lookup
 * goes looking - std::set<Pos> in tilemap.hpp needs to find operator<. */
inline blit_pos_t& operator+=(blit_pos_t& a, blit_pos_t b) { a = blit_pos_add(a, b); return a; }
inline blit_pos_t& operator-=(blit_pos_t& a, blit_pos_t b) { a = blit_pos_sub(a, b); return a; }
inline blit_pos_t& operator*=(blit_pos_t& a, blit_pos_t b) { a = blit_pos_mul(a, b); return a; }
inline blit_pos_t& operator/=(blit_pos_t& a, int d)        { a = blit_pos_div(a, d); return a; }

inline blit_pos_t  operator+ (blit_pos_t a, blit_pos_t b)  { return blit_pos_add(a, b); }
inline blit_pos_t  operator- (blit_pos_t a, blit_pos_t b)  { return blit_pos_sub(a, b); }
inline blit_pos_t  operator* (blit_pos_t a, blit_pos_t b)  { return blit_pos_mul(a, b); }
inline blit_pos_t  operator/ (blit_pos_t a, int d)         { return blit_pos_div(a, d); }
inline blit_pos_t  operator- (blit_pos_t a)                { return blit_pos_neg(a); }
inline blit_pos_t  operator* (int s, blit_pos_t a)         { return blit_pos_scale(s, a); }

inline bool operator==(blit_pos_t a, blit_pos_t b) { return blit_pos_equal(a, b) != 0; }
inline bool operator!=(blit_pos_t a, blit_pos_t b) { return blit_pos_equal(a, b) == 0; }
/* Ordering exists so a position can key a std::set; see blit_pos_less. */
inline bool operator< (blit_pos_t a, blit_pos_t b) { return blit_pos_less(a, b) != 0; }

inline blit_rect_t& operator+=(blit_rect_t& r, blit_pos_t p) { r = blit_rect_offset(r, p); return r; }
inline blit_rect_t& operator-=(blit_rect_t& r, blit_pos_t p) { r = blit_rect_offset(r, blit_pos_neg(p)); return r; }
inline blit_rect_t  operator+ (blit_rect_t r, blit_pos_t p)  { return blit_rect_offset(r, p); }
inline blit_rect_t  operator- (blit_rect_t r, blit_pos_t p)  { return blit_rect_offset(r, blit_pos_neg(p)); }

/* Intersection. */
inline blit_rect_t  operator& (blit_rect_t a, blit_rect_t b)  { return blit_rect_intersect(a, b); }
inline blit_rect_t& operator&=(blit_rect_t& a, blit_rect_t b) { a = blit_rect_intersect(a, b); return a; }

namespace Blit
{
   /* The pixel format and its operations live in blit_pixel.h as plain
    * C. Pixel stays as the name the engine uses for it. */
   typedef blit_pixel_t Pixel;

   typedef blit_pos_t  Pos;
   typedef blit_rect_t Rect;

}

#endif

