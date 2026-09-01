#ifndef TILEMAP_HPP__
#define TILEMAP_HPP__

#include "surface.hpp"
#include "xml.hpp"

#include <string>
#include <set>
#include <map>

namespace Blit
{
   class Tilemap : public Renderable
   {
      public:
         struct Layer
         {
            SurfaceCluster cluster;
            std::map<std::string, std::string> attr;
            std::string name;
         };

         Tilemap()
         {
         }
         Tilemap(const std::string& path);

         std::vector<Layer>& layers() { return m_layers; }
         const std::vector<Layer>& layers() const { return m_layers; }

         void pos(Pos position);
         void render(RenderTarget& target) const;
         void render_until_layer(unsigned index, RenderTarget& target) const;
         void render_after_layer(unsigned index, RenderTarget& target) const;

         int tile_width() const { return tilewidth; }
         int tile_height() const { return tileheight; }
         int tiles_width() const { return width; }
         int tiles_height() const { return height; }
         int pix_width() const { return width * tilewidth; }
         int pix_height() const { return height * tileheight; }

         /* The raw surface, not the C++ wrapper: callers read its rect
          * and attributes and mutate it in place, and it lives in the
          * cluster's storage. */
         const blit_surface_t* find_tile(unsigned layer, Pos pos) const;
         const blit_surface_t* find_tile(const std::string& name, Pos pos) const;
         blit_surface_t* find_tile(unsigned layer, Pos pos);
         blit_surface_t* find_tile(const std::string& name, Pos pos);
         const Layer* find_layer(const std::string& name) const;
         int find_layer_index(const std::string& name) const;
         Layer* find_layer(const std::string& name);

         bool collision(Pos tile) const;

      private:
         std::vector<Layer> m_layers;
         std::set<Pos> collisions;

         int width, height, tilewidth, tileheight;
         std::string dir;

         void add_tileset(std::map<unsigned, Surface>& tiles,
               Blit::Xml::Node node);
         void add_layer(std::map<unsigned, Surface>& tiles,
               Blit::Xml::Node node, int tilewidth, int tileheight);

         std::map<std::string, std::string> get_attributes(Blit::Xml::Node, const std::string& child) const;
   };
}

#endif

