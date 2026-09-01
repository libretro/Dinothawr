/* Dinothawr - compressed audio -> interleaved stereo float decode.
 *
 * The float sibling of vorbis_i16.h, and the same contract: any file
 * audio_transfer recognises from its extension (WAV and Ogg Vorbis
 * here) goes in, interleaved stereo float comes out. Vorbis is float
 * internally, so this path never quantises.
 *
 * MSVC C89.
 */

#ifndef VORBIS_F32_H__
#define VORBIS_F32_H__

#include "mixer_f32.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Required sample rate; the decoders reject anything else. */
#define AUDIO_F32_SAMPLE_RATE 44100

/* Decode the whole file to an interleaved stereo float buffer. Mono
 * sources are duplicated to both channels. Returns a malloc'd buffer and
 * writes the float sample count to *out_samples, or NULL on any error
 * (open failure, wrong sample rate, unsupported channel count, OOM). */
float *audio_f32_decode_file(const char *path, size_t *out_samples);

/* As above, wrapped in a shared buffer (refcount 1; release with
 * f32_buf_unref). NULL on any error. */
f32_buf_t *vorbis_f32_decode_file(const char *path);

/* Loads a WAV as interleaved stereo float. Same entry point as the music
 * path - audio_transfer walks the chunk list and covers the compressed
 * WAV variants too. */
float *wav_f32_load(const char *path, size_t *out_samples);

#ifdef __cplusplus
}
#endif

#endif
