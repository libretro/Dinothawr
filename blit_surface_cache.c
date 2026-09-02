/* Dinothawr - the session surface cache (implementation).
 * MSVC C89. See blit_surface_cache.h. */

#include "blit_surface_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blit_alt_table.h"
#include "blit_str_map.h"
#include "blit_xml.h"
#include "rpng_front.h"

#define BLIT_CACHE_PATH_MAX 512
#define BLIT_CACHE_ERROR_MAX 256

/* A parsed .sprite: its face table and the face it starts on. The cache
 * owns one reference on the table and the string. */
typedef struct
{
   blit_alt_table_t *alts;
   char             *start_id;
} blit_sprite_def_t;

struct blit_surface_cache
{
   blit_str_map_t *images;  /* path (or "path#frame") -> surface data */
   blit_str_map_t *sprites; /* path -> blit_sprite_def_t              */
   char            error[BLIT_CACHE_ERROR_MAX];
};

static blit_surface_cache_t *g_cache;

static void cache_fail(blit_surface_cache_t *cache, const char *what,
      const char *path)
{
   snprintf(cache->error, sizeof(cache->error), "%s: %s", what,
         path ? path : "");
}

/* Directory part of @path, or "." when it has none. */
static void cache_basedir(const char *path, char *out, size_t len)
{
   const char *slash = strrchr(path, '/');
   size_t      n;

   if (!slash)
   {
      out[0] = '.';
      out[1] = '\0';
      return;
   }

   n = (size_t)(slash - path);
   if (n >= len)
      n = len - 1;

   memcpy(out, path, n);
   out[n] = '\0';
}

blit_surface_cache_t *blit_surface_cache(void)
{
   if (g_cache)
      return g_cache;

   if (!(g_cache = (blit_surface_cache_t*)calloc(1, sizeof(*g_cache))))
      return NULL;

   g_cache->images  = blit_str_map_new();
   g_cache->sprites = blit_str_map_new();

   if (!g_cache->images || !g_cache->sprites)
   {
      blit_surface_cache_free();
      return NULL;
   }

   return g_cache;
}

void blit_surface_cache_free(void)
{
   size_t i;

   if (!g_cache)
      return;

   for (i = 0; i < blit_str_map_count(g_cache->images); i++)
      blit_surface_data_unref((blit_surface_data_t*)
            blit_str_map_value_at(g_cache->images, i));

   for (i = 0; i < blit_str_map_count(g_cache->sprites); i++)
   {
      blit_sprite_def_t *def = (blit_sprite_def_t*)
         blit_str_map_value_at(g_cache->sprites, i);

      blit_alt_table_unref(def->alts);
      free(def->start_id);
      free(def);
   }

   blit_str_map_free(g_cache->images);
   blit_str_map_free(g_cache->sprites);
   free(g_cache);
   g_cache = NULL;
}

const char *blit_surface_cache_error(const blit_surface_cache_t *cache)
{
   return cache ? cache->error : "";
}

/* Decode an ARGB image into surface data. Takes ownership of nothing;
 * frees @argb itself. */
static blit_surface_data_t *cache_data_from_argb(uint32_t *argb,
      unsigned width, unsigned height)
{
   blit_pixel_t *pixels;
   size_t        count = (size_t)width * (size_t)height;
   size_t        i;

   if (!(pixels = (blit_pixel_t*)malloc((count ? count : 1)
               * sizeof(*pixels))))
      return NULL;

   for (i = 0; i < count; i++)
      pixels[i] = blit_pixel_argb(
            (unsigned)(argb[i] >> 24) & 0xffu,
            (unsigned)(argb[i] >> 16) & 0xffu,
            (unsigned)(argb[i] >>  8) & 0xffu,
            (unsigned)(argb[i] >>  0) & 0xffu);

   return blit_surface_data_new(pixels, (int)width, (int)height);
}

static blit_surface_data_t *cache_load_image(blit_surface_cache_t *cache,
      const char *path)
{
   blit_surface_data_t *data;
   uint32_t            *image  = NULL;
   unsigned             width  = 0;
   unsigned             height = 0;

   if (!rpng_load_image_argb(path, &image, &width, &height))
   {
      cache_fail(cache, "failed to load image", path);
      return NULL;
   }

   data = cache_data_from_argb(image, width, height);
   free(image);

   if (!data)
      cache_fail(cache, "out of memory decoding image", path);

   return data;
}

/* The image at @path, loading and caching it on first request. */
static blit_surface_data_t *cache_image(blit_surface_cache_t *cache,
      const char *path)
{
   blit_surface_data_t *data = (blit_surface_data_t*)
      blit_str_map_find(cache->images, path);

   if (data)
      return data;

   if (!(data = cache_load_image(cache, path)))
      return NULL;

   if (!blit_str_map_set(cache->images, path, data, NULL, NULL))
   {
      blit_surface_data_unref(data);
      cache_fail(cache, "out of memory caching image", path);
      return NULL;
   }

   return data;
}

static blit_surface_data_t *cache_apng_frame(blit_surface_cache_t *cache,
      const char *path, unsigned frame)
{
   char       key[BLIT_CACHE_PATH_MAX];
   uint32_t **frames = NULL;
   unsigned   width  = 0;
   unsigned   height = 0;
   unsigned   num;
   unsigned   f;

   snprintf(key, sizeof(key), "%s#%u", path, frame);

   {
      blit_surface_data_t *data = (blit_surface_data_t*)
         blit_str_map_find(cache->images, key);
      if (data)
         return data;
   }

   if (!(num = rpng_load_apng_argb(path, &frames, &width, &height)))
   {
      cache_fail(cache, "failed to load animation", path);
      return NULL;
   }

   /* One decode fills every frame's slot: a sprite naming twelve faces
    * from one file reads it once. */
   for (f = 0; f < num; f++)
   {
      blit_surface_data_t *data = cache_data_from_argb(frames[f], width,
            height);
      char frame_key[BLIT_CACHE_PATH_MAX];

      free(frames[f]);

      if (!data)
         continue;

      snprintf(frame_key, sizeof(frame_key), "%s#%u", path, f);
      if (!blit_str_map_set(cache->images, frame_key, data, NULL, NULL))
         blit_surface_data_unref(data);
   }
   free(frames);

   {
      blit_surface_data_t *data = (blit_surface_data_t*)
         blit_str_map_find(cache->images, key);

      if (!data)
         cache_fail(cache, "animation frame out of range", path);

      return data;
   }
}

int blit_surface_cache_image(blit_surface_cache_t *cache,
      const char *path, blit_surface_t *out)
{
   blit_surface_data_t *data;

   blit_surface_init(out);

   if (!cache || !(data = cache_image(cache, path)))
      return 0;

   blit_surface_init_data(out, data);
   return 1;
}

int blit_surface_cache_animation(blit_surface_cache_t *cache,
      const char *path, unsigned frame, blit_surface_t *out)
{
   blit_surface_data_t *data;

   blit_surface_init(out);

   if (!cache || !(data = cache_apng_frame(cache, path, frame)))
      return 0;

   blit_surface_init_data(out, data);
   return 1;
}

/* Parse a .sprite into a face table. */
static blit_sprite_def_t *cache_parse_sprite(blit_surface_cache_t *cache,
      const char *path)
{
   rxml_document_t   *doc;
   rxml_node_t       *sprite;
   rxml_node_t       *face;
   blit_sprite_def_t *def;
   char               basedir[BLIT_CACHE_PATH_MAX];

   if (!(doc = blit_xml_load(path)))
   {
      cache_fail(cache, "failed to load sprite", path);
      return NULL;
   }

   if (!(def = (blit_sprite_def_t*)calloc(1, sizeof(*def)))
         || !(def->alts = blit_alt_table_new()))
   {
      cache_fail(cache, "out of memory parsing sprite", path);
      goto error;
   }

   cache_basedir(path, basedir, sizeof(basedir));
   sprite = blit_xml_root(doc, "sprite");

   for (face = blit_xml_child(sprite, "face"); face;
         face = blit_xml_next_any(face))
   {
      const char          *id    = blit_xml_attr(face, "id");
      const char          *frame = blit_xml_attr(face, "frame");
      blit_surface_data_t *data;
      char                 face_path[BLIT_CACHE_PATH_MAX];

      snprintf(face_path, sizeof(face_path), "%s/%s", basedir,
            blit_xml_attr(face, "source"));

      data = *frame
         ? cache_apng_frame(cache, face_path,
               (unsigned)blit_xml_attr_int(face, "frame"))
         : cache_image(cache, face_path);

      if (!data || !blit_alt_table_add(def->alts, id, data))
         goto error;
   }

   if (!(def->start_id = (char*)malloc(
               strlen(blit_xml_attr(sprite, "start_id")) + 1)))
   {
      cache_fail(cache, "out of memory parsing sprite", path);
      goto error;
   }
   strcpy(def->start_id, blit_xml_attr(sprite, "start_id"));

   if (!blit_str_map_set(cache->sprites, path, def, NULL, NULL))
   {
      cache_fail(cache, "out of memory caching sprite", path);
      goto error;
   }

   rxml_free_document(doc);
   return def;

error:
   if (def)
   {
      blit_alt_table_unref(def->alts);
      free(def->start_id);
      free(def);
   }
   rxml_free_document(doc);
   return NULL;
}

int blit_surface_cache_sprite(blit_surface_cache_t *cache,
      const char *path, blit_surface_t *out)
{
   blit_sprite_def_t *def;

   blit_surface_init(out);

   if (!cache)
      return 0;

   def = (blit_sprite_def_t*)blit_str_map_find(cache->sprites, path);

   if (!def && !(def = cache_parse_sprite(cache, path)))
      return 0;

   if (!blit_surface_init_alts(out, def->alts, def->start_id))
   {
      cache_fail(cache, "sprite has no usable faces", path);
      return 0;
   }

   return 1;
}
