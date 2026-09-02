/* Dinothawr - the save encoding (implementation).
 * MSVC C89. See icy_save.h. */

#include "icy_save.h"

#include <stdio.h>

/* snprintf is C99; this is libretro-common's shim for the MSVC
 * versions that lack it. */
#include <compat/msvc.h>
#include <stdlib.h>
#include <string.h>

void icy_save_encode(char *buf, size_t len, const unsigned *counts,
      const unsigned *levels_per_chapter, size_t chapters)
{
   size_t written = 0;
   size_t flat    = 0;
   size_t c;

   if (!buf || !len)
      return;

   memset(buf, 0, len);

   for (c = 0; c < chapters; c++)
   {
      unsigned l;

      for (l = 0; l < levels_per_chapter[c]; l++, flat++)
      {
         char   field[16];
         size_t n = (size_t)snprintf(field, sizeof(field), "%u,",
               counts ? counts[flat] : 0u);

         if (written + n >= len)
            return;

         memcpy(buf + written, field, n);
         written += n;
      }

      if (written + 1 >= len)
         return;

      buf[written++] = '\n';
   }
}

void icy_save_decode(const char *buf, size_t len, unsigned *counts,
      const unsigned *levels_per_chapter, size_t chapters)
{
   size_t      pos  = 0;
   size_t      flat = 0;
   size_t      c;

   if (!buf || !counts)
      return;

   for (c = 0; c < chapters; c++)
   {
      unsigned l;

      /* A chapter the save stops short of leaves its levels alone. */
      if (pos >= len || !buf[pos])
         return;

      for (l = 0; l < levels_per_chapter[c]; l++, flat++)
      {
         size_t start = pos;

         while (pos < len && buf[pos] && buf[pos] != ',' && buf[pos] != '\n')
            pos++;

         if (pos == start)
            break;

         counts[flat] = (unsigned)strtoul(buf + start, NULL, 10);

         if (pos < len && buf[pos] == ',')
            pos++;
         else
            break;
      }

      /* Skip whatever is left of the line, including a chapter with more
       * levels in the save than the game has now. */
      while (pos < len && buf[pos] && buf[pos] != '\n')
         pos++;
      if (pos < len && buf[pos] == '\n')
         pos++;

      flat += levels_per_chapter[c] - l;
   }
}
