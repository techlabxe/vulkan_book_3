#version 450

layout(location=0) in vec3 inColor;
layout(location=0) out vec4 outColor;

layout(set=0, binding=0)
uniform CubemapEnvParameters
{
  mat4 world[6];
  vec4 colors[6];
  vec4 lightPos;
  vec4 cameraPos;
};

layout(set=0, binding=1)
uniform ViewMatrices
{
  mat4 view[6];
  mat4 proj;
};

void main()
{
  outColor = vec4(inColor, 1);
}
