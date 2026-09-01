/* Dinothawr - shared sprite face table (implementation).
 * MSVC C89. See blit_alt_table.h. */

#include "blit_alt_table.h"

#include <stdlib.h>
#include <string.h>

typedef struct
{
   char                *tag;
   blit_surface_data_t *data;
} blit_alt_t;

struct blit_alt_table
{
   blit_alt_t *entries;
   size_t      count;
   size_t      capacity;
   int         refcount;
};

blit_alt_table_t *blit_alt_table_new(void)
{
   blit_alt_table_t *table = (blit_alt_table_t*)malloc(sizeof(*table));

   if (!table)
      return NULL;

   table->entries  = NULL;
   table->count    = 0;
   table->capacity = 0;
   table->refcount = 1;
   return table;
}

int blit_alt_table_add(blit_alt_table_t *table, const char *tag,
      blit_surface_data_t *data)
{
   size_t len;
   char  *dup;

   if (!table || !tag)
      return 0;

   if (table->count == table->capacity)
   {
      size_t      new_cap = table->capacity ? (table->capacity * 2) : 4;
      blit_alt_t *grown   = (blit_alt_t*)realloc(table->entries,
            new_cap * sizeof(*grown));

      if (!grown)
         return 0;

      table->entries  = grown;
      table->capacity = new_cap;
   }

   len = strlen(tag);
   if (!(dup = (char*)malloc(len + 1)))
      return 0;
   memcpy(dup, tag, len + 1);

   table->entries[table->count].tag  = dup;
   table->entries[table->count].data = blit_surface_data_ref(data);
   table->count++;
   return 1;
}

blit_alt_table_t *blit_alt_table_ref(blit_alt_table_t *table)
{
   if (table)
      table->refcount++;
   return table;
}

void blit_alt_table_unref(blit_alt_table_t *table)
{
   size_t i;

   if (!table)
      return;

   if (--table->refcount > 0)
      return;

   for (i = 0; i < table->count; i++)
   {
      free(table->entries[i].tag);
      blit_surface_data_unref(table->entries[i].data);
   }
   if (table->entries)
      free(table->entries);
   free(table);
}

size_t blit_alt_table_count(const blit_alt_table_t *table,
      const char *tag)
{
   size_t i;
   size_t n = 0;

   if (!table || !tag)
      return 0;

   for (i = 0; i < table->count; i++)
      if (strcmp(table->entries[i].tag, tag) == 0)
         n++;

   return n;
}

blit_surface_data_t *blit_alt_table_at(const blit_alt_table_t *table,
      const char *tag, size_t index)
{
   size_t i;

   if (!table || !tag)
      return NULL;

   for (i = 0; i < table->count; i++)
   {
      if (strcmp(table->entries[i].tag, tag) != 0)
         continue;
      if (index == 0)
         return table->entries[i].data;
      index--;
   }

   return NULL;
}

const char *blit_alt_table_tag(const blit_alt_table_t *table,
      const char *tag)
{
   size_t i;

   if (!table || !tag)
      return NULL;

   for (i = 0; i < table->count; i++)
      if (strcmp(table->entries[i].tag, tag) == 0)
         return table->entries[i].tag;

   return NULL;
}

size_t blit_alt_table_size(const blit_alt_table_t *table)
{
   return table ? table->count : 0;
}

blit_surface_data_t *blit_alt_table_data_at(
      const blit_alt_table_t *table, size_t index)
{
   if (!table || index >= table->count)
      return NULL;
   return table->entries[index].data;
}
