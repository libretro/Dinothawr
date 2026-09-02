/* Dinothawr - the win condition.
 *
 * A level is won when every goal square has a block standing on it. The
 * two sets are collected separately - goal floors from one layer, goal
 * blocks from another - so the check is whether the two sets of
 * positions are the same set.
 *
 * They are compared as sets rather than pairwise in collection order,
 * because nothing puts the two layers in a matching order; sorting both
 * by position is what makes the comparison mean what it says.
 *
 * MSVC C89.
 */

#ifndef ICY_GOAL_H__
#define ICY_GOAL_H__

#include <stddef.h>

#include "blit_geom.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Sorts both arrays in place and reports whether they hold the same
 * positions. Counts must match; the caller checks that, since a
 * mismatch means the level is malformed rather than unfinished. */
int icy_goal_all_covered(blit_pos_t *goals, blit_pos_t *blocks,
      size_t count);

#ifdef __cplusplus
}
#endif

#endif
