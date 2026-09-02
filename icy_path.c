/* Dinothawr - asset paths (implementation).
 * MSVC C89. See icy_path.h. */

#include "icy_path.h"

#include <stdio.h>

/* snprintf is C99; this is libretro-common's shim for the MSVC
 * versions that lack it. */
#include <compat/msvc.h>
#include <string.h>

void icy_path_join(char *out, size_t len, const char *dir,
      const char *name)
{
   if (!out || !len)
      return;

   snprintf(out, len, "%s/%s", dir ? dir : ".", name ? name : "");
}

void icy_path_dir(char *out, size_t len, const char *path)
{
   const char *slash;
   const char *back;
   size_t      n;

   if (!out || !len)
      return;

   if (!path)
      path = "";

   slash = strrchr(path, '/');
   back  = strrchr(path, '\\');

   if (back && (!slash || back > slash))
      slash = back;

   if (!slash)
   {
      snprintf(out, len, ".");
      return;
   }

   n = (size_t)(slash - path);
   if (n >= len)
      n = len - 1;

   memcpy(out, path, n);
   out[n] = '\0';
}
