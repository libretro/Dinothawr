/* Dinothawr - a Tiled map.
 *
 * Layers of tiles over a fixed grid, loaded from a .tmx. Each layer is a
 * cluster of surfaces plus the attributes the level author hung off it;
 * the "floor" layer carries the player's start position, "blocks" holds
 * the pushable ice.
 *
 * Layers are found by name, case-insensitively, because the .tmx files
 * are not consistent about it. Tile lookups are by world position, which
 * is what the movement code has to hand.
 *
 * MSVC C89.
 */

#ifndef BLIT_TILEMAP_H__
#define BLIT_TILEMAP_H__

#include "blit_attr_table.h"
#include "blit_render_target.h"
#include "blit_surface_cluster.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
   blit_surface_cluster_t  cluster;
   blit_attr_table_t      *attr;
   char                   *name;
} blit_layer_t;

typedef struct blit_tilemap blit_tilemap_t;

/* Loads the .tmx at @path. NULL on failure, with @error, if given,
 * receiving a message naming the file and the problem. */
blit_tilemap_t *blit_tilemap_load(const char *path, char *error,
      size_t error_len);

void blit_tilemap_free(blit_tilemap_t *map);

int blit_tilemap_tile_width(const blit_tilemap_t *map);
int blit_tilemap_tile_height(const blit_tilemap_t *map);
int blit_tilemap_tiles_width(const blit_tilemap_t *map);
int blit_tilemap_tiles_height(const blit_tilemap_t *map);
int blit_tilemap_pix_width(const blit_tilemap_t *map);
int blit_tilemap_pix_height(const blit_tilemap_t *map);

/* Moves every layer with the map. */
void blit_tilemap_set_pos(blit_tilemap_t *map, blit_pos_t pos);

void blit_tilemap_render(const blit_tilemap_t *map,
      blit_render_target_t *target);

size_t        blit_tilemap_layer_count(const blit_tilemap_t *map);
blit_layer_t *blit_tilemap_layer_at(const blit_tilemap_t *map,
      size_t index);

/* Case-insensitive by name; NULL or -1 when there is no such layer. */
blit_layer_t *blit_tilemap_find_layer(const blit_tilemap_t *map,
      const char *name);
int blit_tilemap_find_layer_index(const blit_tilemap_t *map,
      const char *name);

/* The tile at world position @pos in a layer, or NULL. */
blit_surface_t *blit_tilemap_find_tile_at(const blit_tilemap_t *map,
      unsigned layer_index, blit_pos_t pos);
blit_surface_t *blit_tilemap_find_tile(const blit_tilemap_t *map,
      const char *layer_name, blit_pos_t pos);

/* Whether @tile, in tile coordinates, is blocked - either by the map's
 * own collision set or by a block standing on it. */
int blit_tilemap_collision(const blit_tilemap_t *map, blit_pos_t tile);

#ifdef __cplusplus
}
#endif

#endif
