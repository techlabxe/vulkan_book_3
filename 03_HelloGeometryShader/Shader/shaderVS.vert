#version 450

layout(location=0) in vec4 inPos;
layout(location=1) in vec3 inNormal;

layout(location=0) out vec4 outColor;

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
  gl_Position = proj * view * world * inPos;
  vec3 worldNormal = mat3(world) * inNormal;
  float nl = dot(worldNormal, normalize(lightDir.xyz));
  float l = clamp(nl, 0, 1);
  outColor = vec4(l,l,l, 1); 
}
