float3 GeneratedProgram(float2 uv)
{
	float3 s0 = 0;
	{ float4 c = code[0]; int idx = 0; int components = 1; float3 a = float3(0, 0, 0), b = float3(0, 0, 0), d = float3(0, 0, 0); float3 r = 0; r = ReadTexture(0, uv); s0 = r; }
	return s0;
}

float4 PSGeneratedProgram(VSOut i) : SV_Target
{
	float3 result = GeneratedProgram(i.uv);
	return float4(result.xxx, 1);
}
