struct RippleFiringRow
{
	float3 origin;
	float  radius;
};

cbuffer RippleParams : register(b2)
{
	RippleFiringRow rippleFirings[kRippleFiringRows];
	float4          rippleStrengths[kRippleFiringRows / 4];
	float3          rippleDirection;
	uint            rippleFiringCount;
	float           rippleWidth;
	float           rippleFrame;
	bool            rippleDisc;
	bool            rippleDirectional;
};

float4 PSRipple(VSOut i) : SV_Target
{
	float3 pos = (singleSource.SampleLevel(linearSampler, i.uv, 0).rgb * 2 - 1) * rippleFrame;
	float  v = 0;
	int    n = (int)min(rippleFiringCount, kRippleFiringRows);
	float  width = max(rippleWidth, 0.01);
	bool   directional = rippleDirectional;
	[loop] for (int k = 0; k < n; ++k) {
		RippleFiringRow firing = rippleFirings[k];
		float3 offset = pos - firing.origin;
		float  d = directional ? dot(offset, rippleDirection) : length(offset);
		float  r = firing.radius;
		float  f;
		if (rippleDisc) {
			float lead = 1 - smoothstep(r - width, r, d);
			float trail = directional ? smoothstep(-width, 0, d) : 1;
			f = lead * trail;
		} else {
			float x = (d - r) / width;
			f = exp(-x * x * 4);
		}
		v = max(v, f * rippleStrengths[k >> 2][k & 3]);
	}
	return float4(v, v, v, 1);
}
