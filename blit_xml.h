/* Dinothawr - XML reading.
 *
 * The handful of things this core asks of an XML document: load one,
 * find the root by name, walk children and siblings by name, and read
 * an attribute as a string or an int. libretro-common's rxml does the
 * parsing; these are the lookups the game files need on top of its node
 * pointers.
 *
 * A node is borrowed - it points into the document and is only valid
 * while that document lives. A missing attribute reads as "" rather
 * than NULL, because every call site treats absent and empty the same
 * and checking both at each one buys nothing.
 *
 * MSVC C89.
 */

#ifndef BLIT_XML_H__
#define BLIT_XML_H__

#include <formats/rxml.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reads and parses @path through the frontend's VFS. NULL on a failed
 * read, a failed parse, or a document with no root element - the
 * callers' next step is always a lookup in the root, so an empty
 * document is a failed load as far as they are concerned. */
rxml_document_t *blit_xml_load(const char *path);

/* The root element if it is called @name, else NULL. Mirrors what a
 * document-level child() meant: the root, if it is the one expected. */
rxml_node_t *blit_xml_root(rxml_document_t *doc, const char *name);

/* First child of @node called @name, or NULL. */
rxml_node_t *blit_xml_child(rxml_node_t *node, const char *name);

/* Next sibling of @node called @name, or NULL. */
rxml_node_t *blit_xml_next(rxml_node_t *node, const char *name);

/* Immediate next sibling, whatever it is called, or NULL. */
rxml_node_t *blit_xml_next_any(rxml_node_t *node);

/* @node's @name attribute, or "" when absent. Never NULL. */
const char *blit_xml_attr(rxml_node_t *node, const char *name);

/* As above, read as a base-guessing integer. 0 when absent. */
int blit_xml_attr_int(rxml_node_t *node, const char *name);

#ifdef __cplusplus
}
#endif

#endif
