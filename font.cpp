#include "font.hpp"
#include <new>
#include <cstring>
#include "xml.hpp"
#include "utils.hpp"

#include <stdexcept>

using namespace std;

namespace Blit
{
   /* The glyph slots are raw C surfaces, so they need initialising by
    * hand where the Surface array used to do it in its constructors. */
   void Font::clear_glyphs()
   {
      unsigned i;
      for (i = 0; i < 256; i++)
      {
         blit_surface_init(&surf_map[i]);
         surf_present[i] = false;
      }
   }

   Font::Font() : glyphwidth(0), glyphheight(0)
   {
      clear_glyphs();
   }

   Font::Font(const Font& other)
      : glyphwidth(other.glyphwidth), glyphheight(other.glyphheight)
   {
      unsigned i;
      for (i = 0; i < 256; i++)
      {
         surf_map[i]     = other.surf_map[i];
         surf_present[i] = other.surf_present[i];
         if (surf_present[i])
            blit_surface_retain(&surf_map[i]);
      }
   }

   Font& Font::operator=(const Font& other)
   {
      if (this != &other)
      {
         unsigned i;

         /* Retain before release: assigning from a Font sharing our
          * glyphs would otherwise free them first. */
         for (i = 0; i < 256; i++)
            if (other.surf_present[i])
               blit_surface_retain(&other.surf_map[i]);

         for (i = 0; i < 256; i++)
            if (surf_present[i])
               blit_surface_release(&surf_map[i]);

         for (i = 0; i < 256; i++)
         {
            surf_map[i]     = other.surf_map[i];
            surf_present[i] = other.surf_present[i];
         }

         glyphwidth  = other.glyphwidth;
         glyphheight = other.glyphheight;
      }
      return *this;
   }

   Font::~Font()
   {
      unsigned i;
      for (i = 0; i < 256; i++)
         if (surf_present[i])
            blit_surface_release(&surf_map[i]);
   }

   Font::Font(const string& font) : glyphwidth(0), glyphheight(0)
   {
      string dir = Utils::basedir(font);

      clear_glyphs();

      xml_doc doc;
      if (!doc.load(font.c_str()))
         throw runtime_error(Utils::join("Failed to load font: ", font, "."));

      rxml_node_t *glyph = blit_xml_child(blit_xml_root(doc.get(), "font"),
            "glyphs");
      char start_ascii = blit_xml_attr_int(glyph, "startascii");

      int width   = blit_xml_attr_int(glyph, "width");
      int height  = blit_xml_attr_int(glyph, "height");
      glyphwidth  = blit_xml_attr_int(glyph, "glyphwidth");
      glyphheight = blit_xml_attr_int(glyph, "glyphheight");
      const char * source = blit_xml_attr(glyph, "source");

      if (!width || !height || !glyphwidth || !glyphheight)
         throw logic_error("Invalid glpyh arguments.");

      blit_surface_t surf = Blit::surface_cache().from_image(
            Utils::join(dir, "/", source));

      if (surf.rect.w != width * glyphwidth || surf.rect.h != height * glyphheight)
      {
         blit_surface_release(&surf);
         throw logic_error("Geometry of font and attributes do not match.");
      }

      for (int y = 0; y < height; y++)
      {
         for (int x = 0; x < width; x++, start_ascii++)
         {
            {
               unsigned char  slot  = (unsigned char)start_ascii;
               blit_surface_t glyph = surface_sub(surf,
                     blit_rect(blit_pos(x * glyphwidth, y * glyphheight),
                        glyphwidth, glyphheight));

               glyph.ignore_camera = 1;

               /* surface_sub hands over ownership, so this takes it
                * rather than copying and retaining. */
               surf_map[slot]     = glyph;
               surf_present[slot] = true;
            }
         }
      }

      blit_surface_release(&surf);
   }

   const blit_surface_t& Font::surface(char c) const
   {
      if (!surf_present[(unsigned char)c])
         throw logic_error(Utils::join("Character '", c, "' not found in font."));

      return surf_map[(unsigned char)c];
   }

   void Font::set_color(Pixel pix)
   {
      unsigned i;
      for (i = 0; i < 256; i++)
         if (surf_present[i] && !blit_surface_refill_color(&surf_map[i], pix))
            throw std::bad_alloc();
   }

   void Font::render_msg(blit_render_target_t& target, const string& str, int x, int y,
         Font::RenderAlignment dir,
         int newline_offset) const
   {
      int orig_x = x;

      std::vector<std::basic_string<char> > lines = Utils::split(str, '\n');
      for (std::vector<std::string>::iterator line = lines.begin(); line!=lines.end(); line++ )
      {
         x -= Font::adjust_x(*line, dir);
         for (std::string::iterator c = line->begin(); c!=line->end(); c++)
         {
            blit_render_target_blit_offset(&target, &surface(*c), blit_rect_zero(),
                  blit_pos(x, y));
            x += glyphwidth;
         }
         y += glyphheight + newline_offset;
         x = orig_x;
      }
   }

   int Font::adjust_x(const string& str, Font::RenderAlignment dir) const
   {
      if (dir == RenderAlignment::Right)
         return glyphwidth * str.size();
      if (dir == RenderAlignment::Centered)
         return glyphwidth * str.size() / 2;
      else return 0;
   }
   
   void FontCluster::add_font(const string& font, Pos offset, Pixel color, string id)
   {
      std::vector<OffsetFont>& fonts = fonts_map[std::move(id)];

      OffsetFont tmp(font);
      tmp.set_color(color);
      tmp.offset = offset;

      fonts.push_back(std::move(tmp));
   }

   void FontCluster::set_id(string id)
   {
      current_id = std::move(id);
   }

   bool FontCluster::func_x (const OffsetFont& a, const OffsetFont& b) {
      return a.glyph_size().x < b.glyph_size().x;
   }

   bool FontCluster::func_y (const OffsetFont& a, const OffsetFont& b) {
      return a.glyph_size().y < b.glyph_size().y;
   }

   Pos FontCluster::glyph_size() const
   {
      std::map<std::string, std::vector<OffsetFont>>::const_iterator itr = fonts_map.find(current_id);
      if (itr == fonts_map.end())
         throw runtime_error(Utils::join("Font ID: ", current_id, " not found in map!"));

      const std::vector<OffsetFont>& fonts = itr->second;

      std::vector<OffsetFont>::const_iterator max_x = max_element(fonts.begin(), fonts.end(), func_x);
      std::vector<OffsetFont>::const_iterator max_y = max_element(fonts.begin(), fonts.end(), func_y);

      return blit_pos(max_x->glyph_size().x, max_y->glyph_size().y);
   }

   void FontCluster::render_msg(blit_render_target_t& target, const string& msg,
         int x, int y,
         Font::RenderAlignment dir,
         int newline_offset) const
   {
      std::map<std::string, std::vector<OffsetFont>>::const_iterator itr = fonts_map.find(current_id);
      if (itr == fonts_map.end())
         throw runtime_error(Utils::join("Font ID: ", current_id, " not found in map!"));

      for (std::vector<OffsetFont>::const_iterator font = itr->second.begin(); font != itr->second.end(); font++)
         font->render_msg(target, msg, x, y, dir, newline_offset);
   }

   FontCluster::OffsetFont::OffsetFont(const string& font) : Font(font)
   {}

   void FontCluster::OffsetFont::render_msg(blit_render_target_t& target, const string& msg,
         int x, int y,
         Font::RenderAlignment dir,
         int newline_offset) const
   {
      Font::render_msg(target, msg, x + offset.x, y + offset.y, dir, newline_offset);
   }
}

