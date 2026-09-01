#include "surface.hpp"

namespace Blit
{
   void SurfaceCluster::render(RenderTarget& target) const
   {
      blit_surface_cluster_render(&c, &target.raw(), position);
   }
}
