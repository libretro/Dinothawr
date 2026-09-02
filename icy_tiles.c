/* Dinothawr - finding tiles by attribute (implementation).
 * MSVC C89. See icy_tiles.h. */

#include "icy_tiles.h"

#include <string.h>

size_t icy_tiles_with_attr(const blit_tilemap_t *map,
      const char *layer_name, const char *attr, const char *value,
      blit_cluster_elem_t **out, size_t max)
{
   blit_layer_t *layer = blit_tilemap_find_layer(map, layer_name);
   size_t        found = 0;
   size_t        i;

   if (!layer)
      return 0;

   for (i = 0; i < layer->cluster.count; i++)
   {
      blit_cluster_elem_t *elem = &layer->cluster.elems[i];
      const char          *have = blit_attr_table_find(elem->surf.attribs,
            attr);

      if (!have)
         continue;

      /* No value asked for: having the attribute is enough. */
      if (value && *value && strcmp(have, value) != 0)
         continue;

      if (out && found < max)
         out[found] = elem;
      found++;
   }

   return found;
}
