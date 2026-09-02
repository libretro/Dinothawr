/* Dinothawr - the save encoding.
 *
 * The save is a fixed-size buffer of text: one line per chapter, holding
 * a comma-separated best-push count per level with a trailing comma, and
 * zero padding to the end. A non-zero count means the level was cleared
 * in that many pushes; zero means it was not.
 *
 * Counts are passed flat, chapter-major, with the per-chapter level
 * counts alongside - the caller knows its own layout and this does not
 * need to.
 *
 * MSVC C89.
 */

#ifndef ICY_SAVE_H__
#define ICY_SAVE_H__

#include <stddef.h>

#include "icy_levels.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Writes the save into @buf, zero-filling the remainder. Truncates
 * rather than overruns if the counts do not fit. */
void icy_save_encode(char *buf, size_t len, const unsigned *counts,
      const unsigned *levels_per_chapter, size_t chapters);

/* Reads @buf into @counts, which must hold the sum of
 * @levels_per_chapter. Levels the save does not mention are left as
 * they are, so a short or empty save leaves the caller's state alone -
 * which is what an unplayed game looks like. */
void icy_save_decode(const char *buf, size_t len, unsigned *counts,
      const unsigned *levels_per_chapter, size_t chapters);

/* The save the frontend sees, and the two operations on it. Fixed size
 * because the frontend is handed the buffer and its length once. */
enum
{
   ICY_SAVE_SIZE = 512,

   /* The buffer holds a few bytes per level, so these are far above what
    * fits in it; they exist so the conversion can use fixed arrays. */
   ICY_SAVE_MAX_CHAPTERS = 64,
   ICY_SAVE_MAX_LEVELS   = 512
};

typedef struct
{
   char data[ICY_SAVE_SIZE];
} icy_save_t;

void icy_save_clear(icy_save_t *save);

/* Writes @list's best-push counts into the buffer. */
void icy_save_store(icy_save_t *save, const icy_level_list_t *list);

/* Reads the buffer back into @list. Levels the save does not mention
 * keep what they have, so a short or empty save leaves an unplayed game
 * unplayed. */
void icy_save_load(const icy_save_t *save, icy_level_list_t *list);

#ifdef __cplusplus
}
#endif

#endif
