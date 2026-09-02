/* Dinothawr - the game's inputs (implementation).
 * MSVC C89. See icy_input.h. */

#include "icy_input.h"

#include <string.h>

/* The directions, and nothing else: the other inputs have no offset and
 * no face, which the lookups below report by falling off the end. */
static const struct
{
   enum icy_input input;
   const char    *face;
   int            x;
   int            y;
} icy_directions[] =
{
   { ICY_INPUT_UP,    "up",     0, -1 },
   { ICY_INPUT_DOWN,  "down",   0,  1 },
   { ICY_INPUT_LEFT,  "left",  -1,  0 },
   { ICY_INPUT_RIGHT, "right",  1,  0 }
};

#define ICY_DIRECTION_COUNT \
   (sizeof(icy_directions) / sizeof(icy_directions[0]))

blit_pos_t icy_input_offset(enum icy_input input)
{
   size_t i;

   for (i = 0; i < ICY_DIRECTION_COUNT; i++)
      if (icy_directions[i].input == input)
         return blit_pos(icy_directions[i].x, icy_directions[i].y);

   return blit_pos_zero();
}

const char *icy_input_face(enum icy_input input)
{
   size_t i;

   for (i = 0; i < ICY_DIRECTION_COUNT; i++)
      if (icy_directions[i].input == input)
         return icy_directions[i].face;

   return "";
}

enum icy_input icy_input_from_face(const char *face)
{
   size_t i;

   if (!face)
      return ICY_INPUT_NONE;

   for (i = 0; i < ICY_DIRECTION_COUNT; i++)
      if (strcmp(icy_directions[i].face, face) == 0)
         return icy_directions[i].input;

   return ICY_INPUT_NONE;
}
