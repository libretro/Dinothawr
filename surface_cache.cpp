#include "surface.hpp"
#include "xml.hpp"
#include "rpng_front.h"
#include <stdexcept>
#include <new>
#include <cstring>
#include <cstdlib>


namespace Blit
{
   SurfaceCache& surface_cache()
   {
      static SurfaceCache cache;
      return cache;
   }

   Surface SurfaceCache::from_image(const std::string& path)
   {
      Surface::Data *ptr = (Surface::Data*)blit_str_map_find(cache,
            path.c_str());

      if (!ptr)
      {
         ptr = load_image(path);
         if (!blit_str_map_set(cache, path.c_str(), ptr, NULL, NULL))
            throw std::bad_alloc();
      }

      return Surface(ptr);
   }

   Surface SurfaceCache::from_animation(const std::string& path, unsigned frame)
   {
      return Surface(load_apng_frame(path, frame));
   }

   SurfaceCache::SurfaceCache()
      : cache(blit_str_map_new()), sprites(blit_str_map_new())
   {
      if (!cache || !sprites)
         throw std::bad_alloc();
   }

   /* The cache holds one reference per entry for the whole session; the
    * map owns its keys but not its values, so the values are released
    * here before it goes. */
   SurfaceCache::~SurfaceCache()
   {
      size_t i;

      for (i = 0; i < blit_str_map_count(cache); i++)
         blit_surface_data_unref(
               (Surface::Data*)blit_str_map_value_at(cache, i));

      for (i = 0; i < blit_str_map_count(sprites); i++)
      {
         SpriteDef *def = (SpriteDef*)blit_str_map_value_at(sprites, i);
         blit_alt_table_unref(def->alts);
         free(def->start_id);
         free(def);
      }

      blit_str_map_free(cache);
      blit_str_map_free(sprites);
   }

   Surface SurfaceCache::from_sprite(const std::string& path)
   {
      SpriteDef *cached = (SpriteDef*)blit_str_map_find(sprites,
            path.c_str());

      if (cached)
         return Surface(cached->alts, cached->start_id);


      {
         Blit::Xml::Document doc;
         if (!doc.load_file(path.c_str()))
            throw std::runtime_error(Utils::join("Failed to load XML sprite: ", path, "."));

         std::basic_string<char> basedir = Utils::basedir(path);
         SpriteDef *def = (SpriteDef*)calloc(1, sizeof(*def));

         if (!def)
            throw std::bad_alloc();

         if (!(def->alts = blit_alt_table_new()))
            throw std::bad_alloc();

         Blit::Xml::Node sprite = doc.child("sprite");
         for (Blit::Xml::Node face = sprite.child("face"); face; face = face.next_sibling())
         {
            const char *id    = face.attribute("id").value();
            const char *frame = face.attribute("frame").value();
            std::basic_string<char> path   = Utils::join(basedir, "/", face.attribute("source").value());

            Surface::Data *ptr;
            if (*frame)
               ptr = load_apng_frame(path, face.attribute("frame").as_int());
            else
            {
               ptr = (Surface::Data*)blit_str_map_find(cache, path.c_str());
               if (!ptr)
               {
                  ptr = load_image(path);
                  if (!blit_str_map_set(cache, path.c_str(), ptr, NULL, NULL))
                     throw std::bad_alloc();
               }
            }

            if (!blit_alt_table_add(def->alts, id, ptr))
               throw std::bad_alloc();
         }

         def->start_id = strdup(sprite.attribute("start_id").value());
         if (!def->start_id
               || !blit_str_map_set(sprites, path.c_str(), def, NULL, NULL))
            throw std::bad_alloc();

         return Surface(def->alts, def->start_id);
      }
   }

   Surface::Data *SurfaceCache::load_apng_frame(
         const std::string& path, unsigned frame)
   {
      std::basic_string<char> key = Utils::join(path, "#", frame);

      Surface::Data *ptr = (Surface::Data*)blit_str_map_find(cache,
            key.c_str());
      if (ptr)
         return ptr;

      uint32_t **frames  = NULL;
      unsigned width     = 0;
      unsigned height    = 0;
      unsigned num       = rpng_load_apng_argb(path.c_str(), &frames,
            &width, &height);

      if (!num)
         throw std::runtime_error(Utils::join("RPNG failed to load APNG: ", path));

      for (unsigned f = 0; f < num; f++)
      {
         Pixel *pix = (Pixel*)malloc((size_t)width * height * sizeof(Pixel));
         if (!pix)
            throw std::bad_alloc();

         for (unsigned i = 0; i < width * height; i++)
         {
            pix[i] = blit_pixel_argb(
                  uint8_t(frames[f][i] >> 24),
                  uint8_t(frames[f][i] >> 16),
                  uint8_t(frames[f][i] >>  8),
                  uint8_t(frames[f][i] >>  0));
         }

         free(frames[f]);
         {
            std::string frame_key = Utils::join(path, "#", f);
            if (!blit_str_map_set(cache, frame_key.c_str(),
                     blit_surface_data_new(pix, width, height), NULL, NULL))
               throw std::bad_alloc();
         }
      }
      free(frames);

      ptr = (Surface::Data*)blit_str_map_find(cache, key.c_str());
      if (!ptr)
         throw std::runtime_error(Utils::join("APNG frame out of range: ",
                  path, "#", frame));
      return ptr;
   }

   Surface::Data *SurfaceCache::load_image(const std::string& path)
   {
      uint32_t *image = NULL;
      unsigned width  = 0;
      unsigned height = 0;
      bool loaded     = rpng_load_image_argb(path.c_str(), &image, &width, &height);

      if (!loaded)
         throw std::runtime_error(Utils::join("RPNG failed to load image: ", path));

      Pixel *pix = (Pixel*)malloc((size_t)width * height * sizeof(Pixel));
      if (!pix)
      {
         free(image);
         throw std::bad_alloc();
      }

      for (unsigned i = 0; i < width * height; i++)
      {
         pix[i] = blit_pixel_argb(
               uint8_t(image[i] >> 24),
               uint8_t(image[i] >> 16),
               uint8_t(image[i] >>  8),
               uint8_t(image[i] >>  0));
      }

      free(image);
      return blit_surface_data_new(pix, width, height);
   }
}

