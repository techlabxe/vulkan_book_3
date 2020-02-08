#version 450

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

layout(location=0) in vec3 inNormal[];
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

  // 面を構成する２辺を求める.
  vec3 e1 = normalize(v1 - v0);
  vec3 e2 = normalize(v2 - v0);

  // 外積により面法線を求める.  
  vec3 normal = normalize(cross(e1, e2));

  // 求めた法線とでライティング計算.
  float nl = dot(normal, normalize(lightDir.xyz));

  mat4 pvw = proj * view * world;
  for(int i=0; i < gl_in.length(); +++i)
  {
	gl_Position = pvw * gl_in[i].gl_Position;
    outColor = clamp(nl, 0, 1).xxx;
	EmitVertex();
  }
  EndPrimitive();
}
