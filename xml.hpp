#ifndef XML_HPP__
#define XML_HPP__

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

/* Owns a parsed document; every node handed out points into it and is
 * only valid while it lives. The lookups themselves are blit_xml.h -
 * this is just the lifetime, which C++ call sites still want. */
class xml_doc
{
   public:
      xml_doc() : doc(NULL) {}
      ~xml_doc() { if (doc) rxml_free_document(doc); }

      xml_doc(const xml_doc&) = delete;
      xml_doc& operator=(const xml_doc&) = delete;

      bool load(const char *path)
      {
         if (doc)
            rxml_free_document(doc);
         doc = blit_xml_load(path);
         return doc != NULL;
      }

      rxml_document_t *get() const { return doc; }

   private:
      rxml_document_t *doc;
};

#endif
