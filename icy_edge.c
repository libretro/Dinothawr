/* Dinothawr - input edge detection (implementation).
 * MSVC C89. See icy_edge.h. */

#include "icy_edge.h"

void icy_edge_init(icy_edge_t *edge)
{
   unsigned i;
   for (i = 0; i < ICY_EDGE_COUNT; i++)
      edge->held[i] = 0;
}

void icy_edge_suppress(icy_edge_t *edge, enum icy_edge_button button)
{
   if (button < ICY_EDGE_COUNT)
      edge->held[button] = 1;
}

int icy_edge_pressed(icy_edge_t *edge, enum icy_edge_button button,
      int state)
{
   int went_down;

   if (button >= ICY_EDGE_COUNT)
      return 0;

   went_down = state && !edge->held[button];
   edge->held[button] = state ? 1 : 0;
   return went_down;
}
