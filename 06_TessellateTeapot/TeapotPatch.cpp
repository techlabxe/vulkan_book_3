#include "TeapotPatch.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <Windows.h>

using namespace TeapotPatch;

#include "TeapotPatch2.inc"

std::vector<vec3> TeapotPatch::GetTeapotPatchPoints()
{
  std::vector<vec3> controls(teapot_points, teapot_points + _countof(teapot_points));
  return controls;
}

std::vector<unsigned int> TeapotPatch::GetTeapotPatchIndices()
{
  std::vector<unsigned int> indices(teapot_patches, teapot_patches + _countof(teapot_patches));
  return indices;
}


