#ifndef UTILS_HPP__
#define UTILS_HPP__

#include <errno.h>
#include <stdlib.h>

#include <algorithm>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "xml.hpp"

namespace Blit
{
   namespace Utils
   {
      template <typename T>
      inline std::string join(T&& t)
      {
         std::ostringstream stream;
         stream << std::forward<T>(t);
         return stream.str();
      }

      template <typename T, typename... U>
      inline std::string join(T&& t, U&&... u)
      {
         std::ostringstream stream;
         stream << std::forward<T>(t) << join(std::forward<U>(u)...);
         return stream.str();
      }

      inline std::vector<std::string> split(const std::string& str, char delim)
      {
         std::vector<std::string> ret;
         std::istringstream stream(str);
         std::string line;

         while (std::getline(stream, line, delim) && !line.empty())
            ret.push_back(std::move(line));

         return ret;
      }

      template <typename T, typename... U>
      inline std::unique_ptr<T> make_unique(U&&... u)
      {
         return std::unique_ptr<T>(new T(std::forward<U>(u)...));
      }

      inline std::string basedir(const std::string& path)
      {
         std::string::size_type last = path.find_last_of("/\\");
         if (last != std::string::npos)
            return path.substr(0, last);
         else
            return ".";
      }

      inline std::string tolower(const std::string& str)
      {
         std::string tmp;
         std::transform(str.begin(), str.end(), std::back_inserter(tmp), [](char c) -> char { return ::tolower(c); });
         return tmp;
      }

      inline std::string toupper(const std::string& str)
      {
         std::string tmp;
         std::transform(str.begin(), str.end(), std::back_inserter(tmp), [](char c) -> char { return ::toupper(c); });
         return tmp;
      }

      // Mirrors std::stoi, as it doesn't seem to work on Mingw 64-bit.
      inline int stoi(const std::string& str)
      {
         char *next = NULL;
         errno = 0;
         long res = strtol(str.c_str(), &next, 10);
         if (errno)
            throw std::invalid_argument("stoi");

         if (next - str.c_str() != static_cast<std::ptrdiff_t>(str.length()))
            throw std::invalid_argument("stoi");

         if (res < std::numeric_limits<int>::min() || res > std::numeric_limits<int>::max())
            throw std::out_of_range("stoi");

         return res;
      }

      template <typename T>
      inline typename T::mapped_type find_or_default(const T& mapper, const typename T::key_type& key, const typename T::mapped_type& def)
      {
         typename T::const_iterator itr = mapper.find(key);
         if (itr == mapper.end())
            return def;

         return itr->second;
      }

   }
}

#endif

