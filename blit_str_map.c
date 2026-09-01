/* Dinothawr - a small string-keyed map (implementation).
 * MSVC C89. See blit_str_map.h. */

#include "blit_str_map.h"

#include <stdlib.h>
#include <string.h>

typedef struct
{
   char *key;
   void *value;
} blit_str_entry_t;

struct blit_str_map
{
   blit_str_entry_t *entries;
   size_t            count;
   size_t            capacity;
};

blit_str_map_t *blit_str_map_new(void)
{
   blit_str_map_t *map = (blit_str_map_t*)malloc(sizeof(*map));

   if (!map)
      return NULL;

   map->entries  = NULL;
   map->count    = 0;
   map->capacity = 0;
   return map;
}

void blit_str_map_free(blit_str_map_t *map)
{
   size_t i;

   if (!map)
      return;

   for (i = 0; i < map->count; i++)
      free(map->entries[i].key);

   if (map->entries)
      free(map->entries);
   free(map);
}

void *blit_str_map_find(const blit_str_map_t *map, const char *key)
{
   size_t i;

   if (!map || !key)
      return NULL;

   for (i = 0; i < map->count; i++)
      if (strcmp(map->entries[i].key, key) == 0)
         return map->entries[i].value;

   return NULL;
}

int blit_str_map_set(blit_str_map_t *map, const char *key, void *value,
      void **old_value, int *replaced)
{
   size_t i;
   size_t len;
   char  *dup;

   if (old_value)
      *old_value = NULL;
   if (replaced)
      *replaced = 0;

   if (!map || !key)
      return 0;

   for (i = 0; i < map->count; i++)
   {
      if (strcmp(map->entries[i].key, key) != 0)
         continue;

      if (old_value)
         *old_value = map->entries[i].value;
      if (replaced)
         *replaced = 1;

      map->entries[i].value = value;
      return 1;
   }

   if (map->count == map->capacity)
   {
      size_t            new_cap = map->capacity ? (map->capacity * 2) : 16;
      blit_str_entry_t *grown   = (blit_str_entry_t*)realloc(map->entries,
            new_cap * sizeof(*grown));

      if (!grown)
         return 0;

      map->entries  = grown;
      map->capacity = new_cap;
   }

   len = strlen(key);
   if (!(dup = (char*)malloc(len + 1)))
      return 0;
   memcpy(dup, key, len + 1);

   map->entries[map->count].key   = dup;
   map->entries[map->count].value = value;
   map->count++;
   return 1;
}

size_t blit_str_map_count(const blit_str_map_t *map)
{
   return map ? map->count : 0;
}

void *blit_str_map_value_at(const blit_str_map_t *map, size_t index)
{
   if (!map || index >= map->count)
      return NULL;
   return map->entries[index].value;
}
