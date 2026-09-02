#ifndef XML_HPP__
#define XML_HPP__

#include <cstdlib>
#include <cstring>
#include <string>

#include "blit_xml.h"

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
      /* Thin C++ shims over blit_xml.h, kept while the call sites still
       * read like pugixml. Each one is a forward; they go as the callers
       * convert. */
      class Attribute
      {
         public:
            Attribute(const char *value) : v(value ? value : "") {}

            const char *value() const { return v; }
            int as_int() const { return (int)std::strtol(v, NULL, 0); }

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

            Node child(const char *name) const
            { return Node(blit_xml_child(n, name)); }

            Node next_sibling(const char *name) const
            { return Node(blit_xml_next(n, name)); }

            Node next_sibling() const
            { return Node(blit_xml_next_any(n)); }

            Attribute attribute(const char *name) const
            { return Attribute(blit_xml_attr(n, name)); }

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

            bool load_file(const char *path)
            {
               if (doc)
               {
                  rxml_free_document(doc);
                  doc = NULL;
               }

               doc = blit_xml_load(path);
               return doc != NULL;
            }

            Node child(const char *name) const
            { return Node(blit_xml_root(doc, name)); }

         private:
            rxml_document_t *doc;
      };
   }
}

#endif
