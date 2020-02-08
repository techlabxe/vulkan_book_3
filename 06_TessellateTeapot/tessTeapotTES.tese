#version 450

layout(quads,fractional_even_spacing,ccw) in;
layout(location=0) out vec4 outColor;

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

vec4 bernsteinBasis(float t)
{
  float invT = 1.0f - t;
  return vec4(invT * invT * invT,	// (1-t)3
    3.0f * t * invT * invT,			// 3t(1-t)2
    3.0f * t * t * invT,			// 3t2(1-t)
    t * t * t);						// t3
}

vec3 CubicInterpolate(vec3 p0, vec3 p1, vec3 p2, vec3 p3, vec4 t)
{
  return p0 * t.x + p1 * t.y + p2 * t.z + p3 * t.w;
}

vec3 CubicTangent(vec3 p1, vec3 p2, vec3 p3, vec3 p4, float t)
{
  float T0 = -1 + 2.0 * t - t * t;
  float T1 = 1.0 - 4 * t + 3 * t * t;
  float T2 = 2 * t - 3 * t * t;
  float T3 = t * t;

  return p1 * T0 * 3 + p2 * T1 * 3 + p3 * T2 * 3 + p4 * T3 * 3;
}

void main()
{
  mat4 pvw = proj * view * world;
  vec4 pos = vec4(0);

  vec4 basisU = bernsteinBasis(gl_TessCoord.x);
  vec4 basisV = bernsteinBasis(gl_TessCoord.y);

  vec3 bezpatch[16];
  for(int i=0;i<16;++i)
  {
    bezpatch[i] = gl_in[i].gl_Position.xyz;
  }

  vec3 q1 = CubicInterpolate(bezpatch[0],  bezpatch[1],  bezpatch[2],  bezpatch[3], basisU);
  vec3 q2 = CubicInterpolate(bezpatch[4],  bezpatch[5],  bezpatch[6],  bezpatch[7], basisU);
  vec3 q3 = CubicInterpolate(bezpatch[8],  bezpatch[9],  bezpatch[10], bezpatch[11], basisU);
  vec3 q4 = CubicInterpolate(bezpatch[12], bezpatch[13], bezpatch[14], bezpatch[15], basisU);

  vec3 localPos = CubicInterpolate(q1, q2, q3, q4, basisV);
  gl_Position = pvw * vec4(localPos, 1);

  vec3 r1 = CubicInterpolate(bezpatch[0], bezpatch[4], bezpatch[8],  bezpatch[12], basisV);
  vec3 r2 = CubicInterpolate(bezpatch[1], bezpatch[5], bezpatch[9],  bezpatch[13], basisV);
  vec3 r3 = CubicInterpolate(bezpatch[2], bezpatch[6], bezpatch[10], bezpatch[14], basisV);
  vec3 r4 = CubicInterpolate(bezpatch[3], bezpatch[7], bezpatch[11], bezpatch[15], basisV);

  vec3 tangent1 = CubicTangent(q1, q2, q3, q4, gl_TessCoord.y);
  vec3 tangent2 = CubicTangent(r1, r2, r3, r4, gl_TessCoord.x);
  vec3 normal = cross(tangent1, tangent2);
  if (length(normal) > 0.000001)
  {
    normal = normalize(normal);
  }
  else
  {
    if (localPos.y < 0.000)
      normal = vec3(0, -1, 0);
    else
      normal = vec3(0, 1, 0);
  }
  outColor.xyz = normal.xyz * 0.5 + 0.5;
  outColor.a = 1.0;
}
