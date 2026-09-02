/* Dinothawr - durations at the frame rate being run (implementation).
 * MSVC C89. See icy_rate.h. */

#include "icy_rate.h"

static double g_fps = 60.0;

void icy_rate_set(double fps)
{
   g_fps = fps > 0.0 ? fps : 60.0;
}

unsigned icy_frames_to_ticks(unsigned frames60)
{
   double ticks = frames60 * g_fps / 60.0 + 0.5;

   if (ticks < 1.0)
      return 1;

   return (unsigned)ticks;
}
