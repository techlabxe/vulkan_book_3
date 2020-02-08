#version 450

layout(vertices=16) out;

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

void main()
{
  if( gl_InvocationID == 0)
  {
    gl_TessLevelOuter[0] = tessOuterLevel;
    gl_TessLevelOuter[1] = tessOuterLevel;
    gl_TessLevelOuter[2] = tessOuterLevel;
	gl_TessLevelOuter[3] = tessOuterLevel;
	gl_TessLevelInner[0] = tessOuterLevel;
	gl_TessLevelInner[1] = tessOuterLevel;
  }

  gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
}
