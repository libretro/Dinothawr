#include "game.hpp"
#ifndef USE_CXX03
#include <string>
#include <memory>
#include <cstdlib>

#include "audio/mixer_i16.h"
#include "audio/vorbis_i16.h"

using namespace std;

namespace Icy
{
   SFXManager::~SFXManager()
   {
      for (std::map<std::string, i16_buf_t*>::iterator it = effects_i16.begin();
            it != effects_i16.end(); ++it)
         i16_buf_unref(it->second);
   }

   void SFXManager::add_stream(const string &ident, const string &path)
   {
      if (!audio_is_float())
      {
         /* int16 pipeline: load the WAV straight to interleaved int16. */
         size_t   samples = 0;
         int16_t *pcm     = wav_i16_load(path.c_str(), &samples);
         if (!pcm)
            throw runtime_error("Failed to open wave.");

         i16_buf_t *buf = i16_buf_new(pcm, samples);
         if (!buf)
         {
            free(pcm);
            throw runtime_error("Failed to allocate SFX buffer.");
         }

         std::map<std::string, i16_buf_t*>::iterator old = effects_i16.find(ident);
         if (old != effects_i16.end())
            i16_buf_unref(old->second);
         effects_i16[ident] = buf;
         return;
      }

      effects[ident] = make_shared<vector<float>>(Audio::WAVFile::load_wave(path));
   }

   void SFXManager::play_sfx(const string &ident, float volume) const
   {
      if (!audio_is_float())
      {
         mixer_i16_t *mixer = get_mixer_i16();
         std::map<std::string, i16_buf_t*>::const_iterator sfx = effects_i16.find(ident);
         if (sfx == effects_i16.end())
            throw runtime_error("Invalid SFX!");

         i16_stream_t *stream = i16_pcm_stream_new(sfx->second, 0,
               mixer_i16_q15_from_float(volume));
         if (!stream)
            return;

         if (mixer && mixer_i16_enabled(mixer))
            mixer_i16_add(mixer, stream);
         else
            stream->destroy(stream); /* not queued: release it */
         return;
      }

      std::map<std::basic_string<char>, std::shared_ptr<std::vector<float> > >::const_iterator sfx = effects.find(ident);
      if (sfx == effects.end())
         throw runtime_error("Invalid SFX!");

      std::shared_ptr<Audio::PCMStream> duped = make_shared<Audio::PCMStream>(sfx->second);
      duped->volume(volume);
      Audio::Mixer& mixer = get_mixer();
      if (mixer.enabled())
         mixer.add_stream(duped);
   }
}
#endif
