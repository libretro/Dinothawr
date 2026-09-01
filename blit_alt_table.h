/* Dinothawr - shared sprite face table.
 *
 * The faces of a sprite: "up", "down", "push" and so on, each naming one
 * or more frames. A .sprite file names them, the session cache parses it
 * once, and every Surface built from that sprite shares this table.
 *
 * Entries keep insertion order, and a tag may appear more than once -
 * that is how a face carries several animation frames, selected by
 * index. This matches what the multimap it replaces guaranteed for
 * equivalent keys.
 *
 * Shared and reference counted, NULL meaning no faces at all, which is
 * the case for every tile and glyph. The table owns one reference on
 * each face's pixel data. It is immutable once built, so there is no
 * copy-on-write path.
 *
 * MSVC C89.
 */

#ifndef BLIT_ALT_TABLE_H__
#define BLIT_ALT_TABLE_H__

#include <stddef.h>

#include "blit_surface_data.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct blit_alt_table blit_alt_table_t;

blit_alt_table_t *blit_alt_table_new(void);

/* Appends a face. Takes its own reference on @data. Non-zero on
 * success. Only called while the table is being built. */
int blit_alt_table_add(blit_alt_table_t *table, const char *tag,
      blit_surface_data_t *data);

blit_alt_table_t *blit_alt_table_ref(blit_alt_table_t *table);
void blit_alt_table_unref(blit_alt_table_t *table);

/* How many entries carry @tag. Safe on a NULL table. */
size_t blit_alt_table_count(const blit_alt_table_t *table,
      const char *tag);

/* The @index'th entry carrying @tag, or NULL when out of range. The
 * data is borrowed - the table holds the reference. */
blit_surface_data_t *blit_alt_table_at(const blit_alt_table_t *table,
      const char *tag, size_t index);

/* The table's own copy of @tag, stable for the table's lifetime, or
 * NULL when no entry carries it. Lets a caller hold the tag without
 * owning a string. */
const char *blit_alt_table_tag(const blit_alt_table_t *table,
      const char *tag);

/* Total entries, in insertion order. */
size_t blit_alt_table_size(const blit_alt_table_t *table);
blit_surface_data_t *blit_alt_table_data_at(
      const blit_alt_table_t *table, size_t index);

#ifdef __cplusplus
}
#endif

#endif
