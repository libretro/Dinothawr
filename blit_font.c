/* Dinothawr - bitmap fonts (implementation).
 * MSVC C89. See blit_font.h. */

#include "blit_font.h"

#include <stdio.h>

/* snprintf is C99; this is libretro-common's shim for the MSVC
 * versions that lack it. */
#include <compat/msvc.h>
#include <stdlib.h>
#include <string.h>

#include "blit_surface_cache.h"
#include "blit_xml.h"

#define BLIT_FONT_PATH_MAX 512

void blit_font_init(blit_font_t *font)
{
   unsigned i;

   for (i = 0; i < 256; i++)
   {
      blit_surface_init(&font->glyphs[i]);
      font->present[i] = 0;
   }

   font->glyph_w = 0;
   font->glyph_h = 0;
}

void blit_font_release(blit_font_t *font)
{
   unsigned i;

   for (i = 0; i < 256; i++)
      if (font->present[i])
         blit_surface_release(&font->glyphs[i]);

   blit_font_init(font);
}

static void font_fail(char *error, size_t error_len, const char *what,
      const char *path)
{
   if (error && error_len)
      snprintf(error, error_len, "%s: %s", what, path ? path : "");
}

int blit_font_load(blit_font_t *font, const char *path, char *error,
      size_t error_len)
{
   rxml_document_t *doc;
   rxml_node_t     *glyphs;
   blit_surface_t   sheet;
   char             dir[BLIT_FONT_PATH_MAX];
   char             source[BLIT_FONT_PATH_MAX];
   int              cols;
   int              rows;
   int              x;
   int              y;
   int              ascii;

   blit_font_init(font);

   if (!(doc = blit_xml_load(path)))
   {
      font_fail(error, error_len, "failed to load font", path);
      return 0;
   }

   glyphs = blit_xml_child(blit_xml_root(doc, "font"), "glyphs");

   cols          = blit_xml_attr_int(glyphs, "width");
   rows          = blit_xml_attr_int(glyphs, "height");
   font->glyph_w = blit_xml_attr_int(glyphs, "glyphwidth");
   font->glyph_h = blit_xml_attr_int(glyphs, "glyphheight");
   ascii         = blit_xml_attr_int(glyphs, "startascii");

   {
      const char *slash = strrchr(path, '/');
      size_t      n     = slash ? (size_t)(slash - path) : 0;

      if (n >= sizeof(dir))
         n = sizeof(dir) - 1;
      memcpy(dir, path, n);
      dir[n] = '\0';
      if (!slash)
         strcpy(dir, ".");
   }

   snprintf(source, sizeof(source), "%s/%s", dir,
         blit_xml_attr(glyphs, "source"));

   rxml_free_document(doc);

   if (cols <= 0 || rows <= 0 || font->glyph_w <= 0 || font->glyph_h <= 0)
   {
      font_fail(error, error_len, "invalid glyph geometry", path);
      return 0;
   }

   if (!blit_surface_cache_image(blit_surface_cache(), source, &sheet))
   {
      font_fail(error, error_len, "failed to load font sheet", source);
      return 0;
   }

   if (     sheet.rect.w != cols * font->glyph_w
         || sheet.rect.h != rows * font->glyph_h)
   {
      blit_surface_release(&sheet);
      font_fail(error, error_len,
            "font sheet does not match its glyph grid", source);
      return 0;
   }

   for (y = 0; y < rows; y++)
   {
      for (x = 0; x < cols; x++, ascii++)
      {
         unsigned char slot = (unsigned char)ascii;

         /* blit_surface_sub hands over ownership. */
         font->glyphs[slot] = blit_surface_sub(&sheet,
               blit_rect(blit_pos(x * font->glyph_w, y * font->glyph_h),
                  font->glyph_w, font->glyph_h));
         font->glyphs[slot].ignore_camera = 1;
         font->present[slot] = 1;
      }
   }

   blit_surface_release(&sheet);
   return 1;
}

int blit_font_set_color(blit_font_t *font, blit_pixel_t colour)
{
   unsigned i;

   for (i = 0; i < 256; i++)
      if (font->present[i]
            && !blit_surface_refill_color(&font->glyphs[i], colour))
         return 0;

   return 1;
}

/* Where a line starts, given its alignment. */
static int font_align_offset(const blit_font_t *font, size_t chars,
      enum blit_font_align align)
{
   if (align == BLIT_FONT_RIGHT)
      return font->glyph_w * (int)chars;
   if (align == BLIT_FONT_CENTERED)
      return font->glyph_w * (int)chars / 2;
   return 0;
}

void blit_font_render(const blit_font_t *font,
      blit_render_target_t *target, const char *msg, int x, int y,
      enum blit_font_align align, int newline_offset)
{
   const char *line = msg;

   if (!msg)
      return;

   /* Walk the message a line at a time rather than splitting it into a
    * list first: the line is only needed for its length and its bytes. */
   for (;;)
   {
      const char *end = strchr(line, '\n');
      size_t      len = end ? (size_t)(end - line) : strlen(line);
      int         pen = x - font_align_offset(font, len, align);
      size_t      i;

      for (i = 0; i < len; i++, pen += font->glyph_w)
      {
         unsigned char c = (unsigned char)line[i];

         if (font->present[c])
            blit_render_target_blit_offset(target, &font->glyphs[c],
                  blit_rect_zero(), blit_pos(pen, y));
      }

      y += font->glyph_h + newline_offset;

      if (!end)
         break;
      line = end + 1;
   }
}

/* ---- Cluster -------------------------------------------------------- */

typedef struct
{
   char        *id;
   blit_font_t  font;
   blit_pos_t   offset;
} blit_cluster_font_t;

struct blit_font_cluster
{
   blit_cluster_font_t *fonts;
   size_t               count;
   size_t               capacity;
   char                *current_id;
};

blit_font_cluster_t *blit_font_cluster_new(void)
{
   blit_font_cluster_t *cluster =
      (blit_font_cluster_t*)calloc(1, sizeof(*cluster));
   return cluster;
}

void blit_font_cluster_free(blit_font_cluster_t *cluster)
{
   size_t i;

   if (!cluster)
      return;

   for (i = 0; i < cluster->count; i++)
   {
      blit_font_release(&cluster->fonts[i].font);
      free(cluster->fonts[i].id);
   }

   free(cluster->fonts);
   free(cluster->current_id);
   free(cluster);
}

static char *cluster_strdup(const char *s)
{
   size_t len;
   char  *out;

   if (!s)
      s = "";

   len = strlen(s);
   if (!(out = (char*)malloc(len + 1)))
      return NULL;
   memcpy(out, s, len + 1);
   return out;
}

int blit_font_cluster_add(blit_font_cluster_t *cluster, const char *id,
      const char *path, blit_pos_t offset, blit_pixel_t colour,
      char *error, size_t error_len)
{
   blit_cluster_font_t *slot;

   if (!cluster)
      return 0;

   if (cluster->count == cluster->capacity)
   {
      size_t               new_cap = cluster->capacity
         ? (cluster->capacity * 2) : 8;
      blit_cluster_font_t *grown   = (blit_cluster_font_t*)realloc(
            cluster->fonts, new_cap * sizeof(*grown));

      if (!grown)
         return 0;

      cluster->fonts    = grown;
      cluster->capacity = new_cap;
   }

   slot = &cluster->fonts[cluster->count];

   if (!blit_font_load(&slot->font, path, error, error_len))
      return 0;

   if (!blit_font_set_color(&slot->font, colour))
   {
      blit_font_release(&slot->font);
      return 0;
   }

   if (!(slot->id = cluster_strdup(id)))
   {
      blit_font_release(&slot->font);
      return 0;
   }

   slot->offset = offset;
   cluster->count++;
   return 1;
}

void blit_font_cluster_set_id(blit_font_cluster_t *cluster,
      const char *id)
{
   char *dup;

   if (!cluster || !(dup = cluster_strdup(id)))
      return;

   free(cluster->current_id);
   cluster->current_id = dup;
}

static int cluster_selected(const blit_font_cluster_t *cluster, size_t i)
{
   const char *want = cluster->current_id ? cluster->current_id : "";
   return strcmp(cluster->fonts[i].id, want) == 0;
}

blit_pos_t blit_font_cluster_glyph_size(
      const blit_font_cluster_t *cluster)
{
   blit_pos_t size = blit_pos_zero();
   size_t     i;

   if (!cluster)
      return size;

   for (i = 0; i < cluster->count; i++)
   {
      if (!cluster_selected(cluster, i))
         continue;
      if (cluster->fonts[i].font.glyph_w > size.x)
         size.x = cluster->fonts[i].font.glyph_w;
      if (cluster->fonts[i].font.glyph_h > size.y)
         size.y = cluster->fonts[i].font.glyph_h;
   }

   return size;
}

void blit_font_cluster_render(const blit_font_cluster_t *cluster,
      blit_render_target_t *target, const char *msg, int x, int y,
      enum blit_font_align align, int newline_offset)
{
   size_t i;

   if (!cluster)
      return;

   /* In the order they were added: a shadow is added before the face
    * that sits on it. */
   for (i = 0; i < cluster->count; i++)
      if (cluster_selected(cluster, i))
         blit_font_render(&cluster->fonts[i].font, target, msg,
               x + cluster->fonts[i].offset.x,
               y + cluster->fonts[i].offset.y, align, newline_offset);
}
