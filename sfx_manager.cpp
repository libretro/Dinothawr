#include "game.hpp"
#ifndef USE_CXX03
#include <string>
#include <cstdlib>

#include "audio/mixer_f32.h"
#include "audio/mixer_i16.h"
#include "audio/vorbis_f32.h"
#include "audio/vorbis_i16.h"

using namespace std;

namespace Icy
{
   SFXManager::~SFXManager()
   {
      for (std::map<std::string, f32_buf_t*>::iterator it = effects_f32.begin();
            it != effects_f32.end(); ++it)
         f32_buf_unref(it->second);

      for (std::map<std::string, i16_buf_t*>::iterator it = effects_i16.begin();
            it != effects_i16.end(); ++it)
         i16_buf_unref(it->second);
   }

   void SFXManager::add_stream(const string &ident, const string &path)
   {
      if (audio_is_float())
      {
         size_t  samples = 0;
         float  *pcm     = wav_f32_load(path.c_str(), &samples);
         f32_buf_t *buf;
         std::map<std::string, f32_buf_t*>::iterator old;

         if (!pcm)
            throw runtime_error("Failed to open wave.");

         if (!(buf = f32_buf_new(pcm, samples)))
         {
            free(pcm);
            throw runtime_error("Failed to allocate SFX buffer.");
         }

         if ((old = effects_f32.find(ident)) != effects_f32.end())
            f32_buf_unref(old->second);
         effects_f32[ident] = buf;
         return;
      }

      {
         size_t   samples = 0;
         int16_t *pcm     = wav_i16_load(path.c_str(), &samples);
         i16_buf_t *buf;
         std::map<std::string, i16_buf_t*>::iterator old;

         if (!pcm)
            throw runtime_error("Failed to open wave.");

         if (!(buf = i16_buf_new(pcm, samples)))
         {
            free(pcm);
            throw runtime_error("Failed to allocate SFX buffer.");
         }

         if ((old = effects_i16.find(ident)) != effects_i16.end())
            i16_buf_unref(old->second);
         effects_i16[ident] = buf;
      }
   }

   void SFXManager::play_sfx(const string &ident, float volume) const
   {
      if (audio_is_float())
      {
         mixer_f32_t *mixer = get_mixer_f32();
         std::map<std::string, f32_buf_t*>::const_iterator sfx =
            effects_f32.find(ident);
         f32_stream_t *stream;

         if (sfx == effects_f32.end())
            throw runtime_error("Invalid SFX!");

         if (!(stream = f32_pcm_stream_new(sfx->second, 0, volume)))
            return;

         if (mixer && mixer_f32_enabled(mixer))
            mixer_f32_add(mixer, stream);
         else
            stream->destroy(stream); /* not queued: release it */
         return;
      }

      {
         mixer_i16_t *mixer = get_mixer_i16();
         std::map<std::string, i16_buf_t*>::const_iterator sfx =
            effects_i16.find(ident);
         i16_stream_t *stream;

         if (sfx == effects_i16.end())
            throw runtime_error("Invalid SFX!");

         if (!(stream = i16_pcm_stream_new(sfx->second, 0,
                     mixer_i16_q15_from_float(volume))))
            return;

         if (mixer && mixer_i16_enabled(mixer))
            mixer_i16_add(mixer, stream);
         else
            stream->destroy(stream);
      }
   }
}
#endif
