/* Dinothawr - shared tile attribute table.
 *
 * The string properties a Tiled map hangs off a tile: "collision",
 * "sprite", and whatever else a level author adds. A handful of entries
 * per tile at most, written once when the map loads and read every
 * frame after that.
 *
 * The table is shared and reference counted rather than owned per
 * Surface, because a Surface is copied on every blit - about eighty
 * times a frame - and copying a map of std::strings each time was the
 * bulk of that copy. Sharing makes the copy an increment.
 *
 * Writes go through blit_attr_table_set on a table the caller holds
 * alone; Surface::set_attr clones first when the table is shared, so a
 * write never reaches another Surface's view. A NULL table is a valid
 * empty one - that is the common case, and it allocates nothing.
 *
 * Lookup is a linear scan. The tables are small enough that it beats
 * anything with a tree in it, and it keeps the whole thing to two
 * allocations.
 *
 * MSVC C89.
 */

#ifndef BLIT_ATTR_TABLE_H__
#define BLIT_ATTR_TABLE_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct blit_attr_table blit_attr_table_t;

blit_attr_table_t *blit_attr_table_new(void);

/* Deep copy, refcount 1. NULL in gives NULL out (an empty table). */
blit_attr_table_t *blit_attr_table_clone(const blit_attr_table_t *table);

blit_attr_table_t *blit_attr_table_ref(blit_attr_table_t *table);
void blit_attr_table_unref(blit_attr_table_t *table);

/* Non-zero when more than one owner holds this table. */
int blit_attr_table_shared(const blit_attr_table_t *table);

/* The value for @key, or NULL when absent. Safe on a NULL table. */
const char *blit_attr_table_find(const blit_attr_table_t *table,
      const char *key);

/* Insert or replace. Returns non-zero on success. The caller must own
 * the table alone. */
int blit_attr_table_set(blit_attr_table_t *table, const char *key,
      const char *value);

size_t blit_attr_table_count(const blit_attr_table_t *table);
const char *blit_attr_table_key_at(const blit_attr_table_t *table,
      size_t index);
const char *blit_attr_table_value_at(const blit_attr_table_t *table,
      size_t index);

#ifdef __cplusplus
}
#endif

#endif
