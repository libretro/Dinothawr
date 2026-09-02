/* Dinothawr - the win condition (implementation).
 * MSVC C89. See icy_goal.h. */

#include "icy_goal.h"

#include <stdlib.h>

static int goal_compare(const void *lhs, const void *rhs)
{
   const blit_pos_t *a = (const blit_pos_t*)lhs;
   const blit_pos_t *b = (const blit_pos_t*)rhs;

   if (blit_pos_less(*a, *b))
      return -1;
   if (blit_pos_less(*b, *a))
      return 1;
   return 0;
}

int icy_goal_all_covered(blit_pos_t *goals, blit_pos_t *blocks,
      size_t count)
{
   size_t i;

   if (!count)
      return 0;

   qsort(goals,  count, sizeof(*goals),  goal_compare);
   qsort(blocks, count, sizeof(*blocks), goal_compare);

   for (i = 0; i < count; i++)
      if (!blit_pos_equal(goals[i], blocks[i]))
         return 0;

   return 1;
}
