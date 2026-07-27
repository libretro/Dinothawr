#ifndef XML_HPP__
#define XML_HPP__

#include <cstdlib>
#include <cstring>
#include <string>

#include <formats/rxml.h>

/* A thin, pugixml-shaped view over libretro-common's rxml.
 *
 * rxml exposes a C tree - name, data, an attribute list and children/next
 * pointers - and the call sites here want the handful of pugixml idioms
 * they already use: child(), next_sibling(), attribute().value() and a
 * truth test.  Wrapping rather than rewriting each consumer keeps the
 * port to what it actually is (one XML parser swapped for another)
 * instead of scattering linked-list walks through the game code.
 *
 * A Node is a non-owning pointer into a Document's tree, exactly as a
 * pugi::xml_node is; it is only valid while that Document lives.
 * attribute() on a missing attribute yields "" rather than NULL, which is
 * what pugixml does and what the call sites assume. */

namespace Blit
{
   namespace Xml
   {
      class Attribute
      {
         public:
            Attribute(const char *value) : v(value ? value : "") {}

            const char *value() const { return v; }
            int as_int() const { return static_cast<int>(std::strtol(v, NULL, 0)); }

         private:
            const char *v;
      };

      class Node
      {
         public:
            Node(rxml_node_t *node = NULL) : n(node) {}

            explicit operator bool() const { return n != NULL; }

            bool operator==(const Node& o) const { return n == o.n; }
            bool operator!=(const Node& o) const { return n != o.n; }

            const char *name() const { return (n && n->name) ? n->name : ""; }
            const char *data() const { return (n && n->data) ? n->data : ""; }

            /* First child with this name, or an empty node. */
            Node child(const char *name) const
            {
               rxml_node_t *c;
               if (!n)
                  return Node();
               for (c = n->children; c; c = c->next)
                  if (c->name && std::strcmp(c->name, name) == 0)
                     return Node(c);
               return Node();
            }

            /* Next sibling with this name, or the immediate next one. */
            Node next_sibling(const char *name) const
            {
               rxml_node_t *c;
               if (!n)
                  return Node();
               for (c = n->next; c; c = c->next)
                  if (c->name && std::strcmp(c->name, name) == 0)
                     return Node(c);
               return Node();
            }

            Node next_sibling() const
            {
               return Node(n ? n->next : NULL);
            }

            Attribute attribute(const char *name) const
            {
               return Attribute(n ? rxml_node_attrib(n, name) : NULL);
            }

         private:
            rxml_node_t *n;
      };

      /* Owns the parsed tree; every Node handed out points into it. */
      class Document
      {
         public:
            Document() : doc(NULL) {}
            ~Document() { if (doc) rxml_free_document(doc); }

            Document(const Document&) = delete;
            Document& operator=(const Document&) = delete;

            /* rxml reads through filestream, so this goes through the
             * frontend's VFS like every other read in the core. */
            bool load_file(const char *path)
            {
               if (doc)
                  rxml_free_document(doc);
               doc = rxml_load_document(path);
               return doc != NULL;
            }

            /* pugixml's document sits above the root element, so
             * doc.child("map") means "the root, if it is called map". */
            Node child(const char *name) const
            {
               rxml_node_t *root = doc ? rxml_root_node(doc) : NULL;
               if (root && root->name && std::strcmp(root->name, name) == 0)
                  return Node(root);
               return Node();
            }

         private:
            rxml_document_t *doc;
      };
   }
}

#endif
