/* Dinothawr - the chapter and level list.
 *
 * What the menu shows and the save records: chapters, each holding
 * levels, each with a path, a name, a preview image and how many pushes
 * it was last cleared in. A chapter opens the next once enough of its
 * levels are cleared.
 *
 * Levels and chapters are addressed by index and never move, so a
 * pointer to one is good for the list's lifetime.
 *
 * MSVC C89.
 */

#ifndef ICY_LEVELS_H__
#define ICY_LEVELS_H__

#include <stddef.h>

#include "blit_surface.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
   char          *path;
   char          *name;
   blit_surface_t preview;
   blit_pos_t     position;
   int            completed;
   unsigned       best_pushes;
} icy_level_t;

typedef struct
{
   icy_level_t *levels;
   size_t       count;
   size_t       capacity;
   char        *name;

   /* How many of this chapter's levels have to be cleared before the
    * next one opens. */
   unsigned     minimum_clear;
} icy_chapter_t;

typedef struct
{
   icy_chapter_t *chapters;
   size_t         count;
   size_t         capacity;
} icy_level_list_t;

void icy_level_list_init(icy_level_list_t *list);
void icy_level_list_release(icy_level_list_t *list);

/* Appends an empty chapter and returns it, or NULL if out of memory. */
icy_chapter_t *icy_level_list_add_chapter(icy_level_list_t *list,
      const char *name);

/* Appends a level to @chapter, taking its own reference on @preview.
 * Returns it, or NULL if out of memory. */
icy_level_t *icy_chapter_add_level(icy_chapter_t *chapter,
      const char *path, const char *name, const blit_surface_t *preview);

void icy_level_set_name(icy_level_t *level, const char *name);

/* Records a clear. A level keeps its best - the fewest pushes it has
 * been cleared in - so a worse run does not overwrite a better one. */
void icy_level_record_clear(icy_level_t *level, unsigned pushes);

unsigned icy_chapter_cleared_count(const icy_chapter_t *chapter);

/* Whether @chapter has been cleared enough to open the next. */
int icy_chapter_cleared(const icy_chapter_t *chapter);

unsigned icy_level_list_total(const icy_level_list_t *list);
unsigned icy_level_list_cleared(const icy_level_list_t *list);

/* The first level in the list that has not been cleared, by chapter and
 * level index. Non-zero when there is one. */
int icy_level_list_first_unsolved(const icy_level_list_t *list,
      unsigned *chapter, unsigned *level);

#ifdef __cplusplus
}
#endif

#endif
