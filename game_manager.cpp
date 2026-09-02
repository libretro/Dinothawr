#include "game.hpp"
#include "icy_save.h"
#include "icy_menu_select.h"
#include "icy_edge.h"
#include "icy_path.h"
#include <cstdio>
#include <new>
#include <cstring>
#include <cstdlib>

#include <iostream>
#include <cstdlib>
#include <assert.h>

using namespace Blit;
using namespace std;

namespace Icy
{
   /* Loads an image or dies: every caller here treats a missing asset
    * as fatal, which is what the C entry point leaves to them. */
   static blit_surface_t cache_image_or_throw(const char *path)
   {
      blit_surface_t out;

      if (!blit_surface_cache_image(blit_surface_cache(), path, &out))
         throw std::runtime_error(
               blit_surface_cache_error(blit_surface_cache()));

      return out;
   }

   /* Owns a parsed document for the length of a scope. One user, so it
    * lives here rather than in a header: the lookups are blit_xml.h and
    * this is only the lifetime, which the throwing loader below needs. */
   class xml_doc
   {
      public:
         xml_doc() : doc(NULL) {}
         ~xml_doc() { if (doc) rxml_free_document(doc); }

         xml_doc(const xml_doc&) = delete;
         xml_doc& operator=(const xml_doc&) = delete;

         bool load(const char *path)
         {
            if (doc)
               rxml_free_document(doc);
            doc = blit_xml_load(path);
            return doc != NULL;
         }

         rxml_document_t *get() const { return doc; }

      private:
         rxml_document_t *doc;
   };

   /* Small shims while GameManager still holds std::strings: the path
    * work itself is icy_path.c. */
   static std::string path_dir(const std::string& path)
   {
      char out[512];
      icy_path_dir(out, sizeof(out), path.c_str());
      return out;
   }

   static std::string path_join(const std::string& dir, const char *name)
   {
      char out[512];
      icy_path_join(out, sizeof(out), dir.c_str(), name);
      return out;
   }

   /* Chapter list accessors for icy_menu_select.c. */
   extern "C" {
      static unsigned menu_levels_cb(void *ctx, unsigned chapter)
      {
         return (unsigned)static_cast<GameManager*>(ctx)
            ->chapter_level_count(chapter);
      }

      static int menu_cleared_cb(void *ctx, unsigned chapter)
      {
         return static_cast<GameManager*>(ctx)
            ->chapter_is_cleared(chapter) ? 1 : 0;
      }
   }

   GameManager::GameManager(const string& path_game,
         input_fn input_cb,
         video_fn video_cb)
      : save(chapters), dir(path_dir(path_game)),
      m_current_chap(0), m_current_level(0), m_game_state(State::Title),
      m_input_cb(input_cb), m_input_ctx(NULL),
      m_video_cb(video_cb), m_video_ctx(NULL),
      chap_select(0), level_select(0),
      menu_slide_dir(blit_pos_zero())
   {
      icy_edge_init(&edges);
      icy_menu_slide_init(&slide);
      init_menu_surfaces();

      xml_doc doc;

      if (!doc.load(path_game.c_str()))
      {
         char msg[512];
         snprintf(msg, sizeof(msg), "Failed to load game: %s.",
               path_game.c_str());
         throw runtime_error(msg);
      }

      /* pugixml's document was itself a node, so every lookup below
       * re-descended into <game>.  Take the element once. */
      rxml_node_t *game_node = blit_xml_root(doc.get(), "game");

      string font_path = path_join(dir, blit_xml_attr(blit_xml_child(game_node, "font"), "source"));
      char   err[256];

      if (!(font = blit_font_cluster_new()))
         throw std::bad_alloc();

      if (!blit_font_cluster_add(font, "yellow", font_path.c_str(), blit_pos(-1, 1), blit_pixel_argb(0xff, 0xc0, 0x98, 0x00),
               err, sizeof(err)))
         throw runtime_error(err);
      if (!blit_font_cluster_add(font, "yellow", font_path.c_str(), blit_pos( 0, 0), blit_pixel_argb(0xff, 0xff, 0xde, 0x00),
               err, sizeof(err)))
         throw runtime_error(err);
      if (!blit_font_cluster_add(font, "white", font_path.c_str(), blit_pos(-1, 1), blit_pixel_argb(0xff, 0x73, 0x73, 0x8b),
               err, sizeof(err)))
         throw runtime_error(err);
      if (!blit_font_cluster_add(font, "white", font_path.c_str(), blit_pos( 0, 0), blit_pixel_argb(0xff, 0xff, 0xff, 0xff),
               err, sizeof(err)))
         throw runtime_error(err);
      if (!blit_font_cluster_add(font, "lime", font_path.c_str(), blit_pos(-1, 1), blit_pixel_argb(0xff, 0x39, 0x5a, 0x94),
               err, sizeof(err)))
         throw runtime_error(err);
      if (!blit_font_cluster_add(font, "lime", font_path.c_str(), blit_pos( 0, 0), blit_pixel_argb(0xff, 0xb8, 0xe8, 0xb0),
               err, sizeof(err)))
         throw runtime_error(err);

      init_menu(blit_xml_attr(blit_xml_child(game_node, "title"), "source"));
      init_menu_sprite(game_node);
      init_sfx(game_node);
      init_bg(game_node);

      for (rxml_node_t *node = blit_xml_child(game_node, "chapter"); node; node = blit_xml_next(node, "chapter"))
      {
         Icy::GameManager::Chapter chapter = load_chapter(node, chapters.size());
         if (chapter.num_levels() > 0)
            chapters.push_back(std::move(chapter));
      }

      blit_render_target_release(&ui_target);
      if (!blit_render_target_init_size(&ui_target, Game::fb_width,
               Game::fb_height))
         throw std::bad_alloc();

   }

   GameManager::GameManager()
      : save(chapters),
      m_current_chap(0), m_current_level(0), m_game_state(State::Game),
      chap_select(0), level_select(0),
      menu_slide_dir(blit_pos_zero())
   {
      icy_edge_init(&edges);
      icy_menu_slide_init(&slide);
      init_menu_surfaces();
   }

   /* The menu surfaces are raw structs: nothing zeroes them for us, and
    * nothing releases them either. */
   void GameManager::init_menu_surfaces()
   {
      font = NULL;
      blit_render_target_init(&target);
      blit_render_target_init(&ui_target);
      blit_surface_init(&lock_sprite);
      blit_surface_init(&level_complete);
      blit_surface_init(&level_select_bg);
      blit_surface_init(&end_credit_bg);
      blit_surface_init(&game_bg);
   }

   GameManager::~GameManager()
   {
      blit_font_cluster_free(font);
      blit_render_target_release(&target);
      blit_render_target_release(&ui_target);
      blit_surface_release(&lock_sprite);
      blit_surface_release(&level_complete);
      blit_surface_release(&level_select_bg);
      blit_surface_release(&end_credit_bg);
      blit_surface_release(&game_bg);
   }

   void GameManager::init_menu_sprite(rxml_node_t *game_node)
   {
      {
         blit_surface_t tmp = cache_image_or_throw(path_join(dir, blit_xml_attr(blit_xml_child(game_node, "level_complete"), "source")).c_str());
         blit_surface_assign(&level_complete, &tmp);
         blit_surface_release(&tmp);
      }

      {
         blit_surface_t tmp = cache_image_or_throw(path_join(dir, blit_xml_attr(blit_xml_child(game_node, "lock_sprite"), "source")).c_str());
         blit_surface_assign(&lock_sprite, &tmp);
         blit_surface_release(&tmp);
      }
      lock_sprite.ignore_camera = 1;
      int arrow_x = (Game::fb_width - lock_sprite.rect.w) / 2;
      lock_sprite.rect.pos = blit_pos( arrow_x, 160 );

      int complete_x = preview_base_x + Game::fb_width / 2 - level_complete.rect.w - 2;
      int complete_y = preview_base_y + Game::fb_height / 2 - level_complete.rect.h - 2;
      level_complete.rect.pos = blit_pos( complete_x, complete_y );
      level_complete.ignore_camera = 1;

      {
         blit_surface_t tmp = cache_image_or_throw(path_join(dir, blit_xml_attr(blit_xml_child(game_node, "menu_bg"), "source")).c_str());
         blit_surface_assign(&level_select_bg, &tmp);
         blit_surface_release(&tmp);
      }
      level_select_bg.ignore_camera = 1;

      {
         blit_surface_t tmp = cache_image_or_throw(path_join(dir, blit_xml_attr(blit_xml_child(game_node, "end_bg"), "source")).c_str());
         blit_surface_assign(&end_credit_bg, &tmp);
         blit_surface_release(&tmp);
      }
      end_credit_bg.ignore_camera = 1;

      {
         blit_surface_t tmp = cache_image_or_throw(path_join(dir, blit_xml_attr(blit_xml_child(game_node, "game_bg"), "source")).c_str());
         blit_surface_assign(&game_bg, &tmp);
         blit_surface_release(&tmp);
      }
      game_bg.ignore_camera = 1;
   }

   void GameManager::init_bg(rxml_node_t *game_node)
   {
      rxml_node_t         *music = blit_xml_child(game_node, "music");
      rxml_node_t         *node;
      vector<std::string>  paths;
      vector<float>        gains;

      /* One walk over the <bg> elements rather than two: the source and
       * the volume come off the same node. */
      for (node = blit_xml_child(music, "bg"); node;
            node = blit_xml_next(node, "bg"))
      {
         const char *source = blit_xml_attr(node, "source");
         const char *volume = blit_xml_attr(node, "volume");

         paths.push_back(path_join(dir, source));
         gains.push_back(*volume ? (float)std::strtod(volume, NULL) : 1.0f);
      }

      {
         vector<const char*> raw;
         size_t i;

         for (i = 0; i < paths.size(); i++)
            raw.push_back(paths[i].c_str());

         if (!icy_bgm_set_tracks(icy_bgm(), raw.empty() ? NULL : &raw[0],
                  gains.empty() ? NULL : &gains[0], raw.size()))
            throw std::bad_alloc();
      }
   }

   void GameManager::init_sfx(rxml_node_t *game_node)
   {
      rxml_node_t *sfx = blit_xml_child(game_node, "sfx");
      rxml_node_t *node;

      for (node = blit_xml_child(sfx, "sound"); node;
            node = blit_xml_next(node, "sound"))
         if (!icy_sfx_add(icy_sfx(), blit_xml_attr(node, "name"),
                  path_join(dir, blit_xml_attr(node, "source")).c_str()))
         {
            char msg[512];
            snprintf(msg, sizeof(msg), "Failed to load sound: %s",
                  blit_xml_attr(node, "source"));
            throw runtime_error(msg);
         }
   }

   GameManager::Chapter GameManager::load_chapter(rxml_node_t *chap, int chapter)
   {
      rxml_node_t  *node;
      vector<Level> levels;

      for (node = blit_xml_child(chap, "map"); node;
            node = blit_xml_next(node, "map"))
      {
         levels.push_back({path_join(dir, blit_xml_attr(node, "source")), game_bg});
         levels.back().set_name(blit_xml_attr(node, "name"));
      }

      int i = 0;
      for (auto& level : levels)
      {
         //cerr << "Found level: " << level.path() << endl;
         level.pos(blit_pos(preview_base_x + i * preview_delta_x, preview_base_y + preview_delta_y * chapter));

         i++;
      }

      Chapter loaded_chap = Chapter(std::move(levels), blit_xml_attr(chap, "name"));
      loaded_chap.set_minimum_clear(blit_xml_attr_int(chap, "minimum_clear"));
      return loaded_chap;
   }

   void GameManager::init_menu(const string& level)
   {
      blit_surface_t surf = cache_image_or_throw(
            path_join(dir, level.c_str()).c_str());

      blit_render_target_release(&target);
      if (!blit_render_target_init_size(&target, Game::fb_width,
               Game::fb_height))
         throw std::bad_alloc();
      blit_render_target_blit(&target, &surf, blit_rect_zero());
      blit_surface_release(&surf);

      blit_font_cluster_set_id(font, "yellow");
      blit_font_cluster_render(font, &target, "Press OK/Push button",
            160, 170, BLIT_FONT_CENTERED, 0);
   }

   void GameManager::reset_level()
   {
      change_level(m_current_chap, m_current_level);
   }

   void GameManager::change_level(unsigned chapter, unsigned level) 
   {
      game.reset(new Game(
            chapters.at(chapter).level(level).path().c_str(), 
            chapter,
            level,
            chapters.at(chapter).level(level).get_best_pushes(),
            font));
      game->input_cb(m_input_cb, m_input_ctx);
      game->video_cb(m_video_cb, m_video_ctx);
      game->set_bg(game_bg);

      m_current_chap  = chapter;
      m_current_level = level;
   }

   void GameManager::init_level(unsigned chapter, unsigned level)
   {
      change_level(chapter, level);
      m_game_state = State::Game;
   }

   bool GameManager::find_next_unsolved_level(unsigned& current_chap, unsigned& current_level)
   {
      if (current_chap == chapters.size() - 1 && current_level == chapters.back().num_levels() - 1)
         return false;

      unsigned chap = current_chap;
      unsigned level = current_level;
      while (chap < chapters.size())
      {
         if (!chapters[chap].get_completion(level))
         {
            current_chap = chap;
            current_level = level;
            return true;
         }

         level++;
         if (level >= chapters[chap].num_levels())
         {
            if (!chapters[chap].cleared())
               break;

            chap++;
            level = 0;
         }
      }

      return false;
   }

   // Find first level that isn't completed yet, and
   // start menu there.
   void GameManager::set_initial_level()
   {
      save.unserialize();
      m_current_chap = 0;
      m_current_level = 0;
      if (!find_next_unsolved_level(m_current_chap, m_current_level))
      {
         chap_select = chapters.size() - 1;
         level_select = chapters.back().num_levels() - 1;
      }
   }

   void GameManager::step_title()
   {
      if (m_input_cb(m_input_ctx, Input::Push) || m_input_cb(m_input_ctx, Input::Menu))
      {
         set_initial_level();
         enter_menu();
      }

      m_video_cb(m_video_ctx, target.buffer, target.rect.w, target.rect.h, target.rect.w * sizeof(Pixel));
   }

   void GameManager::enter_menu()
   {
      save.unserialize();
      /* Entering the menu from a press: do not act on the same press
       * again on the first menu frame. */
      icy_edge_suppress(&edges, ICY_EDGE_OK);
      icy_edge_suppress(&edges, ICY_EDGE_MENU);

      m_game_state = State::Menu;
      level_select = m_current_level;
      chap_select  = m_current_chap;
      ui_target.rect.pos = blit_pos(preview_delta_x * level_select,
            preview_delta_y * chap_select);
   }

   void GameManager::menu_render_ui()
   {
      char hud[64];

      if (menu_slide_dir.y == 0)
      {
         unsigned chap = chap_select;
         if (chap < chapters.size() - 1 && !chapters[chap_select].cleared())
            blit_render_target_blit(&ui_target, &lock_sprite, blit_rect_zero());

         // Render tick if level is complete.
         if (menu_slide_dir.x == 0 && chapters[chap_select].get_completion(level_select))
            blit_render_target_blit(&ui_target, &level_complete, blit_rect_zero());

         blit_font_cluster_set_id(font, "white");
         snprintf(hud, sizeof(hud), "%d-%d", chap_select + 1,
               level_select + 1);
         blit_font_cluster_render(font, &ui_target, hud,
               240, 155, BLIT_FONT_RIGHT, 0);
      }

      blit_font_cluster_set_id(font, "lime");
      snprintf(hud, sizeof(hud), "%u/%u", total_cleared_levels(),
            total_levels());
      blit_font_cluster_render(font, &ui_target, hud,
            10, 185, BLIT_FONT_LEFT, 0);

      snprintf(hud, sizeof(hud), "%u%%",
            100 * total_cleared_levels() / total_levels());
      blit_font_cluster_render(font, &ui_target, hud,
            315, 185, BLIT_FONT_RIGHT, 0);
   }

   void GameManager::step_menu_slide()
   {
      ui_target.rect.pos = blit_pos_add(ui_target.rect.pos,
            icy_menu_slide_step(&slide));

      if (icy_menu_slide_done(&slide))
      {
         m_game_state = State::Menu;
         menu_slide_dir = {};
      }

      blit_render_target_blit(&ui_target, &level_select_bg, blit_rect_zero());

      for (auto& chap : chapters)
         for (auto& preview : chap.levels())
            preview.render(ui_target);

      menu_render_ui();

      m_video_cb(m_video_ctx, ui_target.buffer, ui_target.rect.w, ui_target.rect.h, ui_target.rect.w * sizeof(Pixel));
   }

   const GameManager::Level& GameManager::get_selected_level() const
   {
      return chapters.at(chap_select).level(level_select);
   }

   void GameManager::start_slide(Pos dir, unsigned cnt)
   {
      m_game_state = State::MenuSlide;

      /* cnt is the duration in 60 Hz frames; dir * cnt is the distance it
       * used to cover one tick at a time. Keep the distance, convert the
       * duration. */
      icy_menu_slide_start(&slide, blit_pos_scale((int)cnt, dir),
            frames_to_ticks(cnt));

      menu_slide_dir = dir;

      icy_sfx_play(icy_sfx(), "level_next", 0.5f);
   }

   void GameManager::step_menu()
   {
      blit_render_target_blit(&ui_target, &level_select_bg, blit_rect_zero());

      for (auto& chap : chapters)
         for (auto& preview : chap.levels())
            preview.render(ui_target);

      menu_render_ui();

      /* Edges, not levels: a held direction moves the cursor once. */
      bool pressed_menu_left  = icy_edge_pressed(&edges, ICY_EDGE_LEFT,
            m_input_cb(m_input_ctx, Input::Left));
      bool pressed_menu_right = icy_edge_pressed(&edges, ICY_EDGE_RIGHT,
            m_input_cb(m_input_ctx, Input::Right));
      bool pressed_menu_up    = icy_edge_pressed(&edges, ICY_EDGE_UP,
            m_input_cb(m_input_ctx, Input::Up));
      bool pressed_menu_down  = icy_edge_pressed(&edges, ICY_EDGE_DOWN,
            m_input_cb(m_input_ctx, Input::Down));
      bool pressed_menu_ok    = icy_edge_pressed(&edges, ICY_EDGE_OK,
            m_input_cb(m_input_ctx, Input::Push));
      bool pressed_menu       = icy_edge_pressed(&edges, ICY_EDGE_MENU,
            m_input_cb(m_input_ctx, Input::Menu));

      /* Navigation is index arithmetic over the chapter list, so it
       * lives in icy_menu_select.c; this turns a press into a direction
       * and the move it reports into a slide. */
      {
         icy_menu_chapters_t list;
         icy_menu_cursor_t   cursor;
         enum icy_menu_dir   dir;
         bool                pressed = true;

         list.chapters = (unsigned)chapters.size();
         list.levels   = menu_levels_cb;
         list.cleared  = menu_cleared_cb;
         list.ctx      = this;

         cursor.chapter = (unsigned)chap_select;
         cursor.level   = (unsigned)level_select;

         if (pressed_menu_left)
            dir = ICY_MENU_LEFT;
         else if (pressed_menu_right)
            dir = ICY_MENU_RIGHT;
         else if (pressed_menu_up)
            dir = ICY_MENU_UP;
         else if (pressed_menu_down)
            dir = ICY_MENU_DOWN;
         else
         {
            pressed = false;
            dir     = ICY_MENU_LEFT;
         }

         if (pressed)
         {
            icy_menu_move_t move = icy_menu_move(&cursor, dir, &list);

            if (move.locked)
               icy_sfx_play(icy_sfx(), "chapter_locked", 0.5f);
            else if (move.moved)
            {
               chap_select  = (int)cursor.chapter;
               level_select = (int)cursor.level;
               start_slide(blit_pos(move.level_delta * 8,
                        move.chapter_delta * 8), preview_slide_cnt);
            }
         }
      }

      if (pressed_menu_ok)
         init_level(chap_select, level_select);
      else if (pressed_menu && game)
         m_game_state = State::Game;

      m_video_cb(m_video_ctx, ui_target.buffer, ui_target.rect.w, ui_target.rect.h, ui_target.rect.w * sizeof(Pixel));
   }

   void GameManager::step_game()
   {
      if (!game)
         return;

      game->iterate();

      bool pressed_menu = m_input_cb(m_input_ctx, Input::Menu);
      bool pressed_reset = m_input_cb(m_input_ctx, Input::Reset);

      if (icy_edge_pressed(&edges, ICY_EDGE_RESET, pressed_reset))
         reset_level();
      else if (icy_edge_pressed(&edges, ICY_EDGE_MENU, pressed_menu))
         enter_menu();


      if (game->won())
      {
         unsigned pushes = game->get_pushes();
         chapters[m_current_chap].level(m_current_level).set_best_pushes(pushes);

         game.reset();
         bool trigger_completion = !chapters[m_current_chap].get_completion(m_current_level);
         chapters[m_current_chap].set_completion(m_current_level, true);
         save.serialize();

         // Go to ending screen on the event that all levels have been cleared.
         bool cleared_all = trigger_completion;
         if (trigger_completion)
         {
            for (auto& chap : chapters)
            {
               if (chap.cleared_count() != chap.num_levels())
               {
                  cleared_all = false;
                  break;
               }
            }
         }

         if (cleared_all)
         {
            m_game_state = State::End;
            /* Don't exit the credits on the press that opened them. */
            icy_edge_suppress(&edges, ICY_EDGE_OK);
         }
         else
         {
            if (find_next_unsolved_level(m_current_chap, m_current_level))
               change_level(m_current_chap, m_current_level);
            else
               enter_menu();
         }
      }
   }

   void GameManager::step_end()
   {
      blit_render_target_blit(&ui_target, &end_credit_bg, blit_rect_zero());

      bool trigger_ok   = icy_edge_pressed(&edges, ICY_EDGE_OK,
            m_input_cb(m_input_ctx, Input::Push));
      bool trigger_menu = icy_edge_pressed(&edges, ICY_EDGE_MENU,
            m_input_cb(m_input_ctx, Input::Menu));

      if (trigger_ok || trigger_menu)
         enter_menu();

      blit_font_cluster_set_id(font, "white");
      blit_font_cluster_render(font, &ui_target,
            "You completed all levels!\nAwesome! :D\nThanks for playing Dinothawr!",
            160, 155, BLIT_FONT_CENTERED, 2);
      m_video_cb(m_video_ctx, ui_target.buffer, ui_target.rect.w, ui_target.rect.h, ui_target.rect.w * sizeof(Pixel));
   }

   void GameManager::iterate()
   {
      switch (m_game_state)
      {
         case State::Title: return step_title();
         case State::Menu: return step_menu();
         case State::MenuSlide: return step_menu_slide();
         case State::Game: return step_game();
         case State::End: return step_end();
         default: throw logic_error("Game state is invalid.");
      }
   }

   bool GameManager::done() const
   {
      return false;
   }

   unsigned GameManager::total_levels() const
   {
      unsigned levels = 0;
      for (auto& chap : chapters)
         levels += chap.num_levels();

      return levels;
   }

   unsigned GameManager::total_cleared_levels() const
   {
      unsigned levels = 0;
      for (auto& chap : chapters)
         levels += chap.cleared_count();

      return levels;
   }

   GameManager::Level::Level(const string& path, const blit_surface_t& bg)
      : position(blit_pos_zero()), m_path(path), completion(false),
      best_pushes(0)
   {
      /* preview is a raw surface: it has no constructor to zero it. */
      blit_surface_init(&preview);

      Game game{path.c_str()};
      game.set_bg(bg);

      static const unsigned scale_factor = 2;
      int preview_width  = Game::fb_width / scale_factor;
      int preview_height = Game::fb_height / scale_factor;

      vector<Pixel> data(preview_width * preview_height);
      Pixel *owned;

      /* The downscale needs the destination and its pitch, so it goes
       * through the context pointer rather than a capture. */
      struct preview_ctx { vector<Pixel> *data; int width; };
      preview_ctx ctx = { &data, preview_width };

      game.input_cb([](void*, Input) { return false; });
      game.video_cb([](void *ctxv, const void* pix_data, unsigned width, unsigned height, size_t pitch) {
         preview_ctx *c = static_cast<preview_ctx*>(ctxv);
         vector<Pixel>& data = *c->data;
         int preview_width = c->width;
         const Pixel* pix = reinterpret_cast<const Pixel*>(pix_data);
         pitch /= sizeof(Pixel);

         for (unsigned y = 0; y < height; y += scale_factor)
         {
            for (unsigned x = 0; x < width; x += scale_factor)
            {
               Pixel a0 = pix[pitch * (y + 0) + (x + 0)];
               Pixel a1 = pix[pitch * (y + 0) + (x + 1)];
               Pixel b0 = pix[pitch * (y + 1) + (x + 0)];
               Pixel b1 = pix[pitch * (y + 1) + (x + 1)];
               Pixel res = blit_pixel_blend(blit_pixel_blend(a0, a1),
                     blit_pixel_blend(b0, b1));

               data[preview_width * (y / scale_factor) + (x / scale_factor)] = res | BLIT_PIXEL_ALPHA_MASK;
            }
         }
      }, &ctx);

      game.iterate();

      {
         /* Surface takes its own reference; drop the one we made. */
         blit_surface_data_t *pdata;

         owned = (Pixel*)malloc(data.size() * sizeof(Pixel));
         if (!owned)
            throw std::bad_alloc();
         memcpy(owned, &data[0], data.size() * sizeof(Pixel));

         pdata = blit_surface_data_new(owned, preview_width,
               preview_height);
         if (!pdata)
            throw std::bad_alloc();
         blit_surface_init_data(&preview, pdata);
         blit_surface_data_unref(pdata);
      }
      pos(blit_pos_sub(
               blit_pos_div(blit_pos(Game::fb_width, Game::fb_height),
                  scale_factor),
               blit_pos(5, 5)));
   }

   void GameManager::Level::render(blit_render_target_t& target) const
   {
      //preview.rect().pos = position;
      blit_render_target_blit_offset(&target, &preview, blit_rect_zero(), position);
   }

   GameManager::SaveManager::SaveManager(vector<GameManager::Chapter> &chaps)
      : chaps(chaps)
   {
      save_data.resize(save_game_size);
   }


   void* GameManager::SaveManager::data()
   {
      return save_data.data();
   }

   /* Gathers the layout and the counts, and lets icy_save.c do the
    * encoding: what the bytes look like is not this class's business. */
   void GameManager::SaveManager::serialize()
   {
      std::vector<unsigned> counts;
      std::vector<unsigned> per_chapter;
      size_t c;

      for (c = 0; c < chaps.size(); c++)
      {
         per_chapter.push_back((unsigned)chaps[c].levels().size());
         for (size_t l = 0; l < chaps[c].levels().size(); l++)
            counts.push_back(chaps[c].levels()[l].get_best_pushes());
      }

      icy_save_encode(save_data.data(), save_data.size(),
            counts.empty() ? NULL : &counts[0],
            per_chapter.empty() ? NULL : &per_chapter[0],
            per_chapter.size());
   }

   void GameManager::SaveManager::unserialize()
   {
      std::vector<unsigned> counts;
      std::vector<unsigned> per_chapter;
      size_t c;
      size_t flat = 0;

      for (c = 0; c < chaps.size(); c++)
      {
         per_chapter.push_back((unsigned)chaps[c].levels().size());
         for (size_t l = 0; l < chaps[c].levels().size(); l++)
            counts.push_back(0);
      }

      if (counts.empty())
         return;

      icy_save_decode(save_data.data(), save_data.size(), &counts[0],
            &per_chapter[0], per_chapter.size());

      for (c = 0; c < chaps.size(); c++)
      {
         for (size_t l = 0; l < chaps[c].levels().size(); l++, flat++)
         {
            chaps[c].levels()[l].set_best_pushes(counts[flat]);
            chaps[c].levels()[l].set_completion(counts[flat]);
         }
      }
   }

   size_t GameManager::SaveManager::size() const
   {
      return save_data.size();
   }
}

