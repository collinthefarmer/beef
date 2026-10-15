struct ProgramInputRow
{
	float3 value;
	uint   textureSlot;
};

cbuffer ProgramParams : register(b1)
{
	uint4           programCode[kProgramRowCount];
	ProgramInputRow programInputs[kProgramInputCount];
	TextureRead     programTextures[kProgramTextureCount];
	uint            programCount;
	bool            programVectorResult;
};

float LookupAt(int slot, float v)
{
	float p = saturate(v) * 255;
	int lo = (int)p;
	int hi = min(lo + 1, 255);
	return LerpEntries(LoadLookup(slot, lo), LoadLookup(slot, hi), p);
}

float2 TexelCentre(int slot, float2 uv)
{
	float2 size = ProgramTextureSize(slot);
	if (size.x < 1 || size.y < 1) return uv;
	return (floor(uv * size) + 0.5) / size;
}

float3 ReadTexture(int slot, float2 rawUv)
{
	TextureRead read = programTextures[slot];
	float2 uv = ReadUv(read, rawUv);
	if (HasOption(read.options, kReadNearest)) uv = TexelCentre(slot, uv);
	return Pick(SampleProgramTexture(slot, uv, read.mip), read.channel) * read.normalize;
}

float3 ReadInput(int index, float2 uv)
{
	ProgramInputRow input = programInputs[index];
	if (input.textureSlot == kNoTextureSlot) return input.value;
	return ReadTexture((int)input.textureSlot, uv);
}

struct Instruction
{
	uint  op;
	uint  components;
	uint  pops;
	uint  index;
	float number;
};

uint2 InstructionWords(int k)
{
	uint4 row = programCode[k >> 1];
	return (k & 1) ? row.zw : row.xy;
}

uint WordByte(uint word, uint byteIndex)
{
	return (word >> (byteIndex * 8)) & 0xFF;
}

Instruction InstructionAt(int k)
{
	uint2 words = InstructionWords(k);
	Instruction instruction;
	instruction.op = WordByte(words.x, kInstructionOpByte);
	instruction.components = WordByte(words.x, kInstructionComponentsByte);
	instruction.pops = WordByte(words.x, kInstructionPopsByte);
	instruction.index = WordByte(words.x, kInstructionIndexByte);
	instruction.number = asfloat(words.y);
	return instruction;
}

float NumberAt(int k)
{
	return asfloat(InstructionWords(k).y);
}

float3 SafeDiv(float3 a, float3 b)
{
	return float3(b.x == 0 ? 0 : a.x / b.x, b.y == 0 ? 0 : a.y / b.y, b.z == 0 ? 0 : a.z / b.z);
}

float PowOne(float a, float b)
{
	if (b == 0 || a == 1) return 1;
	float r = exp2(log2(abs(a)) * b);
	if (a < 0) {
		if (b != floor(b)) return 0;
		if (frac(b * 0.5) != 0) r = -r;
	}
	return isfinite(r) ? r : 0;
}

float3 SafePow(float3 a, float3 b)
{
	return float3(PowOne(a.x, b.x), PowOne(a.y, b.y), PowOne(a.z, b.z));
}

uint HashBits(float value)
{
	return (isfinite(value) && value != 0) ? asuint(value) : 0u;
}

float Hash(float value, float salt)
{
	uint mixed = MixBits(HashBits(value) ^ MixBits(HashBits(salt) + 0x9e3779b9u));
	return (float)(mixed >> 8) * (1.0 / 16777216.0);
}

float RandRange(float start, float stop, float seed)
{
	if (!isfinite(start)) return 0;
	float lo = floor(start);
	float hi = isfinite(stop) ? floor(stop) : lo;
	if (!(hi > lo)) return lo;
	float s = isfinite(seed) ? clamp(seed, 0, 1) : 0;
	float span = hi - lo;
	return lo + min(floor(s * span), span - 1);
}

float WheelChannel(float turns, float offset)
{
	return saturate(abs(frac(turns + offset) * 6 - 3) - 1);
}

float3 Hue(float turns)
{
	float t = isfinite(turns) ? turns : 0;
	return float3(WheelChannel(t, 0), WheelChannel(t, 2.0 / 3.0), WheelChannel(t, 1.0 / 3.0));
}

float3 HueShift(float3 color, float turns)
{
	float angle = (isfinite(turns) ? turns : 0) * 6.28318530718;
	float c = cos(angle);
	float s = sin(angle);
	float k = 0.57735026919;
	float along = k * (color.x + color.y + color.z) * (1 - c);
	float3 across = k * float3(color.z - color.y, color.x - color.z, color.y - color.x);
	float3 shifted = color * c + across * s + k * along;
	return isfinite(shifted) ? saturate(shifted) : 0;
}

float Shuffle(float index, float total, float seed)
{
	float outside = (float)kShuffleOutside;
	if (!(total >= 1) || !(index >= 0) || isinf(index)) return outside;
	float n = min(floor(total), (float)kMaxShuffleTotal);
	float i = floor(index);
	if (i >= n) return outside;
	uint count = (uint)n;
	uint p = kPrimeAtLeast[count];
	float s = isfinite(seed) ? frac(seed) : 0;
	uint scale = 1 + min((uint)floor(frac(s * 29.41) * (p - 1)), p - 2);
	uint shift = min((uint)floor(frac(s * 13.37) * p), p - 1);
	uint x = (uint)i;
	[loop] for (uint k = 0; k < p; ++k) {
		x = (scale * x + shift) % p;
		if (x < count) break;
	}
	return (float)x;
}
