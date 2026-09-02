/* Dinothawr - the player's animation frame.
 *
 * Which sprite face the dino shows while moving. The sprite lays them
 * out by index: 0 is standing still, 1 to 4 are the walk cycle, 5 and 6
 * are the slip cycle, and 7 is pushing. Those numbers are the sprite
 * file's, not this code's - they are here so the mapping is in one place
 * rather than spread across the three functions that used to pick a
 * frame.
 *
 * Counts are in ticks, and the period is passed in, so the cycle changes
 * at the same wall-clock moments whatever rate the game is stepped at.
 *
 * MSVC C89.
 */

#ifndef ICY_ANIM_H__
#define ICY_ANIM_H__

#ifdef __cplusplus
extern "C" {
#endif

enum
{
   ICY_ANIM_STILL    = 0,
   ICY_ANIM_WALK     = 1,  /* four frames, 1..4 */
   ICY_ANIM_WALK_LEN = 4,
   ICY_ANIM_SLIP     = 5,  /* two frames, 5..6  */
   ICY_ANIM_SLIP_LEN = 2,
   ICY_ANIM_PUSH     = 7
};

/* The face to show after @ticks of movement: the walk cycle, or the slip
 * cycle when @sliding. @period is how many ticks a frame of the cycle
 * lasts and is clamped to at least one. */
unsigned icy_anim_moving(unsigned ticks, unsigned period, int sliding);

/* The face to show while pushing: the push frame for the first
 * @push_ticks, then standing still, which is what reads as the shove
 * landing. */
unsigned icy_anim_pushing(unsigned ticks, unsigned push_ticks);

#ifdef __cplusplus
}
#endif

#endif
