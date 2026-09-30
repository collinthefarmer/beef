float3 StackField0(float2 uv)
{
	float3 s0 = 0;
	float3 s1 = 0;
	{ float4 c = code[0]; int idx = 0; int components = 1; float3 a = float3(0, 0, 0), b = float3(0, 0, 0), d = float3(0, 0, 0); float3 r = 0; r = ReadTexture(0, uv); s0 = r; }
	{ float4 c = code[1]; int idx = 0; int components = 1; float3 a = float3(0, 0, 0), b = float3(0, 0, 0), d = float3(0, 0, 0); float3 r = 0; r = c.y; s1 = r; }
	{ float4 c = code[2]; int idx = 0; int components = 1; float3 a = s1, b = s0, d = float3(0, 0, 0); float3 r = 0; r = b * a; s0 = r; }
	{ float4 c = code[3]; int idx = 1; int components = 1; float3 a = float3(0, 0, 0), b = float3(0, 0, 0), d = float3(0, 0, 0); float3 r = 0; r = refValues[idx].xyz; s1 = r; }
	{ float4 c = code[4]; int idx = 0; int components = 1; float3 a = s1, b = s0, d = float3(0, 0, 0); float3 r = 0; r = b + a; s0 = r; }
	{ float4 c = code[5]; int idx = 0; int components = 1; float3 a = s0, b = float3(0, 0, 0), d = float3(0, 0, 0); float3 r = 0; r = sin(a); s0 = r; }
	{ float4 c = code[6]; int idx = 0; int components = 1; float3 a = float3(0, 0, 0), b = float3(0, 0, 0), d = float3(0, 0, 0); float3 r = 0; r = c.y; s1 = r; }
	{ float4 c = code[7]; int idx = 0; int components = 1; float3 a = s1, b = s0, d = float3(0, 0, 0); float3 r = 0; r = b * a; s0 = r; }
	{ float4 c = code[8]; int idx = 0; int components = 1; float3 a = float3(0, 0, 0), b = float3(0, 0, 0), d = float3(0, 0, 0); float3 r = 0; r = c.y; s1 = r; }
	{ float4 c = code[9]; int idx = 0; int components = 1; float3 a = s1, b = s0, d = float3(0, 0, 0); float3 r = 0; r = b + a; s0 = r; }
	{ float4 c = code[10]; int idx = 0; int components = 3; float3 a = s0, b = float3(0, 0, 0), d = float3(0, 0, 0); float3 r = 0; r = a.xxx; s0 = r; }
	{ float4 c = code[11]; int idx = 0; int components = 3; float3 a = s0, b = float3(0, 0, 0), d = float3(0, 0, 0); float3 r = 0; r = round(saturate(a) * 255) / 255; s0 = r; }
	return s0;
}

float3 StackField1(float2 uv)
{
	float3 s0 = 0;
	float3 s1 = 0;
	float3 s2 = 0;
	{ float4 c = code[12]; int idx = 2; int components = 1; float3 a = float3(0, 0, 0), b = float3(0, 0, 0), d = float3(0, 0, 0); float3 r = 0; r = ReadTexture(1, uv); s0 = r; }
	{ float4 c = code[13]; int idx = 3; int components = 1; float3 a = float3(0, 0, 0), b = float3(0, 0, 0), d = float3(0, 0, 0); float3 r = 0; r = refValues[idx].xyz; s1 = r; }
	{ float4 c = code[14]; int idx = 0; int components = 1; float3 a = float3(0, 0, 0), b = float3(0, 0, 0), d = float3(0, 0, 0); float3 r = 0; r = c.y; s2 = r; }
	{ float4 c = code[15]; int idx = 0; int components = 1; float3 a = s2, b = s1, d = float3(0, 0, 0); float3 r = 0; r = b * a; s1 = r; }
	{ float4 c = code[16]; int idx = 0; int components = 1; float3 a = s1, b = s0, d = float3(0, 0, 0); float3 r = 0; r = b - a; s0 = r; }
	{ float4 c = code[17]; int idx = 0; int components = 1; float3 a = s0, b = float3(0, 0, 0), d = float3(0, 0, 0); float3 r = 0; r = saturate(a); s0 = r; }
	{ float4 c = code[18]; int idx = 0; int components = 3; float3 a = s0, b = float3(0, 0, 0), d = float3(0, 0, 0); float3 r = 0; r = a.xxx; s0 = r; }
	{ float4 c = code[19]; int idx = 0; int components = 3; float3 a = s0, b = float3(0, 0, 0), d = float3(0, 0, 0); float3 r = 0; r = round(saturate(a) * 255) / 255; s0 = r; }
	return s0;
}

float4 PSGeneratedStack(VSOut i) : SV_Target
{
	float2 rawUv = i.uv;
	float4 below = stackBase.SampleLevel(samp, rawUv, 0);
	{
		float4 s = 0;
		float4 m = 0;
		s = float4(StackField0(rawUv), 1);
		float3 value = LayerValue(stackColor[0], true, s, 4);
		m = float4(StackField1(rawUv), 1);
		float4 result = ComposeLayer(below, value, stackLayer[0].w, true, m, 4, 2, 15);
		below = result;
	}
	return below;
}
