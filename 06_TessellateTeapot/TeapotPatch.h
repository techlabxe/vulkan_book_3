#pragma once

#include <glm/glm.hpp>
#include <vector>

namespace TeapotPatch
{
  using glm::vec3;

  struct ControlPoint {
    vec3 Position;
  };

  std::vector<vec3> GetTeapotPatchPoints();
  std::vector<unsigned int> GetTeapotPatchIndices();
}
