/* Dinothawr - the camera (implementation).
 * MSVC C89. See icy_camera.h. */

#include "icy_camera.h"

void icy_camera_update(blit_render_target_t *target,
      blit_rect_t follow, blit_pos_t map_size)
{
   blit_pos_t view;
   blit_pos_t centre;
   blit_pos_t base;
   blit_pos_t max;

   if (!target)
      return;

   view = blit_pos(target->rect.w, target->rect.h);

   /* The whole map fits: centre it and leave it. */
   if (view.x >= map_size.x && view.y >= map_size.y)
   {
      target->rect.pos = blit_pos_div(blit_pos_sub(map_size, view), 2);
      return;
   }

   /* Otherwise centre on what is being followed, then pull back inside
    * the map so the view never shows past its edges. */
   centre = blit_pos_add(follow.pos,
         blit_pos_div(blit_pos(follow.w, follow.h), 2));

   base = blit_pos_sub(centre, blit_pos_div(view, 2));
   max  = blit_pos_add(base, view);

   if (base.x < 0)
      base.x = 0;
   else if (max.x > map_size.x)
      base.x -= max.x - map_size.x;

   if (base.y < 0)
      base.y = 0;
   else if (max.y > map_size.y)
      base.y -= max.y - map_size.y;

   target->rect.pos = base;
}
