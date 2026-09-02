/* Dinothawr - a level in play.
 *
 * One level: its map, the player, the blocks, and the tick that moves
 * them. Created for a level, stepped once per frame, and destroyed when
 * the level is left.
 *
 * Level data that cannot be right - a sprite missing a face the game
 * asks for, goal squares and blocks that do not match, a surface off the
 * tile grid - is recorded rather than reported where it is noticed, and
 * icy_game_iterate returns zero on the next tick with the reason in
 * icy_game_error. There is one place a broken level surfaces.
 *
 * MSVC C89.
 */

#ifndef ICY_GAME_H__
#define ICY_GAME_H__

#include <stddef.h>

#include "blit_font.h"
#include "blit_surface.h"
#include "icy_input.h"

#ifdef __cplusplus
extern "C" {
#endif

enum
{
   ICY_GAME_FB_WIDTH  = 320,
   ICY_GAME_FB_HEIGHT = 200
};

typedef struct icy_game icy_game_t;

/* Loads the level at @path. @font may be NULL, which is what the menu's
 * level previews pass - they render a level once and never show a HUD.
 * NULL on failure, with @error, if given, receiving the reason. */
icy_game_t *icy_game_new(const char *path, unsigned chapter,
      unsigned level, unsigned best_pushes, blit_font_cluster_t *font,
      char *error, size_t error_len);

void icy_game_free(icy_game_t *game);

void icy_game_set_input_cb(icy_game_t *game, icy_input_fn cb, void *ctx);
void icy_game_set_video_cb(icy_game_t *game, icy_video_fn cb, void *ctx);

/* Borrowed for the game's lifetime; the caller keeps ownership. */
void icy_game_set_bg(icy_game_t *game, const blit_surface_t *bg);

/* One tick: input, movement, the win check, and a frame to the video
 * callback. Zero when the level data turned out to be broken. */
int icy_game_iterate(icy_game_t *game);

/* Why the last iterate returned zero. Never NULL. */
const char *icy_game_error(const icy_game_t *game);

/* Non-zero once the win animation has run its course, or the player
 * cut it short. */
int icy_game_won(const icy_game_t *game);

unsigned icy_game_pushes(const icy_game_t *game);

#ifdef __cplusplus
}
#endif

#endif
