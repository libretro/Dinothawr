/* Dinothawr - one leg of a slide (implementation).
 * MSVC C89. See icy_leg.h. */

#include "icy_leg.h"

void icy_leg_begin(icy_leg_t *leg, unsigned ticks)
{
   leg->tick  = 0;
   leg->ticks = ticks ? ticks : 1;
   leg->moved = 0;
}

int icy_leg_step(icy_leg_t *leg, int distance)
{
   int want;
   int delta;

   leg->tick++;

   want = (int)((unsigned)distance * leg->tick / leg->ticks);
   if (want > distance)
      want = distance;

   delta      = want - leg->moved;
   leg->moved = want;
   return delta;
}

int icy_leg_done(const icy_leg_t *leg)
{
   return leg->tick >= leg->ticks;
}
