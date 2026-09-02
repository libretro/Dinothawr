/* Dinothawr - the session surface cache.
 *
 * Every image the game draws is loaded once and shared: a tileset face
 * used by twelve levels is decoded on the first level that asks and
 * handed to the rest. The cache holds one reference on each decoded
 * image for the whole session and never evicts, which is what makes
 * that sharing safe - a surface built from a cached image can outlive
 * whatever asked for it first.
 *
 * There is one cache, reached through blit_surface_cache(). Loading
 * happens on the thread that runs retro_load_game and retro_run; the
 * audio decode jobs never touch it.
 *
 * Each accessor hands back a surface the caller owns and must release.
 * They report failure by returning zero rather than by throwing, and
 * write a message describing it - a missing or malformed asset is a
 * broken install rather than a bug, and the caller decides what to say.
 *
 * MSVC C89.
 */

#ifndef BLIT_SURFACE_CACHE_H__
#define BLIT_SURFACE_CACHE_H__

#include "blit_surface.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct blit_surface_cache blit_surface_cache_t;

/* The session cache, created on first use. NULL only if that creation
 * failed. */
blit_surface_cache_t *blit_surface_cache(void);

/* Releases the cache and everything in it. Called at core teardown. */
void blit_surface_cache_free(void);

/* A whole PNG. Non-zero on success. */
int blit_surface_cache_image(blit_surface_cache_t *cache,
      const char *path, blit_surface_t *out);

/* One frame of an APNG. The whole animation is decoded and cached on
 * the first request for any of its frames, so a file with twelve faces
 * is read once. Non-zero on success. */
int blit_surface_cache_animation(blit_surface_cache_t *cache,
      const char *path, unsigned frame, blit_surface_t *out);

/* A .sprite: an XML file naming faces, each an image or an APNG frame.
 * The parse is cached alongside the pixels - the faces were already
 * shared, but the document behind them used to be re-read on every
 * request. Non-zero on success. */
int blit_surface_cache_sprite(blit_surface_cache_t *cache,
      const char *path, blit_surface_t *out);

/* Why the last call on this cache returned zero. Never NULL. */
const char *blit_surface_cache_error(const blit_surface_cache_t *cache);

#ifdef __cplusplus
}
#endif

#endif
