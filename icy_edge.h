/* Dinothawr - input edge detection.
 *
 * Menus act on the press, not on the hold: a held direction moves the
 * cursor once. This tracks the previous state of a set of buttons and
 * reports which of them went down this frame.
 *
 * Held as a set rather than one detector per button, because they are
 * always sampled together and the failure mode is one of them not being
 * cleared with the rest - which is what a wall of old_pressed_ members
 * kept inviting.
 *
 * MSVC C89.
 */

#ifndef ICY_EDGE_H__
#define ICY_EDGE_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Buttons the menu reads. */
enum icy_edge_button
{
   ICY_EDGE_LEFT = 0,
   ICY_EDGE_RIGHT,
   ICY_EDGE_UP,
   ICY_EDGE_DOWN,
   ICY_EDGE_OK,
   ICY_EDGE_MENU,
   ICY_EDGE_RESET,
   ICY_EDGE_COUNT
};

typedef struct
{
   unsigned char held[ICY_EDGE_COUNT];
} icy_edge_t;

/* All released. */
void icy_edge_init(icy_edge_t *edge);

/* Marks @button as already held, so the press that is down right now
 * does not read as an edge. Entering a menu from a button press uses
 * this to avoid acting on the same press twice. */
void icy_edge_suppress(icy_edge_t *edge, enum icy_edge_button button);

/* Records this frame's state for @button and returns non-zero if it
 * went down. Call once per button per frame. */
int icy_edge_pressed(icy_edge_t *edge, enum icy_edge_button button,
      int state);

#ifdef __cplusplus
}
#endif

#endif
