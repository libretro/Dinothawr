/* Dinothawr - the chapter and level list (implementation).
 * MSVC C89. See icy_levels.h. */

#include "icy_levels.h"

#include <stdlib.h>
#include <string.h>

static char *levels_strdup(const char *s)
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

void icy_level_list_init(icy_level_list_t *list)
{
   list->chapters = NULL;
   list->count    = 0;
   list->capacity = 0;
}

void icy_level_list_release(icy_level_list_t *list)
{
   size_t c;
   size_t l;

   if (!list)
      return;

   for (c = 0; c < list->count; c++)
   {
      icy_chapter_t *chapter = &list->chapters[c];

      for (l = 0; l < chapter->count; l++)
      {
         blit_surface_release(&chapter->levels[l].preview);
         free(chapter->levels[l].path);
         free(chapter->levels[l].name);
      }

      free(chapter->levels);
      free(chapter->name);
   }

   free(list->chapters);
   icy_level_list_init(list);
}

icy_chapter_t *icy_level_list_add_chapter(icy_level_list_t *list,
      const char *name)
{
   icy_chapter_t *chapter;

   if (list->count == list->capacity)
   {
      size_t         new_cap = list->capacity ? (list->capacity * 2) : 8;
      icy_chapter_t *grown   = (icy_chapter_t*)realloc(list->chapters,
            new_cap * sizeof(*grown));

      if (!grown)
         return NULL;

      list->chapters = grown;
      list->capacity = new_cap;
   }

   chapter = &list->chapters[list->count];
   chapter->levels        = NULL;
   chapter->count         = 0;
   chapter->capacity      = 0;
   chapter->minimum_clear = 0;

   if (!(chapter->name = levels_strdup(name)))
      return NULL;

   list->count++;
   return chapter;
}

icy_level_t *icy_chapter_add_level(icy_chapter_t *chapter,
      const char *path, const char *name, const blit_surface_t *preview)
{
   icy_level_t *level;

   if (chapter->count == chapter->capacity)
   {
      size_t       new_cap = chapter->capacity ? (chapter->capacity * 2) : 8;
      icy_level_t *grown   = (icy_level_t*)realloc(chapter->levels,
            new_cap * sizeof(*grown));

      if (!grown)
         return NULL;

      chapter->levels   = grown;
      chapter->capacity = new_cap;
   }

   level = &chapter->levels[chapter->count];
   level->position    = blit_pos_zero();
   level->completed   = 0;
   level->best_pushes = 0;
   level->path        = levels_strdup(path);
   level->name        = levels_strdup(name);

   blit_surface_init(&level->preview);

   if (!level->path || !level->name)
   {
      free(level->path);
      free(level->name);
      return NULL;
   }

   if (preview)
   {
      level->preview = *preview;
      blit_surface_retain(&level->preview);
   }

   chapter->count++;
   return level;
}

void icy_level_set_name(icy_level_t *level, const char *name)
{
   char *dup = levels_strdup(name);

   if (!dup)
      return;

   free(level->name);
   level->name = dup;
}

void icy_level_record_clear(icy_level_t *level, unsigned pushes)
{
   level->completed = pushes != 0;

   /* The best is the fewest, and zero means never cleared. */
   if (!level->best_pushes || (pushes && pushes < level->best_pushes))
      level->best_pushes = pushes;
}

unsigned icy_chapter_cleared_count(const icy_chapter_t *chapter)
{
   unsigned count = 0;
   size_t   i;

   for (i = 0; i < chapter->count; i++)
      if (chapter->levels[i].completed)
         count++;

   return count;
}

int icy_chapter_cleared(const icy_chapter_t *chapter)
{
   return icy_chapter_cleared_count(chapter) >= chapter->minimum_clear;
}

unsigned icy_level_list_total(const icy_level_list_t *list)
{
   unsigned total = 0;
   size_t   i;

   for (i = 0; i < list->count; i++)
      total += (unsigned)list->chapters[i].count;

   return total;
}

unsigned icy_level_list_cleared(const icy_level_list_t *list)
{
   unsigned cleared = 0;
   size_t   i;

   for (i = 0; i < list->count; i++)
      cleared += icy_chapter_cleared_count(&list->chapters[i]);

   return cleared;
}

int icy_level_list_first_unsolved(const icy_level_list_t *list,
      unsigned *chapter, unsigned *level)
{
   size_t c;
   size_t l;

   for (c = 0; c < list->count; c++)
      for (l = 0; l < list->chapters[c].count; l++)
         if (!list->chapters[c].levels[l].completed)
         {
            *chapter = (unsigned)c;
            *level   = (unsigned)l;
            return 1;
         }

   return 0;
}
