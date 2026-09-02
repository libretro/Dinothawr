/* Dinothawr - XML reading (implementation).
 * MSVC C89. See blit_xml.h. */

#include "blit_xml.h"

#include <stdlib.h>
#include <string.h>

#include <streams/file_stream.h>

rxml_document_t *blit_xml_load(const char *path)
{
   rxml_document_t *doc = NULL;
   void            *buf = NULL;
   int64_t          len = 0;

   if (!path)
      return NULL;

   /* One bulk read with the sequential hint, still through filestream so
    * it goes via the frontend's VFS like every other read here. The
    * buffer already meets rxml_load_document_owned's contract - heap,
    * len + 1 bytes, NUL at [len] - so the document adopts it outright,
    * and rxml frees it itself if the parse fails. */
   if (!filestream_read_file(path, &buf, &len) || !buf)
      return NULL;

   doc = rxml_load_document_owned((char*)buf, (size_t)len);

   if (doc && !rxml_root_node(doc))
   {
      rxml_free_document(doc);
      doc = NULL;
   }

   return doc;
}

rxml_node_t *blit_xml_root(rxml_document_t *doc, const char *name)
{
   rxml_node_t *root = doc ? rxml_root_node(doc) : NULL;

   if (root && root->name && name && strcmp(root->name, name) == 0)
      return root;

   return NULL;
}

rxml_node_t *blit_xml_child(rxml_node_t *node, const char *name)
{
   rxml_node_t *child;

   if (!node || !name)
      return NULL;

   for (child = node->children; child; child = child->next)
      if (child->name && strcmp(child->name, name) == 0)
         return child;

   return NULL;
}

rxml_node_t *blit_xml_next(rxml_node_t *node, const char *name)
{
   rxml_node_t *sibling;

   if (!node || !name)
      return NULL;

   for (sibling = node->next; sibling; sibling = sibling->next)
      if (sibling->name && strcmp(sibling->name, name) == 0)
         return sibling;

   return NULL;
}

rxml_node_t *blit_xml_next_any(rxml_node_t *node)
{
   return node ? node->next : NULL;
}

const char *blit_xml_attr(rxml_node_t *node, const char *name)
{
   const char *value = node ? rxml_node_attrib(node, name) : NULL;
   return value ? value : "";
}

int blit_xml_attr_int(rxml_node_t *node, const char *name)
{
   return (int)strtol(blit_xml_attr(node, name), NULL, 0);
}
