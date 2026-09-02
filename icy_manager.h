/* Dinothawr - the game as a whole.
 *
 * The state machine above a level: the title screen, the level-select
 * menu and its slide, the level in play, and the end credits. Owns the
 * chapter list, the save, the menu's artwork and the level running.
 *
 * Failures loading assets are recorded rather than reported where they
 * are noticed; icy_manager_new returns NULL with the reason, and
 * icy_manager_iterate returns zero when one happens during play.
 *
 * MSVC C89.
 */

#ifndef ICY_MANAGER_H__
#define ICY_MANAGER_H__

#include <stddef.h>

#include "icy_input.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct icy_manager icy_manager_t;

/* Loads the .game at @path. NULL on failure, with @error, if given,
 * receiving the reason. */
icy_manager_t *icy_manager_new(const char *path, icy_input_fn input_cb,
      void *input_ctx, icy_video_fn video_cb, void *video_ctx,
      char *error, size_t error_len);

void icy_manager_free(icy_manager_t *manager);

/* One frame of whatever the game is currently doing. Zero when the
 * level data turned out to be broken. */
int icy_manager_iterate(icy_manager_t *manager);

/* Why the last iterate returned zero. Never NULL. */
const char *icy_manager_error(const icy_manager_t *manager);

/* Non-zero when the game has asked to be shut down. */
int icy_manager_done(const icy_manager_t *manager);

/* The save the frontend hands around as SRAM. */
void  *icy_manager_save_data(icy_manager_t *manager);
size_t icy_manager_save_size(const icy_manager_t *manager);

#ifdef __cplusplus
}
#endif

#endif
