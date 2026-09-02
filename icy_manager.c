/* Dinothawr - the game as a whole (implementation).
 * MSVC C89. See icy_manager.h. */

#include "icy_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <compat/msvc.h>

#include "audio/game_audio.h"
#include "blit_font.h"
#include "blit_render_target.h"
#include "blit_surface_cache.h"
#include "blit_xml.h"
#include "icy_edge.h"
#include "icy_game.h"
#include "icy_levels.h"
#include "icy_menu_select.h"
#include "icy_menu_slide.h"
#include "icy_path.h"
#include "icy_save.h"

/* Where the previews sit in the level-select grid. */
enum
{
   PREVIEW_BASE_X      = 80,
   PREVIEW_BASE_Y      = 50,
   FONT_PREVIEW_BASE_X = 40,
   FONT_PREVIEW_BASE_Y = 40,
   PREVIEW_DELTA_X     = 8 * 24,
   PREVIEW_DELTA_Y     = 8 * 24,
   PREVIEW_SLIDE_CNT   = 24
};

enum icy_state
{
   ICY_STATE_TITLE = 0,
   ICY_STATE_MENU,
   ICY_STATE_MENU_SLIDE,
   ICY_STATE_GAME,
   ICY_STATE_END
};

struct icy_manager
{
   icy_level_list_t      chapters;
   icy_save_t            save;

   int                   m_failed;
   char                  m_error[256];

   icy_game_t           *game;
   char                  dir[512];

   unsigned              m_current_chap;
   unsigned              m_current_level;
   enum icy_state        m_game_state;

   blit_render_target_t  target;
   blit_render_target_t  ui_target;
   blit_font_cluster_t  *font;

   blit_surface_t        lock_sprite;
   blit_surface_t        level_complete;
   blit_surface_t        level_select_bg;
   blit_surface_t        end_credit_bg;
   blit_surface_t        game_bg;

   icy_input_fn          m_input_cb;
   void                 *m_input_ctx;
   icy_video_fn          m_video_cb;
   void                 *m_video_ctx;

   icy_edge_t            edges;
   blit_pos_t            menu_slide_dir;
   icy_menu_slide_t      slide;

   int                   chap_select;
   int                   level_select;
};

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

   /* An empty surface on failure, with the reason left in the cache for
    * the caller to record. */
   static blit_surface_t cache_image(const char *path)
   {
      blit_surface_t out;

      if (!blit_surface_cache_image(blit_surface_cache(), path, &out))
         blit_surface_init(&out);

      return out;
   }

   static const char *cache_why(void)
   {
      return blit_surface_cache_error(blit_surface_cache());
   }


/* ---- Forward declarations -------------------------------------------- */

static void manager_fail(icy_manager_t *m, const char *what);
static void manager_release_owned(icy_manager_t *m);
static void manager_init_menu_surfaces(icy_manager_t *m);
static void manager_init_menu_sprite(icy_manager_t *m, rxml_node_t *game_node);
static void manager_init_bg(icy_manager_t *m, rxml_node_t *game_node);
static void manager_init_sfx(icy_manager_t *m, rxml_node_t *game_node);
static void manager_load_chapter(icy_manager_t *m, rxml_node_t *chap,
      int chapter);
static void manager_init_menu(icy_manager_t *m, const char *level);
static void manager_reset_level(icy_manager_t *m);
static void manager_change_level(icy_manager_t *m, unsigned chapter,
      unsigned level);
static void manager_init_level(icy_manager_t *m, unsigned chapter,
      unsigned level);
static int  manager_find_next_unsolved_level(icy_manager_t *m,
      unsigned *chap, unsigned *level);
static void manager_set_initial_level(icy_manager_t *m);
static void manager_step_title(icy_manager_t *m);
static void manager_enter_menu(icy_manager_t *m);
static void manager_menu_render_ui(icy_manager_t *m);
static void manager_step_menu_slide(icy_manager_t *m);
static void manager_render_previews(icy_manager_t *m);
static const icy_level_t *manager_get_selected_level(icy_manager_t *m);
static void manager_start_slide(icy_manager_t *m, blit_pos_t dir,
      unsigned cnt);
static void manager_step_menu(icy_manager_t *m);
static void manager_step_game(icy_manager_t *m);
static void manager_step_end(icy_manager_t *m);
static unsigned manager_total_levels(icy_manager_t *m);
static unsigned manager_total_cleared_levels(icy_manager_t *m);
static int make_preview(const char *path, const blit_surface_t *bg,
      blit_surface_t *out, char *error, size_t error_len);


/* The level preview renders one frame at half size; these are what it
 * hands the game to read input from and to draw into. */
struct preview_ctx
{
   blit_pixel_t *data;
   int           width;
};

static int preview_input_cb(void *ctx, enum icy_input input)
{
   (void)ctx;
   (void)input;
   return 0;
}

static void preview_video_cb(void *ctxv, const void *pix_data,
      unsigned width, unsigned height, size_t pitch)
{
   struct preview_ctx *c = (struct preview_ctx*)ctxv;
   blit_pixel_t       *data = c->data;
   int                 preview_width = c->width;
   const blit_pixel_t *pix = (const blit_pixel_t*)pix_data;
   unsigned            x;
   unsigned            y;
   const unsigned      scale_factor = 2;

   pitch /= sizeof(blit_pixel_t);

   for (y = 0; y < height; y += scale_factor)
   {
      for (x = 0; x < width; x += scale_factor)
      {
         blit_pixel_t a0 = pix[pitch * (y + 0) + (x + 0)];
         blit_pixel_t a1 = pix[pitch * (y + 0) + (x + 1)];
         blit_pixel_t b0 = pix[pitch * (y + 1) + (x + 0)];
         blit_pixel_t b1 = pix[pitch * (y + 1) + (x + 1)];
         blit_pixel_t res = blit_pixel_blend(blit_pixel_blend(a0, a1),
               blit_pixel_blend(b0, b1));

         data[preview_width * (y / scale_factor) + (x / scale_factor)]
            = res | BLIT_PIXEL_ALPHA_MASK;
      }
   }
}

/* Chapter list accessors for icy_menu_select.c. */
static unsigned menu_levels_cb(void *ctx, unsigned chapter)
{
   const icy_manager_t *m = (const icy_manager_t*)ctx;

   return chapter < m->chapters.count
      ? (unsigned)m->chapters.chapters[chapter].count : 0;
}

static int menu_cleared_cb(void *ctx, unsigned chapter)
{
   const icy_manager_t *m = (const icy_manager_t*)ctx;

   return chapter < m->chapters.count
      && icy_chapter_cleared(&m->chapters.chapters[chapter]);
}

static int manager_load(icy_manager_t *m, const char *path_game)
   {
      rxml_document_t *doc;
      rxml_node_t     *node;
      icy_edge_init(&m->edges);
      icy_menu_slide_init(&m->slide);
      manager_init_menu_surfaces(m);
      icy_path_dir(m->dir, sizeof(m->dir), path_game);

      if (!(doc = blit_xml_load(path_game)))
      {
         char msg[512];
         snprintf(msg, sizeof(msg), "Failed to load game: %s.",
               path_game);
         manager_fail(m, msg);
      }

      /* pugixml's document was itself a node, so every lookup below
       * re-descended into <m->game>.  Take the element once. */
      rxml_node_t *game_node = blit_xml_root(doc, "game");

      char font_path[512];

      icy_path_join(font_path, sizeof(font_path), m->dir,
            blit_xml_attr(blit_xml_child(game_node, "font"), "source"));
      char   err[256];

      if (!m->m_failed && !(m->font = blit_font_cluster_new()))
         manager_fail(m, "Out of memory.");

      if (!m->m_failed && !blit_font_cluster_add(m->font, "yellow", font_path,
               blit_pos(-1, 1), blit_pixel_argb(0xff, 0xc0, 0x98, 0x00), err, sizeof(err)))
         manager_fail(m, err);
      if (!m->m_failed && !blit_font_cluster_add(m->font, "yellow", font_path,
               blit_pos( 0, 0), blit_pixel_argb(0xff, 0xff, 0xde, 0x00), err, sizeof(err)))
         manager_fail(m, err);
      if (!m->m_failed && !blit_font_cluster_add(m->font, "white", font_path,
               blit_pos(-1, 1), blit_pixel_argb(0xff, 0x73, 0x73, 0x8b), err, sizeof(err)))
         manager_fail(m, err);
      if (!m->m_failed && !blit_font_cluster_add(m->font, "white", font_path,
               blit_pos( 0, 0), blit_pixel_argb(0xff, 0xff, 0xff, 0xff), err, sizeof(err)))
         manager_fail(m, err);
      if (!m->m_failed && !blit_font_cluster_add(m->font, "lime", font_path,
               blit_pos(-1, 1), blit_pixel_argb(0xff, 0x39, 0x5a, 0x94), err, sizeof(err)))
         manager_fail(m, err);
      if (!m->m_failed && !blit_font_cluster_add(m->font, "lime", font_path,
               blit_pos( 0, 0), blit_pixel_argb(0xff, 0xb8, 0xe8, 0xb0), err, sizeof(err)))
         manager_fail(m, err);

      if (!m->m_failed)
      {
         manager_init_menu(m, blit_xml_attr(blit_xml_child(game_node, "title"),
                  "source"));
         manager_init_menu_sprite(m, game_node);
         manager_init_sfx(m, game_node);
         manager_init_bg(m, game_node);
      }

      for (node = blit_xml_child(game_node, "chapter");
            node && !m->m_failed; node = blit_xml_next(node, "chapter"))
         manager_load_chapter(m, node, (int)m->chapters.count);

      blit_render_target_release(&m->ui_target);
      if (!m->m_failed && !blit_render_target_init_size(&m->ui_target,
               ICY_GAME_FB_WIDTH, ICY_GAME_FB_HEIGHT))
         manager_fail(m, "Out of memory.");

      rxml_free_document(doc);
      return !m->m_failed;
   }


   /* What the destructor releases, so a failed construction can release
    * it too. */
static void manager_fail(icy_manager_t *m, const char *what)
   {
      if (m->m_failed)
         return;

      m->m_failed = 1;
      snprintf(m->m_error, sizeof(m->m_error), "%s", what);
   }


static void manager_release_owned(icy_manager_t *m)
   {
      icy_game_free(m->game);
      m->game = NULL;
      icy_level_list_release(&m->chapters);
      blit_font_cluster_free(m->font);
      m->font = NULL;
      blit_render_target_release(&m->target);
      blit_render_target_release(&m->ui_target);
      blit_surface_release(&m->lock_sprite);
      blit_surface_release(&m->level_complete);
      blit_surface_release(&m->level_select_bg);
      blit_surface_release(&m->end_credit_bg);
      blit_surface_release(&m->game_bg);
   }




   /* The menu surfaces are raw structs: nothing zeroes them for us, and
    * nothing releases them either. */
static void manager_init_menu_surfaces(icy_manager_t *m)
   {
      /* Plain C members in a C++ class: nothing initialises them. */
      icy_level_list_init(&m->chapters);
      icy_save_clear(&m->save);
      m->m_failed   = 0;
      m->m_error[0] = '\0';
      m->dir[0] = '\0';
      m->font = NULL;
      blit_render_target_init(&m->target);
      blit_render_target_init(&m->ui_target);
      blit_surface_init(&m->lock_sprite);
      blit_surface_init(&m->level_complete);
      blit_surface_init(&m->level_select_bg);
      blit_surface_init(&m->end_credit_bg);
      blit_surface_init(&m->game_bg);
   }




static void manager_init_menu_sprite(icy_manager_t *m, rxml_node_t *game_node)
   {
      {
         blit_surface_t tmp = cache_image(asset(m->dir, blit_xml_attr(blit_xml_child(game_node, "level_complete"), "source")));
         blit_surface_assign(&m->level_complete, &tmp);
         blit_surface_release(&tmp);
      }

      {
         blit_surface_t tmp = cache_image(asset(m->dir, blit_xml_attr(blit_xml_child(game_node, "lock_sprite"), "source")));
         blit_surface_assign(&m->lock_sprite, &tmp);
         blit_surface_release(&tmp);
      }
      m->lock_sprite.ignore_camera = 1;
      int arrow_x = (ICY_GAME_FB_WIDTH - m->lock_sprite.rect.w) / 2;
      m->lock_sprite.rect.pos = blit_pos( arrow_x, 160 );

      int complete_x = PREVIEW_BASE_X + ICY_GAME_FB_WIDTH / 2 - m->level_complete.rect.w - 2;
      int complete_y = PREVIEW_BASE_Y + ICY_GAME_FB_HEIGHT / 2 - m->level_complete.rect.h - 2;
      m->level_complete.rect.pos = blit_pos( complete_x, complete_y );
      m->level_complete.ignore_camera = 1;

      {
         blit_surface_t tmp = cache_image(asset(m->dir, blit_xml_attr(blit_xml_child(game_node, "menu_bg"), "source")));
         blit_surface_assign(&m->level_select_bg, &tmp);
         blit_surface_release(&tmp);
      }
      m->level_select_bg.ignore_camera = 1;

      {
         blit_surface_t tmp = cache_image(asset(m->dir, blit_xml_attr(blit_xml_child(game_node, "end_bg"), "source")));
         blit_surface_assign(&m->end_credit_bg, &tmp);
         blit_surface_release(&tmp);
      }
      m->end_credit_bg.ignore_camera = 1;

      {
         blit_surface_t tmp = cache_image(asset(m->dir, blit_xml_attr(blit_xml_child(game_node, "game_bg"), "source")));
         blit_surface_assign(&m->game_bg, &tmp);
         blit_surface_release(&tmp);
      }
      m->game_bg.ignore_camera = 1;
   }


static void manager_init_bg(icy_manager_t *m, rxml_node_t *game_node)
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

         icy_path_join(paths[count], sizeof(paths[count]), m->dir, source);
         path_ptrs[count] = paths[count];
         gains[count]     = *volume ? (float)strtod(volume, NULL)
            : 1.0f;
         count++;
      }

      if (!icy_bgm_set_tracks(icy_bgm(), count ? path_ptrs : NULL,
               count ? gains : NULL, count))
         manager_fail(m, "Out of memory loading music.");
   }


static void manager_init_sfx(icy_manager_t *m, rxml_node_t *game_node)
   {
      rxml_node_t *sfx = blit_xml_child(game_node, "sfx");
      rxml_node_t *node;

      for (node = blit_xml_child(sfx, "sound"); node;
            node = blit_xml_next(node, "sound"))
         if (!icy_sfx_add(icy_sfx(), blit_xml_attr(node, "name"),
                  asset(m->dir, blit_xml_attr(node, "source"))))
         {
            char msg[512];
            snprintf(msg, sizeof(msg), "Failed to load sound: %s",
                  blit_xml_attr(node, "source"));
            manager_fail(m, msg);
            return;
         }
   }


   static int make_preview(const char *path, const blit_surface_t *bg,
      blit_surface_t *out, char *error, size_t error_len);

static void manager_load_chapter(icy_manager_t *m, rxml_node_t *chap, int chapter)
   {
      icy_chapter_t *loaded = icy_level_list_add_chapter(&m->chapters,
            blit_xml_attr(chap, "name"));
      rxml_node_t   *node;
      int            i = 0;

      if (!loaded)
      {
         manager_fail(m, "Out of memory loading chapter.");
         return;
      }

      for (node = blit_xml_child(chap, "map"); node;
            node = blit_xml_next(node, "map"), i++)
      {
         const char    *path    = asset(m->dir, blit_xml_attr(node, "source"));
         blit_surface_t preview;
         char           err[256];

         if (!make_preview(path, &m->game_bg, &preview, err, sizeof(err)))
         {
            manager_fail(m, err);
            return;
         }

         icy_level_t   *level   = icy_chapter_add_level(loaded,
               path, blit_xml_attr(node, "name"), &preview);

         blit_surface_release(&preview);

         if (!level)
         {
            manager_fail(m, "Out of memory loading level.");
            return;
         }

         level->position = blit_pos(PREVIEW_BASE_X + i * PREVIEW_DELTA_X,
               PREVIEW_BASE_Y + PREVIEW_DELTA_Y * chapter);
      }

      loaded->minimum_clear = blit_xml_attr_int(chap, "minimum_clear");

      /* A chapter with no levels is not one. */
      if (!loaded->count)
      {
         free(loaded->name);
         m->chapters.count--;
      }
   }


static void manager_init_menu(icy_manager_t *m, const char *level)
   {
      blit_surface_t surf = cache_image(asset(m->dir, level));

      blit_render_target_release(&m->target);
      if (!blit_render_target_init_size(&m->target, ICY_GAME_FB_WIDTH,
               ICY_GAME_FB_HEIGHT))
      {
         manager_fail(m, "Out of memory.");
         return;
      }
      blit_render_target_blit(&m->target, &surf, blit_rect_zero());
      blit_surface_release(&surf);

      blit_font_cluster_set_id(m->font, "yellow");
      blit_font_cluster_render(m->font, &m->target, "Press OK/Push button",
            160, 170, BLIT_FONT_CENTERED, 0);
   }


static void manager_reset_level(icy_manager_t *m)
   {
      manager_change_level(m, m->m_current_chap, m->m_current_level);
   }


static void manager_change_level(icy_manager_t *m, unsigned chapter, unsigned level)
   {
      {
         char err[256];

         /* The unique_ptr this replaced freed the outgoing level; a
          * plain assignment does not. */
         icy_game_free(m->game);
         m->game = (icy_game_new(
                  m->chapters.chapters[chapter].levels[level].path,
                  chapter, level,
                  m->chapters.chapters[chapter].levels[level].best_pushes,
                  m->font, err, sizeof(err)));

         if (!m->game)
         {
            manager_fail(m, err);
            return;
         }
      }
      icy_game_set_input_cb(m->game, m->m_input_cb, m->m_input_ctx);
      icy_game_set_video_cb(m->game, m->m_video_cb, m->m_video_ctx);
      icy_game_set_bg(m->game, &m->game_bg);

      m->m_current_chap  = chapter;
      m->m_current_level = level;
   }


static void manager_init_level(icy_manager_t *m, unsigned chapter, unsigned level)
   {
      manager_change_level(m, chapter, level);
      m->m_game_state = ICY_STATE_GAME;
   }


static int manager_find_next_unsolved_level(icy_manager_t *m,
      unsigned *current_chap, unsigned *current_level)
   {
      if ((*current_chap) == m->chapters.count - 1
            && (*current_level) == m->chapters.chapters[m->chapters.count - 1].count - 1)
         return 0;

      unsigned chap = (*current_chap);
      unsigned level = (*current_level);
      while (chap < m->chapters.count)
      {
         if (!m->chapters.chapters[chap].levels[level].completed)
         {
            (*current_chap) = chap;
            (*current_level) = level;
            return 1;
         }

         level++;
         if (level >= m->chapters.chapters[chap].count)
         {
            if (!icy_chapter_cleared(&m->chapters.chapters[chap]))
               break;

            chap++;
            level = 0;
         }
      }

      return 0;
   }


   /* Find first level that isn't completed yet, and */
   /* start menu there. */
static void manager_set_initial_level(icy_manager_t *m)
   {
      icy_save_load(&m->save, &m->chapters);
      m->m_current_chap = 0;
      m->m_current_level = 0;
      if (!manager_find_next_unsolved_level(m, &m->m_current_chap, &m->m_current_level))
      {
         m->chap_select = (int)m->chapters.count - 1;
         m->level_select = (int)m->chapters.chapters[m->chapters.count - 1].count - 1;
      }
   }


static void manager_step_title(icy_manager_t *m)
   {
      if (m->m_input_cb(m->m_input_ctx, ICY_INPUT_PUSH) || m->m_input_cb(m->m_input_ctx, ICY_INPUT_MENU))
      {
         manager_set_initial_level(m);
         manager_enter_menu(m);
      }

      m->m_video_cb(m->m_video_ctx, m->target.buffer, m->target.rect.w, m->target.rect.h, m->target.rect.w * sizeof(blit_pixel_t));
   }


static void manager_enter_menu(icy_manager_t *m)
   {
      icy_save_load(&m->save, &m->chapters);
      /* Entering the menu from a press: do not act on the same press
       * again on the first menu frame. */
      icy_edge_suppress(&m->edges, ICY_EDGE_OK);
      icy_edge_suppress(&m->edges, ICY_EDGE_MENU);

      m->m_game_state = ICY_STATE_MENU;
      m->level_select = m->m_current_level;
      m->chap_select  = m->m_current_chap;
      m->ui_target.rect.pos = blit_pos(PREVIEW_DELTA_X * m->level_select,
            PREVIEW_DELTA_Y * m->chap_select);
   }


static void manager_menu_render_ui(icy_manager_t *m)
   {
      char hud[64];

      if (m->menu_slide_dir.y == 0)
      {
         unsigned chap = m->chap_select;
         if (chap < m->chapters.count - 1
               && !icy_chapter_cleared(&m->chapters.chapters[m->chap_select]))
            blit_render_target_blit(&m->ui_target, &m->lock_sprite, blit_rect_zero());

         /* Render tick if level is complete. */
         if (m->menu_slide_dir.x == 0
               && m->chapters.chapters[m->chap_select].levels[m->level_select].completed)
            blit_render_target_blit(&m->ui_target, &m->level_complete, blit_rect_zero());

         blit_font_cluster_set_id(m->font, "white");
         snprintf(hud, sizeof(hud), "%d-%d", m->chap_select + 1,
               m->level_select + 1);
         blit_font_cluster_render(m->font, &m->ui_target, hud,
               240, 155, BLIT_FONT_RIGHT, 0);
      }

      blit_font_cluster_set_id(m->font, "lime");
      snprintf(hud, sizeof(hud), "%u/%u", manager_total_cleared_levels(m),
            manager_total_levels(m));
      blit_font_cluster_render(m->font, &m->ui_target, hud,
            10, 185, BLIT_FONT_LEFT, 0);

      snprintf(hud, sizeof(hud), "%u%%",
            100 * manager_total_cleared_levels(m) / manager_total_levels(m));
      blit_font_cluster_render(m->font, &m->ui_target, hud,
            315, 185, BLIT_FONT_RIGHT, 0);
   }


static void manager_step_menu_slide(icy_manager_t *m)
   {
      m->ui_target.rect.pos = blit_pos_add(m->ui_target.rect.pos,
            icy_menu_slide_step(&m->slide));

      if (icy_menu_slide_done(&m->slide))
      {
         m->m_game_state = ICY_STATE_MENU;
         m->menu_slide_dir = blit_pos_zero();
      }

      blit_render_target_blit(&m->ui_target, &m->level_select_bg, blit_rect_zero());

      manager_render_previews(m);

      manager_menu_render_ui(m);

      m->m_video_cb(m->m_video_ctx, m->ui_target.buffer, m->ui_target.rect.w, m->ui_target.rect.h, m->ui_target.rect.w * sizeof(blit_pixel_t));
   }


   /* Every level's preview, at the position the level was given when the
    * chapter loaded. */
static void manager_render_previews(icy_manager_t *m)
   {
      size_t c;
      size_t l;

      for (c = 0; c < m->chapters.count; c++)
         for (l = 0; l < m->chapters.chapters[c].count; l++)
         {
            icy_level_t *level = &m->chapters.chapters[c].levels[l];

            blit_render_target_blit_offset(&m->ui_target, &level->preview,
                  blit_rect_zero(), level->position);
         }
   }


static const icy_level_t *manager_get_selected_level(icy_manager_t *m)
   {
      return &m->chapters.chapters[m->chap_select].levels[m->level_select];
   }

static void manager_start_slide(icy_manager_t *m, blit_pos_t dir, unsigned cnt)
   {
      m->m_game_state = ICY_STATE_MENU_SLIDE;

      /* cnt is the duration in 60 Hz frames; dir * cnt is the distance it
       * used to cover one tick at a time. Keep the distance, convert the
       * duration. */
      icy_menu_slide_start(&m->slide, blit_pos_scale((int)cnt, dir),
            icy_frames_to_ticks(cnt));

      m->menu_slide_dir = dir;

      icy_sfx_play(icy_sfx(), "level_next", 0.5f);
   }


static void manager_step_menu(icy_manager_t *m)
   {
      blit_render_target_blit(&m->ui_target, &m->level_select_bg, blit_rect_zero());

      manager_render_previews(m);

      manager_menu_render_ui(m);

      /* Edges, not levels: a held direction moves the cursor once. */
      int pressed_menu_left  = icy_edge_pressed(&m->edges, ICY_EDGE_LEFT,
            m->m_input_cb(m->m_input_ctx, ICY_INPUT_LEFT));
      int pressed_menu_right = icy_edge_pressed(&m->edges, ICY_EDGE_RIGHT,
            m->m_input_cb(m->m_input_ctx, ICY_INPUT_RIGHT));
      int pressed_menu_up    = icy_edge_pressed(&m->edges, ICY_EDGE_UP,
            m->m_input_cb(m->m_input_ctx, ICY_INPUT_UP));
      int pressed_menu_down  = icy_edge_pressed(&m->edges, ICY_EDGE_DOWN,
            m->m_input_cb(m->m_input_ctx, ICY_INPUT_DOWN));
      int pressed_menu_ok    = icy_edge_pressed(&m->edges, ICY_EDGE_OK,
            m->m_input_cb(m->m_input_ctx, ICY_INPUT_PUSH));
      int pressed_menu       = icy_edge_pressed(&m->edges, ICY_EDGE_MENU,
            m->m_input_cb(m->m_input_ctx, ICY_INPUT_MENU));

      /* Navigation is index arithmetic over the chapter list, so it
       * lives in icy_menu_select.c; this turns a press into a direction
       * and the move it reports into a slide. */
      {
         icy_menu_chapters_t list;
         icy_menu_cursor_t   cursor;
         enum icy_menu_dir   dir;
         int                pressed = 1;

         list.chapters = (unsigned)m->chapters.count;
         list.levels   = menu_levels_cb;
         list.cleared  = menu_cleared_cb;
         list.ctx      = m;

         cursor.chapter = (unsigned)m->chap_select;
         cursor.level   = (unsigned)m->level_select;

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
            pressed = 0;
            dir     = ICY_MENU_LEFT;
         }

         if (pressed)
         {
            icy_menu_move_t move = icy_menu_move(&cursor, dir, &list);

            if (move.locked)
               icy_sfx_play(icy_sfx(), "chapter_locked", 0.5f);
            else if (move.moved)
            {
               m->chap_select  = (int)cursor.chapter;
               m->level_select = (int)cursor.level;
               manager_start_slide(m, blit_pos(move.level_delta * 8,
                        move.chapter_delta * 8), PREVIEW_SLIDE_CNT);
            }
         }
      }

      if (pressed_menu_ok)
         manager_init_level(m, m->chap_select, m->level_select);
      else if (pressed_menu && m->game)
         m->m_game_state = ICY_STATE_GAME;

      m->m_video_cb(m->m_video_ctx, m->ui_target.buffer, m->ui_target.rect.w, m->ui_target.rect.h, m->ui_target.rect.w * sizeof(blit_pixel_t));
   }


static void manager_step_game(icy_manager_t *m)
   {
      if (!m->game)
         return;

      if (!icy_game_iterate(m->game))
      {
         manager_fail(m, icy_game_error(m->game));
         return;
      }

      int pressed_menu = m->m_input_cb(m->m_input_ctx, ICY_INPUT_MENU);
      int pressed_reset = m->m_input_cb(m->m_input_ctx, ICY_INPUT_RESET);

      if (icy_edge_pressed(&m->edges, ICY_EDGE_RESET, pressed_reset))
         manager_reset_level(m);
      else if (icy_edge_pressed(&m->edges, ICY_EDGE_MENU, pressed_menu))
         manager_enter_menu(m);

      if (icy_game_won(m->game))
      {
         unsigned pushes = icy_game_pushes(m->game);
         int trigger_completion;

         icy_level_record_clear(
               &m->chapters.chapters[m->m_current_chap]
                  .levels[m->m_current_level], pushes);

         /* The level is finished with; game.reset() freed it here. */
         icy_game_free(m->game);
         m->game = NULL;

         trigger_completion = !m->chapters.chapters[m->m_current_chap].levels[m->m_current_level].completed;
         m->chapters.chapters[m->m_current_chap].levels[m->m_current_level].completed = 1;
         icy_save_store(&m->save, &m->chapters);

         /* Go to ending screen on the event that all levels have been cleared. */
         int cleared_all = trigger_completion;
         if (trigger_completion)
         {
            cleared_all = icy_level_list_cleared(&m->chapters)
               == icy_level_list_total(&m->chapters);
         }

         if (cleared_all)
         {
            m->m_game_state = ICY_STATE_END;
            /* Don't exit the credits on the press that opened them. */
            icy_edge_suppress(&m->edges, ICY_EDGE_OK);
         }
         else
         {
            if (manager_find_next_unsolved_level(m, &m->m_current_chap, &m->m_current_level))
               manager_change_level(m, m->m_current_chap, m->m_current_level);
            else
               manager_enter_menu(m);
         }
      }
   }


static void manager_step_end(icy_manager_t *m)
   {
      blit_render_target_blit(&m->ui_target, &m->end_credit_bg, blit_rect_zero());

      int trigger_ok   = icy_edge_pressed(&m->edges, ICY_EDGE_OK,
            m->m_input_cb(m->m_input_ctx, ICY_INPUT_PUSH));
      int trigger_menu = icy_edge_pressed(&m->edges, ICY_EDGE_MENU,
            m->m_input_cb(m->m_input_ctx, ICY_INPUT_MENU));

      if (trigger_ok || trigger_menu)
         manager_enter_menu(m);

      blit_font_cluster_set_id(m->font, "white");
      blit_font_cluster_render(m->font, &m->ui_target,
            "You completed all levels!\nAwesome! :D\nThanks for playing Dinothawr!",
            160, 155, BLIT_FONT_CENTERED, 2);
      m->m_video_cb(m->m_video_ctx, m->ui_target.buffer, m->ui_target.rect.w, m->ui_target.rect.h, m->ui_target.rect.w * sizeof(blit_pixel_t));
   }


static int manager_iterate(icy_manager_t *m)
   {
      switch (m->m_game_state)
      {
         case ICY_STATE_TITLE: manager_step_title(m); break;
         case ICY_STATE_MENU: manager_step_menu(m); break;
         case ICY_STATE_MENU_SLIDE: manager_step_menu_slide(m); break;
         case ICY_STATE_GAME: manager_step_game(m); break;
         case ICY_STATE_END: manager_step_end(m); break;
         default: manager_fail(m, "Game state is invalid."); break;
      }

      return !m->m_failed;
   }


static int manager_done(icy_manager_t *m)
   {
      return 0;
   }


static unsigned manager_total_levels(icy_manager_t *m)
   {
      return icy_level_list_total(&m->chapters);
   }


static unsigned manager_total_cleared_levels(icy_manager_t *m)
   {
      return icy_level_list_cleared(&m->chapters);
   }


   /* Renders one frame of a level at half size, which is what the menu
    * shows for it. */
   static int make_preview(const char *path, const blit_surface_t *bg,
      blit_surface_t *out, char *error, size_t error_len)
   {
      blit_surface_t preview;

      blit_surface_init(&preview);
      *out = preview;

      /* No font: a preview draws the level once and never a HUD. */
      char        err[256];
      icy_game_t *preview_game = icy_game_new(path, 0, 0, 0, NULL,
            error, error_len);

      if (!preview_game)
         return 0;

      icy_game_set_bg(preview_game, bg);

      static const unsigned scale_factor = 2;
      int preview_width  = ICY_GAME_FB_WIDTH / scale_factor;
      int preview_height = ICY_GAME_FB_HEIGHT / scale_factor;

      /* The destination is the surface's own buffer, filled directly by
       * the video callback - there is no reason for a vector and a copy
       * out of it. */
      blit_pixel_t *owned = (blit_pixel_t*)calloc(
            (size_t)preview_width * preview_height, sizeof(*owned));

      /* The downscale needs the destination and its pitch, so it goes
       * through the context pointer. */
      struct preview_ctx ctx;

      ctx.data  = owned;
      ctx.width = preview_width;

      if (!owned)
      {
         icy_game_free(preview_game);
         snprintf(error, error_len, "Out of memory building preview.");
         return 0;
      }

      icy_game_set_input_cb(preview_game, preview_input_cb, NULL);
      icy_game_set_video_cb(preview_game, preview_video_cb, &ctx);

      if (!icy_game_iterate(preview_game))
      {
         snprintf(error, error_len, "%s", icy_game_error(preview_game));
         icy_game_free(preview_game);
         free(owned);
         return 0;
      }

      icy_game_free(preview_game);

      {
         /* The surface takes its own reference; drop the one made here. */
         blit_surface_data_t *pdata = blit_surface_data_new(owned,
               preview_width, preview_height);

         if (!pdata)
         {
            free(owned);
            snprintf(error, error_len, "Out of memory building preview.");
            return 0;
         }

         blit_surface_init_data(&preview, pdata);
         blit_surface_data_unref(pdata);
      }

      *out = preview;
      return 1;
   }

/* ---- Public interface ------------------------------------------------ */

icy_manager_t *icy_manager_new(const char *path, icy_input_fn input_cb,
      void *input_ctx, icy_video_fn video_cb, void *video_ctx,
      char *error, size_t error_len)
{
   icy_manager_t *m = (icy_manager_t*)calloc(1, sizeof(*m));

   if (!m)
      return NULL;

   icy_edge_init(&m->edges);
   icy_menu_slide_init(&m->slide);
   manager_init_menu_surfaces(m);

   m->m_input_cb  = input_cb;
   m->m_input_ctx = input_ctx;
   m->m_video_cb  = video_cb;
   m->m_video_ctx = video_ctx;
   m->m_game_state = ICY_STATE_TITLE;
   m->menu_slide_dir = blit_pos_zero();

   icy_path_dir(m->dir, sizeof(m->dir), path);

   if (!manager_load(m, path))
   {
      if (error)
         snprintf(error, error_len, "%s", m->m_error);
      icy_manager_free(m);
      return NULL;
   }

   return m;
}

void icy_manager_free(icy_manager_t *m)
{
   if (!m)
      return;

   manager_release_owned(m);
   free(m);
}

int icy_manager_iterate(icy_manager_t *m)
{
   return manager_iterate(m);
}

const char *icy_manager_error(const icy_manager_t *m)
{
   return m ? m->m_error : "";
}

int icy_manager_done(const icy_manager_t *m)
{
   (void)m;
   return 0;
}

void *icy_manager_save_data(icy_manager_t *m)
{
   return m->save.data;
}

size_t icy_manager_save_size(const icy_manager_t *m)
{
   return sizeof(m->save.data);
}
