/* Dinothawr - shared tile attribute table (implementation).
 * MSVC C89. See blit_attr_table.h. */

#include "blit_attr_table.h"

#include <stdlib.h>
#include <string.h>

typedef struct
{
   char *key;
   char *value;
} blit_attr_t;

struct blit_attr_table
{
   blit_attr_t *entries;
   size_t       count;
   size_t       capacity;
   int          refcount;
};

static char *attr_strdup(const char *s)
{
   size_t len;
   char  *out;

   if (!s)
      s = "";

   len = strlen(s);
   if (!(out = (char*)malloc(len + 1)))
      return NULL;

   memcpy(out, s, len + 1);
   return out;
}

blit_attr_table_t *blit_attr_table_new(void)
{
   blit_attr_table_t *table =
      (blit_attr_table_t*)malloc(sizeof(*table));

   if (!table)
      return NULL;

   table->entries  = NULL;
   table->count    = 0;
   table->capacity = 0;
   table->refcount = 1;
   return table;
}

blit_attr_table_t *blit_attr_table_ref(blit_attr_table_t *table)
{
   if (table)
      table->refcount++;
   return table;
}

void blit_attr_table_unref(blit_attr_table_t *table)
{
   size_t i;

   if (!table)
      return;

   if (--table->refcount > 0)
      return;

   for (i = 0; i < table->count; i++)
   {
      free(table->entries[i].key);
      free(table->entries[i].value);
   }
   if (table->entries)
      free(table->entries);
   free(table);
}

int blit_attr_table_shared(const blit_attr_table_t *table)
{
   return table && table->refcount > 1;
}

const char *blit_attr_table_find(const blit_attr_table_t *table,
      const char *key)
{
   size_t i;

   if (!table || !key)
      return NULL;

   for (i = 0; i < table->count; i++)
      if (strcmp(table->entries[i].key, key) == 0)
         return table->entries[i].value;

   return NULL;
}

int blit_attr_table_set(blit_attr_table_t *table, const char *key,
      const char *value)
{
   size_t i;
   char  *dup_key;
   char  *dup_value;

   if (!table || !key)
      return 0;

   for (i = 0; i < table->count; i++)
   {
      if (strcmp(table->entries[i].key, key) == 0)
      {
         if (!(dup_value = attr_strdup(value)))
            return 0;
         free(table->entries[i].value);
         table->entries[i].value = dup_value;
         return 1;
      }
   }

   if (table->count == table->capacity)
   {
      size_t       new_cap = table->capacity ? (table->capacity * 2) : 4;
      blit_attr_t *grown   = (blit_attr_t*)realloc(table->entries,
            new_cap * sizeof(*grown));

      if (!grown)
         return 0;

      table->entries  = grown;
      table->capacity = new_cap;
   }

   if (!(dup_key = attr_strdup(key)))
      return 0;

   if (!(dup_value = attr_strdup(value)))
   {
      free(dup_key);
      return 0;
   }

   table->entries[table->count].key   = dup_key;
   table->entries[table->count].value = dup_value;
   table->count++;
   return 1;
}

blit_attr_table_t *blit_attr_table_clone(const blit_attr_table_t *table)
{
   blit_attr_table_t *out;
   size_t             i;

   if (!table)
      return NULL;

   if (!(out = blit_attr_table_new()))
      return NULL;

   for (i = 0; i < table->count; i++)
   {
      if (!blit_attr_table_set(out, table->entries[i].key,
               table->entries[i].value))
      {
         blit_attr_table_unref(out);
         return NULL;
      }
   }

   return out;
}

size_t blit_attr_table_count(const blit_attr_table_t *table)
{
   return table ? table->count : 0;
}

const char *blit_attr_table_key_at(const blit_attr_table_t *table,
      size_t index)
{
   if (!table || index >= table->count)
      return NULL;
   return table->entries[index].key;
}

const char *blit_attr_table_value_at(const blit_attr_table_t *table,
      size_t index)
{
   if (!table || index >= table->count)
      return NULL;
   return table->entries[index].value;
}
