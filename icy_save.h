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

#ifdef __cplusplus
}
#endif

#endif
