/* Dinothawr - the tiles a map is built from.
 *
 * Indexed by Tiled's global tile id: a tileset declares a first_gid and
 * numbers its tiles upward from there, so the ids are dense and an
 * array indexed by id is the natural shape. A map has a few hundred of
 * them and every cell in every layer looks one up while the map loads.
 *
 * The set owns a reference on each tile's surface tables. Ids that no
 * tileset declared are absent, which is distinct from a tile that
 * exists and happens to be empty.
 *
 * MSVC C89.
 */

#ifndef BLIT_TILE_SET_H__
#define BLIT_TILE_SET_H__

#include <stddef.h>

#include "blit_surface.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct blit_tile_set blit_tile_set_t;

blit_tile_set_t *blit_tile_set_new(void);
void blit_tile_set_free(blit_tile_set_t *set);

/* Stores a copy of @surf at @id, taking its own reference and releasing
 * whatever was there. Grows the array as needed. Non-zero on success. */
int blit_tile_set_put(blit_tile_set_t *set, unsigned id,
      const blit_surface_t *surf);

/* The tile at @id, or NULL when no tileset declared it. The pointer is
 * good until the next put. */
blit_surface_t *blit_tile_set_get(const blit_tile_set_t *set,
      unsigned id);

#ifdef __cplusplus
}
#endif

#endif
