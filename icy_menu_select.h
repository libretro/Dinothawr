/* Dinothawr - level-select navigation.
 *
 * Which chapter and level the menu cursor sits on, and what a direction
 * press does to it. Moving off the end of a chapter carries into the
 * next one, and moving into a chapter that has not been cleared is
 * refused.
 *
 * This is index arithmetic over the chapter and level counts and nothing
 * else. It does not know what a preview looks like or how far the grid
 * scrolls per step - it reports the move it made and the caller turns
 * that into a slide.
 *
 * MSVC C89.
 */

#ifndef ICY_MENU_SELECT_H__
#define ICY_MENU_SELECT_H__

#ifdef __cplusplus
extern "C" {
#endif

enum icy_menu_dir
{
   ICY_MENU_LEFT = 0,
   ICY_MENU_RIGHT,
   ICY_MENU_UP,
   ICY_MENU_DOWN
};

/* What the caller has to know about the chapter list to navigate it. */
typedef struct
{
   unsigned    chapters;
   /* Levels in @chapter. */
   unsigned  (*levels)(void *ctx, unsigned chapter);
   /* Whether @chapter has been cleared, which is what unlocks the next. */
   int       (*cleared)(void *ctx, unsigned chapter);
   void       *ctx;
} icy_menu_chapters_t;

typedef struct
{
   unsigned chapter;
   unsigned level;
} icy_menu_cursor_t;

/* The outcome of a press. 'moved' is zero when nothing happened and the
 * caller should not slide; 'locked' says why - the next chapter is not
 * open yet, which the caller answers with a sound. The deltas are in
 * levels and chapters, for the caller to scale into pixels. */
typedef struct
{
   int moved;
   int locked;
   int level_delta;
   int chapter_delta;
} icy_menu_move_t;

/* Applies @dir to @cursor. */
icy_menu_move_t icy_menu_move(icy_menu_cursor_t *cursor,
      enum icy_menu_dir dir, const icy_menu_chapters_t *chapters);

#ifdef __cplusplus
}
#endif

#endif
