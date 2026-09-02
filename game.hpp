#ifndef GAME_HPP__
#define GAME_HPP__

#include "blit_pixel.h"
#include "blit_geom.h"
#include "blit_surface_cache.h"

#include <vector>
#include "blit_tilemap.h"
#include "blit_xml.h"
#include "icy_menu_slide.h"
#include "icy_edge.h"
#include "icy_rate.h"
#include "icy_game.h"
#include "icy_input.h"
#include "icy_leg.h"
#include "icy_camera.h"
#include <map>
#include "blit_font.h"
#include "audio/mixer_f32.h"
#include "audio/mixer_i16.h"
#include "audio/game_audio.h"

#include <cstddef>
#include <memory>
#include <functional>
#include <string>

#include "libretro.h"

namespace Icy
{

   /* An attribute or a default, for the loader's many optional keys. */
   inline const char *attr_or(const blit_attr_table_t *table,
         const char *key, const char *fallback)
   {
      const char *value = blit_attr_table_find(table, key);
      return value ? value : fallback;
   }

   /* The two audio backends. Exactly one is live for a given game:
    * audio_is_float() says which, and the other accessor returns NULL.
    * They are the same mixer in two sample types, so the managers drive
    * both through the same sequence of calls. */
   mixer_f32_t* get_mixer_f32();
   mixer_i16_t* get_mixer_i16();
   bool audio_is_float();
   const std::string& get_basedir();

   /* retro_run advances the game exactly once per call, so the sim is
    * stepped at the frontend's frame rate.
    *
    * The sim was written against a fixed 60 Hz tick and says so all over
    * game.cpp - the walk cycle changes every 10 ticks, a tile takes 8,
    * the win animation counts to 24. Read literally at 120 Hz those are
    * half the durations they were written as, which is the dino walking
    * at double speed with an animation too fast to read. Every one of
    * them is a *duration* now, passed through here, so the game keeps
    * its original speed while actually updating - and sampling input -
    * once per output frame. That is the point of running at 120: not
    * smoother 60 Hz motion, but a sim that responds in half the time.
    *
    * Returns the ticks spanning the same wall-clock time as @frames60
    * frames of the original sim. Never less than 1, so a duration can't
    * collapse to nothing at low rates, and exactly @frames60 at 60 Hz,
    * which is what keeps the default rate bit-for-bit what it was. */

   /* The frontend's two hooks live in icy_input.h, so the C side of the
    * game can hold them. */
   typedef icy_input_fn input_fn;
   typedef icy_video_fn video_fn;

   class GameManager
   {
      public:

         enum class State
         {
            Title,
            Menu,
            MenuSlide,
            Game,
            End
         };

         GameManager(const std::string& path_game,
               input_fn input_cb,
               video_fn video_cb);

         GameManager();
         ~GameManager();

         /* The menu surfaces are raw and counted by hand, and a
          * GameManager is a singleton held by unique_ptr. */
         GameManager(const GameManager&) = delete;
         GameManager& operator=(const GameManager&) = delete;

         void input_cb(input_fn cb, void *ctx = NULL)
         { m_input_cb = cb; m_input_ctx = ctx; }
         void video_cb(video_fn cb, void *ctx = NULL)
         { m_video_cb = cb; m_video_ctx = ctx; }

         void iterate();

         bool done() const;

         void reset_level();
         void change_level(unsigned chapter, unsigned level);
         unsigned current_level() const { return m_current_level; }
         State game_state() const { return m_game_state; }

         std::size_t save_size() const { return save.size(); }
         void* save_data() { return save.data(); }

      private:

         class Level
         {
            public:
               Level() : position(blit_pos_zero()),
                  completion(false), best_pushes(0)
               { blit_surface_init(&preview); }

               /* preview is a raw surface, so the reference counting is
                * explicit: Chapter keeps Levels in a vector. */
               Level(const Level& other)
                  : position(other.position), m_path(other.m_path),
                  m_name(other.m_name), preview(other.preview),
                  completion(other.completion),
                  best_pushes(other.best_pushes)
               { blit_surface_retain(&preview); }

               Level& operator=(const Level& other)
               {
                  if (this != &other)
                  {
                     blit_surface_retain(&other.preview);
                     blit_surface_release(&preview);

                     position    = other.position;
                     preview     = other.preview;
                     m_path      = other.m_path;
                     m_name      = other.m_name;
                     completion  = other.completion;
                     best_pushes = other.best_pushes;
                  }
                  return *this;
               }

               ~Level() { blit_surface_release(&preview); }

               blit_pos_t pos() const { return position; }
               void pos(blit_pos_t p) { position = p; }

               Level(const std::string& path, const blit_surface_t& bg);
               const std::string& path() const { return m_path; }

               void set_name(const std::string& name) { m_name = name; }
               const std::string& name() const { return m_name; }

               void render(blit_render_target_t& target) const;

               void set_completion(bool state) { completion = state; }
               bool get_completion() const { return completion; }

               void set_best_pushes(unsigned pushes) { if (!best_pushes || pushes < best_pushes) best_pushes = pushes; }
               unsigned get_best_pushes() const { return best_pushes; }

            private:
               blit_pos_t position;
               std::string m_path;
               std::string m_name;
               blit_surface_t preview;
               bool completion;
               unsigned best_pushes;
         };

         class Chapter
         {
            public:
               Chapter() : minimum_clear(0) {}

               Chapter(std::vector<Level> levels, const std::string& name) :
                  m_levels(std::move(levels)), m_name(name), minimum_clear(0) {}

               void set_minimum_clear(unsigned minimum) { minimum_clear = minimum; }
               bool cleared() const
               {
                  return cleared_count() >= minimum_clear;
               }

               unsigned cleared_count() const
               {
                  unsigned clear_cnt = 0;
                  for (std::vector<Icy::GameManager::Level>::const_iterator level = m_levels.begin(); level != m_levels.end(); level++)
                     clear_cnt += level->get_completion();

                  return clear_cnt;
               }

               void set_completion(unsigned level, bool state) { m_levels.at(level).set_completion(state); }
               bool get_completion(unsigned level) const { return m_levels.at(level).get_completion(); }
               const std::string& name() const { return m_name; }
               const Level& level(unsigned i) const { return m_levels.at(i); }
               Level& level(unsigned i) { return m_levels.at(i); }
               const std::vector<Level>& levels() const { return m_levels; }
               std::vector<Level>& levels() { return m_levels; }
               unsigned num_levels() const { return m_levels.size(); }

            private:
               std::vector<Level> m_levels;
               std::string m_name;
               unsigned minimum_clear;
         };

         class SaveManager
         {
            public:
               SaveManager(std::vector<Chapter> &chaps);

               void *data();
               void serialize();
               void unserialize();
               std::size_t size() const;

            private:
               std::vector<Chapter> &chaps;
               std::vector<char> save_data;
               static const std::size_t save_game_size = 512;
         };

         /* chapters must be declared before save: SaveManager holds a
          * reference to it and is handed that reference in the member
          * initialiser list, so it has to be constructed first. */
         std::vector<Chapter> chapters;

         SaveManager save;

         /* The level in play, or nothing while in the menu. */
         struct GameDeleter
         { void operator()(icy_game_t *g) const { icy_game_free(g); } };
         std::unique_ptr<icy_game_t, GameDeleter> game;
         std::string dir;

         unsigned m_current_chap;
         unsigned m_current_level;
         State m_game_state;

         blit_render_target_t target;

         blit_render_target_t ui_target;
         blit_font_cluster_t *font;

         blit_surface_t lock_sprite;

         blit_surface_t level_complete;
         blit_surface_t level_select_bg;
         blit_surface_t end_credit_bg;
         blit_surface_t game_bg;

         input_fn m_input_cb;
         void    *m_input_ctx;
         video_fn m_video_cb;
         void    *m_video_ctx;

         /* Public for the C navigation module's callbacks, which take a
          * GameManager as their context. */
      public:
         unsigned chapter_level_count(unsigned chapter) const
         { return chapter < chapters.size()
            ? (unsigned)chapters[chapter].num_levels() : 0; }
         bool chapter_is_cleared(unsigned chapter) const
         { return chapter < chapters.size() && chapters[chapter].cleared(); }

      private:
         void init_menu_surfaces();
         void release_owned();
         void init_menu(const std::string& title);
         void init_menu_sprite(rxml_node_t* game_node);
         void init_level(unsigned chapter, unsigned level);
         void init_sfx(rxml_node_t* game_node);
         void init_bg(rxml_node_t* game_node);

         Chapter load_chapter(rxml_node_t* chap_node, int chapter);
         const Level& get_selected_level() const;

         void step_title();
         void step_game();
         void step_end();

         // Menu stuff.
         void enter_menu();
         void set_initial_level();
         bool find_next_unsolved_level(unsigned& chap, unsigned& level);
         void step_menu();
         void step_menu_slide();
         void start_slide(blit_pos_t dir, unsigned cnt);
         void menu_render_ui();

         int chap_select;
         int level_select;
         unsigned total_levels() const;
         unsigned total_cleared_levels() const;

         /* One set rather than a flag per button: they are always
          * sampled together, and the failure mode was one of them not
          * being cleared with the rest. */
         icy_edge_t edges;

         blit_pos_t menu_slide_dir;

         icy_menu_slide_t slide;

         enum {
            preview_base_x      = 80,
            preview_base_y      = 50,
            font_preview_base_x = 40,
            font_preview_base_y = 40,
            preview_delta_x     = 8 * 24,
            preview_delta_y     = 8 * 24,
            preview_slide_cnt   = 24
         };
   };
}

extern retro_log_printf_t log_cb;

#endif

