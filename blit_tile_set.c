/* Dinothawr - the tiles a map is built from (implementation).
 * MSVC C89. See blit_tile_set.h. */

#include "blit_tile_set.h"

#include <stdlib.h>

struct blit_tile_set
{
   blit_surface_t *tiles;
   char           *present;
   size_t          count;
};

blit_tile_set_t *blit_tile_set_new(void)
{
   blit_tile_set_t *set = (blit_tile_set_t*)malloc(sizeof(*set));

   if (!set)
      return NULL;

   set->tiles   = NULL;
   set->present = NULL;
   set->count   = 0;
   return set;
}

void blit_tile_set_free(blit_tile_set_t *set)
{
   size_t i;

   if (!set)
      return;

   for (i = 0; i < set->count; i++)
      if (set->present[i])
         blit_surface_release(&set->tiles[i]);

   if (set->tiles)
      free(set->tiles);
   if (set->present)
      free(set->present);
   free(set);
}

static int blit_tile_set_grow(blit_tile_set_t *set, size_t need)
{
   blit_surface_t *tiles;
   char           *present;
   size_t          new_count = set->count ? set->count : 64;
   size_t          i;

   while (new_count < need)
      new_count *= 2;

   if (!(tiles = (blit_surface_t*)realloc(set->tiles,
               new_count * sizeof(*tiles))))
      return 0;
   set->tiles = tiles;

   if (!(present = (char*)realloc(set->present,
               new_count * sizeof(*present))))
      return 0;
   set->present = present;

   for (i = set->count; i < new_count; i++)
   {
      blit_surface_init(&set->tiles[i]);
      set->present[i] = 0;
   }

   set->count = new_count;
   return 1;
}

int blit_tile_set_put(blit_tile_set_t *set, unsigned id,
      const blit_surface_t *surf)
{
   if (!set || !surf)
      return 0;

   if (id >= set->count && !blit_tile_set_grow(set, (size_t)id + 1))
      return 0;

   if (set->present[id])
      blit_surface_release(&set->tiles[id]);

   set->tiles[id] = *surf;
   blit_surface_retain(&set->tiles[id]);
   set->present[id] = 1;
   return 1;
}

blit_surface_t *blit_tile_set_get(const blit_tile_set_t *set,
      unsigned id)
{
   if (!set || id >= set->count || !set->present[id])
      return NULL;
   return &set->tiles[id];
}
