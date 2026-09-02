#include "tilemap.hpp"
#include <cstring>
#include <file/file_path.h>
#include "utils.hpp"

#include <iostream>
#include <stdexcept>
#include <map>
#include <utility>
#include <string>
#include "xml.hpp"


namespace Blit
{
   Tilemap::Tilemap(const std::string& path) : dir(Utils::basedir(path))
   {
      Blit::Xml::Document doc;
      if (!doc.load_file(path.c_str()))
         throw std::runtime_error(Utils::join("Failed to load XML map: ", path, "."));

      Blit::Xml::Node map   = doc.child("map");
      width      = map.attribute("width").as_int();
      height     = map.attribute("height").as_int();
      tilewidth  = map.attribute("tilewidth").as_int();
      tileheight = map.attribute("tileheight").as_int();

      if (!width || !height || !tilewidth || !tileheight)
         throw std::logic_error("Tilemap is malformed.");

      blit_tile_set_t *tiles = blit_tile_set_new();

      if (!tiles)
         throw std::bad_alloc();

      for (auto set = map.child("tileset"); set; set = set.next_sibling("tileset"))
         add_tileset(tiles, set);

      for (auto layer = map.child("layer"); layer; layer = layer.next_sibling("layer"))
         add_layer(tiles, layer, tilewidth, tileheight);

      blit_tile_set_free(tiles);
   }

   std::map<std::string, std::string> Tilemap::get_attributes(Blit::Xml::Node parent, const std::string& child) const
   {
      std::map<std::string, std::string> attrs;

      for (auto node = parent.child(child.c_str()); node; node = node.next_sibling(child.c_str()))
      {
         const char *name = node.attribute("name").value();
         const char *value = node.attribute("value").value();
         attrs.insert({name, value});
      }

      return attrs;
   }

   void Tilemap::add_tileset(blit_tile_set_t *tiles, Blit::Xml::Node node)
   {
      int first_gid  = node.attribute("firstgid").as_int();
      int id_cnt     = 0;
      int tilewidth  = node.attribute("tilewidth").as_int();
      int tileheight = node.attribute("tileheight").as_int();

      Blit::Xml::Node image     = node.child("image");
      const char *source    = image.attribute("source").value();
      int width      = image.attribute("width").as_int();
      int height     = image.attribute("height").as_int();

      if (!width || !height || !tilewidth || !tileheight)
         throw std::logic_error("Tilemap is malformed.");

      std::map<std::basic_string<char>, std::basic_string<char> > global_attr = get_attributes(node.child("properties"), "property");

      /* An .apng tileset carries one tile per frame, in the same
       * row-major order the sheet was cut in, so the local tile id is
       * the frame number.  The image element's width/height keep
       * describing the virtual sheet so the tile count is unchanged. */
      std::basic_string<char> path = Utils::join(dir, "/", source);
      std::size_t src_len = std::strlen(source);
      bool apng = src_len >= 5 &&
         std::strcmp(source + src_len - 5, ".apng") == 0;

      /* Content in the wild mixes level and asset generations - an
       * updated assets/ folder next to older .tmx files or the other
       * way around - so when the named form is absent and the sibling
       * with the other extension exists, load that one instead. */
      if (!path_is_valid(path.c_str()))
      {
         std::basic_string<char> alt = apng
            ? path.substr(0, path.size() - 5) + ".png"
            : path.substr(0, path.size() - 4) + ".apng";
         if (path_is_valid(alt.c_str()))
         {
            path = alt;
            apng = !apng;
         }
      }

      if (apng)
      {
         int count = (width / tilewidth) * (height / tileheight);
         for (; id_cnt < count; id_cnt++)
         {
            int id = first_gid + id_cnt;
            blit_surface_t tile = surface_cache().from_animation(path, id_cnt);

            if (tile.rect.w != tilewidth || tile.rect.h != tileheight)
               throw std::logic_error("Tilemap geometry does not correspond with image values.");

            for (std::map<std::string, std::string>::const_iterator ga =
                  global_attr.begin(); ga != global_attr.end(); ++ga)
               blit_surface_set_attr(&tile, ga->first.c_str(), ga->second.c_str());

            if (!blit_tile_set_put(tiles, id, &tile))
               throw std::bad_alloc();
            blit_surface_release(&tile);
         }
      }
      else
      {
         blit_surface_t surf = surface_cache().from_image(path);

         if (surf.rect.w != width || surf.rect.h != height)
            throw std::logic_error("Tilemap geometry does not correspond with image values.");

         for (int y = 0; y < height; y += tileheight)
         {
            for (int x = 0; x < width; x += tilewidth, id_cnt++)
            {
               int id = first_gid + id_cnt;
               blit_surface_t tile = surface_sub(surf,
                     blit_rect(blit_pos(x, y), tilewidth, tileheight));

               for (std::map<std::string, std::string>::const_iterator ga =
                     global_attr.begin(); ga != global_attr.end(); ++ga)
                  blit_surface_set_attr(&tile, ga->first.c_str(), ga->second.c_str());

               if (!blit_tile_set_put(tiles, id, &tile))
                  throw std::bad_alloc();
               blit_surface_release(&tile);
            }
         }

         blit_surface_release(&surf);
      }

      // Load all attributes for a tile into the surface.
      for (auto tile = node.child("tile"); tile; tile = tile.next_sibling("tile"))
      {
         int id = first_gid + tile.attribute("id").as_int();

         std::map<std::basic_string<char>, std::basic_string<char> > attrs = get_attributes(tile.child("properties"), "property");
         std::copy(global_attr.begin(), global_attr.end(), std::inserter(attrs, attrs.begin()));

         auto itr = attrs.find("sprite");

         if (itr != attrs.end())
         {
            blit_surface_t sprite_tile = surface_cache().from_sprite(
                  Utils::join(dir, "/", itr->second));

            if (!blit_tile_set_put(tiles, id, &sprite_tile))
               throw std::bad_alloc();
            blit_surface_release(&sprite_tile);
         }

         {
            blit_surface_t *tile = blit_tile_set_get(tiles, id);
            std::map<std::string, std::string>::const_iterator a;

            if (!tile)
               throw std::logic_error("Tile attributes name an id no tileset declared.");

            for (a = attrs.begin(); a != attrs.end(); ++a)
               if (!blit_surface_set_attr(tile, a->first.c_str(),
                        a->second.c_str()))
                  throw std::bad_alloc();
         }
      }
   }

   void Tilemap::add_layer(blit_tile_set_t *tiles, Blit::Xml::Node node,
         int tilewidth, int tileheight)
   {
      Layer layer;
      int width  = node.attribute("width").as_int();
      int height = node.attribute("height").as_int();

      if (!width || !height)
         throw std::logic_error("Layer is empty.");

      Utils::xml_node_walker walk{node.child("data"), "tile", "gid"};
      int index = 0;
      for (auto& gid_str : walk)
      {
         Pos pos = blit_pos(index % width, index / width);

         unsigned gid = Utils::stoi(gid_str);
         if (gid)
         {
            blit_surface_t *tile = blit_tile_set_get(tiles, gid);
            blit_surface_t surf;

            if (!tile)
               throw std::logic_error("Layer names a tile id no tileset declared.");

            surf = *tile;
            blit_surface_retain(&surf);
            surf.rect.pos = pos * blit_pos(tilewidth, tileheight);

            layer.cluster.add(&surf, blit_pos_zero());

            {
               const char *coll = blit_attr_table_find(surf.attribs,
                     "collision");
               if (coll && std::strcmp(coll, "true") == 0)
                  collisions.insert(pos);
            }

            /* The cluster took its own reference. */
            blit_surface_release(&surf);
         }

         index++;
      }

      layer.attr = get_attributes(node.child("properties"), "property");
      layer.name = node.attribute("name").value();
      m_layers.push_back(std::move(layer));
   }

   void Tilemap::pos(Pos position)
   {
      for (auto& layer : m_layers)
         layer.cluster.pos(position);
      this->position = position;
   }

   void Tilemap::render(blit_render_target_t& target) const
   {
      for (auto& layer : m_layers)
         layer.cluster.render(target);
   }

   void Tilemap::render_until_layer(unsigned index, blit_render_target_t& target) const
   {
      for (unsigned i = 0; i <= index; i++)
         m_layers.at(i).cluster.render(target);
   }

   void Tilemap::render_after_layer(unsigned index, blit_render_target_t& target) const
   {
      for (unsigned i = index + 1; i < m_layers.size(); i++)
         m_layers.at(i).cluster.render(target);
   }

   bool Tilemap::collision(Pos tile) const
   {
      return collisions.count(tile) ||
         find_tile("blocks", {tile.x * tilewidth, tile.y * tileheight});
   }

   blit_surface_t* Tilemap::find_tile(unsigned layer_index, Pos offset)
   {
      Blit::Tilemap::Layer& layer = m_layers.at(layer_index);
      SurfaceCluster::Elem *elem = layer.cluster.find(offset);
      return elem ? &elem->surf : NULL;
   }

   blit_surface_t* Tilemap::find_tile(const std::string& name, Pos pos)
   {
      std::vector<Blit::Tilemap::Layer>::iterator layer = std::find_if(m_layers.begin(), m_layers.end(), [&name](const Layer& layer) {
               return Utils::tolower(layer.name) == name;
            });

      if (layer == m_layers.end())
         return NULL;

      return find_tile(std::distance(m_layers.begin(), layer), pos);
   }

   const blit_surface_t* Tilemap::find_tile(unsigned layer_index, Pos offset) const
   {
      const Blit::Tilemap::Layer& layer = m_layers.at(layer_index);
      const SurfaceCluster::Elem *elem = layer.cluster.find(offset);
      return elem ? &elem->surf : NULL;
   }

   const blit_surface_t* Tilemap::find_tile(const std::string& name, Pos pos) const
   {
      auto layer = std::find_if(m_layers.begin(), m_layers.end(), [&name](const Layer& layer) {
               return Utils::tolower(layer.name) == name;
            });

      if (layer == m_layers.end())
         return NULL;

      return find_tile(std::distance(m_layers.begin(), layer), pos);
   }

   const Tilemap::Layer* Tilemap::find_layer(const std::string& name) const
   {
      auto layer = std::find_if(m_layers.begin(), m_layers.end(), [&name](const Layer& layer) {
               return Utils::tolower(layer.name) == name;
            });

      if (layer != m_layers.end())
         return &*layer;
      else
         return NULL;
   }

   int Tilemap::find_layer_index(const std::string& name) const
   {
      auto layer = std::find_if(m_layers.begin(), m_layers.end(), [&name](const Layer& layer) {
               return Utils::tolower(layer.name) == name;
            });

      if (layer != m_layers.end())
         return layer - m_layers.begin();
      else
         return -1;
   }

   Tilemap::Layer* Tilemap::find_layer(const std::string& name)
   {
      auto layer = std::find_if(m_layers.begin(), m_layers.end(), [&name](const Layer& layer) {
               return Utils::tolower(layer.name) == name;
            });

      if (layer != m_layers.end())
         return &*layer;
      else
         return NULL;
   }
}

