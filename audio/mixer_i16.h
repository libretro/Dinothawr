/* Dinothawr - deterministic int16 audio mixer.
 *
 * This is the integer fallback used when the frontend does not advertise
 * float audio output (RETRO_ENVIRONMENT_GET_AUDIO_SAMPLE_BATCH_FLOAT).
 * Everything here is interleaved stereo int16 end-to-end: no float, no
 * float<->int16 round-trips, deterministic across platforms.
 *
 * Written in MSVC C89 (the target dialect for the eventual full port of
 * this codebase): C comments only, declarations at the top of each block,
 * no C99/C11 features, no VLAs, no designated initialisers. The header is
 * C++-includable via the extern "C" guard.
 *
 * Threading: the mixer does no locking of its own. Callers that add or
 * clear streams from a thread other than the one calling
 * mixer_i16_render() must serialise those calls themselves (the existing
 * Mixer C++ wrapper already holds a recursive_mutex across add/clear/
 * render, so that contract is preserved when this is slotted in behind it).
 */

#ifndef MIXER_I16_H__
#define MIXER_I16_H__

#include <stddef.h>
#include <stdint.h>

#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIXER_I16_CHANNELS 2

/* Q15 fixed-point unity gain (1.0 == 32768). Volumes are expressed in
 * this format so the mix hot path stays pure-integer; the one float->Q15
 * conversion happens once, when a gain from the level XML is applied. */
#define MIXER_I16_UNITY_Q15 32768

typedef struct i16_stream i16_stream_t;
typedef struct mixer_i16  mixer_i16_t;

/* Per-stream vtable. render() writes 'frames' interleaved stereo int16
 * frames into 'out' and returns the number of frames actually produced
 * (< frames at end of a non-looping stream). valid() returns non-zero
 * while the stream still has audio to give. rewind() restarts it.
 * destroy() releases it (and is called by the mixer when the stream is
 * purged or the mixer is cleared/freed). */
typedef size_t (*i16_render_fn) (i16_stream_t *s, int16_t *out, size_t frames);
typedef int    (*i16_valid_fn)  (const i16_stream_t *s);
typedef void   (*i16_rewind_fn) (i16_stream_t *s);
typedef void   (*i16_destroy_fn)(i16_stream_t *s);

struct i16_stream
{
   i16_render_fn  render;
   i16_valid_fn   valid;
   i16_rewind_fn  rewind;
   i16_destroy_fn destroy;
   int32_t        volume_q15; /* per-stream gain, Q15 */
   int            loop;       /* non-zero: wrap at end */
};

/* Convert a floating-point gain (as stored in the level data) to Q15.
 * Rounds to nearest; clamps negatives to 0. Kept here so callers never
 * open-code the conversion. */
int32_t mixer_i16_q15_from_float(float gain);

/* ---- Shared PCM buffer (reference counted) ------------------------- *
 * Interleaved stereo int16 sample data, shared between every PCMStream
 * that plays the same clip (e.g. one decoded SFX triggered repeatedly).
 * i16_buf_new() takes ownership of 'data' (freed with free() on the last
 * unref). 'samples' is the total int16 count, i.e. frames * 2.          */
typedef struct i16_buf i16_buf_t;

i16_buf_t *i16_buf_new(int16_t *data, size_t samples);
i16_buf_t *i16_buf_ref(i16_buf_t *buf);
void       i16_buf_unref(i16_buf_t *buf);

/* PCM stream over a shared buffer. Takes its own ref on 'buf'. */
i16_stream_t *i16_pcm_stream_new(i16_buf_t *buf, int loop, int32_t volume_q15);

/* ---- WAV loader ---------------------------------------------------- *
 * Loads a 16-bit PCM WAV (mono or stereo, 44100 Hz) as interleaved
 * stereo int16. Mono input is duplicated to both channels. On success
 * returns a malloc'd buffer and writes the int16 count to *out_samples;
 * the caller owns the buffer (free() it, or hand it to i16_buf_new()).
 * Returns NULL on any error.                                            */
int16_t *wav_i16_load(const char *path, size_t *out_samples);

/* ---- Mixer --------------------------------------------------------- */
mixer_i16_t *mixer_i16_new(void);
void mixer_i16_free(mixer_i16_t *mixer);

/* Append a fire-and-forget stream (e.g. a sound effect); the mixer takes
 * ownership and destroy()s it once it goes invalid. */
void mixer_i16_add(mixer_i16_t *mixer, i16_stream_t *stream);

/* Dedicated music slot. set_music() replaces (and destroy()s) any current
 * music stream and takes ownership of the new one (NULL just clears it).
 * music_active() reports whether a music stream is present and still
 * playing. Keeping music in its own slot lets the caller drive a playlist
 * ("load the next track once music_active() is false") without holding a
 * pointer into the mixer. */
void mixer_i16_set_music(mixer_i16_t *mixer, i16_stream_t *stream);
int  mixer_i16_music_active(const mixer_i16_t *mixer);

/* Destroy all streams (SFX and music). */
void mixer_i16_clear(mixer_i16_t *mixer);

/* Master gain, Q15. Defaults to unity. */
void mixer_i16_set_master_q15(mixer_i16_t *mixer, int32_t q15);

/* Enable flag, mirroring the float mixer: SFX are only queued while
 * enabled. Defaults to disabled until the audio callback turns it on. */
void mixer_i16_set_enabled(mixer_i16_t *mixer, int enabled);
int  mixer_i16_enabled(const mixer_i16_t *mixer);

/* Optional serialisation hooks. When the libretro audio callback runs on
 * its own thread, the main thread may add SFX / swap the music track
 * while the audio thread is rendering. Set these (e.g. backed by a mutex)
 * so the mixer guards stream mutation and render against each other, the
 * same way the float mixer does internally. NULL hooks mean no locking
 * (single-threaded use). */
typedef void (*i16_lock_fn)(void *data);
void mixer_i16_set_lock(mixer_i16_t *mixer, i16_lock_fn lock,
      i16_lock_fn unlock, void *data);

/* Render 'frames' interleaved stereo int16 frames into 'out'. Dead
 * streams are purged first; remaining streams are summed with their
 * (master * per-stream) Q15 gain applied, accumulated in int32 and
 * saturated to int16. 'out' is fully written (silence where no stream
 * contributes). */
void mixer_i16_render(mixer_i16_t *mixer, int16_t *out, size_t frames);

#ifdef __cplusplus
}
#endif

#endif
