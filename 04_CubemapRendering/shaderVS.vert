#version 450

layout(location=0) in vec4 inPos;
layout(location=1) in vec3 inNormal;

layout(location=0) out vec3 outColor;
layout(location=1) out vec3 outNormal;
layout(location=2) out vec4 outWorldPos;


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
  float l = dot(worldNormal, normalize(lightDir.xyz)) * 0.5 + 0.5;
  outColor = vec3(l);
  outNormal = worldNormal;
  outWorldPos = world * inPos;
}
