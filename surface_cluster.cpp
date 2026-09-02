#include "surface_cluster.hpp"

namespace Blit
{
   void SurfaceCluster::render(blit_render_target_t& target) const
   {
      blit_surface_cluster_render(&c, &target, position);
   }
}
