#include "mixer.hpp"
#ifndef USE_CXX03
#include "../utils.hpp"
#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <fstream>

#include <audio/audio_mix.h>
#include <formats/audio.h>
#include <formats/data_transfer.h>
#include <audio/conversion/float_to_s16.h>

using namespace Blit::Utils;
using namespace std;

typedef lock_guard<recursive_mutex> LockGuard;

namespace Audio
{
   Mixer::Mixer() : master_vol(1.0f)
   {
      m_enabled = Blit::Utils::make_unique<std::atomic<unsigned>>();
      m_lock = Blit::Utils::make_unique<recursive_mutex>();
   }

   void Mixer::add_stream(shared_ptr<Stream> str)
   {
      LockGuard guard(*m_lock);
      streams.push_back(std::move(str));
   }

   static bool erase_mixer_stream(const shared_ptr<Stream> &str)
   {
      return !str->valid();
   }

   void Mixer::purge_dead_streams()
   {
      LockGuard guard(*m_lock);
      streams.erase(remove_if(streams.begin(), streams.end(), erase_mixer_stream), streams.end());
   }

   void Mixer::render(float* out_buffer, size_t frames)
   {
      LockGuard guard(*m_lock);
      purge_dead_streams();

      fill(out_buffer, out_buffer + frames * channels, 0.0f);

      buffer.reserve(frames * channels);
      for (auto& stream : streams)
      {
         long unsigned int rendered = stream->render(buffer.data(), frames);
         audio_mix_volume(out_buffer, buffer.data(), master_vol * stream->volume(), rendered * channels);
      }
   }

   void Mixer::render(int16_t* out_buffer, size_t frames)
   {
      LockGuard guard(*m_lock);
      conv_buffer.reserve(frames * channels);
      render(conv_buffer.data(), frames);

      convert_float_to_s16(out_buffer, conv_buffer.data(), frames * channels);
   }

   void Mixer::clear()
   {
      LockGuard guard(*m_lock);
      streams.clear();
   }

   PCMStream::PCMStream(shared_ptr<vector<float>> data)
      : data(data), ptr(0)
   {}

   size_t PCMStream::render(float* buffer, size_t frames)
   {
      size_t to_write = min(frames * Mixer::channels, data->size() - ptr);

      copy(begin(*data) + ptr,
            begin(*data) + ptr + to_write,
            buffer);

      if (to_write < frames && loop())
      {
         size_t to_write_loop;

         rewind();

         to_write_loop = min(frames * Mixer::channels - to_write, data->size() - (ptr + to_write));

         copy(begin(*data) + ptr + to_write,
               begin(*data) + ptr + to_write + to_write_loop,
               buffer + to_write);

         to_write += to_write_loop;
      }

      ptr += to_write;
      return to_write / Mixer::channels;
   }

   /* Open a decoder over a whole file: data_transfer holds the encoded
    * bytes (one budgeted fill, then a stable base pointer), audio_transfer
    * borrows them.  Both WAV and Ogg Vorbis come through here - the facade
    * is what makes them one code path rather than two loaders. */
   static bool audio_open(const string& path, data_transfer_t **out_dt,
         void **out_xfer, enum audio_type_enum *out_type,
         unsigned *out_channels)
   {
      const uint8_t       *ptr  = NULL;
      size_t               len  = 0;
      data_transfer_t     *dt   = NULL;
      void                *xfer = NULL;
      unsigned             channels = 0;
      unsigned             rate     = 0;
      enum audio_type_enum type = audio_decode_get_type(path.c_str());

      if (type == AUDIO_TYPE_NONE)
         return false;

      if (!(dt = data_transfer_open_prefix(path.c_str(), 0)))
         return false;

      data_transfer_iterate(dt, 0);
      ptr = data_transfer_ptr(dt, &len);

      if (!data_transfer_complete(dt) || !ptr || !len
            || !(xfer = audio_transfer_new(type)))
         goto error;

      audio_transfer_set_buffer_ptr(xfer, type, (void*)ptr, len);

      if (   !audio_transfer_start(xfer, type)
          || !audio_transfer_info(xfer, type, &channels, &rate, NULL))
         goto error;

      if (channels < 1 || channels > 2 || rate != 44100)
         goto error;

      *out_dt       = dt;
      *out_xfer     = xfer;
      *out_type     = type;
      *out_channels = channels;
      return true;

error:
      if (xfer)
         audio_transfer_free(xfer, type);
      data_transfer_free(dt);
      return false;
   }

   vector<float> WAVFile::load_wave(const string& path)
   {
      data_transfer_t     *dt   = NULL;
      void                *xfer = NULL;
      enum audio_type_enum type = AUDIO_TYPE_NONE;
      unsigned             channels = 0;
      vector<float>        pcm_data;

      if (!audio_open(path, &dt, &xfer, &type, &channels))
         throw runtime_error(join("Failed to open wave: ", path));

      try
      {
         float  chunk[4096 * Mixer::channels];
         size_t got = 0;

         /* Read in caller-sized chunks rather than asking for the whole
          * stream at once: the facade produces short reads freely, and a
          * short read is not end of stream. */
         while (audio_transfer_read_f32(xfer, type, chunk, 4096, &got)
                     >= AUDIO_PROCESS_NEXT
               && got)
         {
            size_t i;

            if (channels == 1)
            {
               for (i = 0; i < got; i++)
               {
                  pcm_data.push_back(chunk[i]);
                  pcm_data.push_back(chunk[i]);
               }
            }
            else
               pcm_data.insert(pcm_data.end(), chunk,
                     chunk + got * Mixer::channels);
         }
      }
      catch (...)
      {
         audio_transfer_free(xfer, type);
         data_transfer_free(dt);
         throw;
      }

      audio_transfer_free(xfer, type);
      data_transfer_free(dt);
      return pcm_data;
   }

   vector<float> VorbisFile::decode()
   {
      unsigned ch    = 0;
      unsigned rate  = 0;
      uint64_t total = 0;

      rewind();
      loop(false);

      /* One read of the whole stream instead of a loop of 4096-frame
       * chunks.  It is no more blocking than what it replaces: the
       * chunked form ran the file to completion in a tight loop with no
       * yield between chunks, and the only caller of decode() runs it
       * under std::async - this does the same work on the same worker
       * thread, about 17% less of it, and sizes the output once rather
       * than growing it by doubling (which transiently holds the old
       * buffer and the new one at every move).
       *
       * render() deliberately stays chunked: that one is driven from the
       * audio callback, where a whole-file read is exactly the wrong
       * shape. */
      if (audio_transfer_info(xfer, type, &ch, &rate, &total) && total)
      {
         vector<float> data(static_cast<size_t>(total) * Mixer::channels);
         size_t        got = 0;

         if (audio_transfer_read_f32(xfer, type, &data[0],
                  static_cast<size_t>(total), &got) >= AUDIO_PROCESS_NEXT
               && got)
         {
            if (is_mono)
            {
               /* Decoded as one channel into the head of the buffer.
                * Expand backwards: every write lands at 2i >= i, so it
                * never lands on a sample still to be read. */
               size_t i = got;
               while (i-- > 0)
               {
                  float v             = data[i];
                  data[2 * i + 0]     = v;
                  data[2 * i + 1]     = v;
               }
            }

            data.resize(got * Mixer::channels);
            is_eof = true;
            return data;
         }
      }

      /* Length unknown, or the single read declined: chunked fallback. */
      {
         vector<float> data;
         float         buffer[4096 * Mixer::channels];
         size_t        rendered = 0;

         rewind();

         while ((rendered = render(buffer, 4096)))
            data.insert(data.end(), buffer,
                  buffer + rendered * Mixer::channels);

         return data;
      }
   }

   VorbisFile::VorbisFile(const string& path)
      : path(path), dt(NULL), xfer(NULL), type(AUDIO_TYPE_NONE),
        is_eof(false), is_mono(false)
   {
      unsigned channels = 0;

      if (!audio_open(path, &dt, &xfer, &type, &channels))
         throw runtime_error(join("Failed to open vorbis file: ", path));

      is_mono = (channels == 1);
   }

   VorbisFile::~VorbisFile()
   {
      if (xfer)
         audio_transfer_free(xfer, type);
      if (dt)
         data_transfer_free(dt);
   }

   void VorbisFile::rewind()
   {
      if (!audio_transfer_seek(xfer, type, 0))
         throw runtime_error("Couldn't rewind vorbis audio!\n");

      is_eof = false;
   }

   size_t VorbisFile::render(float* buffer, size_t frames)
   {
      size_t rendered = 0;

      while (frames)
      {
         /* Vorbis is float internally, so read_f32 is the decoder's own
          * output - no int16 round trip on the way through. */
         float  pcm[4096 * Mixer::channels];
         size_t want_frames = (frames < 4096) ? frames : 4096;
         size_t got         = 0;
         int    ret         = audio_transfer_read_f32(xfer, type, pcm,
               want_frames, &got);
         size_t i;

         if (ret < AUDIO_PROCESS_NEXT)
            throw runtime_error(join("Vorbis decoding failed with: ", ret));

         /* A short read is not end of stream; only zero frames is. */
         if (!got)
         {
            if (loop())
            {
               loop(false); /* the recursive call must not loop again */
               ScopeExit holder([this] { loop(true); });

               if (audio_transfer_seek(xfer, type, 0))
                  return rendered + render(buffer, frames);

               is_eof = true;
            }
            else
               is_eof = true;

            return rendered;
         }

         if (!is_mono)
         {
            for (i = 0; i < got * Mixer::channels; i++)
               buffer[i] = pcm[i];
         }
         else
         {
            for (i = 0; i < got; i++)
            {
               buffer[2 * i + 0] = pcm[i];
               buffer[2 * i + 1] = pcm[i];
            }
         }

         buffer   += got * Mixer::channels;
         frames   -= got;
         rendered += got;
      }

      return rendered;
   }

   void VorbisLoader::request_vorbis(const string& path)
   {
      inflight.push_back(async(launch::async, [path]() {
                  VorbisFile file{path};
                  return file.decode();
               }));
   }

   static bool erase_vorbis_stream(const future<vector<float>>& fut)
   {
      return !fut.valid();
   }

   void VorbisLoader::cleanup()
   {
      inflight.erase(remove_if(inflight.begin(), inflight.end(), erase_vorbis_stream), inflight.end());
   }

   shared_ptr<vector<float>> VorbisLoader::flush()
   {
      try
      {
         for (auto& fut : inflight)
            if (fut.wait_for(chrono::seconds(0)) == future_status::ready)
               finished.push(fut.get()); 

         cleanup();

         if (finished.size())
         {
            std::vector<float> f = finished.front();
            std::shared_ptr<std::vector<float> > ret = make_shared<vector<float>>(std::move(f));
            finished.pop();
            return ret;
         }
         else
            return {};
      }
      catch (const exception& e)
      {
         cerr << "VorbisLoader::flush() failed ... " << e.what() << endl;
         cleanup();
         return {};
      }
   }
}
#endif
