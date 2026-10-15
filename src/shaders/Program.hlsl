float3 RunProgram(int first, int count, float2 uv)
{
	float3 st[kProgramStackDepth];
	int    sp = 0;
	int    end = min(first + count, (int)kProgramInstructionCount);
	[loop] for (int k = max(first, 0); k < end; ++k) {
		Instruction instruction = InstructionAt(k);
		int    pops = (int)instruction.pops;
		float3 a = 0, b = 0, d = 0;
		if (pops >= 1) { if (sp > 0) { --sp; a = st[sp]; } }
		if (pops >= 2) { if (sp > 0) { --sp; b = st[sp]; } }
		if (pops >= 3) { if (sp > 0) { --sp; d = st[sp]; } }
		float3 r = RunOpcode((int)instruction.op, a, b, d, instruction.number, (int)instruction.index, (int)instruction.components, uv);
		if (sp < (int)kProgramStackDepth) { st[sp] = r; ++sp; }
	}
	return sp > 0 ? st[sp - 1] : float3(0, 0, 0);
}

float4 PSProgram(VSOut i) : SV_Target
{
	float3 result = RunProgram(0, (int)programCount, i.uv);
	return programVectorResult ? float4(result, 1) : float4(result.xxx, 1);
}
