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

using namespace std;

namespace Icy
{
   /* Loads an image or dies: every caller here treats a missing asset
    * as fatal, which is what the C entry point leaves to them. */
   /* Joins an asset path onto the game's directory. Returns a pointer to
    * a rotating set of buffers, so a caller may hold a few at once
    * without owning any of them - they are all consumed immediately by
    * a load. */
   static const char *asset(const char *dir, const char *name)
   {
      static char bufs[4][512];
      static unsigned next;
      char *out = bufs[next++ & 3];

      icy_path_join(out, 512, dir, name);
      return out;
   }

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

   GameManager::GameManager(const char *path_game,
         input_fn input_cb,
         video_fn video_cb)
      :
      m_current_chap(0), m_current_level(0), m_game_state(State::Title),
      m_input_cb(input_cb), m_input_ctx(NULL),
      m_video_cb(video_cb), m_video_ctx(NULL),
      chap_select(0), level_select(0),
      menu_slide_dir(blit_pos_zero())
   {
      icy_edge_init(&edges);
      icy_menu_slide_init(&slide);
      init_menu_surfaces();
      icy_path_dir(dir, sizeof(dir), path_game);

      /* A throw out of a constructor skips the destructor, so anything
       * this body has already taken has to be released by hand. Every
       * asset load below can fail on a broken install, and until
       * retro_load_game started catching them a failure took the
       * frontend down and the leak did not matter. */
      try
      {
      xml_doc doc;

      if (!doc.load(path_game))
      {
         char msg[512];
         snprintf(msg, sizeof(msg), "Failed to load game: %s.",
               path_game);
         throw runtime_error(msg);
      }

      /* pugixml's document was itself a node, so every lookup below
       * re-descended into <game>.  Take the element once. */
      rxml_node_t *game_node = blit_xml_root(doc.get(), "game");

      char font_path[512];

      icy_path_join(font_path, sizeof(font_path), dir,
            blit_xml_attr(blit_xml_child(game_node, "font"), "source"));
      char   err[256];

      if (!(font = blit_font_cluster_new()))
         throw std::bad_alloc();

      if (!blit_font_cluster_add(font, "yellow", font_path, blit_pos(-1, 1), blit_pixel_argb(0xff, 0xc0, 0x98, 0x00),
               err, sizeof(err)))
         throw runtime_error(err);
      if (!blit_font_cluster_add(font, "yellow", font_path, blit_pos( 0, 0), blit_pixel_argb(0xff, 0xff, 0xde, 0x00),
               err, sizeof(err)))
         throw runtime_error(err);
      if (!blit_font_cluster_add(font, "white", font_path, blit_pos(-1, 1), blit_pixel_argb(0xff, 0x73, 0x73, 0x8b),
               err, sizeof(err)))
         throw runtime_error(err);
      if (!blit_font_cluster_add(font, "white", font_path, blit_pos( 0, 0), blit_pixel_argb(0xff, 0xff, 0xff, 0xff),
               err, sizeof(err)))
         throw runtime_error(err);
      if (!blit_font_cluster_add(font, "lime", font_path, blit_pos(-1, 1), blit_pixel_argb(0xff, 0x39, 0x5a, 0x94),
               err, sizeof(err)))
         throw runtime_error(err);
      if (!blit_font_cluster_add(font, "lime", font_path, blit_pos( 0, 0), blit_pixel_argb(0xff, 0xb8, 0xe8, 0xb0),
               err, sizeof(err)))
         throw runtime_error(err);

      init_menu(blit_xml_attr(blit_xml_child(game_node, "title"), "source"));
      init_menu_sprite(game_node);
      init_sfx(game_node);
      init_bg(game_node);

      for (rxml_node_t *node = blit_xml_child(game_node, "chapter"); node; node = blit_xml_next(node, "chapter"))
      {
         load_chapter(node, (int)chapters.count);
      }

      blit_render_target_release(&ui_target);
      if (!blit_render_target_init_size(&ui_target, ICY_GAME_FB_WIDTH,
               ICY_GAME_FB_HEIGHT))
         throw std::bad_alloc();
      }
      catch (...)
      {
         release_owned();
         throw;
      }
   }

   /* What the destructor releases, so a failed construction can release
    * it too. */
   void GameManager::release_owned()
   {
      icy_level_list_release(&chapters);
      blit_font_cluster_free(font);
      font = NULL;
      blit_render_target_release(&target);
      blit_render_target_release(&ui_target);
      blit_surface_release(&lock_sprite);
      blit_surface_release(&level_complete);
      blit_surface_release(&level_select_bg);
      blit_surface_release(&end_credit_bg);
      blit_surface_release(&game_bg);
   }

   GameManager::GameManager()
      :
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
      /* Plain C members in a C++ class: nothing initialises them. */
      icy_level_list_init(&chapters);
      icy_save_clear(&save);
      dir[0] = '\0';
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
      release_owned();
   }

   void GameManager::init_menu_sprite(rxml_node_t *game_node)
   {
      {
         blit_surface_t tmp = cache_image_or_throw(asset(dir, blit_xml_attr(blit_xml_child(game_node, "level_complete"), "source")));
         blit_surface_assign(&level_complete, &tmp);
         blit_surface_release(&tmp);
      }

      {
         blit_surface_t tmp = cache_image_or_throw(asset(dir, blit_xml_attr(blit_xml_child(game_node, "lock_sprite"), "source")));
         blit_surface_assign(&lock_sprite, &tmp);
         blit_surface_release(&tmp);
      }
      lock_sprite.ignore_camera = 1;
      int arrow_x = (ICY_GAME_FB_WIDTH - lock_sprite.rect.w) / 2;
      lock_sprite.rect.pos = blit_pos( arrow_x, 160 );

      int complete_x = preview_base_x + ICY_GAME_FB_WIDTH / 2 - level_complete.rect.w - 2;
      int complete_y = preview_base_y + ICY_GAME_FB_HEIGHT / 2 - level_complete.rect.h - 2;
      level_complete.rect.pos = blit_pos( complete_x, complete_y );
      level_complete.ignore_camera = 1;

      {
         blit_surface_t tmp = cache_image_or_throw(asset(dir, blit_xml_attr(blit_xml_child(game_node, "menu_bg"), "source")));
         blit_surface_assign(&level_select_bg, &tmp);
         blit_surface_release(&tmp);
      }
      level_select_bg.ignore_camera = 1;

      {
         blit_surface_t tmp = cache_image_or_throw(asset(dir, blit_xml_attr(blit_xml_child(game_node, "end_bg"), "source")));
         blit_surface_assign(&end_credit_bg, &tmp);
         blit_surface_release(&tmp);
      }
      end_credit_bg.ignore_camera = 1;

      {
         blit_surface_t tmp = cache_image_or_throw(asset(dir, blit_xml_attr(blit_xml_child(game_node, "game_bg"), "source")));
         blit_surface_assign(&game_bg, &tmp);
         blit_surface_release(&tmp);
      }
      game_bg.ignore_camera = 1;
   }

   void GameManager::init_bg(rxml_node_t *game_node)
   {
      rxml_node_t *music = blit_xml_child(game_node, "music");
      rxml_node_t *node;
      /* A .game names a handful of tracks; this is well above any of
       * them and keeps the collection allocation-free. */
      enum { max_tracks = 32 };
      char         paths[max_tracks][512];
      const char  *path_ptrs[max_tracks];
      float        gains[max_tracks];
      size_t       count = 0;

      /* One walk over the <bg> elements rather than two: the source and
       * the volume come off the same node. */
      for (node = blit_xml_child(music, "bg"); node;
            node = blit_xml_next(node, "bg"))
      {
         const char *source = blit_xml_attr(node, "source");
         const char *volume = blit_xml_attr(node, "volume");

         if (count == max_tracks)
            break;

         icy_path_join(paths[count], sizeof(paths[count]), dir, source);
         path_ptrs[count] = paths[count];
         gains[count]     = *volume ? (float)std::strtod(volume, NULL)
            : 1.0f;
         count++;
      }

      if (!icy_bgm_set_tracks(icy_bgm(), count ? path_ptrs : NULL,
               count ? gains : NULL, count))
         throw std::bad_alloc();
   }

   void GameManager::init_sfx(rxml_node_t *game_node)
   {
      rxml_node_t *sfx = blit_xml_child(game_node, "sfx");
      rxml_node_t *node;

      for (node = blit_xml_child(sfx, "sound"); node;
            node = blit_xml_next(node, "sound"))
         if (!icy_sfx_add(icy_sfx(), blit_xml_attr(node, "name"),
                  asset(dir, blit_xml_attr(node, "source"))))
         {
            char msg[512];
            snprintf(msg, sizeof(msg), "Failed to load sound: %s",
                  blit_xml_attr(node, "source"));
            throw runtime_error(msg);
         }
   }

   static blit_surface_t make_preview(const char *path,
         const blit_surface_t& bg);

   void GameManager::load_chapter(rxml_node_t *chap, int chapter)
   {
      icy_chapter_t *loaded = icy_level_list_add_chapter(&chapters,
            blit_xml_attr(chap, "name"));
      rxml_node_t   *node;
      int            i = 0;

      if (!loaded)
         throw std::bad_alloc();

      for (node = blit_xml_child(chap, "map"); node;
            node = blit_xml_next(node, "map"), i++)
      {
         const char    *path    = asset(dir, blit_xml_attr(node, "source"));
         blit_surface_t preview = make_preview(path, game_bg);
         icy_level_t   *level   = icy_chapter_add_level(loaded,
               path, blit_xml_attr(node, "name"), &preview);

         blit_surface_release(&preview);

         if (!level)
            throw std::bad_alloc();

         level->position = blit_pos(preview_base_x + i * preview_delta_x,
               preview_base_y + preview_delta_y * chapter);
      }

      loaded->minimum_clear = blit_xml_attr_int(chap, "minimum_clear");

      /* A chapter with no levels is not one. */
      if (!loaded->count)
      {
         free(loaded->name);
         chapters.count--;
      }
   }

   void GameManager::init_menu(const char *level)
   {
      blit_surface_t surf = cache_image_or_throw(asset(dir, level));

      blit_render_target_release(&target);
      if (!blit_render_target_init_size(&target, ICY_GAME_FB_WIDTH,
               ICY_GAME_FB_HEIGHT))
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
      {
         char err[256];

         game.reset(icy_game_new(
                  chapters.chapters[chapter].levels[level].path,
                  chapter, level,
                  chapters.chapters[chapter].levels[level].best_pushes,
                  font, err, sizeof(err)));

         if (!game)
            throw runtime_error(err);
      }
      icy_game_set_input_cb(game.get(), m_input_cb, m_input_ctx);
      icy_game_set_video_cb(game.get(), m_video_cb, m_video_ctx);
      icy_game_set_bg(game.get(), &game_bg);

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
      if (current_chap == chapters.count - 1
            && current_level == chapters.chapters[chapters.count - 1].count - 1)
         return false;

      unsigned chap = current_chap;
      unsigned level = current_level;
      while (chap < chapters.count)
      {
         if (!chapters.chapters[chap].levels[level].completed)
         {
            current_chap = chap;
            current_level = level;
            return true;
         }

         level++;
         if (level >= chapters.chapters[chap].count)
         {
            if (!icy_chapter_cleared(&chapters.chapters[chap]))
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
      icy_save_load(&save, &chapters);
      m_current_chap = 0;
      m_current_level = 0;
      if (!find_next_unsolved_level(m_current_chap, m_current_level))
      {
         chap_select = (int)chapters.count - 1;
         level_select = (int)chapters.chapters[chapters.count - 1].count - 1;
      }
   }

   void GameManager::step_title()
   {
      if (m_input_cb(m_input_ctx, ICY_INPUT_PUSH) || m_input_cb(m_input_ctx, ICY_INPUT_MENU))
      {
         set_initial_level();
         enter_menu();
      }

      m_video_cb(m_video_ctx, target.buffer, target.rect.w, target.rect.h, target.rect.w * sizeof(blit_pixel_t));
   }

   void GameManager::enter_menu()
   {
      icy_save_load(&save, &chapters);
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
         if (chap < chapters.count - 1
               && !icy_chapter_cleared(&chapters.chapters[chap_select]))
            blit_render_target_blit(&ui_target, &lock_sprite, blit_rect_zero());

         // Render tick if level is complete.
         if (menu_slide_dir.x == 0
               && chapters.chapters[chap_select].levels[level_select].completed)
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

      render_previews();

      menu_render_ui();

      m_video_cb(m_video_ctx, ui_target.buffer, ui_target.rect.w, ui_target.rect.h, ui_target.rect.w * sizeof(blit_pixel_t));
   }

   /* Every level's preview, at the position the level was given when the
    * chapter loaded. */
   void GameManager::render_previews()
   {
      size_t c;
      size_t l;

      for (c = 0; c < chapters.count; c++)
         for (l = 0; l < chapters.chapters[c].count; l++)
         {
            icy_level_t *level = &chapters.chapters[c].levels[l];

            blit_render_target_blit_offset(&ui_target, &level->preview,
                  blit_rect_zero(), level->position);
         }
   }

   const icy_level_t *GameManager::get_selected_level() const
   {
      return &chapters.chapters[chap_select].levels[level_select];
   }

   void GameManager::start_slide(blit_pos_t dir, unsigned cnt)
   {
      m_game_state = State::MenuSlide;

      /* cnt is the duration in 60 Hz frames; dir * cnt is the distance it
       * used to cover one tick at a time. Keep the distance, convert the
       * duration. */
      icy_menu_slide_start(&slide, blit_pos_scale((int)cnt, dir),
            icy_frames_to_ticks(cnt));

      menu_slide_dir = dir;

      icy_sfx_play(icy_sfx(), "level_next", 0.5f);
   }

   void GameManager::step_menu()
   {
      blit_render_target_blit(&ui_target, &level_select_bg, blit_rect_zero());

      render_previews();

      menu_render_ui();

      /* Edges, not levels: a held direction moves the cursor once. */
      bool pressed_menu_left  = icy_edge_pressed(&edges, ICY_EDGE_LEFT,
            m_input_cb(m_input_ctx, ICY_INPUT_LEFT));
      bool pressed_menu_right = icy_edge_pressed(&edges, ICY_EDGE_RIGHT,
            m_input_cb(m_input_ctx, ICY_INPUT_RIGHT));
      bool pressed_menu_up    = icy_edge_pressed(&edges, ICY_EDGE_UP,
            m_input_cb(m_input_ctx, ICY_INPUT_UP));
      bool pressed_menu_down  = icy_edge_pressed(&edges, ICY_EDGE_DOWN,
            m_input_cb(m_input_ctx, ICY_INPUT_DOWN));
      bool pressed_menu_ok    = icy_edge_pressed(&edges, ICY_EDGE_OK,
            m_input_cb(m_input_ctx, ICY_INPUT_PUSH));
      bool pressed_menu       = icy_edge_pressed(&edges, ICY_EDGE_MENU,
            m_input_cb(m_input_ctx, ICY_INPUT_MENU));

      /* Navigation is index arithmetic over the chapter list, so it
       * lives in icy_menu_select.c; this turns a press into a direction
       * and the move it reports into a slide. */
      {
         icy_menu_chapters_t list;
         icy_menu_cursor_t   cursor;
         enum icy_menu_dir   dir;
         bool                pressed = true;

         list.chapters = (unsigned)chapters.count;
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

      m_video_cb(m_video_ctx, ui_target.buffer, ui_target.rect.w, ui_target.rect.h, ui_target.rect.w * sizeof(blit_pixel_t));
   }

   void GameManager::step_game()
   {
      if (!game)
         return;

      if (!icy_game_iterate(game.get()))
         throw runtime_error(icy_game_error(game.get()));

      bool pressed_menu = m_input_cb(m_input_ctx, ICY_INPUT_MENU);
      bool pressed_reset = m_input_cb(m_input_ctx, ICY_INPUT_RESET);

      if (icy_edge_pressed(&edges, ICY_EDGE_RESET, pressed_reset))
         reset_level();
      else if (icy_edge_pressed(&edges, ICY_EDGE_MENU, pressed_menu))
         enter_menu();

      if (icy_game_won(game.get()))
      {
         unsigned pushes = icy_game_pushes(game.get());
         icy_level_record_clear(&chapters.chapters[m_current_chap].levels[m_current_level], pushes);

         game.reset();
         bool trigger_completion = !chapters.chapters[m_current_chap].levels[m_current_level].completed;
         chapters.chapters[m_current_chap].levels[m_current_level].completed = 1;
         icy_save_store(&save, &chapters);

         // Go to ending screen on the event that all levels have been cleared.
         bool cleared_all = trigger_completion;
         if (trigger_completion)
         {
            cleared_all = icy_level_list_cleared(&chapters)
               == icy_level_list_total(&chapters);
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
            m_input_cb(m_input_ctx, ICY_INPUT_PUSH));
      bool trigger_menu = icy_edge_pressed(&edges, ICY_EDGE_MENU,
            m_input_cb(m_input_ctx, ICY_INPUT_MENU));

      if (trigger_ok || trigger_menu)
         enter_menu();

      blit_font_cluster_set_id(font, "white");
      blit_font_cluster_render(font, &ui_target,
            "You completed all levels!\nAwesome! :D\nThanks for playing Dinothawr!",
            160, 155, BLIT_FONT_CENTERED, 2);
      m_video_cb(m_video_ctx, ui_target.buffer, ui_target.rect.w, ui_target.rect.h, ui_target.rect.w * sizeof(blit_pixel_t));
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
      return icy_level_list_total(&chapters);
   }

   unsigned GameManager::total_cleared_levels() const
   {
      return icy_level_list_cleared(&chapters);
   }

   /* Renders one frame of a level at half size, which is what the menu
    * shows for it. */
   static blit_surface_t make_preview(const char *path,
         const blit_surface_t& bg)
   {
      blit_surface_t preview;

      blit_surface_init(&preview);

      /* No font: a preview draws the level once and never a HUD. */
      char        err[256];
      icy_game_t *preview_game = icy_game_new(path, 0, 0, 0, NULL,
            err, sizeof(err));

      if (!preview_game)
         throw runtime_error(err);

      icy_game_set_bg(preview_game, &bg);

      static const unsigned scale_factor = 2;
      int preview_width  = ICY_GAME_FB_WIDTH / scale_factor;
      int preview_height = ICY_GAME_FB_HEIGHT / scale_factor;

      /* The destination is the surface's own buffer, filled directly by
       * the video callback - there is no reason for a vector and a copy
       * out of it. */
      blit_pixel_t *owned = (blit_pixel_t*)calloc(
            (size_t)preview_width * preview_height, sizeof(*owned));

      /* The downscale needs the destination and its pitch, so it goes
       * through the context pointer rather than a capture. */
      struct preview_ctx { blit_pixel_t *data; int width; };
      preview_ctx ctx = { owned, preview_width };

      if (!owned)
         throw std::bad_alloc();

      icy_game_set_input_cb(preview_game,
            [](void*, enum icy_input) { return 0; }, NULL);
      icy_game_set_video_cb(preview_game,
            [](void *ctxv, const void* pix_data, unsigned width, unsigned height, size_t pitch) {
         preview_ctx  *c = static_cast<preview_ctx*>(ctxv);
         blit_pixel_t *data = c->data;
         int preview_width = c->width;
         const blit_pixel_t* pix = reinterpret_cast<const blit_pixel_t*>(pix_data);
         pitch /= sizeof(blit_pixel_t);

         for (unsigned y = 0; y < height; y += scale_factor)
         {
            for (unsigned x = 0; x < width; x += scale_factor)
            {
               blit_pixel_t a0 = pix[pitch * (y + 0) + (x + 0)];
               blit_pixel_t a1 = pix[pitch * (y + 0) + (x + 1)];
               blit_pixel_t b0 = pix[pitch * (y + 1) + (x + 0)];
               blit_pixel_t b1 = pix[pitch * (y + 1) + (x + 1)];
               blit_pixel_t res = blit_pixel_blend(blit_pixel_blend(a0, a1),
                     blit_pixel_blend(b0, b1));

               data[preview_width * (y / scale_factor) + (x / scale_factor)] = res | BLIT_PIXEL_ALPHA_MASK;
            }
         }
      }, &ctx);

      if (!icy_game_iterate(preview_game))
      {
         char why[256];

         snprintf(why, sizeof(why), "%s", icy_game_error(preview_game));
         icy_game_free(preview_game);
         free(owned);
         throw runtime_error(why);
      }

      icy_game_free(preview_game);

      {
         /* The surface takes its own reference; drop the one made here. */
         blit_surface_data_t *pdata = blit_surface_data_new(owned,
               preview_width, preview_height);

         if (!pdata)
         {
            free(owned);
            throw std::bad_alloc();
         }

         blit_surface_init_data(&preview, pdata);
         blit_surface_data_unref(pdata);
      }
      return preview;
   }

}

