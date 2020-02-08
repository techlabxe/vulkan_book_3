#version 450

layout(triangles) in;
layout(line_strip, max_vertices = 2) out;

layout(location=0) out vec3 outColor;

layout(set=0, binding=0)
uniform SceneParameters
{
  mat4  world;
  mat4  view;
  mat4  proj;
  vec4  lightDir;
};

in gl_PerVertex
{
  vec4 gl_Position;
} gl_in[];


void main()
{
  vec3 v0 = gl_in[0].gl_Position.xyz;
  vec3 v1 = gl_in[1].gl_Position.xyz;
  vec3 v2 = gl_in[2].gl_Position.xyz;

  vec3 e1 = normalize(v1 - v0);
  vec3 e2 = normalize(v2 - v0);

  vec3 normal = normalize(cross(e1, e2));
  vec3 center = (v0 + v1 + v2) / 3;

  mat4 pvw = proj * view * world;

  vec4 pos = vec4(center,1);
  gl_Position = pvw * pos;
  outColor = vec3(0,0,1);
  EmitVertex();

  pos.xyz += normal * 0.1;
  gl_Position = pvw * pos;
  outColor = vec3(0,0,1);
  EmitVertex();

  EndPrimitive();
}
