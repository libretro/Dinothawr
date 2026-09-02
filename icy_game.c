/* Dinothawr - a level in play (implementation).
 * MSVC C89. See icy_game.h. */

#include "icy_game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <compat/msvc.h>

#include "blit_render_target.h"
#include "blit_tilemap.h"
#include "blit_surface_cache.h"
#include "audio/game_audio.h"
#include "icy_anim.h"
#include "icy_camera.h"
#include "icy_collide.h"
#include "icy_edge.h"
#include "icy_goal.h"
#include "icy_leg.h"
#include "icy_path.h"
#include "icy_rate.h"
#include "icy_tiles.h"

/* Durations in 60 Hz frames; icy_frames_to_ticks turns them into tick
 * counts at the rate actually being run. */
enum
{
   ICY_WON_FRAME_LIMIT    = 60 * 5,
   ICY_WON_FRAMES_PER_ITER = 24,
   ICY_ANIM_FRAMES_PER_STEP = 10,
   ICY_PUSH_ANIM_FRAMES   = 7,

   /* Levels have a handful of goal squares, so the searches fill a
    * fixed array rather than allocating. */
   ICY_MAX_TAGGED_TILES   = 64
};

/* What is being stepped, if anything. There are exactly two things it
 * can be - a surface crossing a tile, or the win animation. */
enum icy_stepper
{
   ICY_STEPPER_NONE = 0,
   ICY_STEPPER_TILE,
   ICY_STEPPER_WIN
};

struct icy_game
{
   blit_tilemap_t       *map;
   blit_render_target_t  target;
   blit_surface_t        player;
   blit_pos_t            player_off;
   blit_font_cluster_t  *font;
   const blit_surface_t *bg;
   enum icy_input        facing;

   unsigned              won_frame_cnt;
   int                   won_early;
   int                   failed;
   char                  error[192];

   icy_input_fn          input_cb;
   void                 *input_ctx;
   icy_video_fn          video_cb;
   void                 *video_ctx;

   enum icy_stepper      stepper;
   blit_surface_t       *stepper_surf;
   blit_pos_t            stepper_dir;

   unsigned              frame_cnt;
   int                   player_walking;
   int                   is_sliding;
   unsigned              stepper_cnt;

   icy_leg_t             leg;

   unsigned              best_pushes;
   unsigned              pushes;
   unsigned              chapter;
   unsigned              level;

   icy_edge_t            push;
};

static const char *attr_or(const blit_attr_table_t *table, const char *key,
      const char *fallback)
{
   const char *value = blit_attr_table_find(table, key);
   return value ? value : fallback;
}

/* Recorded, not reported where it is noticed: this runs from the middle
 * of a tick, and icy_game_iterate is where a broken level surfaces. */
static void game_fail(icy_game_t *g, const char *what)
{
   if (g->failed)
      return;

   g->failed = 1;
   snprintf(g->error, sizeof(g->error), "%s", what);
}

static void game_set_player_alt(icy_game_t *g, const char *id,
      unsigned index)
{
   if (!blit_surface_set_active_alt(&g->player, id, index))
   {
      char msg[192];

      snprintf(msg, sizeof(msg),
            "Player sprite has no face \"%s\" at index %u.", id, index);
      game_fail(g, msg);
   }
}

static void game_set_player_alt_index(icy_game_t *g, unsigned index)
{
   game_set_player_alt(g, g->player.active_alt ? g->player.active_alt : "",
         index);
}

static size_t game_tiles_with_attr(icy_game_t *g, const char *layer,
      const char *attr, const char *val, blit_cluster_elem_t **out)
{
   return icy_tiles_with_attr(g->map, layer, attr, val, out,
         ICY_MAX_TAGGED_TILES);
}

static int game_set_initial_pos(icy_game_t *g, const char *level,
      char *error, size_t error_len)
{
   blit_layer_t *layer = blit_tilemap_find_layer(g->map, "floor");
   const char   *named;
   const char   *face;
   char          path[512];
   int           x;
   int           y;

   if (!layer)
   {
      if (error)
         snprintf(error, error_len, "Floor layer not found.");
      return 0;
   }

   /* Either the level names a sprite, in which case it is relative to
    * the level, or the sprite is the level's own name with the extension
    * swapped. */
   named = attr_or(layer->attr, "player_sprite", "");

   if (*named)
   {
      char dir[512];

      icy_path_dir(dir, sizeof(dir), level);
      icy_path_join(path, sizeof(path), dir, named);
   }
   else
      snprintf(path, sizeof(path), "%s.sprite", level);

   {
      blit_surface_t sprite;

      /* The cache hands over ownership. */
      if (!blit_surface_cache_sprite(blit_surface_cache(), path, &sprite))
      {
         if (error)
            snprintf(error, error_len, "%s",
                  blit_surface_cache_error(blit_surface_cache()));
         return 0;
      }

      blit_surface_release(&g->player);
      g->player = sprite;
   }

   x    = atoi(attr_or(layer->attr, "start_x", "1"));
   y    = atoi(attr_or(layer->attr, "start_y", "1"));
   face = attr_or(layer->attr, "start_facing", "right");

   g->player.rect.pos = blit_pos(x * blit_tilemap_tile_width(g->map),
         y * blit_tilemap_tile_height(g->map));
   g->player_off = blit_pos(
         atoi(attr_or(layer->attr, "player_offset_x", "0")),
         atoi(attr_or(layer->attr, "player_offset_y", "0")));
   g->facing = icy_input_from_face(face);
   game_set_player_alt(g, face, 0);
   return 1;
}

icy_game_t *icy_game_new(const char *path, unsigned chapter,
      unsigned level, unsigned best_pushes, blit_font_cluster_t *font,
      char *error, size_t error_len)
{
   icy_game_t *g = (icy_game_t*)calloc(1, sizeof(*g));

   if (!g)
      return NULL;

   /* The map first: everything that follows reads its size. */
   if (!(g->map = blit_tilemap_load(path, error, error_len)))
   {
      free(g);
      return NULL;
   }

   if (!blit_render_target_init_size(&g->target, ICY_GAME_FB_WIDTH,
            ICY_GAME_FB_HEIGHT))
   {
      if (error)
         snprintf(error, error_len, "Out of memory.");
      blit_tilemap_free(g->map);
      free(g);
      return NULL;
   }

   blit_surface_init(&g->player);
   icy_edge_init(&g->push);
   /* Entering with push held must not push on the first frame. */
   icy_edge_suppress(&g->push, ICY_EDGE_OK);
   icy_leg_begin(&g->leg, 1);

   g->font        = font;
   g->chapter     = chapter;
   g->level       = level;
   g->best_pushes = best_pushes;

   if (!game_set_initial_pos(g, path, error, error_len))
   {
      icy_game_free(g);
      return NULL;
   }

   return g;
}

void icy_game_free(icy_game_t *g)
{
   if (!g)
      return;

   blit_tilemap_free(g->map);
   blit_render_target_release(&g->target);
   blit_surface_release(&g->player);
   free(g);
}

void icy_game_set_input_cb(icy_game_t *g, icy_input_fn cb, void *ctx)
{
   g->input_cb  = cb;
   g->input_ctx = ctx;
}

void icy_game_set_video_cb(icy_game_t *g, icy_video_fn cb, void *ctx)
{
   g->video_cb  = cb;
   g->video_ctx = ctx;
}

void icy_game_set_bg(icy_game_t *g, const blit_surface_t *bg)
{
   g->bg = bg;
}

const char *icy_game_error(const icy_game_t *g)
{
   return g ? g->error : "";
}

unsigned icy_game_pushes(const icy_game_t *g)
{
   return g ? g->pushes : 0;
}

int icy_game_won(const icy_game_t *g)
{
   return g->won_early
      || (g->won_frame_cnt >= icy_frames_to_ticks(ICY_WON_FRAME_LIMIT));
}

/* The check itself is grid arithmetic and lives in icy_collide.c; what
 * stays here is the assertion, because only the game knows that an
 * off-grid surface means a broken level rather than a bad call. */
static int game_offset_collision(icy_game_t *g, blit_surface_t *surf,
      blit_pos_t offset)
{
   if (!icy_collide_aligned(g->map, surf->rect))
   {
      /* Report a collision so nothing moves further off the grid before
       * iterate reports this. */
      game_fail(g, "Offset collision check was performed outside tile grid.");
      return 1;
   }

   return icy_collide_offset(g->map, surf->rect, offset);
}

/* A leg covers one tile. At 60 Hz the sim moved 2 px per tick, so a
 * 16 px tile took 8; keeping that as the duration and interpolating the
 * position across it gives 1 px per tick at 120 Hz and lands on the grid
 * exactly at the end of the leg whatever the rate. */
static void game_begin_leg(icy_game_t *g, int tile_size)
{
   icy_leg_begin(&g->leg, icy_frames_to_ticks((unsigned)tile_size / 2));
}

static void game_begin_tile_stepper(icy_game_t *g, blit_surface_t *surf,
      blit_pos_t dir)
{
   g->stepper      = ICY_STEPPER_TILE;
   g->stepper_surf = surf;
   g->stepper_dir  = dir;
}

static int game_tile_stepper(icy_game_t *g, blit_surface_t *surf,
      blit_pos_t step_dir)
{
   int tile_size = step_dir.x ? blit_tilemap_tile_width(g->map)
      : blit_tilemap_tile_height(g->map);
   blit_surface_t *floor;
   const char     *slip;

   surf->rect = blit_rect_offset(surf->rect,
         blit_pos_scale(icy_leg_step(&g->leg, tile_size), step_dir));

   if (!g->player_walking)
   {
      game_set_player_alt_index(g, icy_anim_pushing(g->stepper_cnt,
               icy_frames_to_ticks(ICY_PUSH_ANIM_FRAMES)));
      g->stepper_cnt++;
   }

   if (!icy_leg_done(&g->leg))
      return 1;

   game_begin_leg(g, tile_size);

   if (game_offset_collision(g, surf, step_dir))
   {
      g->is_sliding = 0;

      if (surf != &g->player)
         icy_sfx_play(icy_sfx(), "ice_bump", 0.25f);

      return 0;
   }

   floor = blit_tilemap_find_tile(g->map, "floor", surf->rect.pos);
   slip  = floor ? blit_attr_table_find(floor->attribs,
         surf == &g->player ? "slippery_player" : "slippery_block")
      : NULL;

   g->is_sliding = slip && strcmp(slip, "true") == 0;
   return g->is_sliding;
}

static int game_win_animation_stepper(icy_game_t *g)
{
   blit_cluster_elem_t *goal_blocks[ICY_MAX_TAGGED_TILES];
   size_t   goal_block_count;
   unsigned frame_per_iter;
   const char *state = "frozen";
   size_t   i;

   g->won_frame_cnt++;

   goal_block_count = game_tiles_with_attr(g, "blocks", "goal", "true",
         goal_blocks);
   frame_per_iter   = icy_frames_to_ticks(ICY_WON_FRAMES_PER_ITER);

   if (g->won_frame_cnt >= 3 * frame_per_iter)
   {
      unsigned jump = ((g->won_frame_cnt / frame_per_iter - 3) >> 1) & 1;
      unsigned last = (((g->won_frame_cnt - 1) / frame_per_iter - 3) >> 1) & 1;

      state = jump ? "cheer" : "down";
      game_set_player_alt(g, state, 0);

      if (jump && !last)
         icy_sfx_play(icy_sfx(), "dino_jump", 0.4f);
   }
   else if (g->won_frame_cnt >= 2 * frame_per_iter)
      state = "defrost2";
   else if (g->won_frame_cnt >= 1 * frame_per_iter)
      state = "defrost1";

   /* Over the count, not the array: only its first goal_block_count
    * entries were filled. */
   for (i = 0; i < goal_block_count; i++)
   {
      blit_surface_set_active_alt(&goal_blocks[i]->surf, state, 0);

      /* Shift a defrosted block the same way the player sprite is
       * shifted (16x17 and so on), but only once defrost starts. */
      if (g->won_frame_cnt >= 1 * frame_per_iter)
         goal_blocks[i]->offset = g->player_off;
   }

   g->won_early = (g->won_frame_cnt >= frame_per_iter * 3)
      && icy_edge_pressed(&g->push, ICY_EDGE_OK,
            g->input_cb(g->input_ctx, ICY_INPUT_PUSH));

   return 1;
}

static void game_prepare_won_animation(icy_game_t *g)
{
   g->won_frame_cnt  = 1;
   g->player_walking = 0;

   /* Don't exit the win animation early. */
   icy_edge_suppress(&g->push, ICY_EDGE_OK);
   g->won_early = 0;
   g->stepper   = ICY_STEPPER_WIN;
   icy_sfx_play(icy_sfx(), "frozen_dino_melt", 0.25f);
}

static int game_won_condition(icy_game_t *g)
{
   blit_cluster_elem_t *goal_floor[ICY_MAX_TAGGED_TILES];
   blit_cluster_elem_t *goal_blocks[ICY_MAX_TAGGED_TILES];
   blit_pos_t goals[ICY_MAX_TAGGED_TILES];
   blit_pos_t blocks[ICY_MAX_TAGGED_TILES];
   size_t floor_count;
   size_t block_count;
   size_t i;

   floor_count = game_tiles_with_attr(g, "floor",  "goal", "true",
         goal_floor);
   block_count = game_tiles_with_attr(g, "blocks", "goal", "true",
         goal_blocks);

   if (floor_count != block_count)
   {
      game_fail(g, "Number of goal floors and goal blocks do not match.");
      return 0;
   }

   if (!floor_count)
   {
      game_fail(g, "Goal floor or blocks are empty.");
      return 0;
   }

   if (floor_count > ICY_MAX_TAGGED_TILES)
   {
      game_fail(g, "Level has more goal tiles than the game can track.");
      return 0;
   }

   for (i = 0; i < floor_count; i++)
   {
      goals[i]  = goal_floor[i]->surf.rect.pos;
      blocks[i] = goal_blocks[i]->surf.rect.pos;
   }

   return icy_goal_all_covered(goals, blocks, floor_count);
}

static void game_run_stepper(icy_game_t *g)
{
   int more;

   switch (g->stepper)
   {
      case ICY_STEPPER_TILE:
         more = game_tile_stepper(g, g->stepper_surf, g->stepper_dir);
         break;
      case ICY_STEPPER_WIN:
         more = game_win_animation_stepper(g);
         break;
      default:
         return;
   }

   if (!more)
      g->stepper = ICY_STEPPER_NONE;
}

static void game_move_if_no_collision(icy_game_t *g, enum icy_input input)
{
   blit_pos_t offset;

   g->facing = input;
   game_set_player_alt(g, icy_input_face(g->facing), 0);

   offset = icy_input_offset(input);

   if (!game_offset_collision(g, &g->player, offset))
   {
      game_begin_tile_stepper(g, &g->player, offset);
      game_begin_leg(g, offset.x ? blit_tilemap_tile_width(g->map)
            : blit_tilemap_tile_height(g->map));
      g->player_walking = 1;
   }
}

static void game_push_block(icy_game_t *g)
{
   blit_pos_t offset = icy_input_offset(g->facing);
   blit_pos_t dir    = blit_pos_mul(offset,
         blit_pos(blit_tilemap_tile_width(g->map),
            blit_tilemap_tile_height(g->map)));
   blit_surface_t *tile = blit_tilemap_find_tile(g->map, "blocks",
         blit_pos_add(g->player.rect.pos, dir));
   blit_pos_t tile_pos;

   if (!tile)
      return;

   tile_pos = blit_pos(
         g->player.rect.pos.x / blit_tilemap_tile_width(g->map),
         g->player.rect.pos.y / blit_tilemap_tile_height(g->map));

   if (!blit_tilemap_collision(g->map,
            blit_pos_add(tile_pos, blit_pos_scale(2, offset))))
   {
      game_begin_tile_stepper(g, tile, offset);
      game_begin_leg(g, offset.x ? blit_tilemap_tile_width(g->map)
            : blit_tilemap_tile_height(g->map));
      g->stepper_cnt    = 0;
      g->player_walking = 0;
      game_set_player_alt_index(g, ICY_ANIM_STILL);
      icy_sfx_play(icy_sfx(), "dino_push", 1.0f);
      g->pushes++;
   }
}

static void game_update_input(icy_game_t *g)
{
   if (icy_edge_pressed(&g->push, ICY_EDGE_OK,
            g->input_cb(g->input_ctx, ICY_INPUT_PUSH)))
      game_push_block(g);
   else if (g->input_cb(g->input_ctx, ICY_INPUT_UP))
      game_move_if_no_collision(g, ICY_INPUT_UP);
   else if (g->input_cb(g->input_ctx, ICY_INPUT_DOWN))
      game_move_if_no_collision(g, ICY_INPUT_DOWN);
   else if (g->input_cb(g->input_ctx, ICY_INPUT_LEFT))
      game_move_if_no_collision(g, ICY_INPUT_LEFT);
   else if (g->input_cb(g->input_ctx, ICY_INPUT_RIGHT))
      game_move_if_no_collision(g, ICY_INPUT_RIGHT);
}

static void game_update_triggers(icy_game_t *g)
{
   icy_edge_pressed(&g->push, ICY_EDGE_OK,
         g->input_cb(g->input_ctx, ICY_INPUT_PUSH));
}

static void game_update_animation(icy_game_t *g)
{
   g->frame_cnt++;

   /* frame_cnt counts ticks, so the period is the tick count that spans
    * what used to be 10 frames - the cycle changes at the same
    * wall-clock moments at 60 Hz and at 240. */
   game_set_player_alt_index(g, icy_anim_moving(g->frame_cnt,
            icy_frames_to_ticks(ICY_ANIM_FRAMES_PER_STEP), g->is_sliding));
}

static void game_update_player(icy_game_t *g)
{
   int had_stepper;

   if (!g->input_cb)
      return;

   had_stepper = g->stepper != ICY_STEPPER_NONE;
   game_run_stepper(g);

   if (g->won_frame_cnt)
      return;

   if (g->stepper == ICY_STEPPER_NONE)
      game_update_input(g);
   else
      game_update_triggers(g);

   /* Reset animation. */
   if (!had_stepper && g->stepper != ICY_STEPPER_NONE)
      g->frame_cnt = 0;
   else if (g->stepper == ICY_STEPPER_NONE)
   {
      g->frame_cnt = 0;
      game_set_player_alt_index(g, ICY_ANIM_STILL);
   }

   if (g->stepper != ICY_STEPPER_NONE && g->player_walking)
      game_update_animation(g);

   if (game_won_condition(g))
      game_prepare_won_animation(g);
}

int icy_game_iterate(icy_game_t *g)
{
   /* One place a broken level surfaces. */
   if (g->failed)
      return 0;

   game_update_player(g);

   if (g->failed)
      return 0;

   if (g->bg)
      blit_render_target_blit(&g->target, g->bg, blit_rect_zero());
   else
      blit_render_target_clear(&g->target,
            blit_pixel_argb(0x00, 0x00, 0x00, 0x00));

   /* The camera follows the player over the map; both are read fresh, so
    * nothing has to be told when either moves. */
   icy_camera_update(&g->target, g->player.rect,
         blit_pos(blit_tilemap_pix_width(g->map),
            blit_tilemap_pix_height(g->map)));

   blit_tilemap_render(g->map, &g->target);
   blit_render_target_blit_offset(&g->target, &g->player, blit_rect_zero(),
         g->player_off);

   if (g->font)
   {
      char hud[64];

      blit_font_cluster_set_id(g->font, "lime");

      snprintf(hud, sizeof(hud), "%u-%u", g->chapter + 1, g->level + 1);
      blit_font_cluster_render(g->font, &g->target, hud, 314, 184,
            BLIT_FONT_RIGHT, 0);

      if (!g->best_pushes)
         snprintf(hud, sizeof(hud), " Pushes:%u", g->pushes);
      else
         snprintf(hud, sizeof(hud), " Pushes:%u Best:%u", g->pushes,
               g->best_pushes);

      blit_font_cluster_render(g->font, &g->target, hud, 2, 184,
            BLIT_FONT_LEFT, 0);
   }

   if (g->video_cb)
      g->video_cb(g->video_ctx, g->target.buffer, g->target.rect.w,
            g->target.rect.h,
            g->target.rect.w * sizeof(blit_pixel_t));

   return 1;
}
