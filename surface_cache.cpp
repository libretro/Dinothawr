#include "surface.hpp"
#include "xml.hpp"
#include "rpng_front.h"
#include <stdexcept>
#include <new>
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
      std::shared_ptr<const Blit::Surface::Data> ptr = cache[path];
      if (ptr)
         return Surface(ptr);
      
      cache[path] = load_image(path);
      return cache[path];
   }

   Surface SurfaceCache::from_animation(const std::string& path, unsigned frame)
   {
      return Surface(load_apng_frame(path, frame));
   }

   Surface SurfaceCache::from_sprite(const std::string& path)
   {
      std::map<std::string, SpriteDef>::iterator cached = sprites.find(path);
      if (cached != sprites.end())
         return Surface(cached->second.alts, cached->second.start_id.c_str());

      {
         Blit::Xml::Document doc;
         if (!doc.load_file(path.c_str()))
            throw std::runtime_error(Utils::join("Failed to load XML sprite: ", path, "."));

         std::basic_string<char> basedir = Utils::basedir(path);
         SpriteDef def;

         Blit::Xml::Node sprite = doc.child("sprite");
         for (Blit::Xml::Node face = sprite.child("face"); face; face = face.next_sibling())
         {
            const char *id    = face.attribute("id").value();
            const char *frame = face.attribute("frame").value();
            std::basic_string<char> path   = Utils::join(basedir, "/", face.attribute("source").value());

            std::shared_ptr<const Blit::Surface::Data> ptr;
            if (*frame)
               ptr = load_apng_frame(path, face.attribute("frame").as_int());
            else
            {
               ptr = cache[path];
               if (!ptr)
               {
                  cache[path] = load_image(path);
                  ptr = cache[path];
               }
            }

            def.alts.push_back(Surface::Alt{ptr, id});
         }

         def.start_id = sprite.attribute("start_id").value();
         sprites[path] = def;
         return Surface(def.alts, def.start_id.c_str());
      }
   }

   std::shared_ptr<const Surface::Data> SurfaceCache::load_apng_frame(
         const std::string& path, unsigned frame)
   {
      std::basic_string<char> key = Utils::join(path, "#", frame);

      std::shared_ptr<const Surface::Data> ptr = cache[key];
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
         std::vector<Pixel> pix(width * height);
         for (unsigned i = 0; i < width * height; i++)
         {
            pix[i] = Pixel::ARGB(
                  uint8_t(frames[f][i] >> 24),
                  uint8_t(frames[f][i] >> 16),
                  uint8_t(frames[f][i] >>  8),
                  uint8_t(frames[f][i] >>  0));
         }

         free(frames[f]);
         cache[Utils::join(path, "#", f)] =
            std::make_shared<Surface::Data>(std::move(pix), width, height);
      }
      free(frames);

      ptr = cache[key];
      if (!ptr)
         throw std::runtime_error(Utils::join("APNG frame out of range: ",
                  path, "#", frame));
      return ptr;
   }

   std::shared_ptr<const Surface::Data> SurfaceCache::load_image(const std::string& path)
   {
      uint32_t *image = NULL;
      unsigned width  = 0;
      unsigned height = 0;
      bool loaded     = rpng_load_image_argb(path.c_str(), &image, &width, &height);

      if (!loaded)
         throw std::runtime_error(Utils::join("RPNG failed to load image: ", path));

      std::vector<Pixel> pix(width * height);
      for (unsigned i = 0; i < width * height; i++)
      {
         pix[i] = Pixel::ARGB(
               uint8_t(image[i] >> 24),
               uint8_t(image[i] >> 16),
               uint8_t(image[i] >>  8),
               uint8_t(image[i] >>  0));
      }

      free(image);
      return std::make_shared<Surface::Data>(std::move(pix), width, height);
   }
}

