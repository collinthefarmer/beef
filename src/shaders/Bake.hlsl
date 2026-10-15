struct BakeIn  { float2 uv : TEXCOORD0; float3 value : COLOR0; };
struct BakeOut { float4 pos : SV_Position; float3 value : COLOR0; };

BakeOut BakeVS(BakeIn i)
{
	BakeOut o;
	o.pos = float4(i.uv.x * 2 - 1, 1 - i.uv.y * 2, 0, 1);
	o.value = i.value;
	return o;
}

float4 BakePS(BakeOut i) : SV_Target
{
	return float4(i.value, 1);
}
