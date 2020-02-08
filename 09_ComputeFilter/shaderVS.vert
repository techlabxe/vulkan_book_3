#version 450

layout(location=0) in vec4 inPos;
layout(location=1) in vec2 inUV;

layout(location=0) out vec2 outUV;

layout(set=0, binding=0)
uniform ShaderParameters
{
  mat4 proj;
};

out gl_PerVertex
{
  vec4 gl_Position;
};

void main()
{
  gl_Position = proj * inPos;
  outUV = inUV;
}
