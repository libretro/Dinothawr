/* Dinothawr - dump a parsed tilemap.
 *
 * Prints everything the .tmx parse produced - layer names and
 * attributes, every tile's position, size and attributes, and the
 * collision grid - in a stable order.
 *
 * This exists because the frame hash is the wrong oracle for a parser.
 * It says the picture changed; it does not say which tile got the wrong
 * attributes. Rewriting the tilemap means diffing this output before and
 * after, and only then checking the hash.
 *
 * Build against the objects of the core you want to inspect:
 *
 *   make
 *   g++ -O1 -std=c++11 -DHAVE_RPNG -DHAVE_RWAV -DHAVE_RVORBIS \
 *       -I../libretro-common/include -I.. tests/dump_tilemap.cpp \
 *       $(ls *.o | grep -vE "^(game|game_manager|libretro|bg_manager|sfx_manager)\\.o") \
 *       $(find libretro-common audio -name '*.o' | grep -v neon) \
 *       -o dump_tilemap -lm -lpthread
 *
 *   ./dump_tilemap path/to/level_1-1.tmx > before.txt
 *
 * Then diff that against the same run on the rewritten parser. Do it for
 * more than one level: 1-1 has a single Blocks tile and exercises very
 * little of the tileset code.
 */

#include "tilemap.hpp"
#include <cstdio>
#include <string>
using namespace Blit;

/* Prints everything the parse produced, in a stable order, so a C
 * rewrite can be diffed against it rather than against the screen. */
int main(int argc, char **argv)
{
   Tilemap map(argv[1]);
   std::printf("tile %dx%d grid %dx%d\n", map.tile_width(), map.tile_height(),
         map.tiles_width(), map.tiles_height());

   const std::vector<Tilemap::Layer>& layers = map.layers();
   for (size_t i = 0; i < layers.size(); i++)
   {
      std::printf("layer %zu name=%s tiles=%zu\n", i, layers[i].name.c_str(),
            layers[i].cluster.size());
      for (std::map<std::string,std::string>::const_iterator a = layers[i].attr.begin();
            a != layers[i].attr.end(); ++a)
         std::printf("  layerattr %s=%s\n", a->first.c_str(), a->second.c_str());

      for (size_t e = 0; e < layers[i].cluster.size(); e++)
      {
         const blit_cluster_elem_t *el = layers[i].cluster.at(e);
         std::printf("  tile %zu pos=%d,%d off=%d,%d wh=%dx%d alt=%s\n", e,
               el->surf.rect.pos.x, el->surf.rect.pos.y,
               el->offset.x, el->offset.y,
               el->surf.rect.w, el->surf.rect.h,
               el->surf.active_alt ? el->surf.active_alt : "-");
         for (size_t k = 0; k < blit_attr_table_count(el->surf.attribs); k++)
            std::printf("    attr %s=%s\n",
                  blit_attr_table_key_at(el->surf.attribs, k),
                  blit_attr_table_value_at(el->surf.attribs, k));
      }
   }

   for (int y = 0; y < map.tiles_height(); y++)
      for (int x = 0; x < map.tiles_width(); x++)
         if (map.collision(blit_pos(x, y)))
            std::printf("collide %d,%d\n", x, y);
   return 0;
}
