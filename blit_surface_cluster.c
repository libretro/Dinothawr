/* Dinothawr - a cluster of surfaces drawn together (implementation).
 * MSVC C89. See blit_surface_cluster.h. */

#include "blit_surface_cluster.h"

#include <stdlib.h>

void blit_surface_cluster_init(blit_surface_cluster_t *cluster)
{
   cluster->elems    = NULL;
   cluster->count    = 0;
   cluster->capacity = 0;
}

void blit_surface_cluster_release(blit_surface_cluster_t *cluster)
{
   size_t i;

   for (i = 0; i < cluster->count; i++)
      blit_surface_release(&cluster->elems[i].surf);

   if (cluster->elems)
      free(cluster->elems);

   blit_surface_cluster_init(cluster);
}

int blit_surface_cluster_add(blit_surface_cluster_t *cluster,
      const blit_surface_t *surf, blit_pos_t offset)
{
   blit_cluster_elem_t *slot;

   if (cluster->count == cluster->capacity)
   {
      size_t               new_cap = cluster->capacity
         ? (cluster->capacity * 2) : 16;
      blit_cluster_elem_t *grown   = (blit_cluster_elem_t*)realloc(
            cluster->elems, new_cap * sizeof(*grown));

      if (!grown)
         return 0;

      cluster->elems    = grown;
      cluster->capacity = new_cap;
   }

   slot         = &cluster->elems[cluster->count];
   slot->surf   = *surf;
   slot->offset = offset;
   slot->tag    = 0;

   /* The copy above duplicated the pointers; this makes them ours. */
   blit_surface_retain(&slot->surf);

   cluster->count++;
   return 1;
}

void blit_surface_cluster_render(const blit_surface_cluster_t *cluster,
      blit_render_target_t *target, blit_pos_t origin)
{
   size_t i;

   for (i = 0; i < cluster->count; i++)
      blit_render_target_blit_offset(target, &cluster->elems[i].surf,
            blit_rect_zero(),
            blit_pos_add(origin, cluster->elems[i].offset));
}

blit_cluster_elem_t *blit_surface_cluster_find(
      const blit_surface_cluster_t *cluster, blit_pos_t offset)
{
   size_t i;

   for (i = 0; i < cluster->count; i++)
   {
      blit_pos_t at = blit_pos_add(cluster->elems[i].surf.rect.pos,
            cluster->elems[i].offset);

      if (blit_pos_equal(at, offset))
         return &cluster->elems[i];
   }

   return NULL;
}
