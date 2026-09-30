float3 GeneratedProgram(float2 uv)
{
	float3 s0 = 0;
	float3 s1 = 0;
	{ float4 c = code[0]; int idx = 0; int components = 3; float3 a = float3(0, 0, 0), b = float3(0, 0, 0), d = float3(0, 0, 0); float3 r = 0; r = ReadTexture(0, uv); s0 = r; }
	{ float4 c = code[1]; int idx = 1; int components = 3; float3 a = float3(0, 0, 0), b = float3(0, 0, 0), d = float3(0, 0, 0); float3 r = 0; r = ReadTexture(1, uv); s1 = r; }
	{ float4 c = code[2]; int idx = 0; int components = 3; float3 a = s1, b = s0, d = float3(0, 0, 0); float3 r = 0; r = b - a; s0 = r; }
	{ float4 c = code[3]; int idx = 0; int components = 3; float3 a = s0, b = float3(0, 0, 0), d = float3(0, 0, 0); float3 r = 0; r = abs(a); s0 = r; }
	return s0;
}

float4 PSGenerated(VSOut i) : SV_Target
{
	float3 result = GeneratedProgram(i.uv);
	return float4(result, 1);
}
