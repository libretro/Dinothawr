#include "game.hpp"
#include "icy_collide.h"
#include "icy_anim.h"
#include "icy_goal.h"
#include "icy_tiles.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace std;

namespace Icy
{
   /* Loads in the initialiser list: the map has to exist before
    * anything that reads its size. */
   static blit_tilemap_t *load_map(const char *path)
   {
      char            err[256];
      blit_tilemap_t *map = blit_tilemap_load(path, err,
            sizeof(err));

      if (!map)
         throw std::runtime_error(err);

      return map;
   }

   Game::Game(const char *level_path, unsigned chapter, unsigned level, unsigned best_pushes, blit_font_cluster_t *font)
      : map(load_map(level_path)),
         player_off(blit_pos_zero()), font(font),
         won_frame_cnt(0), is_sliding(false), best_pushes(best_pushes), pushes(0),
         chapter(chapter), level(level)
   {
      if (!blit_render_target_init_size(&target, fb_width, fb_height))
         throw std::bad_alloc();
      /* player is a raw surface: no constructor to zero it, and the
       * destructor below is what releases it. */
      icy_edge_init(&push);
      /* Entering with push held must not push on the first frame. */
      icy_edge_suppress(&push, ICY_EDGE_OK);
      stepper      = Stepper::None;
      stepper_surf = NULL;
      stepper_dir  = blit_pos_zero();
      icy_leg_begin(&leg, 1);
      blit_surface_init(&player);
      m_failed    = false;
      m_error[0]  = '\0';
      m_won_early = false;
      set_initial_pos(level_path);
      bg = NULL;
   }

   Game::Game(const char *level_path)
      : map(load_map(level_path)),
         player_off(blit_pos_zero()), font(NULL),
         won_frame_cnt(0), is_sliding(false)
   {
      if (!blit_render_target_init_size(&target, fb_width, fb_height))
         throw std::bad_alloc();
      icy_edge_init(&push);
      /* Entering with push held must not push on the first frame. */
      icy_edge_suppress(&push, ICY_EDGE_OK);
      stepper      = Stepper::None;
      stepper_surf = NULL;
      stepper_dir  = blit_pos_zero();
      icy_leg_begin(&leg, 1);
      blit_surface_init(&player);
      m_failed    = false;
      m_error[0]  = '\0';
      m_won_early = false;
      set_initial_pos(level_path);
      bg = NULL;
   }

   Game::~Game()
   {
      blit_tilemap_free(map);
      blit_render_target_release(&target);
      blit_surface_release(&player);
   }

   /* Recorded, not thrown: this is called from the middle of a tick and
    * from the win animation, and iterate() is where a broken level is
    * reported. */
   void Game::fail(const char *what)
   {
      if (m_failed)
         return;

      m_failed = true;
      snprintf(m_error, sizeof(m_error), "%s", what);
   }

   void Game::set_player_alt(const char *id, unsigned index)
   {
      if (!blit_surface_set_active_alt(&player, id, index))
      {
         char msg[192];
         snprintf(msg, sizeof(msg),
               "Player sprite has no face \"%s\" at index %u.", id, index);
         fail(msg);
      }
   }

   void Game::set_player_alt_index(unsigned index)
   {
      set_player_alt(player.active_alt ? player.active_alt : "", index);
   }

   void Game::set_bg(const blit_surface_t& bg)
   {
      this->bg = &bg;
   }

   void Game::set_initial_pos(const char *level)
   {
      blit_layer_t *layer = blit_tilemap_find_layer(map, "floor");
      if (!layer)
         throw runtime_error("Floor layer not found.");

      {
         /* Either the level names a sprite, in which case it is relative
          * to the level, or the sprite is the level's own name with the
          * extension swapped. */
         const char *named = attr_or(layer->attr, "player_sprite", "");
         char        path[512];

         if (*named)
         {
            const char *slash = strrchr(level, '/');
            int         dir   = slash ? (int)(slash - level) : 1;

            snprintf(path, sizeof(path), "%.*s/%s",
                  slash ? dir : 1, slash ? level : ".", named);
         }
         else
            snprintf(path, sizeof(path), "%s.sprite", level);

         /* The cache hands over ownership. */
         blit_surface_t sprite;

         if (!blit_surface_cache_sprite(blit_surface_cache(), path,
                  &sprite))
            throw runtime_error(
                  blit_surface_cache_error(blit_surface_cache()));

         blit_surface_release(&player);
         player = sprite;
      }

      int x     = atoi(attr_or(layer->attr, "start_x", "1"));
      int y     = atoi(attr_or(layer->attr, "start_y", "1"));
      int off_x = atoi(attr_or(layer->attr, "player_offset_x", "0"));
      int off_y = atoi(attr_or(layer->attr, "player_offset_y", "0"));
      const char *face = attr_or(layer->attr, "start_facing", "right");

      player.rect.pos = blit_pos(x * blit_tilemap_tile_width(map), y * blit_tilemap_tile_height(map));
      player_off = blit_pos(off_x, off_y);
      facing = icy_input_from_face(face);
      set_player_alt(face);
   }

   void Game::iterate()
   {
      /* One place a broken level surfaces, and one exception for it. */
      if (m_failed)
         throw runtime_error(m_error);

      update_player();

      if (m_failed)
         throw runtime_error(m_error);

      if (bg)
         blit_render_target_blit(&target, bg, blit_rect_zero());
      else
         blit_render_target_clear(&target, blit_pixel_argb(0x00, 0x00, 0x00, 0x00));

      /* The camera follows the player over the map; both are read fresh,
       * so nothing has to be told when either moves. */
      icy_camera_update(&target, player.rect,
            blit_pos(blit_tilemap_pix_width(map),
               blit_tilemap_pix_height(map)));

      blit_tilemap_render(map, &target);
      blit_render_target_blit_offset(&target, &player, blit_rect_zero(), player_off);

      if (font)
      {
         blit_font_cluster_set_id(font, "lime");
         char hud[64];

         snprintf(hud, sizeof(hud), "%u-%u", chapter + 1, level + 1);
         blit_font_cluster_render(font, &target, hud,
               314, 184, BLIT_FONT_RIGHT, 0);
         if (!best_pushes)
         {
            snprintf(hud, sizeof(hud), " Pushes:%u", pushes);
            blit_font_cluster_render(font, &target, hud,
               2, 184, BLIT_FONT_LEFT, 0);
         }
         else
         {
            snprintf(hud, sizeof(hud), " Pushes:%u Best:%u", pushes,
                  best_pushes);
            blit_font_cluster_render(font, &target, hud,
               2, 184, BLIT_FONT_LEFT, 0);
         }
      }

      if (m_video_cb)
         m_video_cb(m_video_ctx, target.buffer, target.rect.w, target.rect.h, target.rect.w * sizeof(blit_pixel_t));
   }

   /* A plain scan rather than copy_if over a reference_wrapper vector:
    * the elements are about to stop living in a std::vector, and this
    * form does not care what holds them. */
   size_t Game::get_tiles_with_attr(const char *name, const char *attr,
         const char *val, blit_cluster_elem_t **out)
   {
      return icy_tiles_with_attr(map, name, attr, val, out,
            max_tagged_tiles);
   }

   bool Game::win_animation_stepper()
   {
      won_frame_cnt++;

      blit_cluster_elem_t *goal_blocks[max_tagged_tiles];
      size_t goal_block_count = get_tiles_with_attr("blocks", "goal",
            "true", goal_blocks);

      const unsigned frame_per_iter = icy_frames_to_ticks(won_frames_per_iter);
      size_t         i;

      const char *state = "frozen";
      if (won_frame_cnt >= 3 * frame_per_iter)
      {
         bool jump = ((won_frame_cnt / frame_per_iter - 3) >> 1) & 1;
         unsigned last_jump = (((won_frame_cnt - 1) / frame_per_iter - 3) >> 1) & 1;
         state = jump ? "cheer" : "down";
         set_player_alt(state);

         if (jump && !last_jump)
            icy_sfx_play(icy_sfx(), "dino_jump", 0.4f);
      }
      else if (won_frame_cnt >= 2 * frame_per_iter)
         state = "defrost2";
      else if (won_frame_cnt >= 1 * frame_per_iter)
         state = "defrost1";

      /* Over the count, not the array: the array is fixed-size and only
       * its first goal_block_count entries were filled. */
      for (i = 0; i < goal_block_count; i++)
      {
         blit_cluster_elem_t *block = goal_blocks[i];

         blit_surface_set_active_alt(&block->surf, state, 0);

         /* Shift a defrosted block the same way the player sprite is
          * shifted (16x17 and so on), but only once defrost starts. */
         if (won_frame_cnt >= 1 * frame_per_iter)
            block->offset = player_off;
      }

      m_won_early = (won_frame_cnt >= frame_per_iter * 3) && icy_edge_pressed(&push, ICY_EDGE_OK, m_input_cb(m_input_ctx, ICY_INPUT_PUSH));
      return true;
   }

   void Game::prepare_won_animation()
   {
      won_frame_cnt = 1;
      player_walking = false;

      icy_edge_suppress(&push, ICY_EDGE_OK); /* Don't exit the win animation early. */
      m_won_early = false;
      stepper = Stepper::WinAnimation;
      icy_sfx_play(icy_sfx(), "frozen_dino_melt", 0.25f);
   }

   bool Game::won() const
   {
      return m_won_early
         || (won_frame_cnt >= icy_frames_to_ticks(won_frame_cnt_limit));
   }

   bool Game::won_condition()
   {
      blit_cluster_elem_t *goal_floor[max_tagged_tiles];
      blit_cluster_elem_t *goal_blocks[max_tagged_tiles];
      size_t floor_count  = get_tiles_with_attr("floor",  "goal", "true",
            goal_floor);
      size_t block_count  = get_tiles_with_attr("blocks", "goal", "true",
            goal_blocks);
      blit_pos_t goals[max_tagged_tiles];
      blit_pos_t blocks[max_tagged_tiles];
      size_t i;

      if (floor_count != block_count)
      {
         fail("Number of goal floors and goal blocks do not match.");
         return false;
      }

      if (!floor_count)
      {
         fail("Goal floor or blocks are empty.");
         return false;
      }

      if (floor_count > max_tagged_tiles)
      {
         fail("Level has more goal tiles than the game can track.");
         return false;
      }

      for (i = 0; i < floor_count; i++)
      {
         goals[i]  = goal_floor[i]->surf.rect.pos;
         blocks[i] = goal_blocks[i]->surf.rect.pos;
      }

      return icy_goal_all_covered(goals, blocks, floor_count) != 0;
   }

   void Game::update_player()
   {
      if (!m_input_cb)
         return;
      
      bool had_stepper = stepper != Stepper::None;
      run_stepper();

      if (won_frame_cnt)
         return;

      if (stepper == Stepper::None)
         update_input();
      else
         update_triggers();

      // Reset animation.
      if (!had_stepper && stepper != Stepper::None)
         frame_cnt = 0;
      else if (stepper == Stepper::None)
      {
         frame_cnt = 0;
         set_player_alt_index(ICY_ANIM_STILL);
      }

      if (stepper != Stepper::None && player_walking)
         update_animation();

      if (won_condition())
         prepare_won_animation();
   }

   void Game::update_animation()
   {
      frame_cnt++;

      /* frame_cnt counts ticks, so the period is the tick count that
       * spans what used to be 10 frames - the cycle changes at the same
       * wall-clock moments at 60 Hz and at 240. */
      set_player_alt_index(icy_anim_moving(frame_cnt,
               icy_frames_to_ticks(anim_frames_per_step), is_sliding));
   }

   void Game::update_triggers()
   {
      icy_edge_pressed(&push, ICY_EDGE_OK, m_input_cb(m_input_ctx, ICY_INPUT_PUSH));
   }

   void Game::update_input()
   {
      bool push_trigger = icy_edge_pressed(&push, ICY_EDGE_OK, m_input_cb(m_input_ctx, ICY_INPUT_PUSH));

      if (push_trigger)
         push_block();
      else if (m_input_cb(m_input_ctx, ICY_INPUT_UP))
         move_if_no_collision(ICY_INPUT_UP);
      else if (m_input_cb(m_input_ctx, ICY_INPUT_DOWN))
         move_if_no_collision(ICY_INPUT_DOWN);
      else if (m_input_cb(m_input_ctx, ICY_INPUT_LEFT))
         move_if_no_collision(ICY_INPUT_LEFT);
      else if (m_input_cb(m_input_ctx, ICY_INPUT_RIGHT))
         move_if_no_collision(ICY_INPUT_RIGHT);
   }

   /* The check itself is grid arithmetic and lives in icy_collide.c;
    * what stays here is the assertion, because only the game knows that
    * an off-grid surface means a broken level rather than a bad call. */
   bool Game::is_offset_collision(blit_surface_t& surf, blit_pos_t offset)
   {
      if (!icy_collide_aligned(map, surf.rect))
      {
         /* Report a collision so nothing moves further off the grid
          * before iterate() raises this. */
         fail("Offset collision check was performed outside tile grid.");
         return true;
      }

      return icy_collide_offset(map, surf.rect, offset) != 0;
   }

   void Game::push_block()
   {
      blit_pos_t offset = icy_input_offset(facing);
      blit_pos_t dir    = blit_pos_mul(offset,
            blit_pos(blit_tilemap_tile_width(map),
               blit_tilemap_tile_height(map)));
      blit_surface_t *tile  = blit_tilemap_find_tile(map, "blocks",
            blit_pos_add(player.rect.pos, dir));

      if (!tile)
         return;

      int tile_x = player.rect.pos.x / blit_tilemap_tile_width(map);
      int tile_y = player.rect.pos.y / blit_tilemap_tile_height(map);
      blit_pos_t tile_pos = blit_pos(tile_x, tile_y);

      if (!blit_tilemap_collision(map,
               blit_pos_add(tile_pos, blit_pos_scale(2, offset))))
      {
         begin_tile_stepper(*tile, offset);
         begin_leg(offset.x ? blit_tilemap_tile_width(map) : blit_tilemap_tile_height(map));
         stepper_cnt = 0;
         player_walking = false;
         set_player_alt_index(ICY_ANIM_STILL);
         icy_sfx_play(icy_sfx(), "dino_push", 1.0f);
         pushes++;
      }
   }

   void Game::move_if_no_collision(enum icy_input input)
   {
      facing = input;
      set_player_alt(icy_input_face(facing));

      blit_pos_t offset = icy_input_offset(input);
      if (!is_offset_collision(player, offset))
      {
         begin_tile_stepper(player, offset);
         begin_leg(offset.x ? blit_tilemap_tile_width(map) : blit_tilemap_tile_height(map));
         player_walking = true;
      }
   }

   /* A leg covers one tile. At 60 Hz the sim moved 2 px per tick, so a
    * 16 px tile took 8; keeping that as the duration and interpolating
    * the position across it gives 1 px per tick at 120 Hz and lands on
    * the grid exactly at the end of the leg whatever the rate. */
   void Game::begin_leg(int tile_size)
   {
      icy_leg_begin(&leg, icy_frames_to_ticks(tile_size / 2));
   }

   bool Game::tile_stepper(blit_surface_t& surf, blit_pos_t step_dir)
   {
      int tile_size = step_dir.x ? blit_tilemap_tile_width(map) : blit_tilemap_tile_height(map);

      surf.rect = blit_rect_offset(surf.rect,
            blit_pos_scale(icy_leg_step(&leg, tile_size), step_dir));

      if (!player_walking)
      {
         set_player_alt_index(icy_anim_pushing(stepper_cnt,
                  icy_frames_to_ticks(push_anim_frames)));
         stepper_cnt++;
      }

      if (!icy_leg_done(&leg))
         return true;

      begin_leg(tile_size);

      if (is_offset_collision(surf, step_dir))
      {
         is_sliding = false;

         if (&surf != &player)
            icy_sfx_play(icy_sfx(), "ice_bump", 0.25f);

         return false;
      }

      //cerr << "Player: " << player.rect.pos << " Surf: " << surf->rect().pos << endl; 
      blit_surface_t *surface = blit_tilemap_find_tile(map, "floor", surf.rect.pos);
      const char *slip = surface ? blit_attr_table_find(surface->attribs,
            &surf == &player ? "slippery_player" : "slippery_block") : NULL;
      bool slippery = slip && std::strcmp(slip, "true") == 0;

      is_sliding = slippery;
      return slippery;
   }

   void Game::begin_tile_stepper(blit_surface_t& surf, blit_pos_t dir)
   {
      stepper      = Stepper::Tile;
      stepper_surf = &surf;
      stepper_dir  = dir;
   }

   void Game::run_stepper()
   {
      bool more;

      switch (stepper)
      {
         case Stepper::Tile:
            more = tile_stepper(*stepper_surf, stepper_dir);
            break;
         case Stepper::WinAnimation:
            more = win_animation_stepper();
            break;
         default:
            return;
      }

      if (!more)
         stepper = Stepper::None;
   }
}


