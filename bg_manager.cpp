#include "game.hpp"
#ifndef USE_CXX03
#include <stdlib.h>

#include <future>
#include <chrono>

#include "audio/mixer_i16.h"
#include "audio/vorbis_i16.h"

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

      /* Reset the int16 path: drain any decode still in flight from a
       * previous game (discarding its buffer) and clear the music slot.
       * The float path resets via its own mixer reassignment. */
      if (!audio_is_float())
      {
         if (i16_future.valid())
         {
            i16_buf_t *buf = i16_future.get();
            if (buf)
               i16_buf_unref(buf);
         }
         mixer_i16_set_music(get_mixer_i16(), NULL);
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

   void BGManager::step(Audio::Mixer& mixer)
   {
      if (!audio_is_float())
      {
         /* int16 pipeline: drive the mixer's music slot, decoding the next
          * track off-thread (like the float loader) so a track change does
          * not stall the game. While the slot is empty we keep exactly one
          * decode in flight and install it once ready. */
         mixer_i16_t *m = get_mixer_i16();

         if (mixer_i16_music_active(m))
            return;
         if (!tracks.size())
            return;

         if (!i16_future.valid())
         {
            std::string path = tracks[next_index()].path;
            i16_future = std::async(std::launch::async,
                  [path] { return vorbis_i16_decode_file(path.c_str()); });
         }

         if (i16_future.wait_for(std::chrono::seconds(0))
               == std::future_status::ready)
         {
            i16_buf_t *buf = i16_future.get(); /* clears the future */

            if (buf)
            {
               i16_stream_t *stream = i16_pcm_stream_new(buf, 0,
                     mixer_i16_q15_from_float(tracks[last].gain));
               i16_buf_unref(buf); /* stream holds its own reference */
               mixer_i16_set_music(m, stream);
            }
         }
         return;
      }

      lock_guard<Audio::Mixer> guard(mixer);

      if (current && current->valid())
         return;

      if (!tracks.size())
         return;

      /* The float path used to inline a second copy of next_index(); the
       * two are the same choice and drifting apart would be a bug. */
      if (!loader.size())
         loader.request_vorbis(tracks[next_index()].path);

      std::shared_ptr<std::vector<float> > ret = loader.flush();

      if (ret)
      {
         current = make_shared<Audio::PCMStream>(ret);
         current->volume(tracks[last].gain);
      }
      else
         current.reset();

      if (current)
         mixer.add_stream(current);
   }
}
#endif
