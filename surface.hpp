#ifndef SURFACE_HPP__
#define SURFACE_HPP__

#include "blit.hpp"

#include <memory>
#include <vector>
#include <map>
#include <functional>
#include <utility>

namespace Blit
{
   class Surface
   {
      public:
         struct Data
         {
            Data(std::vector<Pixel> pixels, int w, int h);
            Data(Pixel pixel, int w, int h);

            std::vector<Pixel> pixels;
            int w, h;
         };

         struct Alt
         {
            std::shared_ptr<const Data> data;
            std::string tag; 
         };

         Surface();
         Surface(Pixel pix, int width, int height);
         Surface(std::shared_ptr<const Data> data);
         Surface(const std::vector<Alt>& alts, const std::string& start_id);

         Surface sub(Rect rect) const;
         void refill_color(Pixel pix);

         Rect& rect() { return m_rect; }
         const Rect& rect() const { return m_rect; }

         void ignore_camera(bool ignore);
         bool ignore_camera() const;

         Pixel pixel(Pos pos) const;
         const Pixel* pixel_raw(Pos pos) const;

         std::pair<std::string, unsigned> active_alt() const { return std::pair<std::string, unsigned>(m_active_alt, m_active_alt_index); }
         void active_alt(const std::string& id, unsigned index = 0);
         void active_alt_index(unsigned index);

         std::map<std::string, std::string>& attr() { return attribs; }
         const std::map<std::string, std::string>& attr() const { return attribs; }

      private:
         std::shared_ptr<const Data> data;

         std::multimap<std::string, std::shared_ptr<const Data>> alts;
         std::string m_active_alt;
         unsigned m_active_alt_index;

         std::map<std::string, std::string> attribs;
         Rect m_rect;
         bool m_ignore_camera;
   };

   class RenderTarget;

   class Renderable
   {
      public:
         /* Polymorphic base: Level and SurfaceCluster derive from it and are
          * held in containers that destroy through this type. */
         virtual ~Renderable() {}

         virtual void render(RenderTarget& target) const = 0;
         virtual Pos pos() const { return position; }
         virtual void pos(Pos position) { this->position = position; }

         void move(Pos offset) { pos(pos() + offset); }

      protected:
         Pos position;
   };

   class SurfaceCluster : public Renderable
   {
      public:
         struct Elem
         {
            Surface surf;
            Pos offset;
            unsigned tag;
         };

         SurfaceCluster()
         {
         }

         std::vector<Elem>& vec();
         const std::vector<Elem>& vec() const;

         void set_transform(std::function<Pos (Pos)> func);
         void render(RenderTarget& target) const;

      private:
         std::vector<Elem> elems;
         std::function<Pos (Pos)> func;
   };

   class SurfaceCache
   {
      public:
         Surface from_image(const std::string& path);
         Surface from_sprite(const std::string& path);

      private:
         std::map<std::string, std::shared_ptr<const Surface::Data>> cache;
         /* from_sprite's XML parse, kept alongside the pixel cache: the
          * faces it names were already shared, but the .sprite document
          * behind them was re-read and re-parsed on every call - 50 times
          * each for dino.sprite and frozen.sprite over a session. */
         struct SpriteDef
         {
            std::vector<Surface::Alt> alts;
            std::string start_id;
         };
         std::map<std::string, SpriteDef> sprites;
         std::shared_ptr<const Surface::Data> load_image(const std::string& path);
         /* Face inside an APNG: decodes the whole animation on the first
          * request for any of its frames and fills one cache entry per
          * frame under "path#N" keys, so the file is read and decoded
          * once however many faces reference it. */
         std::shared_ptr<const Surface::Data> load_apng_frame(
               const std::string& path, unsigned frame);
   };

   /* One cache for the whole session.
    *
    * Every user of this used to hold its own: a local inside the tileset
    * and glyph loaders (destroyed at the end of the function, so those
    * cached nothing at all), and a member on each Game, which is
    * constructed per level.  The tilesets and the dino sprites are shared
    * across levels, so they were re-read and re-decoded once per level -
    * about 2100 file opens over a session where 57 files exist.
    *
    * Sharing is safe because Surface::Data is immutable once cached:
    * Surface::pixel_raw is a const accessor, and refill_color replaces
    * the shared_ptr with a fresh Data rather than writing through it.
    * The only mutable pixel buffer is RenderTarget's, which is its own.
    * All loading happens on the thread that calls retro_load_game and
    * retro_run - the core's only worker threads decode audio. */
   SurfaceCache& surface_cache();

   class RenderTarget
   {
      public:
         RenderTarget()
         {
         }

         RenderTarget(int width, int height);

         Surface convert_surface();

         const Pixel* buffer() const;
         Pixel* pixel_raw(Pos pos);
         Pixel* pixel_raw_no_offset(Pos pos);

         int width() const;
         int height() const;

         void clear(Pixel pix);

         void camera_move(Pos pos);
         void camera_set(Pos pos);
         Pos camera_pos() const;

         void blit(const Surface& surf, Rect subrect);
         void blit_offset(const Surface& surf, Rect subrect, Pos offset);

      private:
         std::vector<Pixel> m_buffer;
         Rect rect;
   };
}

#endif

