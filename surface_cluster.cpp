#include "surface.hpp"

namespace Blit
{
   std::vector<SurfaceCluster::Elem>& SurfaceCluster::vec()
   {
      return elems;
   }

   const std::vector<SurfaceCluster::Elem>& SurfaceCluster::vec() const
   {
      return elems;
   }

   void SurfaceCluster::render(RenderTarget& target) const
   {
      std::vector<Elem>::const_iterator surf;

      for (surf = elems.begin(); surf != elems.end(); ++surf)
         target.blit_offset(surf->surf, blit_rect_zero(),
               position + surf->offset);
   }
}
