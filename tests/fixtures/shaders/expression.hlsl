float3 GeneratedProgram(float2 uv)
{
	float3 s0 = 0;
	float3 s1 = 0;
	float3 s2 = 0;
	float3 s3 = 0;
	{ float number = NumberAt(0); int idx = 0; int components = 1; float3 a = float3(0, 0, 0), b = float3(0, 0, 0), d = float3(0, 0, 0); float3 r = 0; r = ReadTexture(0, uv); s0 = r; }
	{ float number = NumberAt(1); int idx = 0; int components = 1; float3 a = float3(0, 0, 0), b = float3(0, 0, 0), d = float3(0, 0, 0); float3 r = 0; r = number; s1 = r; }
	{ float number = NumberAt(2); int idx = 0; int components = 1; float3 a = s1, b = s0, d = float3(0, 0, 0); float3 r = 0; r = SafePow(b, a); s0 = r; }
	{ float number = NumberAt(3); int idx = 1; int components = 1; float3 a = float3(0, 0, 0), b = float3(0, 0, 0), d = float3(0, 0, 0); float3 r = 0; r = programInputs[idx].value; s1 = r; }
	{ float number = NumberAt(4); int idx = 0; int components = 1; float3 a = s1, b = s0, d = float3(0, 0, 0); float3 r = 0; r = b * a; s0 = r; }
	{ float number = NumberAt(5); int idx = 0; int components = 1; float3 a = float3(0, 0, 0), b = float3(0, 0, 0), d = float3(0, 0, 0); float3 r = 0; r = ReadTexture(0, uv); s1 = r; }
	{ float number = NumberAt(6); int idx = 0; int components = 1; float3 a = float3(0, 0, 0), b = float3(0, 0, 0), d = float3(0, 0, 0); float3 r = 0; r = number; s2 = r; }
	{ float number = NumberAt(7); int idx = 1; int components = 1; float3 a = float3(0, 0, 0), b = float3(0, 0, 0), d = float3(0, 0, 0); float3 r = 0; r = programInputs[idx].value; s3 = r; }
	{ float number = NumberAt(8); int idx = 0; int components = 1; float3 a = s3, b = s2, d = s1; float3 r = 0; r = lerp(d, b, a); s1 = r; }
	{ float number = NumberAt(9); int idx = 0; int components = 1; float3 a = s1, b = s0, d = float3(0, 0, 0); float3 r = 0; r = b + a; s0 = r; }
	{ float number = NumberAt(10); int idx = 0; int components = 1; float3 a = s0, b = float3(0, 0, 0), d = float3(0, 0, 0); float3 r = 0; r = saturate(a); s0 = r; }
	return s0;
}

float4 PSGeneratedProgram(VSOut i) : SV_Target
{
	float3 result = GeneratedProgram(i.uv);
	return float4(result.xxx, 1);
}
