#include "game.hpp"
#ifndef USE_CXX03
#include <stdlib.h>

#include "audio/mixer_f32.h"
#include "audio/mixer_i16.h"
#include "audio/vorbis_f32.h"
#include "audio/vorbis_i16.h"
#include "audio/async_job.h"

using namespace std;

namespace Icy
{
   void BGManager::init(const vector<Track>& tracks)
   {
      this->tracks = tracks;

      /* Seeded from the track list, not from a clock.  A wall-clock seed
       * makes a session unreproducible for no gain the player can hear -
       * the order is arbitrary either way - and it is one of the two
       * reasons this core's audio output differs run to run.  Hashing
       * the paths keeps a different game shuffling differently while
       * making one game's order a function of its content.  Zero is the
       * one state xorshift cannot leave, so it is steered away from. */
      rng_state = 2166136261u;
      for (std::vector<Track>::const_iterator t = this->tracks.begin();
            t != this->tracks.end(); ++t)
      {
         const char *c;
         for (c = t->path.c_str(); *c; c++)
            rng_state = (rng_state ^ (uint32_t)(unsigned char)*c)
                      * 16777619u;
      }
      if (!rng_state)
         rng_state = 0x9e3779b9u;

      first = true;
      last = 0;

      /* Drain any decode still in flight from a previous game
       * (discarding its buffer) and clear the music slot. */
      deinit();
   }

   /* The buffer a decode in flight is producing is reachable only
    * through the job, so it has to be collected here or it is lost.
    * Collecting waits for the decode to finish, which is bounded by one
    * track and is what a reset already does. */
   void BGManager::deinit()
   {
      if (audio_is_float())
      {
         mixer_f32_t *m = get_mixer_f32();

         if (job)
         {
            f32_buf_unref((f32_buf_t*)async_job_collect(job));
            job = NULL;
         }
         if (m)
            mixer_f32_set_music(m, NULL);
         return;
      }

      {
         mixer_i16_t *m = get_mixer_i16();

         if (job)
         {
            i16_buf_unref((i16_buf_t*)async_job_collect(job));
            job = NULL;
         }
         if (m)
            mixer_i16_set_music(m, NULL);
      }
   }

   /* xorshift32: enough for picking one of a handful of tracks, and it
    * is ours, so nothing outside this object can perturb it or be
    * perturbed by it. */
   unsigned BGManager::rng_next(unsigned n)
   {
      rng_state ^= rng_state << 13;
      rng_state ^= rng_state >> 17;
      rng_state ^= rng_state << 5;

      /* Multiply-shift rather than a modulo: no division, and none of
       * the bias that taking a remainder of a power-of-two range by a
       * count that does not divide it introduces. */
      return (unsigned)(((uint64_t)rng_state * (uint64_t)n) >> 32);
   }

   /* Choose the next track index: the first track initially, then a
    * random track that differs from the previous one. */
   unsigned BGManager::next_index()
   {
      unsigned index;

      if (first)
      {
         first = false;
         last  = 0;
         return 0;
      }

      index = rng_next((unsigned)tracks.size());
      if (index == last)
         index = (index + 1) % tracks.size();
      last = index;
      return index;
   }

   /* Both run on the job's thread. Neither touches the mixer or any
    * BGManager state, only the path it was handed - the decoded buffer
    * travels back as the job's result and is installed by whoever
    * collects it. C linkage to match the job's function-pointer type. */
   extern "C" {
      static void *bg_decode_f32(void *userdata)
      {
         return vorbis_f32_decode_file((const char*)userdata);
      }

      static void *bg_decode_i16(void *userdata)
      {
         return vorbis_i16_decode_file((const char*)userdata);
      }
   }

   /* Drive the mixer's music slot, decoding the next track off-thread so
    * a track change does not stall the game. While the slot is empty we
    * keep exactly one decode in flight and install it once ready.
    *
    * The two sample types are the same sequence of calls; only the
    * decode entry point, the buffer type and the stream constructor
    * differ. */
   void BGManager::step()
   {
      if (audio_is_float())
      {
         mixer_f32_t *m = get_mixer_f32();

         if (!m || mixer_f32_music_active(m) || !tracks.size())
            return;

         if (!job)
         {
            /* job_path is a member so it outlives step(): the job reads
             * it on its own thread until it is collected. */
            job_path = tracks[next_index()].path;
            job      = async_job_start(bg_decode_f32, (void*)job_path.c_str());

            /* No thread available - decode inline rather than go silent.
             * A track change is the only place this can be reached and
             * one track is the whole cost. */
            if (!job)
            {
               f32_buf_t *buf = vorbis_f32_decode_file(job_path.c_str());
               if (buf)
               {
                  mixer_f32_set_music(m, f32_pcm_stream_new(buf, 0,
                           tracks[last].gain));
                  f32_buf_unref(buf); /* the stream holds its own ref */
               }
               return;
            }
         }

         if (async_job_ready(job))
         {
            f32_buf_t *buf = (f32_buf_t*)async_job_collect(job);
            job = NULL;

            if (buf)
            {
               mixer_f32_set_music(m, f32_pcm_stream_new(buf, 0,
                        tracks[last].gain));
               f32_buf_unref(buf);
            }
         }
         return;
      }

      {
         mixer_i16_t *m = get_mixer_i16();

         if (!m || mixer_i16_music_active(m) || !tracks.size())
            return;

         if (!job)
         {
            job_path = tracks[next_index()].path;
            job      = async_job_start(bg_decode_i16, (void*)job_path.c_str());

            if (!job)
            {
               i16_buf_t *buf = vorbis_i16_decode_file(job_path.c_str());
               if (buf)
               {
                  mixer_i16_set_music(m, i16_pcm_stream_new(buf, 0,
                           mixer_i16_q15_from_float(tracks[last].gain)));
                  i16_buf_unref(buf);
               }
               return;
            }
         }

         if (async_job_ready(job))
         {
            i16_buf_t *buf = (i16_buf_t*)async_job_collect(job);
            job = NULL;

            if (buf)
            {
               mixer_i16_set_music(m, i16_pcm_stream_new(buf, 0,
                        mixer_i16_q15_from_float(tracks[last].gain)));
               i16_buf_unref(buf);
            }
         }
      }
   }
}
#endif
