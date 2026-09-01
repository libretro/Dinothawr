/* Dinothawr - float audio mixer.
 *
 * This is the path used when the frontend advertises float audio output
 * (RETRO_ENVIRONMENT_GET_AUDIO_SAMPLE_BATCH_FLOAT). Everything is
 * interleaved stereo float end-to-end, so the samples that come out of
 * the decoders go to the frontend without an int16 round trip.
 *
 * The structure deliberately mirrors mixer_i16.h: same stream vtable
 * shape, same fire-and-forget SFX list beside a dedicated music slot,
 * same ownership rules. The two differ only in sample type and in how
 * gain is expressed - float here, Q15 there.
 *
 * Written in MSVC C89: C comments only, declarations at the top of each
 * block, no C99/C11 features, no VLAs, no designated initialisers. The
 * header is C++-includable via the extern "C" guard.
 *
 * Threading: the mixer does no locking. Audio is rendered from retro_run
 * on the same thread that adds and clears streams, and the decode jobs
 * only ever hand back a finished buffer for that thread to install.
 */

#ifndef MIXER_F32_H__
#define MIXER_F32_H__

#include <stddef.h>
#include <stdint.h>

#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIXER_F32_CHANNELS 2

typedef struct f32_stream f32_stream_t;
typedef struct mixer_f32  mixer_f32_t;

/* Per-stream vtable. render() writes 'frames' interleaved stereo float
 * frames into 'out' and returns the number of frames actually produced
 * (< frames at the end of a non-looping stream). valid() returns
 * non-zero while the stream still has audio to give. rewind() restarts
 * it. destroy() releases it, and is called by the mixer when the stream
 * is purged or the mixer is cleared or freed. */
typedef size_t (*f32_render_fn) (f32_stream_t *s, float *out, size_t frames);
typedef int    (*f32_valid_fn)  (const f32_stream_t *s);
typedef void   (*f32_rewind_fn) (f32_stream_t *s);
typedef void   (*f32_destroy_fn)(f32_stream_t *s);

struct f32_stream
{
   f32_render_fn  render;
   f32_valid_fn   valid;
   f32_rewind_fn  rewind;
   f32_destroy_fn destroy;
   float          volume;  /* per-stream gain, 1.0 == unity */
   int            loop;    /* non-zero: wrap at end */
};

/* ---- Shared PCM buffer (reference counted) ------------------------- *
 * Interleaved stereo float sample data, shared between every stream
 * that plays the same clip (e.g. one decoded SFX triggered repeatedly).
 * f32_buf_new() takes ownership of 'data' (freed with free() on the last
 * unref). 'samples' is the total float count, i.e. frames * 2.         */
typedef struct f32_buf f32_buf_t;

f32_buf_t *f32_buf_new(float *data, size_t samples);
f32_buf_t *f32_buf_ref(f32_buf_t *buf);
void       f32_buf_unref(f32_buf_t *buf);

/* PCM stream over a shared buffer. Takes its own ref on 'buf'. */
f32_stream_t *f32_pcm_stream_new(f32_buf_t *buf, int loop, float volume);

/* ---- Mixer --------------------------------------------------------- */
mixer_f32_t *mixer_f32_new(void);
void mixer_f32_free(mixer_f32_t *mixer);

/* Append a fire-and-forget stream (e.g. a sound effect); the mixer takes
 * ownership and destroy()s it once it goes invalid. */
void mixer_f32_add(mixer_f32_t *mixer, f32_stream_t *stream);

/* Dedicated music slot. set_music() replaces (and destroy()s) any
 * current music stream and takes ownership of the new one (NULL just
 * clears it). music_active() reports whether a music stream is present
 * and still playing, which is how the caller drives a playlist without
 * holding a pointer into the mixer. */
void mixer_f32_set_music(mixer_f32_t *mixer, f32_stream_t *stream);
int  mixer_f32_music_active(const mixer_f32_t *mixer);

/* Destroy all streams (SFX and music). */
void mixer_f32_clear(mixer_f32_t *mixer);

/* Master gain. Defaults to unity. Negative values clamp to silence. */
void  mixer_f32_set_master(mixer_f32_t *mixer, float gain);
float mixer_f32_master(const mixer_f32_t *mixer);

/* Enable flag: SFX are only queued while enabled. Defaults to disabled
 * until a game is loaded. */
void mixer_f32_set_enabled(mixer_f32_t *mixer, int enabled);
int  mixer_f32_enabled(const mixer_f32_t *mixer);

/* Render 'frames' interleaved stereo float frames into 'out'. Dead
 * streams are purged first; the rest are summed with their
 * (master * per-stream) gain applied. 'out' is fully written (silence
 * where no stream contributes). */
void mixer_f32_render(mixer_f32_t *mixer, float *out, size_t frames);

#ifdef __cplusplus
}
#endif

#endif
