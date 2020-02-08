#version 450

layout(location=0) in vec4 inPos;
layout(location=1) in vec3 inNormal;

layout(location=0) out vec3 outNormal;

out gl_PerVertex
{
  vec4 gl_Position;
};

layout(set=0, binding=0)
uniform SceneParameters
{
  mat4  world;
  mat4  view;
  mat4  proj;
  vec4  lightDir;
};

void main()
{
  gl_Position = inPos;
  outNormal = mat3(world) * inNormal;
}
