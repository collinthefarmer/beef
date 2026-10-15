cbuffer BakeNoiseParams : register(b7)
{
	uint noiseOctaves;
	uint noiseSeed;
	uint noiseStyle;
};

uint LatticeHash(int3 c, uint seed)
{
	return MixBits(asuint(c.x) * 73856093u ^ asuint(c.y) * 19349663u ^ asuint(c.z) * 83492791u ^ seed * 2654435761u);
}

float LatticeValue(int3 c, uint seed)
{
	return (float)(LatticeHash(c, seed) >> 8) * (1.0 / 16777216.0);
}

float ValueNoise(float3 p, uint seed)
{
	float3 cell = floor(p);
	int3 c = int3(cell);
	float3 f = p - cell;
	float3 u = f * f * (3.0 - 2.0 * f);
	float y0z0 = lerp(LatticeValue(c, seed), LatticeValue(c + int3(1, 0, 0), seed), u.x);
	float y1z0 = lerp(LatticeValue(c + int3(0, 1, 0), seed), LatticeValue(c + int3(1, 1, 0), seed), u.x);
	float y0z1 = lerp(LatticeValue(c + int3(0, 0, 1), seed), LatticeValue(c + int3(1, 0, 1), seed), u.x);
	float y1z1 = lerp(LatticeValue(c + int3(0, 1, 1), seed), LatticeValue(c + int3(1, 1, 1), seed), u.x);
	return lerp(lerp(y0z0, y1z0, u.y), lerp(y0z1, y1z1, u.y), u.z);
}

float FractalNoise(float3 p, uint octaves, uint seed)
{
	float sum = 0;
	float total = 0;
	float amplitude = 1;
	float frequency = 1;
	[unroll] for (uint o = 0; o < kMaxNoiseOctaves; ++o) {
		if (o < octaves) {
			sum += amplitude * ValueNoise(p * frequency, seed + o * 7919u);
			total += amplitude;
		}
		amplitude *= 0.5;
		frequency *= 2.0;
	}
	return sum / total;
}

float CellularEdge(float3 p, uint seed)
{
	float3 cell = floor(p);
	int3 c = int3(cell);
	float nearest = 8.0;
	float second = 8.0;
	[unroll] for (int dz = -1; dz <= 1; ++dz)
	[unroll] for (int dy = -1; dy <= 1; ++dy)
	[unroll] for (int dx = -1; dx <= 1; ++dx) {
		int3 n = c + int3(dx, dy, dz);
		float3 feature = float3(n) + float3(
			LatticeValue(n, seed + 1013u),
			LatticeValue(n, seed + 2026u),
			LatticeValue(n, seed + 3039u));
		float d = length(feature - p);
		if (d < nearest) {
			second = nearest;
			nearest = d;
		} else if (d < second) {
			second = d;
		}
	}
	return saturate(second - nearest);
}

float CellularNoise(float3 p, uint octaves, uint seed)
{
	float sum = 0;
	float total = 0;
	float amplitude = 1;
	float frequency = 1;
	[loop] for (uint o = 0; o < kMaxNoiseOctaves; ++o) {
		if (o < octaves) {
			sum += amplitude * CellularEdge(p * frequency, seed + o * 7919u);
			total += amplitude;
		}
		amplitude *= 0.5;
		frequency *= 2.0;
	}
	return sum / total;
}

float StyledNoise(float3 p, uint octaves, uint seed, uint style)
{
	if (style == kNoiseCracks)
		return CellularNoise(p, octaves, seed);
	float n = FractalNoise(p, octaves, seed);
	return style == kNoiseRidges ? abs(2.0 * n - 1.0) : n;
}

float4 BakeNoisePS(BakeOut i) : SV_Target
{
	uint octaves = clamp(noiseOctaves, 1u, kMaxNoiseOctaves);
	uint seed = noiseSeed;
	uint style = min(noiseStyle, kNoiseCracks);
	return float4(
		StyledNoise(i.value, octaves, seed, style),
		StyledNoise(i.value + float3(17.3, 5.9, 31.1), octaves, seed + 101u, style),
		StyledNoise(i.value + float3(41.7, 23.3, 11.9), octaves, seed + 202u, style),
		1);
}
