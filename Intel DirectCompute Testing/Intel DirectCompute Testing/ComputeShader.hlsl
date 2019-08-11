#ifdef SPLIT_INOUT
	#ifdef STRUCTURED_BUFFERS
	RWStructuredBuffer<uint> inBuffer;
	RWStructuredBuffer<uint> outBuffer;
	#else
	RWBuffer<uint> inBuffer;
	RWBuffer<uint> outBuffer;
	#endif
#else
	#ifdef STRUCTURED_BUFFERS
	RWStructuredBuffer<uint> mainBuffer;
	#else
	RWBuffer<uint> mainBuffer;
	#endif
#endif

[numthreads(3, 3, 3)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
	uint id = DTid.x * 9 * 9 + DTid.y * 9 + DTid.z;

#if SPLIT_INOUT
	outBuffer[id] = inBuffer[id] + 2;
#else
	mainBuffer[id] = mainBuffer[id] + 2;
#endif
}