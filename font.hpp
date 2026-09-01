#ifndef FONT_HPP__
#define FONT_HPP__

#include "surface.hpp"
#include <map>

namespace Blit
{
   class Font
   {
      public:
         Font();
         Font(const std::string& font);

         const Surface& surface(char c) const;
         Pos glyph_size() const { return blit_pos(glyphwidth, glyphheight); }

         enum RenderAlignment
         {
            Left = 0,
            Right,
            Centered
         };

         void render_msg(RenderTarget& target, const std::string& msg, int x, int y,
               RenderAlignment dir, int newline_offset) const;

         void set_color(Pixel pix);

      private:
         /* Indexed by character rather than keyed by one: a font has at
          * most 256 glyphs, the lookup runs per character rendered, and
          * a flat array makes it an index instead of a tree walk. The
          * present flags say which slots were filled. */
         Surface surf_map[256];
         bool    surf_present[256];
         int glyphwidth, glyphheight;
         int adjust_x(const std::string& str, Font::RenderAlignment dir) const;
   };

   class FontCluster
   {
      public:
         FontCluster()
         {
         }

         Pos glyph_size() const;

         void add_font(const std::string& font, Pos offset, Pixel color, std::string id = "");
         void set_id(std::string id);
         void render_msg(RenderTarget& target, const std::string& msg, int x, int y,
               Font::RenderAlignment dir = Font::Left, int newline_offset = 0) const;

      private:
         struct OffsetFont : public Font
         {
            OffsetFont() : offset(blit_pos_zero())
            {
            }
            OffsetFont(const std::string& font);

            void render_msg(RenderTarget& target, const std::string& msg, int x, int y,
                  Font::RenderAlignment dir, int newline_offset) const;
            Pos offset;
         };

         std::map<std::string, std::vector<OffsetFont>> fonts_map;
         std::string current_id;

         static bool func_x (const OffsetFont& a, const OffsetFont& b);
         static bool func_y(const OffsetFont& a, const OffsetFont& b);
   };
}

#endif

