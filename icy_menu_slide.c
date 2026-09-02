/* Dinothawr - the level-select slide (implementation).
 * MSVC C89. See icy_menu_slide.h. */

#include "icy_menu_slide.h"

void icy_menu_slide_init(icy_menu_slide_t *slide)
{
   slide->total = blit_pos_zero();
   slide->moved = blit_pos_zero();
   slide->tick  = 0;
   slide->ticks = 1;
}

void icy_menu_slide_start(icy_menu_slide_t *slide, blit_pos_t total,
      unsigned ticks)
{
   slide->total = total;
   slide->moved = blit_pos_zero();
   slide->tick  = 0;
   slide->ticks = ticks ? ticks : 1;
}

blit_pos_t icy_menu_slide_step(icy_menu_slide_t *slide)
{
   blit_pos_t want;
   blit_pos_t delta;

   slide->tick++;
   if (slide->tick > slide->ticks)
      slide->tick = slide->ticks;

   /* Interpolated from the total, not accumulated: the last tick lands
    * exactly on it whatever the rounding did on the way. */
   want = blit_pos(
         slide->total.x * (int)slide->tick / (int)slide->ticks,
         slide->total.y * (int)slide->tick / (int)slide->ticks);

   delta        = blit_pos_sub(want, slide->moved);
   slide->moved = want;
   return delta;
}

int icy_menu_slide_done(const icy_menu_slide_t *slide)
{
   return slide->tick >= slide->ticks;
}
