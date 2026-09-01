/* Dinothawr - a drawable surface.
 *
 * Everything the blit path reads: the pixel data of the face currently
 * showing, the face table it came from, the tile's attributes, where the
 * surface sits and whether it follows the camera. All three tables are
 * shared and reference counted, so a surface is seven fields wide and
 * copying one is three counter increments.
 *
 * This has value semantics without a copy constructor to enforce them:
 * blit_surface_retain and blit_surface_release are the copy and destroy
 * steps, and a caller that duplicates the struct must call retain on the
 * copy. Blit::Surface in surface.hpp is a thin C++ shim that does that
 * automatically for the engine code that is still C++; it holds one of
 * these and forwards to the functions here.
 *
 * MSVC C89.
 */

#ifndef BLIT_SURFACE_H__
#define BLIT_SURFACE_H__

#include "blit_pixel.h"
#include "blit_geom.h"
#include "blit_surface_data.h"
#include "blit_alt_table.h"
#include "blit_attr_table.h"

#include <retro_inline.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
   blit_surface_data_t *data;
   blit_alt_table_t    *alts;
   blit_attr_table_t   *attribs;

   /* Points into alts' own storage, valid while this surface holds its
    * reference. NULL when no face is selected. */
   const char          *active_alt;
   unsigned             active_alt_index;

   blit_rect_t          rect;
   int                  ignore_camera;
} blit_surface_t;

/* An empty surface: no pixels, no tables, zero rect. */
void blit_surface_init(blit_surface_t *surf);

/* A surface over @data, sized to it. Takes its own reference. */
void blit_surface_init_data(blit_surface_t *surf,
      blit_surface_data_t *data);

/* A w*h surface filled with one colour. Non-zero on success. */
int blit_surface_init_filled(blit_surface_t *surf, blit_pixel_t fill,
      int w, int h);

/* Retain every table this surface points at. Call on a struct that was
 * duplicated by assignment, so the copy owns its own references. */
void blit_surface_retain(const blit_surface_t *surf);

/* Release every table and leave the struct empty. */
void blit_surface_release(blit_surface_t *surf);

/* The pixel at @pos in surface coordinates, or NULL when outside. The
 * caller decides what an out-of-range read means; this does not throw
 * and does not report why.
 *
 * Inline: the blit calls it once per row block, and it is a subtract,
 * two compares and an index. */
static INLINE const blit_pixel_t *blit_surface_pixel_raw(
      const blit_surface_t *surf, blit_pos_t pos)
{
   int x;
   int y;

   if (!surf->data)
      return NULL;

   x = pos.x - surf->rect.pos.x;
   y = pos.y - surf->rect.pos.y;

   if (x < 0 || y < 0 || x >= surf->data->w || y >= surf->data->h)
      return NULL;

   return &surf->data->pixels[y * surf->data->w + x];
}

/* The pixel at @pos, or 0 when outside. */
static INLINE blit_pixel_t blit_surface_pixel(const blit_surface_t *surf,
      blit_pos_t pos)
{
   const blit_pixel_t *pixel = blit_surface_pixel_raw(surf, pos);
   return pixel ? *pixel : (blit_pixel_t)0;
}

/* Select the @index'th face carrying @id. Returns non-zero on success,
 * zero when no such face exists. */
int blit_surface_set_active_alt(blit_surface_t *surf, const char *id,
      unsigned index);

/* Insert or replace an attribute, cloning a shared table first so the
 * write cannot reach another surface's view. Non-zero on success. */
int blit_surface_set_attr(blit_surface_t *surf, const char *key,
      const char *value);

/* Replace the pixels with a mask of @colour: every pixel that had alpha
 * becomes @colour, the rest become transparent. The surface takes sole
 * ownership of the result. Non-zero on success. */
int blit_surface_refill_color(blit_surface_t *surf, blit_pixel_t colour);

#ifdef __cplusplus
}
#endif

#endif
