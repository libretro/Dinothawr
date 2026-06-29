/* Dinothawr - Ogg Vorbis -> interleaved stereo int16 decode.
 *
 * Decodes an entire Ogg Vorbis file into a reference-counted int16 PCM
 * buffer (i16_buf_t) for the integer audio pipeline. Dinothawr fully
 * pre-decodes its (short) music tracks at load time, so a one-shot
 * whole-file decode matches how the float path already works.
 *
 * MSVC C89. The decode is isolated behind this single entry point: the
 * current implementation runs the (float) reference decoder once at load
 * and converts to int16, but the contract is "Ogg path in, int16 i16_buf
 * out". Swapping the guts to the integer-only Tremor decoder (ov_read)
 * for a fully deterministic, float-free decode is then a local change
 * here, with no caller impact.
 */

#ifndef VORBIS_I16_H__
#define VORBIS_I16_H__

#include "mixer_i16.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Required sample rate; mirrors the assertion in the float path. */
#define VORBIS_I16_SAMPLE_RATE 44100

/* Decode the whole file at 'path' to an interleaved stereo int16 buffer.
 * Mono sources are duplicated to both channels. Returns a new i16_buf_t
 * (refcount 1; release with i16_buf_unref) or NULL on any error
 * (open failure, wrong sample rate, unsupported channel count, OOM). */
i16_buf_t *vorbis_i16_decode_file(const char *path);

#ifdef __cplusplus
}
#endif

#endif
