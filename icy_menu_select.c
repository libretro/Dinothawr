/* Dinothawr - level-select navigation (implementation).
 * MSVC C89. See icy_menu_select.h. */

#include "icy_menu_select.h"

static unsigned menu_levels(const icy_menu_chapters_t *c, unsigned chapter)
{
   return c->levels ? c->levels(c->ctx, chapter) : 0;
}

static int menu_cleared(const icy_menu_chapters_t *c, unsigned chapter)
{
   return c->cleared ? c->cleared(c->ctx, chapter) : 0;
}

static icy_menu_move_t menu_none(void)
{
   icy_menu_move_t out;
   out.moved         = 0;
   out.locked        = 0;
   out.level_delta   = 0;
   out.chapter_delta = 0;
   return out;
}

static icy_menu_move_t menu_locked(void)
{
   icy_menu_move_t out = menu_none();
   out.locked = 1;
   return out;
}

static icy_menu_move_t menu_to(icy_menu_cursor_t *cursor,
      unsigned chapter, unsigned level)
{
   icy_menu_move_t out = menu_none();

   out.moved         = 1;
   out.level_delta   = (int)level - (int)cursor->level;
   out.chapter_delta = (int)chapter - (int)cursor->chapter;

   cursor->chapter = chapter;
   cursor->level   = level;
   return out;
}

icy_menu_move_t icy_menu_move(icy_menu_cursor_t *cursor,
      enum icy_menu_dir dir, const icy_menu_chapters_t *chapters)
{
   unsigned here;

   if (!chapters->chapters || cursor->chapter >= chapters->chapters)
      return menu_none();

   here = menu_levels(chapters, cursor->chapter);

   switch (dir)
   {
      case ICY_MENU_LEFT:
         if (cursor->level > 0)
            return menu_to(cursor, cursor->chapter, cursor->level - 1);

         /* Off the front of a chapter: the end of the previous one. No
          * clear check going backwards - a chapter the player has
          * reached is one they may return to. */
         if (cursor->chapter > 0)
         {
            unsigned prev = menu_levels(chapters, cursor->chapter - 1);
            if (prev)
               return menu_to(cursor, cursor->chapter - 1, prev - 1);
         }
         return menu_none();

      case ICY_MENU_RIGHT:
         if (here && cursor->level + 1 < here)
            return menu_to(cursor, cursor->chapter, cursor->level + 1);

         if (cursor->chapter + 1 < chapters->chapters)
         {
            if (!menu_cleared(chapters, cursor->chapter))
               return menu_locked();
            return menu_to(cursor, cursor->chapter + 1, 0);
         }
         return menu_none();

      case ICY_MENU_UP:
         if (cursor->chapter > 0)
         {
            unsigned prev  = menu_levels(chapters, cursor->chapter - 1);
            unsigned level = cursor->level;

            if (!prev)
               return menu_none();
            if (level > prev - 1)
               level = prev - 1;

            return menu_to(cursor, cursor->chapter - 1, level);
         }
         return menu_none();

      case ICY_MENU_DOWN:
         if (cursor->chapter + 1 < chapters->chapters)
         {
            unsigned next;
            unsigned level;

            if (!menu_cleared(chapters, cursor->chapter))
               return menu_locked();

            next  = menu_levels(chapters, cursor->chapter + 1);
            level = cursor->level;

            if (!next)
               return menu_none();
            if (level > next - 1)
               level = next - 1;

            return menu_to(cursor, cursor->chapter + 1, level);
         }
         return menu_none();

      default:
         break;
   }

   return menu_none();
}
