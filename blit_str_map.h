/* Dinothawr - a small string-keyed map.
 *
 * Backs the session surface cache and the parsed sprite definitions:
 * both are keyed by asset path, filled while a game loads and read for
 * the rest of the session, never evicted. Tens of entries, so lookup is
 * a linear scan over owned key strings - a tree would cost more in
 * pointer chasing than the compares it saves at this size.
 *
 * The map owns its keys and copies them on insert. It does not own its
 * values: what a value is and how it is released is the caller's, which
 * is why blit_str_map_free leaves them alone and the iteration
 * accessors exist.
 *
 * MSVC C89.
 */

#ifndef BLIT_STR_MAP_H__
#define BLIT_STR_MAP_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct blit_str_map blit_str_map_t;

blit_str_map_t *blit_str_map_new(void);

/* Frees the map and its keys. Values are the caller's - walk them with
 * the accessors below first if they need releasing. */
void blit_str_map_free(blit_str_map_t *map);

/* The value for @key, or NULL when absent. Safe on a NULL map. */
void *blit_str_map_find(const blit_str_map_t *map, const char *key);

/* Insert or replace. The old value is returned when @key was present,
 * so the caller can release it; NULL otherwise. @replaced is set
 * non-zero when a value was replaced, which distinguishes a replaced
 * NULL from an insert. Returns non-zero on success. */
int blit_str_map_set(blit_str_map_t *map, const char *key, void *value,
      void **old_value, int *replaced);

size_t blit_str_map_count(const blit_str_map_t *map);
void *blit_str_map_value_at(const blit_str_map_t *map, size_t index);

#ifdef __cplusplus
}
#endif

#endif
