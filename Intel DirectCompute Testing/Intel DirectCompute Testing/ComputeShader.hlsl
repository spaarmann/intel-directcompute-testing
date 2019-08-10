#ifdef STRUCTURED_BUFFERS
RWStructuredBuffer<uint> mainBuffer;
#else
RWBuffer<uint> mainBuffer;
#endif

[numthreads(3, 3, 3)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
	uint id = DTid.x * 9 * 9 + DTid.y * 9 + DTid.z;
	mainBuffer[id] = mainBuffer[id] + 2;
}