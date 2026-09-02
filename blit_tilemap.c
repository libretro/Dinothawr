/* Dinothawr - a Tiled map (implementation).
 * MSVC C89. See blit_tilemap.h. */

#include "blit_tilemap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <file/file_path.h>

#include "blit_surface_cache.h"
#include "blit_tile_set.h"
#include "blit_xml.h"

#define BLIT_TILEMAP_PATH_MAX 512

struct blit_tilemap
{
   blit_layer_t *layers;
   size_t        layer_count;
   size_t        layer_capacity;

   /* One byte per grid cell rather than a set of positions: the grid is
    * small, bounded and known up front, and this is read once per tile
    * per move. */
   char         *collisions;

   blit_pos_t    position;
   int           width;
   int           height;
   int           tile_w;
   int           tile_h;
   char          dir[BLIT_TILEMAP_PATH_MAX];
   char         *error;
   size_t        error_len;
};

static void map_fail(blit_tilemap_t *map, const char *what,
      const char *detail)
{
   if (map->error && map->error_len)
      snprintf(map->error, map->error_len, "%s: %s", what,
            detail ? detail : "");
}

static char *map_strdup(const char *s)
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

static int map_lower(int c)
{
   return (c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c;
}

/* The .tmx files are not consistent about layer name casing. */
static int map_name_equal(const char *a, const char *b)
{
   if (!a || !b)
      return 0;

   while (*a && *b)
   {
      if (map_lower((unsigned char)*a) != map_lower((unsigned char)*b))
         return 0;
      a++;
      b++;
   }

   return *a == *b;
}

/* Reads <child name= value=> pairs under @parent. First value for a
 * name wins, matching the std::map insert this replaces. */
static blit_attr_table_t *map_attributes(rxml_node_t *parent,
      const char *child)
{
   blit_attr_table_t *table = blit_attr_table_new();
   rxml_node_t       *node;

   if (!table)
      return NULL;

   for (node = blit_xml_child(parent, child); node;
         node = blit_xml_next(node, child))
   {
      const char *name = blit_xml_attr(node, "name");

      if (blit_attr_table_find(table, name))
         continue;

      if (!blit_attr_table_set(table, name, blit_xml_attr(node, "value")))
      {
         blit_attr_table_unref(table);
         return NULL;
      }
   }

   return table;
}

static void map_apply_attrs(blit_surface_t *surf,
      const blit_attr_table_t *attrs)
{
   size_t i;
   for (i = 0; i < blit_attr_table_count(attrs); i++)
      blit_surface_set_attr(surf, blit_attr_table_key_at(attrs, i),
            blit_attr_table_value_at(attrs, i));
}

static int map_add_tileset(blit_tilemap_t *map, blit_tile_set_t *tiles,
      rxml_node_t *node)
{
   blit_attr_table_t *global;
   rxml_node_t       *image  = blit_xml_child(node, "image");
   const char        *source = blit_xml_attr(image, "source");
   int   first_gid = blit_xml_attr_int(node, "firstgid");
   int   tile_w    = blit_xml_attr_int(node, "tilewidth");
   int   tile_h    = blit_xml_attr_int(node, "tileheight");
   int   width     = blit_xml_attr_int(image, "width");
   int   height    = blit_xml_attr_int(image, "height");
   int   id_cnt    = 0;
   int   apng;
   size_t src_len  = strlen(source);
   char  path[BLIT_TILEMAP_PATH_MAX];
   rxml_node_t *tile_node;
   int   ok = 0;

   if (!width || !height || !tile_w || !tile_h)
   {
      map_fail(map, "tileset is malformed", source);
      return 0;
   }

   if (!(global = map_attributes(blit_xml_child(node, "properties"),
               "property")))
      return 0;

   snprintf(path, sizeof(path), "%s/%s", map->dir, source);

   /* An .apng tileset carries one tile per frame, in the same row-major
    * order the sheet was cut in, so the local tile id is the frame
    * number. The image element keeps describing the virtual sheet. */
   apng = src_len >= 5 && strcmp(source + src_len - 5, ".apng") == 0;

   /* Content in the wild mixes level and asset generations, so when the
    * named form is absent and the sibling with the other extension
    * exists, load that one instead. */
   if (!path_is_valid(path))
   {
      char   alt[BLIT_TILEMAP_PATH_MAX];
      size_t len = strlen(path);

      strcpy(alt, path);
      if (apng)
         strcpy(alt + len - 5, ".png");
      else
         strcpy(alt + len - 4, ".apng");

      if (path_is_valid(alt))
      {
         strcpy(path, alt);
         apng = !apng;
      }
   }

   if (apng)
   {
      int count = (width / tile_w) * (height / tile_h);

      for (; id_cnt < count; id_cnt++)
      {
         blit_surface_t tile;

         if (!blit_surface_cache_animation(blit_surface_cache(), path,
                  (unsigned)id_cnt, &tile))
         {
            map_fail(map, "failed to load tileset", path);
            goto done;
         }

         if (tile.rect.w != tile_w || tile.rect.h != tile_h)
         {
            blit_surface_release(&tile);
            map_fail(map, "tileset geometry does not match its image",
                  path);
            goto done;
         }

         map_apply_attrs(&tile, global);
         blit_tile_set_put(tiles, (unsigned)(first_gid + id_cnt), &tile);
         blit_surface_release(&tile);
      }
   }
   else
   {
      blit_surface_t sheet;
      int x;
      int y;

      if (!blit_surface_cache_image(blit_surface_cache(), path, &sheet))
      {
         map_fail(map, "failed to load tileset", path);
         goto done;
      }

      if (sheet.rect.w != width || sheet.rect.h != height)
      {
         blit_surface_release(&sheet);
         map_fail(map, "tileset geometry does not match its image", path);
         goto done;
      }

      for (y = 0; y < height; y += tile_h)
      {
         for (x = 0; x < width; x += tile_w, id_cnt++)
         {
            blit_surface_t tile = blit_surface_sub(&sheet,
                  blit_rect(blit_pos(x, y), tile_w, tile_h));

            map_apply_attrs(&tile, global);
            blit_tile_set_put(tiles, (unsigned)(first_gid + id_cnt),
                  &tile);
            blit_surface_release(&tile);
         }
      }

      blit_surface_release(&sheet);
   }

   /* Per-tile attributes, which may also name a sprite to use instead of
    * the sheet's slice. */
   for (tile_node = blit_xml_child(node, "tile"); tile_node;
         tile_node = blit_xml_next(tile_node, "tile"))
   {
      unsigned id = (unsigned)(first_gid
            + blit_xml_attr_int(tile_node, "id"));
      blit_attr_table_t *attrs = map_attributes(
            blit_xml_child(tile_node, "properties"), "property");
      const char     *sprite;
      blit_surface_t *tile;
      size_t          i;

      if (!attrs)
         goto done;

      /* The tile's own values win over the tileset's. */
      for (i = 0; i < blit_attr_table_count(global); i++)
         if (!blit_attr_table_find(attrs,
                  blit_attr_table_key_at(global, i)))
            blit_attr_table_set(attrs, blit_attr_table_key_at(global, i),
                  blit_attr_table_value_at(global, i));

      if ((sprite = blit_attr_table_find(attrs, "sprite")))
      {
         blit_surface_t from_sprite;
         char           sprite_path[BLIT_TILEMAP_PATH_MAX];

         snprintf(sprite_path, sizeof(sprite_path), "%s/%s", map->dir,
               sprite);

         if (!blit_surface_cache_sprite(blit_surface_cache(),
                  sprite_path, &from_sprite))
         {
            blit_attr_table_unref(attrs);
            map_fail(map, "failed to load tile sprite", sprite_path);
            goto done;
         }

         blit_tile_set_put(tiles, id, &from_sprite);
         blit_surface_release(&from_sprite);
      }

      if (!(tile = blit_tile_set_get(tiles, id)))
      {
         blit_attr_table_unref(attrs);
         map_fail(map, "tile attributes name an undeclared id", source);
         goto done;
      }

      map_apply_attrs(tile, attrs);
      blit_attr_table_unref(attrs);
   }

   ok = 1;

done:
   blit_attr_table_unref(global);
   return ok;
}

static int map_add_layer(blit_tilemap_t *map, blit_tile_set_t *tiles,
      rxml_node_t *node)
{
   blit_layer_t *layer;
   rxml_node_t  *data;
   rxml_node_t  *tile_node;
   int           width  = blit_xml_attr_int(node, "width");
   int           height = blit_xml_attr_int(node, "height");
   int           index  = 0;

   if (!width || !height)
   {
      map_fail(map, "layer is empty", blit_xml_attr(node, "name"));
      return 0;
   }

   if (map->layer_count == map->layer_capacity)
   {
      size_t        new_cap = map->layer_capacity
         ? (map->layer_capacity * 2) : 8;
      blit_layer_t *grown   = (blit_layer_t*)realloc(map->layers,
            new_cap * sizeof(*grown));

      if (!grown)
         return 0;

      map->layers         = grown;
      map->layer_capacity = new_cap;
   }

   layer = &map->layers[map->layer_count];
   blit_surface_cluster_init(&layer->cluster);
   layer->attr = NULL;
   layer->name = NULL;

   data = blit_xml_child(node, "data");

   for (tile_node = blit_xml_child(data, "tile"); tile_node;
         tile_node = blit_xml_next(tile_node, "tile"), index++)
   {
      blit_pos_t      pos = blit_pos(index % width, index / width);
      unsigned        gid = (unsigned)strtoul(
            blit_xml_attr(tile_node, "gid"), NULL, 10);
      blit_surface_t *from;
      blit_surface_t  tile;
      const char     *collides;

      if (!gid)
         continue;

      if (!(from = blit_tile_set_get(tiles, gid)))
      {
         map_fail(map, "layer names an undeclared tile id",
               blit_xml_attr(node, "name"));
         goto error;
      }

      tile = *from;
      blit_surface_retain(&tile);
      tile.rect.pos = blit_pos_mul(pos, blit_pos(map->tile_w,
               map->tile_h));

      blit_surface_cluster_add(&layer->cluster, &tile, blit_pos_zero());

      collides = blit_attr_table_find(tile.attribs, "collision");
      if (collides && strcmp(collides, "true") == 0
            && pos.x >= 0 && pos.x < map->width
            && pos.y >= 0 && pos.y < map->height)
         map->collisions[pos.y * map->width + pos.x] = 1;

      /* The cluster took its own reference. */
      blit_surface_release(&tile);
   }

   layer->attr = map_attributes(blit_xml_child(node, "properties"),
         "property");
   layer->name = map_strdup(blit_xml_attr(node, "name"));

   if (!layer->attr || !layer->name)
      goto error;

   map->layer_count++;
   return 1;

error:
   blit_surface_cluster_release(&layer->cluster);
   blit_attr_table_unref(layer->attr);
   free(layer->name);
   return 0;
}

blit_tilemap_t *blit_tilemap_load(const char *path, char *error,
      size_t error_len)
{
   blit_tilemap_t  *map;
   blit_tile_set_t *tiles = NULL;
   rxml_document_t *doc   = NULL;
   rxml_node_t     *root;
   rxml_node_t     *node;

   if (!(map = (blit_tilemap_t*)calloc(1, sizeof(*map))))
      return NULL;

   map->error     = error;
   map->error_len = error_len;

   {
      const char *slash = strrchr(path, '/');
      size_t      n     = slash ? (size_t)(slash - path) : 0;

      if (n >= sizeof(map->dir))
         n = sizeof(map->dir) - 1;
      memcpy(map->dir, path, n);
      map->dir[n] = '\0';
      if (!slash)
         strcpy(map->dir, ".");
   }

   if (!(doc = blit_xml_load(path)))
   {
      map_fail(map, "failed to load map", path);
      goto error;
   }

   root        = blit_xml_root(doc, "map");
   map->width  = blit_xml_attr_int(root, "width");
   map->height = blit_xml_attr_int(root, "height");
   map->tile_w = blit_xml_attr_int(root, "tilewidth");
   map->tile_h = blit_xml_attr_int(root, "tileheight");

   if (!map->width || !map->height || !map->tile_w || !map->tile_h)
   {
      map_fail(map, "map is malformed", path);
      goto error;
   }

   if (!(map->collisions = (char*)calloc((size_t)map->width
               * (size_t)map->height, 1)))
      goto error;

   if (!(tiles = blit_tile_set_new()))
      goto error;

   for (node = blit_xml_child(root, "tileset"); node;
         node = blit_xml_next(node, "tileset"))
      if (!map_add_tileset(map, tiles, node))
         goto error;

   for (node = blit_xml_child(root, "layer"); node;
         node = blit_xml_next(node, "layer"))
      if (!map_add_layer(map, tiles, node))
         goto error;

   blit_tile_set_free(tiles);
   rxml_free_document(doc);
   return map;

error:
   if (tiles)
      blit_tile_set_free(tiles);
   if (doc)
      rxml_free_document(doc);
   blit_tilemap_free(map);
   return NULL;
}

void blit_tilemap_free(blit_tilemap_t *map)
{
   size_t i;

   if (!map)
      return;

   for (i = 0; i < map->layer_count; i++)
   {
      blit_surface_cluster_release(&map->layers[i].cluster);
      blit_attr_table_unref(map->layers[i].attr);
      free(map->layers[i].name);
   }

   free(map->layers);
   free(map->collisions);
   free(map);
}

int blit_tilemap_tile_width(const blit_tilemap_t *map)
{ return map ? map->tile_w : 0; }

int blit_tilemap_tile_height(const blit_tilemap_t *map)
{ return map ? map->tile_h : 0; }

int blit_tilemap_tiles_width(const blit_tilemap_t *map)
{ return map ? map->width : 0; }

int blit_tilemap_tiles_height(const blit_tilemap_t *map)
{ return map ? map->height : 0; }

int blit_tilemap_pix_width(const blit_tilemap_t *map)
{ return map ? map->width * map->tile_w : 0; }

int blit_tilemap_pix_height(const blit_tilemap_t *map)
{ return map ? map->height * map->tile_h : 0; }

void blit_tilemap_set_pos(blit_tilemap_t *map, blit_pos_t pos)
{
   if (map)
      map->position = pos;
}

void blit_tilemap_render(const blit_tilemap_t *map,
      blit_render_target_t *target)
{
   size_t i;

   if (!map)
      return;

   for (i = 0; i < map->layer_count; i++)
      blit_surface_cluster_render(&map->layers[i].cluster, target,
            map->position);
}

size_t blit_tilemap_layer_count(const blit_tilemap_t *map)
{ return map ? map->layer_count : 0; }

blit_layer_t *blit_tilemap_layer_at(const blit_tilemap_t *map,
      size_t index)
{
   if (!map || index >= map->layer_count)
      return NULL;
   return &map->layers[index];
}

int blit_tilemap_find_layer_index(const blit_tilemap_t *map,
      const char *name)
{
   size_t i;

   if (!map)
      return -1;

   for (i = 0; i < map->layer_count; i++)
      if (map_name_equal(map->layers[i].name, name))
         return (int)i;

   return -1;
}

blit_layer_t *blit_tilemap_find_layer(const blit_tilemap_t *map,
      const char *name)
{
   int index = blit_tilemap_find_layer_index(map, name);
   return index < 0 ? NULL : &map->layers[index];
}

blit_surface_t *blit_tilemap_find_tile_at(const blit_tilemap_t *map,
      unsigned layer_index, blit_pos_t pos)
{
   blit_layer_t        *layer = blit_tilemap_layer_at(map, layer_index);
   blit_cluster_elem_t *elem;

   if (!layer)
      return NULL;

   elem = blit_surface_cluster_find(&layer->cluster, pos);
   return elem ? &elem->surf : NULL;
}

blit_surface_t *blit_tilemap_find_tile(const blit_tilemap_t *map,
      const char *layer_name, blit_pos_t pos)
{
   int index = blit_tilemap_find_layer_index(map, layer_name);

   if (index < 0)
      return NULL;

   return blit_tilemap_find_tile_at(map, (unsigned)index, pos);
}

int blit_tilemap_collision(const blit_tilemap_t *map, blit_pos_t tile)
{
   if (!map)
      return 0;

   if (     tile.x >= 0 && tile.x < map->width
         && tile.y >= 0 && tile.y < map->height
         && map->collisions[tile.y * map->width + tile.x])
      return 1;

   return blit_tilemap_find_tile(map, "blocks",
         blit_pos(tile.x * map->tile_w, tile.y * map->tile_h)) != NULL;
}
