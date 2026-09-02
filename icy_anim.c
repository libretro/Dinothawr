/* Dinothawr - the player's animation frame (implementation).
 * MSVC C89. See icy_anim.h. */

#include "icy_anim.h"

unsigned icy_anim_moving(unsigned ticks, unsigned period, int sliding)
{
   unsigned step;

   if (!period)
      period = 1;

   step = ticks / period;

   if (sliding)
      return ICY_ANIM_SLIP + (step % ICY_ANIM_SLIP_LEN);

   return ICY_ANIM_WALK + (step % ICY_ANIM_WALK_LEN);
}

unsigned icy_anim_pushing(unsigned ticks, unsigned push_ticks)
{
   return ticks < push_ticks ? ICY_ANIM_PUSH : ICY_ANIM_STILL;
}
