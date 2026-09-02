/* Dinothawr - grid collision (implementation).
 * MSVC C89. See icy_collide.h. */

#include "icy_collide.h"

int icy_collide_aligned(const blit_tilemap_t *map, blit_rect_t surf_rect)
{
   int tile_w = blit_tilemap_tile_width(map);
   int tile_h = blit_tilemap_tile_height(map);

   if (tile_w <= 0 || tile_h <= 0)
      return 0;

   return (surf_rect.pos.x % tile_w) == 0
       && (surf_rect.pos.y % tile_h) == 0;
}

int icy_collide_offset(const blit_tilemap_t *map, blit_rect_t surf_rect,
      blit_pos_t offset)
{
   int tile_w = blit_tilemap_tile_width(map);
   int tile_h = blit_tilemap_tile_height(map);
   blit_pos_t current;
   blit_pos_t dest;
   int min_x;
   int max_x;
   int min_y;
   int max_y;
   int x;
   int y;

   if (tile_w <= 0 || tile_h <= 0)
      return 0;

   current = blit_pos(surf_rect.pos.x / tile_w, surf_rect.pos.y / tile_h);
   dest    = blit_pos_add(surf_rect.pos, offset);

   /* Tile-sized whatever the sprite's own size is. */
   min_x = dest.x / tile_w;
   max_x = (dest.x + tile_w - 1) / tile_w;
   min_y = dest.y / tile_h;
   max_y = (dest.y + tile_h - 1) / tile_h;

   for (y = min_y; y <= max_y; y++)
   {
      for (x = min_x; x <= max_x; x++)
      {
         blit_pos_t tile = blit_pos(x, y);

         if (blit_pos_equal(tile, current))
            continue;

         if (blit_tilemap_collision(map, tile))
            return 1;
      }
   }

   return 0;
}
