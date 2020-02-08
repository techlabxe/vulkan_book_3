#version 450

layout(vertices=4) out;

layout(location=0) in vec2 inUV[];
layout(location=0) out vec2 outUV[];

in gl_PerVertex
{
  vec4 gl_Position;
} gl_in[gl_MaxPatchVertices];

layout(set=0, binding=0)
uniform TessShaderParameters
{
  mat4 world;
  mat4 view;
  mat4 proj;
  vec4 lightPos;
  vec4 cameraPos;
};

layout(set=0, binding=1)
uniform sampler2D texSampler;
layout(set=0, binding=2)
uniform sampler2D normalSampler;

float CalcTessFactor(vec4 v)
{
  float tessNear = 2.0;
  float tessFar = 150;

  float dist = length((world * v).xyz - cameraPos.xyz);
  const float MaxTessFactor = 32.0;
  float val = MaxTessFactor - (MaxTessFactor - 1) * (dist - tessNear) / (tessFar - tessNear);
  val = clamp(val, 1, MaxTessFactor);
  return val;
}

float CalcNormalBias(vec4 p, vec3 n)
{
  const float normalThreshold = 0.85; // –ñ60“x.
  vec3 camPos = cameraPos.xyz;
  vec3 fromCamera = normalize(p.xyz - camPos);
  float cos2 = dot(n, fromCamera);
  cos2 *= cos2;
  float normalFactor = 1.0 - cos2;
  float bias = max(normalFactor - normalThreshold, 0) / (1.0 - normalThreshold);
  return bias * 32;
}

void ComputeTessLevel()
{
  vec4 v[4];
  vec3 n[4];
  int indices[][2] = {
   { 2, 0 }, {0, 1}, {1, 3}, { 2, 3 }
  };
  for(int i=0;i<4;++i)
  {
    int idx0 = indices[i][0];
	int idx1 = indices[i][1];
	v[i] = 0.5 * (gl_in[idx0].gl_Position + gl_in[idx1].gl_Position);

	vec2 uv = 0.5 * (inUV[idx0] + inUV[idx1]);
	n[i] = texture(normalSampler, uv).xyz;
	n[i] = normalize(n[i] - 0.5);
  }

  gl_TessLevelOuter[0] = CalcTessFactor(v[0]);
  gl_TessLevelOuter[2] = CalcTessFactor(v[2]);
  gl_TessLevelOuter[0] += CalcNormalBias(v[0], n[0]);
  gl_TessLevelOuter[2] += CalcNormalBias(v[2], n[2]);
  gl_TessLevelInner[0] = 0.5 * (gl_TessLevelOuter[0] + gl_TessLevelOuter[2]);

  gl_TessLevelOuter[1] = CalcTessFactor(v[1]);
  gl_TessLevelOuter[3] = CalcTessFactor(v[3]);
  gl_TessLevelOuter[1] += CalcNormalBias(v[1], n[1]);
  gl_TessLevelOuter[3] += CalcNormalBias(v[3], n[3]);
  gl_TessLevelInner[1] = 0.5 * (gl_TessLevelOuter[1] + gl_TessLevelOuter[3]);


/*
  vec4 pos = gl_in[0].gl_Position + gl_in[1].gl_Position + gl_in[2].gl_Position + gl_in[3].gl_Position;
  pos = pos * 0.25;
  pos = world * vec4(pos.xyz, 1);
  float dist = length(pos.xyz - cameraPos.xyz);
  float tessNear = 2.0;
  float tessFar = 150;
  float val = 32.0 - (32.0f - 1) * (dist - tessNear) / (tessFar - tessNear);
  val = clamp(val, 1, 32);
  gl_TessLevelOuter[0] = val;
  gl_TessLevelOuter[1] = val;
  gl_TessLevelOuter[2] = val;
  gl_TessLevelOuter[3] = val;
  gl_TessLevelInner[0] = val;
  gl_TessLevelInner[1] = val;
*/
}

void main()
{
  if(gl_InvocationID == 0)
  {
    ComputeTessLevel();
  }
  gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
  outUV[gl_InvocationID] = inUV[gl_InvocationID];
}
