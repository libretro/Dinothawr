/* Dinothawr - the game's inputs.
 *
 * The buttons the game reads, and the three ways it needs to talk about
 * a direction: as an offset in tiles, as the sprite face name that
 * direction shows, and back from the name a level file gives for the
 * player's starting facing.
 *
 * The mapping lived as three switch statements on the game class. It is
 * one table here, so a direction that gains a name cannot gain it in one
 * of the three and not the others.
 *
 * MSVC C89.
 */

#ifndef ICY_INPUT_H__
#define ICY_INPUT_H__

#include "blit_geom.h"

#ifdef __cplusplus
extern "C" {
#endif

enum icy_input
{
   ICY_INPUT_UP = 0,
   ICY_INPUT_DOWN,
   ICY_INPUT_LEFT,
   ICY_INPUT_RIGHT,
   ICY_INPUT_PUSH,
   ICY_INPUT_MENU,
   ICY_INPUT_RESET,
   ICY_INPUT_NONE
};

/* One tile in @input's direction, or zero for a non-direction. */
blit_pos_t icy_input_offset(enum icy_input input);

/* The sprite face @input shows, or "" for a non-direction. Never NULL. */
const char *icy_input_face(enum icy_input input);

/* The direction named by @face, or ICY_INPUT_NONE. */
enum icy_input icy_input_from_face(const char *face);

#ifdef __cplusplus
}
#endif

#endif
