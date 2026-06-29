#include "game.hpp"
#ifndef USE_CXX03
#include <stdlib.h>

#include "audio/mixer_i16.h"
#include "audio/vorbis_i16.h"

using namespace std;

namespace Icy
{
   void BGManager::init(const vector<Track>& tracks)
   {
      this->tracks = tracks;
      srand(time(NULL));
      first = true;
      last = 0;

      /* Drop any music left over from a previous game on the int16 path
       * (the float path resets via its own mixer reassignment). */
      if (!audio_is_float())
         mixer_i16_set_music(get_mixer_i16(), NULL);
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

      index = rand() % tracks.size();
      if (index == last)
         index = (index + 1) % tracks.size();
      last = index;
      return index;
   }

   void BGManager::step(Audio::Mixer& mixer)
   {
      if (!audio_is_float())
      {
         /* int16 pipeline: drive the mixer's music slot. A track is
          * decoded to int16 and handed over; when it finishes the slot
          * empties and we load the next. Decode is synchronous here
          * (a brief track-change cost on the fallback path); moving it
          * off-thread to match the float loader is a later refinement. */
         mixer_i16_t *m = get_mixer_i16();

         if (mixer_i16_music_active(m))
            return;
         if (!tracks.size())
            return;

         {
            unsigned   index = next_index();
            i16_buf_t *buf   = vorbis_i16_decode_file(tracks[index].path.c_str());

            if (buf)
            {
               i16_stream_t *stream = i16_pcm_stream_new(buf, 0,
                     mixer_i16_q15_from_float(tracks[index].gain));
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

      if (!loader.size())
      {
         if (first)
         {
            loader.request_vorbis(tracks[0].path);
            last = 0;
         }
         else
         {
            unsigned index = rand() % tracks.size();
            if (index == last)
               index = (index + 1) % tracks.size();

            loader.request_vorbis(tracks[index].path);
            last = index;
         }

         first = false;
      }

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
