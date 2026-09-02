/* Dinothawr - asset paths.
 *
 * Every asset the game names is relative to the file that names it: a
 * tileset to its map, a sprite to its level, a sound to the .game. So
 * the two operations are joining a directory to a name, and taking the
 * directory a file sits in.
 *
 * Both truncate rather than overrun. A truncated asset path fails to
 * open, which the caller already handles; there is nothing better to do
 * with a path longer than the buffer.
 *
 * MSVC C89.
 */

#ifndef ICY_PATH_H__
#define ICY_PATH_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Writes "@dir/@name" into @out. */
void icy_path_join(char *out, size_t len, const char *dir,
      const char *name);

/* Writes the directory part of @path into @out, or "." when it has
 * none. Splits on both slash flavours, because Windows paths reach this
 * from the frontend. */
void icy_path_dir(char *out, size_t len, const char *path);

#ifdef __cplusplus
}
#endif

#endif
