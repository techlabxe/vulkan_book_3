#version 450

layout(location=0) in vec3 inColor;

layout(location=0) out vec4 outColor;

layout(set=0, binding=0)
uniform SceneParameters
{
  mat4  world;
  mat4  view;
  mat4  proj;
  vec4  lightPos;
  vec4  cameraPos;
};

void main()
{
//  vec3 toEye = normalize(cameraPos.xyz - inWorldPos.xyz);
//  vec3 toLight = normalize(lightPos.xyz);
//  vec3 halfVector = normalize(toEye + toLight);
//  float val = clamp(dot(halfVector, normalize(inNormal)), 0, 1);
//
//  float shininess = 20;
//  float specular = pow(val, shininess);
//
//  vec4 color = inColor;
//  color.rgb += specular;

  outColor = vec4(inColor, 1);
}
