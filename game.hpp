#ifndef GAME_HPP__
#define GAME_HPP__

#include "surface_cluster.hpp"
#include "tilemap.hpp"
#include "blit_font.h"
#include "audio/mixer_f32.h"
#include "audio/mixer_i16.h"
#include "audio/async_job.h"

#include <string>
#include <functional>
#include <cstddef>
#include <functional>

#include "libretro.h"

namespace Icy
{
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
   unsigned frames_to_ticks(unsigned frames60);

   enum class Input : unsigned
   {
      Up = 0,
      Down,
      Left,
      Right,
      Push,
      Menu,
      Reset,
      None
   };

   class SFXManager
   {
#ifndef USE_CXX03
      public:
         SFXManager() = default;
         ~SFXManager();
         /* Owns raw-pointer int16 buffers (released in the destructor), so
          * copying would double-unref. It is a singleton; forbid copies. */
         SFXManager(const SFXManager&) = delete;
         SFXManager& operator=(const SFXManager&) = delete;
         void add_stream(const std::string &ident, const std::string &path);
         void play_sfx(const std::string &ident, float volume = 1.0f) const;

      private:
         /* Decoded WAV kept as reference-counted PCM in whichever sample
          * type the live mixer wants, played as pcm streams into it. */
         std::map<std::string, f32_buf_t*> effects_f32;
         std::map<std::string, i16_buf_t*> effects_i16;
#else
      public:
         void add_stream(const std::string &ident, const std::string &path) {}
         void play_sfx(const std::string &ident, float volume = 1.0f) const {}
#endif
   };

   SFXManager& get_sfx();

   class BGManager
   {
#ifndef USE_CXX03
      public:
         struct Track
         {
            std::string path;
            float gain;
         };

         BGManager() : first(true), last(0), rng_state(0), job(NULL) {}

         void init(const std::vector<Track>& tracks);

         /* Releases everything the int16 path is holding, including a
          * decode still in flight. Call before the mixer it feeds goes
          * away. */
         void deinit();
         void step();

      private:
         std::vector<Track> tracks;
         bool first;
         unsigned last;

         /* Private shuffle state.  This used to be srand()/rand(): a core
          * reseeding the global C PRNG walks over whatever the frontend
          * or another statically linked core had set up, and gets walked
          * over in turn.  time(NULL) also has one-second resolution, so
          * two cores starting in the same second shuffled identically. */
         uint32_t rng_state;
         unsigned rng_next(unsigned n);

         /* The next track is decoded off-thread so a track change does
          * not stall the game. The job outlives step(), and job_path is
          * the string it reads on its own thread, so both live here
          * rather than on the stack. */
         async_job_t *job;
         std::string job_path;

         unsigned next_index();
#else
      public:
         struct Track
         {
            std::string path;
            float gain;
         };
         void init(const std::vector<Track>& tracks) {}
         void step() {}
#endif
   };

   BGManager& get_bg();

   class CameraManager
   {
      public:
         CameraManager(blit_render_target_t& target, const Blit::Rect& rect, Blit::Pos map_size);
         void update();

      private:
         blit_render_target_t* target;
         const Blit::Rect* rect;
         Blit::Pos map_size;
   };

   class EdgeDetector
   {
      public:
         EdgeDetector(bool init);
         bool set(bool state);
      private:
         bool pos;
   };

   class Game
   {
      public:
         Game(const std::string& level_path, unsigned chapter, unsigned level, unsigned best_pushes, blit_font_cluster_t* font);
         Game(const std::string& level_path);

         void input_cb(std::function<bool (Input)> cb) { m_input_cb = cb; }
         void video_cb(std::function<void (const void*, unsigned, unsigned, std::size_t)> cb) { m_video_cb = cb; }

         int width() const { return map.pix_width(); }
         int height() const { return map.pix_height(); }

         unsigned get_pushes() const { return pushes; }
         ~Game();

         /* player is a raw surface counted by hand, and a Game is only
          * ever held by unique_ptr or as a local, so copying one is a
          * mistake rather than something to support. */
         Game(const Game&) = delete;
         Game& operator=(const Game&) = delete;

         void set_bg(const blit_surface_t& bg);

         /* player is a raw surface, so face selection goes through the
          * C entry point; these keep the failure message the wrapper
          * used to raise. */
         void set_player_alt(const std::string& id, unsigned index = 0);
         void set_player_alt_index(unsigned index);

         void iterate();
         bool won() const;

         static const unsigned fb_width = 320;
         static const unsigned fb_height = 200;

      private:
         Blit::Tilemap map;
         blit_render_target_t target;
         blit_surface_t player;
         Blit::Pos player_off;
         blit_font_cluster_t *font;
         const blit_surface_t *bg;
         Input facing;

         CameraManager camera;

         unsigned won_frame_cnt;
         bool m_won_early;
         /* Durations in 60 Hz frames; frames_to_ticks() turns them into
          * tick counts at the rate actually being run. */
         enum { won_frame_cnt_limit  = 60 * 5 };
         enum { won_frames_per_iter  = 24 };
         enum { anim_frames_per_step = 10 };
         enum { push_anim_frames     = 7 };
         bool won_condition();

         std::function<bool (Input)> m_input_cb;
         std::function<void (const void*, unsigned, unsigned, std::size_t)> m_video_cb;

         std::function<bool ()> stepper;
         void run_stepper();

         unsigned frame_cnt;
         bool player_walking;
         bool is_sliding;
         unsigned stepper_cnt;

         /* A tile_stepper leg - one tile of travel - is interpolated
          * across leg_ticks ticks rather than advanced by a fixed 2 px,
          * so it takes the same time at any rate and still lands exactly
          * on the grid. leg_moved is how far into the tile the surface
          * has been carried so far. */
         unsigned leg_tick;
         unsigned leg_ticks;
         int      leg_moved;
         void begin_leg(int tile_size);

         void set_initial_pos(const std::string& level);
         void update_player();
         void update_animation();
         void prepare_won_animation();
         void update_input();
         void update_triggers();
         void move_if_no_collision(Input input);
         void push_block();
         bool is_offset_collision(blit_surface_t& surf, Blit::Pos offset);

         bool tile_stepper(blit_surface_t& surf, Blit::Pos step_dir);
         bool win_animation_stepper();

         unsigned best_pushes;
         unsigned pushes;
         unsigned chapter;
         unsigned level;

         static Blit::Pos input_to_offset(Input input);
         std::string input_to_string(Input input);
         Input string_to_input(const std::string& dir);

         std::vector<Blit::SurfaceCluster::Elem*> get_tiles_with_attr(const std::string& layer,
               const std::string& attr, const std::string& val = "");

         EdgeDetector push;
   };

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
               std::function<bool (Input)> input_cb,
               std::function<void (const void*, unsigned, unsigned, std::size_t)> video_cb);

         GameManager();
         ~GameManager();

         /* The menu surfaces are raw and counted by hand, and a
          * GameManager is a singleton held by unique_ptr. */
         GameManager(const GameManager&) = delete;
         GameManager& operator=(const GameManager&) = delete;

         void input_cb(std::function<bool (Input)> cb) { m_input_cb = cb; }
         void video_cb(std::function<void (const void*, unsigned, unsigned, std::size_t)> cb) { m_video_cb = cb; }

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

               Blit::Pos pos() const { return position; }
               void pos(Blit::Pos p) { position = p; }

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
               Blit::Pos position;
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

         std::unique_ptr<Game> game;
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

         std::function<bool (Input)> m_input_cb;
         std::function<void (const void*, unsigned, unsigned, std::size_t)> m_video_cb;

         void init_menu_surfaces();
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
         void start_slide(Blit::Pos dir, unsigned cnt);
         void menu_render_ui();

         int chap_select;
         int level_select;
         unsigned total_levels() const;
         unsigned total_cleared_levels() const;

         bool old_pressed_menu_left;
         bool old_pressed_menu_right;
         bool old_pressed_menu_up;
         bool old_pressed_menu_down;
         bool old_pressed_menu_ok;
         bool old_pressed_menu;
         bool old_pressed_reset;

         Blit::Pos menu_slide_dir;

         /* The slide used to be slide_end applications of menu_slide_dir,
          * one per tick. It is now a total displacement interpolated over
          * the tick count spanning the same wall-clock time, so the pan
          * takes as long at any rate and ends exactly on slide_total. */
         Blit::Pos slide_total;
         Blit::Pos slide_moved;
         unsigned slide_cnt;
         unsigned slide_end;

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

