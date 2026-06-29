#include "mixer.hpp"
#ifndef USE_CXX03
#include "../utils.hpp"
#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <fstream>

#include <audio/audio_mix.h>
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
      streams.push_back(move(str));
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

   vector<float> WAVFile::load_wave(const string& path)
   {
      using namespace Blit::Utils;
      ifstream file;

      file.exceptions(ifstream::badbit | ifstream::failbit | ifstream::eofbit);
      try
      {
         char header[44];
         vector<int16_t> wave;
         vector<float> pcm_data;

         file.open(path, ifstream::in | ifstream::binary);
         file.read(header, sizeof(header));

         if (!equal(header + 0, header + 4, "RIFF"))
            throw logic_error("Invalid WAV file.");

         if (!equal(header + 8, header + 12, "WAVE"))
            throw logic_error("Invalid WAV file.");

         if (!equal(header + 12, header + 16, "fmt "))
            throw logic_error("Invalid WAV file.");

         if (read_le16(header + 20) != 1)
            throw logic_error("WAV file not uncompressed.");

         unsigned channels    = read_le16(header + 22);
         unsigned sample_rate = read_le32(header + 24);
         unsigned bits        = read_le16(header + 34);

         if (channels < 1 || channels > 2)
            throw logic_error("Invalid number of channels.");

         if (sample_rate != 44100)
            throw logic_error("Invalid sample rate.");

         if (bits != 16)
            throw logic_error("Invalid bit depth.");

         unsigned wave_size = read_le32(header + 4);
         wave_size += 8;
         wave_size -= sizeof(header);
         wave.resize(wave_size / sizeof(int16_t));

         file.read(reinterpret_cast<char*>(wave.data()), wave_size);

         if (channels == 1)
         {
            pcm_data.resize(2 * wave_size / sizeof(int16_t));
            std::vector<float>::iterator ptr = pcm_data.begin();
            for (auto val : wave)
            {
               float fval = static_cast<float>(val) / 0x8000;
               *ptr++ = fval;
               *ptr++ = fval;
            }

         }
         else
         {
            pcm_data.resize(wave_size / sizeof(int16_t));
            std::vector<float>::iterator ptr = pcm_data.begin();
            for (auto val : wave)
            {
               float fval = static_cast<float>(val) / 0x8000;
               *ptr++ = fval;
            }
         }

         return pcm_data;
      }
      catch (const ifstream::failure& e)
      {
         cerr << "iostream error: " << e.what() << endl;
         throw runtime_error("Failed to open wave.");
      }
   }

   vector<float> VorbisFile::decode()
   {
      vector<float> data;
      float buffer[4096 * Mixer::channels];
      size_t rendered = 0;

      rewind();
      loop(false);

      while ((rendered = render(buffer, 4096)))
         data.insert(data.end(), buffer, buffer + rendered * Mixer::channels);

      return data;
   }

   VorbisFile::VorbisFile(const string& path)
      : path(path), is_eof(false), is_mono(false)
   {
      vorbis_info *info = NULL;
      FILE        *file = fopen(path.c_str(), "rb");

      if (!file)
         throw runtime_error(join("Failed to open vorbis file: ", path));

      /* Tremor's ov_open takes ownership of the FILE*; ov_clear() closes
       * it. On failure we still own it and must close it ourselves. */
      if (ov_open(file, &vf, NULL, 0) < 0)
      {
         fclose(file);
         throw runtime_error(join("Failed to open vorbis file: ", path));
      }

      info = (vorbis_info*)ov_info(&vf, -1);

      if (info)
      {
         switch (info->channels)
         {
            case 1:
               is_mono = true;
               break;

            case 2:
               is_mono = false;
               break;

            default:
               throw logic_error(join("Vorbis file has ", info->channels, " channels."));
         }

         if (info->rate != 44100)
            throw logic_error(join("Sampling rate of file is: ", info->rate));
      }
      else
         throw logic_error("Couldn't find info for vorbis file.");
   }

   VorbisFile::~VorbisFile()
   {
      ov_clear(&vf);
   }

   void VorbisFile::rewind()
   {
      /* Tremor's ov_time_seek position is in milliseconds. */
      if (ov_time_seek(&vf, 0) != 0)
         throw runtime_error("Couldn't rewind vorbis audio!\n");

      is_eof = false;
   }

   size_t VorbisFile::render(float* buffer, size_t frames)
   {
      size_t rendered = 0;

      while (frames)
      {
         int     bitstream;
         /* Tremor decodes interleaved int16 (host-endian). Scratch holds
          * up to 4096 stereo frames; mono yields half as many int16. */
         int16_t pcm[4096 * Mixer::channels];
         size_t  want_frames = (frames < 4096) ? frames : 4096;
         int     want_bytes  = (int)(want_frames
               * (is_mono ? 1 : Mixer::channels) * sizeof(int16_t));
         long    ret = ov_read(&vf, (char*)pcm, want_bytes, &bitstream);

         if (ret < 0)
            throw runtime_error(join("Vorbis decoding failed with: ", ret));

         if (ret == 0) // EOF
         {
            if (loop())
            {
               loop(false); // Avoid infinite recursion incause our audio clip is really short.
               ScopeExit holder([this] { loop(true); });

               if (ov_time_seek(&vf, 0) == 0)
               {
                  long unsigned int ret = render(buffer, frames);
                  return rendered + ret;
               }
               else
                  is_eof = true;
            }
            else
               is_eof = true;

            return rendered;
         }

         {
            long in_frames = (ret / 2) / (is_mono ? 1 : Mixer::channels);
            long i;

            if (!is_mono)
            {
               for (i = 0; i < in_frames; i++)
               {
                  buffer[2 * i + 0] = pcm[2 * i + 0] / 32768.0f;
                  buffer[2 * i + 1] = pcm[2 * i + 1] / 32768.0f;
               }
            }
            else
            {
               for (i = 0; i < in_frames; i++)
               {
                  float v = pcm[i] / 32768.0f;
                  buffer[2 * i + 0] = v;
                  buffer[2 * i + 1] = v;
               }
            }

            buffer   += in_frames * Mixer::channels;
            frames   -= in_frames;
            rendered += in_frames;
         }
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
            std::shared_ptr<std::vector<float> > ret = make_shared<vector<float>>(move(f));
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
