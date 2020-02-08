#version 450

layout(location=0) in vec4 inPos;

layout(set=0, binding=0)
uniform TesseSceneParameters
{
  mat4 world;
  mat4 view;
  mat4 proj;
  vec4 lightPos;
  vec4 cameraPos;
  float tessOuterLevel;
  float tessInnerLevel;
};

out gl_PerVertex
{
  vec4 gl_Position;
};

void main()
{
  gl_Position = inPos;
}
