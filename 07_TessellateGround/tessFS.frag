#version 450

layout(location=0) in vec4 inColor;
layout(location=1) in vec3 inNormal;
layout(location=0) out vec4 outColor;

void main()
{
  vec3 lightDir = normalize(vec3(1,1,1));
  float lmb = clamp(dot(normalize(inNormal), lightDir), 0, 1);

  outColor = inColor;
  outColor = vec4( lmb, lmb, lmb, 1);
}
