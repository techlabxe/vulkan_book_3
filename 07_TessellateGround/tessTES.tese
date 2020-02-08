#version 450

layout(quads,fractional_even_spacing, ccw) in;

layout(location=0) in vec2 inUV[];

layout(location=0) out vec4 outColor;
layout(location=1) out vec3 outNormal;

layout(set=0, binding=0)
uniform TessShaderParameters
{
  mat4 world;
  mat4 view;
  mat4 proj;
  vec4 lightPos;
  vec4 cameraPos;
  float tessOuterLevel;
  float tessInnerLevel;
};
layout(set=0, binding=1)
uniform sampler2D texSampler;
layout(set=0, binding=2)
uniform sampler2D normalSampler;

out gl_PerVertex
{
  vec4 gl_Position;
};

void main()
{
  vec4 pos = vec4(0);
  vec2 uv = vec2(0);

  vec3 domain = gl_TessCoord;
  vec4 p0 = mix(gl_in[0].gl_Position, gl_in[1].gl_Position, domain.x);
  vec4 p1 = mix(gl_in[2].gl_Position, gl_in[3].gl_Position, domain.x);
  pos = mix(p0, p1, domain.y);

  vec2 uv0 = mix(inUV[0], inUV[1], domain.x);
  vec2 uv1 = mix(inUV[2], inUV[3], domain.x);
  uv = mix(uv0, uv1, domain.y);

  // ハイトマップを参照して頂点位置を変更.
  float height = texture(texSampler, uv).x;
  vec3  normal = normalize(texture(normalSampler, uv).xyz - 0.5);

  pos.y += height*25;

  gl_Position = proj * view * world * pos;
  outColor = vec4(uv, 0, 1);
  outColor = vec4(normal.xyz*0.5+0.5, 1);

  outNormal = mat3(world) * normal;
}
