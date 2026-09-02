/* Dinothawr - bitmap fonts.
 *
 * A font is a grid of fixed-size glyphs sliced out of one image, indexed
 * by character. A cluster is a set of fonts under a name, drawn one on
 * top of another at their own offsets - that is how the game's text gets
 * its drop shadow, and why "yellow" is two fonts rather than one.
 *
 * Rendering is a blit per character with no kerning and no measurement
 * beyond the glyph grid, which is all a fixed-width bitmap font needs.
 *
 * MSVC C89.
 */

#ifndef BLIT_FONT_H__
#define BLIT_FONT_H__

#include "blit_surface.h"
#include "blit_render_target.h"

#ifdef __cplusplus
extern "C" {
#endif

enum blit_font_align
{
   BLIT_FONT_LEFT = 0,
   BLIT_FONT_RIGHT,
   BLIT_FONT_CENTERED
};

typedef struct
{
   /* Indexed by character rather than keyed by one: at most 256 glyphs,
    * the lookup runs per character rendered, and the present flags say
    * which slots a font actually filled. */
   blit_surface_t glyphs[256];
   char           present[256];
   int            glyph_w;
   int            glyph_h;
} blit_font_t;

typedef struct blit_font_cluster blit_font_cluster_t;

void blit_font_init(blit_font_t *font);

/* Loads the .font at @path and slices its glyphs. Non-zero on success;
 * on failure the font is left empty and @error, if given, receives a
 * message. */
int blit_font_load(blit_font_t *font, const char *path, char *error,
      size_t error_len);

void blit_font_release(blit_font_t *font);

/* Replaces every glyph's pixels with @colour, keeping their shape. */
int blit_font_set_color(blit_font_t *font, blit_pixel_t colour);

/* Draws @msg at (@x, @y), breaking lines on '\n' and advancing by the
 * glyph height plus @newline_offset. Characters the font has no glyph
 * for are skipped. */
void blit_font_render(const blit_font_t *font,
      blit_render_target_t *target, const char *msg, int x, int y,
      enum blit_font_align align, int newline_offset);

/* ---- Cluster -------------------------------------------------------- */

blit_font_cluster_t *blit_font_cluster_new(void);
void blit_font_cluster_free(blit_font_cluster_t *cluster);

/* Adds a font under @id, tinted @colour and drawn at @offset. Fonts
 * under one id are drawn in the order they were added, so a shadow is
 * added before the face that sits on it. Non-zero on success. */
int blit_font_cluster_add(blit_font_cluster_t *cluster, const char *id,
      const char *path, blit_pos_t offset, blit_pixel_t colour,
      char *error, size_t error_len);

/* Selects which id subsequent renders use. */
void blit_font_cluster_set_id(blit_font_cluster_t *cluster,
      const char *id);

/* The largest glyph size among the selected id's fonts. */
blit_pos_t blit_font_cluster_glyph_size(
      const blit_font_cluster_t *cluster);

void blit_font_cluster_render(const blit_font_cluster_t *cluster,
      blit_render_target_t *target, const char *msg, int x, int y,
      enum blit_font_align align, int newline_offset);

#ifdef __cplusplus
}
#endif

#endif
