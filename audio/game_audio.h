/* Dinothawr - the game's two audio managers.
 *
 * SFX: named one-shot samples, decoded once at load and played into the
 * live mixer whenever the game asks. BG: a playlist of music tracks,
 * shuffled, one decoded off-thread while the previous one plays.
 *
 * Both work in whichever sample type the frontend negotiated - the float
 * mixer or the int16 one - and hold their decoded audio in that type.
 * Only one of the two is live for a given game.
 *
 * There is one of each, reached through the accessors below.
 *
 * MSVC C89.
 */

#ifndef GAME_AUDIO_H__
#define GAME_AUDIO_H__

#include <boolean.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Supplied by the frontend glue: which mixer the frontend negotiated,
 * and the live one. Declared here rather than in libretro.cpp so the
 * managers depend on an interface rather than on a translation unit. */
struct mixer_f32;
struct mixer_i16;
int               icy_audio_is_float(void);
struct mixer_f32 *icy_mixer_f32(void);
struct mixer_i16 *icy_mixer_i16(void);

typedef struct icy_sfx     icy_sfx_t;
typedef struct icy_bgm     icy_bgm_t;

icy_sfx_t *icy_sfx(void);
icy_bgm_t *icy_bgm(void);

/* Releases both, and everything they hold. Called at core teardown. */
void icy_game_audio_free(void);

/* Decodes @path and files it under @ident, replacing any previous
 * sample of that name. Non-zero on success. */
int icy_sfx_add(icy_sfx_t *sfx, const char *ident, const char *path);

/* Plays @ident at @volume, if the mixer is enabled and the sample
 * exists. A missing sample is silently ignored: sound effects are
 * triggered from gameplay, and a broken install should not take the
 * game down mid-move. */
void icy_sfx_play(const icy_sfx_t *sfx, const char *ident, float volume);

/* Replaces the playlist. Each call also drops whatever was playing and
 * any decode in flight. Non-zero on success. */
int icy_bgm_set_tracks(icy_bgm_t *bgm, const char *const *paths,
      const float *gains, size_t count);

/* Drops the current track and any decode in flight. Call before the
 * mixer it feeds goes away. */
void icy_bgm_stop(icy_bgm_t *bgm);

/* Called once a frame: starts the next track's decode when the music
 * slot is empty, and installs it when the decode finishes. */
void icy_bgm_step(icy_bgm_t *bgm);

#ifdef __cplusplus
}
#endif

#endif
