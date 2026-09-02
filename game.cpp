#include "game.hpp"
#include <cstring>
#include "utils.hpp"
#include <iostream>
#include <stdexcept>

using namespace Blit;
using namespace std;

namespace Icy
{
   EdgeDetector::EdgeDetector(bool init) : pos(init)
   {}

   bool EdgeDetector::set(bool state)
   {
      bool ret = state && !pos;
      pos = state;
      return ret;
   }

   Game::Game(const string& level_path, unsigned chapter, unsigned level, unsigned best_pushes, blit_font_cluster_t *font)
      : map(level_path),
         player_off(blit_pos_zero()), font(font),
         camera(target, player.rect, blit_pos(map.pix_width(), map.pix_height())),
         won_frame_cnt(0), is_sliding(false), leg_tick(0), leg_ticks(1),
         leg_moved(0), best_pushes(best_pushes), pushes(0),
         chapter(chapter), level(level), push(true) 
   {
      if (!blit_render_target_init_size(&target, fb_width, fb_height))
         throw std::bad_alloc();
      /* player is a raw surface: no constructor to zero it, and the
       * destructor below is what releases it. */
      blit_surface_init(&player);
      m_won_early = false;
      set_initial_pos(level_path);
      bg = NULL;
   }

   Game::Game(const string& level_path)
      : map(level_path),
         player_off(blit_pos_zero()), font(NULL),
         camera(target, player.rect, blit_pos(map.pix_width(), map.pix_height())),
         won_frame_cnt(0), is_sliding(false), leg_tick(0), leg_ticks(1),
         leg_moved(0), push(true)
   {
      if (!blit_render_target_init_size(&target, fb_width, fb_height))
         throw std::bad_alloc();
      blit_surface_init(&player);
      m_won_early = false;
      set_initial_pos(level_path);
      bg = NULL;
   }

   Game::~Game()
   {
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
      Blit::Tilemap::Layer *layer = map.find_layer("floor");
      if (!layer)
         throw runtime_error("Floor layer not found.");

      std::basic_string<char> sprite_path = Utils::find_or_default(layer->attr, "player_sprite", "");
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

      int x     = Utils::stoi(Utils::find_or_default(layer->attr, "start_x", "1"));
      int y     = Utils::stoi(Utils::find_or_default(layer->attr, "start_y", "1"));
      int off_x = Utils::stoi(Utils::find_or_default(layer->attr, "player_offset_x", "0"));
      int off_y = Utils::stoi(Utils::find_or_default(layer->attr, "player_offset_y", "0"));
      std::basic_string<char> face = Utils::find_or_default(layer->attr, "start_facing", "right");

      player.rect.pos = blit_pos(x * map.tile_width(), y * map.tile_height());
      player_off = blit_pos(off_x, off_y);
      facing = string_to_input(face);
      set_player_alt(face);
   }

   void Game::iterate()
   {
      update_player();

      if (bg)
         blit_render_target_blit(&target, bg, blit_rect_zero());
      else
         blit_render_target_clear(&target, blit_pixel_argb(0x00, 0x00, 0x00, 0x00));

      camera.update();

      map.render(target);
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
   vector<SurfaceCluster::Elem*> Game::get_tiles_with_attr(const string& name,
         const string& attr, const string& val)
   {
      vector<SurfaceCluster::Elem*> surfs;
      Blit::Tilemap::Layer *layer = map.find_layer(name);
      if (!layer)
         return surfs;

      {
         Blit::SurfaceCluster& cluster = layer->cluster;
         size_t i;

         for (i = 0; i < cluster.size(); i++)
         {
            SurfaceCluster::Elem *elem = cluster.at(i);
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

      std::vector<Blit::SurfaceCluster::Elem*> goal_blocks = get_tiles_with_attr("blocks", "goal", "true");

      const unsigned frame_per_iter = frames_to_ticks(won_frames_per_iter);

      std::string state = "frozen";
      if (won_frame_cnt >= 3 * frame_per_iter)
      {
         bool jump = ((won_frame_cnt / frame_per_iter - 3) >> 1) & 1;
         unsigned last_jump = (((won_frame_cnt - 1) / frame_per_iter - 3) >> 1) & 1;
         state = jump ? "cheer" : "down";
         set_player_alt(state);

         if (jump && !last_jump)
            get_sfx().play_sfx("dino_jump", 0.4);
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
      stepper = bind(&Game::win_animation_stepper, this);
      get_sfx().play_sfx("frozen_dino_melt", 0.25);
   }

   bool Game::won() const
   {
      return m_won_early
         || (won_frame_cnt >= frames_to_ticks(won_frame_cnt_limit));
   }

   static bool is_game_won(const Blit::SurfaceCluster::Elem *a,
         const Blit::SurfaceCluster::Elem *b)
   {
      return blit_pos_equal(a->surf.rect.pos, b->surf.rect.pos) != 0;
   }

   static bool sort_blocks(const SurfaceCluster::Elem *a,
         const SurfaceCluster::Elem *b)
   {
      return blit_pos_less(a->surf.rect.pos, b->surf.rect.pos) != 0;
   }

   // Checks if all goals on floor and blocks are aligned with each other.
   bool Game::won_condition()
   {
      std::vector<Blit::SurfaceCluster::Elem*> goal_floor  = get_tiles_with_attr("floor", "goal", "true");
      std::vector<Blit::SurfaceCluster::Elem*> goal_blocks = get_tiles_with_attr("blocks", "goal", "true");

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
      
      bool had_stepper = static_cast<bool>(stepper);
      run_stepper();

      if (won_frame_cnt)
         return;

      if (!stepper)
         update_input();
      else
         update_triggers();

      // Reset animation.
      if (!had_stepper && stepper)
         frame_cnt = 0;
      else if (!stepper)
      {
         frame_cnt = 0;
         set_player_alt_index(0);
      }

      if (stepper && player_walking)
         update_animation();

      if (won_condition())
         prepare_won_animation();
   }

   void Game::update_animation()
   {
      frame_cnt++;

      // Animation from index 1 to 4, "neutral position" in 0. "Slippery" animations in 5 and 6.
      unsigned anim_index;
      /* frame_cnt counts ticks, so the divisor is the tick count that
       * spans what used to be 10 frames - the cycle changes at the same
       * wall-clock moments at 60 Hz and at 240. */
      unsigned period = frames_to_ticks(anim_frames_per_step);
      if (is_sliding)
         anim_index = (frame_cnt / period) % 2 + 5;
      else
         anim_index = (frame_cnt / period) % 4 + 1;

      set_player_alt_index(anim_index);
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

   string Game::input_to_string(Input input)
   {
      switch (input)
      {
         case Input::Up:    return "up";
         case Input::Left:  return "left";
         case Input::Right: return "right";
         case Input::Down:  return "down";
         default:           return "";
      }
   }

   Blit::Pos Game::input_to_offset(Input input)
   {
      switch (input)
      {
         case Input::Up:    return blit_pos(0, -1);
         case Input::Left:  return blit_pos(-1, 0);
         case Input::Right: return blit_pos(1, 0);
         case Input::Down:  return blit_pos(0, 1);
         default:           return blit_pos_zero();
      }
   }

   Input Game::string_to_input(const string& dir)
   {
      if (dir == "up") return Input::Up;
      if (dir == "down") return Input::Down;
      if (dir == "left") return Input::Left;
      if (dir == "right") return Input::Right;
      return Input::None;
   }

   bool Game::is_offset_collision(blit_surface_t& surf, Pos offset)
   {
      Blit::Rect new_rect = surf.rect + offset;

      // Always assume that the rect in question is inside a single tile.
      // This is needed as the dino sprite can be slightly larger than 16x16, but it's
      // *assumed* from a collition detection POV that a surface is tile sized to simplify things.
      new_rect.w = map.tile_width();
      new_rect.h = map.tile_height();

      bool outside_grid = surf.rect.pos.x % map.tile_width() || surf.rect.pos.y % map.tile_height();
      if (outside_grid)
         throw logic_error("Offset collision check was performed outside tile grid.");

      int current_x = surf.rect.pos.x / map.tile_width();
      int current_y = surf.rect.pos.y / map.tile_height();

      int min_tile_x = new_rect.pos.x / map.tile_width();
      int max_tile_x = (new_rect.pos.x + new_rect.w - 1) / map.tile_width();

      int min_tile_y = new_rect.pos.y / map.tile_height();
      int max_tile_y = (new_rect.pos.y + new_rect.h - 1) / map.tile_height();

      for (int y = min_tile_y; y <= max_tile_y; y++)
         for (int x = min_tile_x; x <= max_tile_x; x++)
            if (blit_pos(x, y) != blit_pos(current_x, current_y) && map.collision(blit_pos(x, y))) // Can't collide against ourselves.
               return true;

      return false;
   }

   void Game::push_block()
   {
      Blit::Pos offset = input_to_offset(facing);
      Blit::Pos dir    = offset * blit_pos(map.tile_width(), map.tile_height());
      blit_surface_t *tile  = map.find_tile("blocks", player.rect.pos + dir);

      if (!tile)
         return;

      int tile_x = player.rect.pos.x / map.tile_width();
      int tile_y = player.rect.pos.y / map.tile_height();
      Pos tile_pos = blit_pos(tile_x, tile_y);

      if (!map.collision(tile_pos + (2 * offset)))
      {
         stepper = bind(&Game::tile_stepper, this, ref(*tile), offset);
         begin_leg(offset.x ? map.tile_width() : map.tile_height());
         stepper_cnt = 0;
         player_walking = false;
         set_player_alt_index(0);
         get_sfx().play_sfx("dino_push", 1.0);
         pushes++;
      }
   }

   void Game::move_if_no_collision(Input input)
   {
      facing = input;
      set_player_alt(input_to_string(facing));

      Blit::Pos offset = input_to_offset(input);
      if (!is_offset_collision(player, offset))
      {
         stepper = bind(&Game::tile_stepper, this, ref(player), offset);
         begin_leg(offset.x ? map.tile_width() : map.tile_height());
         player_walking = true;
      }
   }

   /* A leg covers one tile. At 60 Hz the sim moved 2 px per tick, so a
    * 16 px tile took 8; keeping that as the duration and interpolating
    * the position across it gives 1 px per tick at 120 Hz and lands on
    * the grid exactly at the end of the leg whatever the rate. */
   void Game::begin_leg(int tile_size)
   {
      leg_tick  = 0;
      leg_ticks = frames_to_ticks(tile_size / 2);
      leg_moved = 0;
   }

   bool Game::tile_stepper(blit_surface_t& surf, Pos step_dir)
   {
      int tile_size = step_dir.x ? map.tile_width() : map.tile_height();
      int want;

      /* leg_ticks is fixed for the duration of a leg - begin_leg() is the
       * only writer, and it runs when a leg starts, never during one.
       * That is what guarantees want reaches exactly tile_size on the
       * last tick, so a surface always comes to rest on the tile grid
       * even if the frame rate changed while it was in flight. A rate
       * change is picked up by the next leg. */
      leg_tick++;
      want = (int)((unsigned)tile_size * leg_tick / leg_ticks);
      if (want > tile_size)
         want = tile_size;

      surf.rect += (want - leg_moved) * step_dir;
      leg_moved    = want;

      if (!player_walking)
      {
         unsigned alt = stepper_cnt < frames_to_ticks(push_anim_frames) ? 7 : 0;
         set_player_alt_index(alt);
         stepper_cnt++;
      }

      /* Mid-leg. The tick count decides this rather than grid alignment:
       * at rates above 2 ticks per pixel the first ticks of a leg round
       * to zero movement, which leaves the surface still aligned and
       * would otherwise read as a completed leg. */
      if (leg_tick < leg_ticks)
         return true;

      begin_leg(tile_size);

      if (is_offset_collision(surf, step_dir))
      {
         is_sliding = false;

         if (&surf != &player)
            get_sfx().play_sfx("ice_bump", 0.25);

         return false;
      }

      //cerr << "Player: " << player.rect.pos << " Surf: " << surf->rect().pos << endl; 
      blit_surface_t *surface = map.find_tile("floor", surf.rect.pos);
      const char *slip = surface ? blit_attr_table_find(surface->attribs,
            &surf == &player ? "slippery_player" : "slippery_block") : NULL;
      bool slippery = slip && std::strcmp(slip, "true") == 0;

      is_sliding = slippery;
      return slippery;
   }

   void Game::run_stepper()
   {
      if (stepper && !stepper())
         stepper = {};
   }

   CameraManager::CameraManager(blit_render_target_t& target, const Rect& rect, Blit::Pos map_size)
      : target(&target), rect(&rect), map_size(map_size)
   {}

   void CameraManager::update()
   {
      // Map can fit completely inside our rect, just center it.
      if (target->rect.w >= map_size.x && target->rect.h >= map_size.y)
         target->rect.pos = ((map_size - blit_pos(target->rect.w, target->rect.h)) / 2);
      else // Center around player, but clamp if player isn't near walls.
      {
         Blit::Pos pos = rect->pos;
         pos += blit_pos(rect->w, rect->h) / 2;

         Pos target_size = blit_pos(target->rect.w, target->rect.h);

         Blit::Pos pos_base = pos - target_size / 2;
         Blit::Pos pos_max  = pos_base + target_size;

         if (pos_base.x < 0)
            pos_base.x = 0;
         else if (pos_max.x > map_size.x)
            pos_base.x -= pos_max.x - map_size.x;

         if (pos_base.y < 0)
            pos_base.y = 0;
         else if (pos_max.y > map_size.y)
            pos_base.y -= pos_max.y - map_size.y;

         target->rect.pos = (pos_base);
      }
   }
}


