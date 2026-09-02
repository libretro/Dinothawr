/* Dinothawr - one leg of a slide.
 *
 * A surface crossing one tile does it over a fixed number of ticks: the
 * leg. Both the player walking and a block sliding on ice move a leg at
 * a time, and a leg always ends exactly on the tile grid.
 *
 * The distance moved is computed from the tick count rather than
 * accumulated, so the last tick lands on the tile boundary whatever the
 * rounding did on the way. The duration is fixed when the leg starts and
 * never changes during one, which is what makes that guarantee hold if
 * the frame rate changes mid-leg - the new rate is picked up by the next
 * leg.
 *
 * MSVC C89.
 */

#ifndef ICY_LEG_H__
#define ICY_LEG_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
   unsigned tick;
   unsigned ticks;  /* duration; never zero once started */
   int      moved;  /* pixels of this leg already applied */
} icy_leg_t;

/* Starts a leg of @ticks. A zero duration is clamped to one, so a leg
 * completes in a single step rather than dividing by zero. */
void icy_leg_begin(icy_leg_t *leg, unsigned ticks);

/* Advances one tick and returns how many pixels to move, given the leg
 * covers @distance in total. */
int icy_leg_step(icy_leg_t *leg, int distance);

/* Non-zero once the leg has run its ticks. Asked by tick count, not by
 * grid alignment: above two ticks per pixel the first ticks of a leg
 * round to no movement, which leaves the surface still aligned and would
 * otherwise read as a finished leg. */
int icy_leg_done(const icy_leg_t *leg);

#ifdef __cplusplus
}
#endif

#endif
