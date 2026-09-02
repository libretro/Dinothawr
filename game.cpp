#include "game.hpp"
#include "icy_collide.h"
#include "icy_anim.h"
#include <cstring>
#include "utils.hpp"
#include <iostream>
#include <stdexcept>

using namespace Blit;
using namespace std;

namespace Icy
{
   /* Loads in the initialiser list: the map has to exist before
    * anything that reads its size. */
   static blit_tilemap_t *load_map(const std::string& path)
   {
      char            err[256];
      blit_tilemap_t *map = blit_tilemap_load(path.c_str(), err,
            sizeof(err));

      if (!map)
         throw std::runtime_error(err);

      return map;
   }

   EdgeDetector::EdgeDetector(bool init) : pos(init)
   {}

   bool EdgeDetector::set(bool state)
   {
      bool ret = state && !pos;
      pos = state;
      return ret;
   }

   Game::Game(const string& level_path, unsigned chapter, unsigned level, unsigned best_pushes, blit_font_cluster_t *font)
      : map(load_map(level_path)),
         player_off(blit_pos_zero()), font(font),
         won_frame_cnt(0), is_sliding(false), best_pushes(best_pushes), pushes(0),
         chapter(chapter), level(level), push(true) 
   {
      if (!blit_render_target_init_size(&target, fb_width, fb_height))
         throw std::bad_alloc();
      /* player is a raw surface: no constructor to zero it, and the
       * destructor below is what releases it. */
      stepper      = Stepper::None;
      stepper_surf = NULL;
      stepper_dir  = blit_pos_zero();
      icy_leg_begin(&leg, 1);
      blit_surface_init(&player);
      m_won_early = false;
      set_initial_pos(level_path);
      bg = NULL;
   }

   Game::Game(const string& level_path)
      : map(load_map(level_path)),
         player_off(blit_pos_zero()), font(NULL),
         won_frame_cnt(0), is_sliding(false), push(true)
   {
      if (!blit_render_target_init_size(&target, fb_width, fb_height))
         throw std::bad_alloc();
      stepper      = Stepper::None;
      stepper_surf = NULL;
      stepper_dir  = blit_pos_zero();
      icy_leg_begin(&leg, 1);
      blit_surface_init(&player);
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

   void Game::set_player_alt(const string& id, unsigned index)
   {
      if (!blit_surface_set_active_alt(&player, id.c_str(), index))
         throw logic_error(Utils::join("Player sprite has no face \"",
                  id, "\" at index ", index, "."));
   }

   void Game::set_player_alt_index(unsigned index)
   {
      set_player_alt(player.active_alt ? player.active_alt : "", index);
   }

   void Game::set_bg(const blit_surface_t& bg)
   {
      this->bg = &bg;
   }

   void Game::set_initial_pos(const string& level)
   {
      blit_layer_t *layer = blit_tilemap_find_layer(map, "floor");
      if (!layer)
         throw runtime_error("Floor layer not found.");

      std::basic_string<char> sprite_path = attr_or(layer->attr, "player_sprite", "");
      {
         /* The cache hands back a wrapper; take the raw surface out of
          * it and keep our own reference. */
         blit_surface_t sprite = sprite_path.empty()
            ? Blit::cache_sprite(Utils::join(level, ".sprite"))
            : Blit::cache_sprite(
                  Utils::join(Utils::basedir(level), "/", sprite_path));

         /* from_sprite hands over ownership. */
         blit_surface_release(&player);
         player = sprite;
      }

      int x     = Utils::stoi(attr_or(layer->attr, "start_x", "1"));
      int y     = Utils::stoi(attr_or(layer->attr, "start_y", "1"));
      int off_x = Utils::stoi(attr_or(layer->attr, "player_offset_x", "0"));
      int off_y = Utils::stoi(attr_or(layer->attr, "player_offset_y", "0"));
      std::basic_string<char> face = attr_or(layer->attr, "start_facing", "right");

      player.rect.pos = blit_pos(x * blit_tilemap_tile_width(map), y * blit_tilemap_tile_height(map));
      player_off = blit_pos(off_x, off_y);
      facing = (Input)icy_input_from_face(face.c_str());
      set_player_alt(face);
   }

   void Game::iterate()
   {
      update_player();

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
         blit_font_cluster_render(font, &target, (Utils::join((chapter + 1), "-", (level + 1))).c_str(),
               314, 184, BLIT_FONT_RIGHT, 0);
         if (!best_pushes)
            blit_font_cluster_render(font, &target, (Utils::join(" Pushes:", pushes)).c_str(),
               2, 184, BLIT_FONT_LEFT, 0);
         else
            blit_font_cluster_render(font, &target, (Utils::join(" Pushes:", pushes, " Best:", best_pushes)).c_str(),
               2, 184, BLIT_FONT_LEFT, 0);
      }

      if (m_video_cb)
         m_video_cb(target.buffer, target.rect.w, target.rect.h, target.rect.w * sizeof(Pixel));
   }

   /* A plain scan rather than copy_if over a reference_wrapper vector:
    * the elements are about to stop living in a std::vector, and this
    * form does not care what holds them. */
   vector<blit_cluster_elem_t*> Game::get_tiles_with_attr(const string& name,
         const string& attr, const string& val)
   {
      vector<blit_cluster_elem_t*> surfs;
      blit_layer_t *layer = blit_tilemap_find_layer(map, name.c_str());
      if (!layer)
         return surfs;

      {
         blit_surface_cluster_t& cluster = layer->cluster;
         size_t i;

         for (i = 0; i < cluster.count; i++)
         {
            blit_cluster_elem_t *elem = &cluster.elems[i];
            const char *found = blit_attr_table_find(elem->surf.attribs,
                  attr.c_str());

            if (val.empty() ? (found != NULL) : (found && val == found))
               surfs.push_back(elem);
         }
      }

      return surfs;
   }

   bool Game::win_animation_stepper()
   {
      won_frame_cnt++;

      std::vector<blit_cluster_elem_t*> goal_blocks = get_tiles_with_attr("blocks", "goal", "true");

      const unsigned frame_per_iter = frames_to_ticks(won_frames_per_iter);

      std::string state = "frozen";
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

      for (auto& block : goal_blocks)
      {
         blit_surface_set_active_alt(&block->surf, state.c_str(), 0);

         // Shift defrosted block same way player sprite is (16x17, etc), but only when defrost kicks in.
         if (won_frame_cnt >= 1 * frame_per_iter)
            block->offset = player_off;
      }

      m_won_early = (won_frame_cnt >= frame_per_iter * 3) && push.set(m_input_cb(Input::Push));
      return true;
   }

   void Game::prepare_won_animation()
   {
      won_frame_cnt = 1;
      player_walking = false;

      push.set(true); // Avoid exiting win animation early.
      m_won_early = false;
      stepper = Stepper::WinAnimation;
      icy_sfx_play(icy_sfx(), "frozen_dino_melt", 0.25f);
   }

   bool Game::won() const
   {
      return m_won_early
         || (won_frame_cnt >= frames_to_ticks(won_frame_cnt_limit));
   }

   static bool is_game_won(const blit_cluster_elem_t *a,
         const blit_cluster_elem_t *b)
   {
      return blit_pos_equal(a->surf.rect.pos, b->surf.rect.pos) != 0;
   }

   static bool sort_blocks(const blit_cluster_elem_t *a,
         const blit_cluster_elem_t *b)
   {
      return blit_pos_less(a->surf.rect.pos, b->surf.rect.pos) != 0;
   }

   // Checks if all goals on floor and blocks are aligned with each other.
   bool Game::won_condition()
   {
      std::vector<blit_cluster_elem_t*> goal_floor  = get_tiles_with_attr("floor", "goal", "true");
      std::vector<blit_cluster_elem_t*> goal_blocks = get_tiles_with_attr("blocks", "goal", "true");

      if (goal_floor.size() != goal_blocks.size())
         throw logic_error("Number of goal floors and goal blocks do not match.");

      if (goal_floor.empty() || goal_blocks.empty())
         throw logic_error("Goal floor or blocks are empty.");

      sort(goal_floor.begin(), goal_floor.end(), sort_blocks);
      sort(goal_blocks.begin(), goal_blocks.end(), sort_blocks);

      return equal(goal_floor.begin(), goal_floor.end(),
            goal_blocks.begin(), is_game_won);
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
               frames_to_ticks(anim_frames_per_step), is_sliding));
   }

   void Game::update_triggers()
   {
      push.set(m_input_cb(Input::Push));
   }

   void Game::update_input()
   {
      bool push_trigger = push.set(m_input_cb(Input::Push));

      if (push_trigger)
         push_block();
      else if (m_input_cb(Input::Up))
         move_if_no_collision(Input::Up);
      else if (m_input_cb(Input::Down))
         move_if_no_collision(Input::Down);
      else if (m_input_cb(Input::Left))
         move_if_no_collision(Input::Left);
      else if (m_input_cb(Input::Right))
         move_if_no_collision(Input::Right);
   }

   /* The check itself is grid arithmetic and lives in icy_collide.c;
    * what stays here is the assertion, because only the game knows that
    * an off-grid surface means a broken level rather than a bad call. */
   bool Game::is_offset_collision(blit_surface_t& surf, Pos offset)
   {
      if (!icy_collide_aligned(map, surf.rect))
         throw logic_error("Offset collision check was performed outside tile grid.");

      return icy_collide_offset(map, surf.rect, offset) != 0;
   }

   void Game::push_block()
   {
      Blit::Pos offset = icy_input_offset((enum icy_input)facing);
      Blit::Pos dir    = offset * blit_pos(blit_tilemap_tile_width(map), blit_tilemap_tile_height(map));
      blit_surface_t *tile  = blit_tilemap_find_tile(map, "blocks", player.rect.pos + dir);

      if (!tile)
         return;

      int tile_x = player.rect.pos.x / blit_tilemap_tile_width(map);
      int tile_y = player.rect.pos.y / blit_tilemap_tile_height(map);
      Pos tile_pos = blit_pos(tile_x, tile_y);

      if (!blit_tilemap_collision(map, tile_pos + (2 * offset)))
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

   void Game::move_if_no_collision(Input input)
   {
      facing = input;
      set_player_alt(icy_input_face((enum icy_input)facing));

      Blit::Pos offset = icy_input_offset((enum icy_input)input);
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
      icy_leg_begin(&leg, frames_to_ticks(tile_size / 2));
   }

   bool Game::tile_stepper(blit_surface_t& surf, Pos step_dir)
   {
      int tile_size = step_dir.x ? blit_tilemap_tile_width(map) : blit_tilemap_tile_height(map);

      surf.rect += icy_leg_step(&leg, tile_size) * step_dir;

      if (!player_walking)
      {
         set_player_alt_index(icy_anim_pushing(stepper_cnt,
                  frames_to_ticks(push_anim_frames)));
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

   void Game::begin_tile_stepper(blit_surface_t& surf, Pos dir)
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


