/* Dinothawr - the level-select slide.
 *
 * Moving between levels in the menu scrolls the preview grid rather than
 * jumping: a slide covers a fixed distance over a fixed number of ticks,
 * and the caller applies the difference to its camera each tick.
 *
 * The distance is what the caller asked for; the duration is in ticks
 * rather than frames, because the game steps its logic on a tick that is
 * not the frame rate. Positions are integer and interpolated from the
 * total rather than accumulated, so a slide always ends exactly where it
 * was asked to and never drifts.
 *
 * MSVC C89.
 */

#ifndef ICY_MENU_SLIDE_H__
#define ICY_MENU_SLIDE_H__

#include "blit_geom.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
   blit_pos_t total;  /* the whole distance to cover      */
   blit_pos_t moved;  /* how much of it has been applied  */
   unsigned   tick;
   unsigned   ticks;  /* duration; never zero once started */
} icy_menu_slide_t;

void icy_menu_slide_init(icy_menu_slide_t *slide);

/* Starts a slide covering @total over @ticks. A zero duration is
 * clamped to one, so the slide completes in a single step rather than
 * dividing by zero. */
void icy_menu_slide_start(icy_menu_slide_t *slide, blit_pos_t total,
      unsigned ticks);

/* Advances one tick and returns how far to move this tick. */
blit_pos_t icy_menu_slide_step(icy_menu_slide_t *slide);

/* Non-zero once the whole distance has been applied. */
int icy_menu_slide_done(const icy_menu_slide_t *slide);

#ifdef __cplusplus
}
#endif

#endif
