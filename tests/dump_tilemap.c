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

#include <stdio.h>

#include "blit_tilemap.h"

/* Prints everything the parse produced, in a stable order, so a change
 * to the map reader can be diffed against it rather than against the
 * screen. */
int main(int argc, char **argv)
{
   char            err[256];
   blit_tilemap_t *map;
   size_t          i;
   size_t          e;
   size_t          k;
   int             x;
   int             y;

   if (argc < 2)
   {
      printf("usage: %s <level.tmx>\n", argv[0]);
      return 1;
   }

   if (!(map = blit_tilemap_load(argv[1], err, sizeof(err))))
   {
      printf("load failed: %s\n", err);
      return 1;
   }

   printf("tile %dx%d grid %dx%d\n", blit_tilemap_tile_width(map),
         blit_tilemap_tile_height(map), blit_tilemap_tiles_width(map),
         blit_tilemap_tiles_height(map));

   for (i = 0; i < blit_tilemap_layer_count(map); i++)
   {
      blit_layer_t *layer = blit_tilemap_layer_at(map, i);

      printf("layer %u name=%s tiles=%u\n", (unsigned)i, layer->name,
            (unsigned)layer->cluster.count);

      for (k = 0; k < blit_attr_table_count(layer->attr); k++)
         printf("  layerattr %s=%s\n",
               blit_attr_table_key_at(layer->attr, k),
               blit_attr_table_value_at(layer->attr, k));

      for (e = 0; e < layer->cluster.count; e++)
      {
         blit_cluster_elem_t *el = &layer->cluster.elems[e];

         printf("  tile %u pos=%d,%d off=%d,%d wh=%dx%d alt=%s\n",
               (unsigned)e, el->surf.rect.pos.x, el->surf.rect.pos.y,
               el->offset.x, el->offset.y, el->surf.rect.w,
               el->surf.rect.h,
               el->surf.active_alt ? el->surf.active_alt : "-");

         for (k = 0; k < blit_attr_table_count(el->surf.attribs); k++)
            printf("    attr %s=%s\n",
                  blit_attr_table_key_at(el->surf.attribs, k),
                  blit_attr_table_value_at(el->surf.attribs, k));
      }
   }

   for (y = 0; y < blit_tilemap_tiles_height(map); y++)
      for (x = 0; x < blit_tilemap_tiles_width(map); x++)
         if (blit_tilemap_collision(map, blit_pos(x, y)))
            printf("collide %d,%d\n", x, y);

   blit_tilemap_free(map);
   return 0;
}
