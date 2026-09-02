/* Dinothawr - durations at the frame rate being run.
 *
 * Every duration in the game is written in 60 Hz frames, because that is
 * the rate the fixed-step simulation this replaced ran at. The frontend
 * picks the actual rate, so a duration has to be converted to ticks
 * before it is counted down.
 *
 * MSVC C89.
 */

#ifndef ICY_RATE_H__
#define ICY_RATE_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Sets the rate the game is being stepped at. */
void icy_rate_set(double fps);

/* @frames60 at 60 Hz expressed in ticks at the current rate. Rounded, so
 * a duration lands on the nearest whole tick rather than always short,
 * and floored at one so nothing becomes instantaneous at low rates. At
 * 60 Hz it is the identity, which is what keeps the default rate
 * identical to the fixed-step simulation. */
unsigned icy_frames_to_ticks(unsigned frames60);

#ifdef __cplusplus
}
#endif

#endif
