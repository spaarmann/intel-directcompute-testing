// Substantial portions of this code are copied and adapted from the following DirectX sample:
// https://github.com/walbourn/directx-sdk-samples/blob/master/BasicCompute11/BasicCompute11.cpp
// That code is MIT licensed, see the LICENSE file for details.

#include <stdio.h>

#include <d3d11.h>
#include <d3dcompiler.h>

#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")

bool g_useStructuredBuffers = false;
bool g_useSplitInOutBuffers = false;

HRESULT CompileComputeShader(LPCWSTR srcFile, LPCSTR entryPoint, ID3D11Device* device, ID3DBlob** blob) {
	if (!srcFile || !entryPoint || !device || !blob)
		return E_INVALIDARG;

	*blob = nullptr;

	UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
	flags |= D3DCOMPILE_DEBUG;
#endif

	LPCSTR profile = "cs_5_0";

	const D3D_SHADER_MACRO defines_structured_split[] = {
		"STRUCTURED_BUFFERS", "1", "SPLIT_INOUT", "1", nullptr, nullptr };
	const D3D_SHADER_MACRO defines_structured[] = {
		"STRUCTURED_BUFFERS", "1", nullptr, nullptr };
	const D3D_SHADER_MACRO defines_split[] = {
		"SPLIT_INOUT", "1", nullptr, nullptr };
	const D3D_SHADER_MACRO defines_none[] = {
		nullptr, nullptr };

	const D3D_SHADER_MACRO* defines;
	if (g_useStructuredBuffers && g_useSplitInOutBuffers)
		defines = defines_structured_split;
	else if (g_useStructuredBuffers)
		defines = defines_structured;
	else if (g_useSplitInOutBuffers)
		defines = defines_split;
	else
		defines = defines_none;

	ID3DBlob* shaderBlob = nullptr;
	ID3DBlob* errorBlob = nullptr;
	HRESULT hr = D3DCompileFromFile(srcFile, defines, D3D_COMPILE_STANDARD_FILE_INCLUDE, entryPoint, profile,
		flags, 0, &shaderBlob, &errorBlob);

	if (FAILED(hr)) {
		if (errorBlob) {
			printf("Compiler error: %s\n", (char*)errorBlob->GetBufferPointer());
			errorBlob->Release();
		}
		if (shaderBlob) {
			shaderBlob->Release();
		}
		return hr;
	}

	*blob = shaderBlob;
	return hr;
}

HRESULT CreateStructuredBuffer(ID3D11Device* device, UINT elementSize, UINT count, void* initData, ID3D11Buffer** bufferOut) {
	*bufferOut = nullptr;

	D3D11_BUFFER_DESC desc = {};
	desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	desc.ByteWidth = elementSize * count;
	desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	desc.StructureByteStride = elementSize;

	if (initData == nullptr)
		return device->CreateBuffer(&desc, nullptr, bufferOut);

	D3D11_SUBRESOURCE_DATA d3dInitData;
	d3dInitData.pSysMem = initData;
	return device->CreateBuffer(&desc, &d3dInitData, bufferOut);
}

HRESULT CreateRawBuffer(ID3D11Device* device, UINT size, void* initData, ID3D11Buffer** bufferOut) {
	*bufferOut = nullptr;

	D3D11_BUFFER_DESC desc = {};
	desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	desc.ByteWidth = size;
	desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;

	if (initData == nullptr)
		return device->CreateBuffer(&desc, nullptr, bufferOut);

	D3D11_SUBRESOURCE_DATA d3dInitData;
	d3dInitData.pSysMem = initData;
	return device->CreateBuffer(&desc, &d3dInitData, bufferOut);
}

HRESULT CreateBufferUAV(ID3D11Device* device, ID3D11Buffer* buffer, ID3D11UnorderedAccessView** uavOut) {
	*uavOut = nullptr;

	D3D11_BUFFER_DESC bufferDesc = {};
	buffer->GetDesc(&bufferDesc);

	D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
	desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	desc.Buffer.FirstElement = 0;

	if (bufferDesc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS) {
		desc.Format = DXGI_FORMAT_R32_UINT;
		desc.Buffer.NumElements = bufferDesc.ByteWidth / 4;
	}
	else if (bufferDesc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) {
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.Buffer.NumElements = bufferDesc.ByteWidth / bufferDesc.StructureByteStride;
	}
	else {
		return E_INVALIDARG;
	}

	HRESULT res = device->CreateUnorderedAccessView(buffer, &desc, uavOut);
	return res;
}

void RunComputeShader(ID3D11DeviceContext* immContext, ID3D11ComputeShader* computeShader, ID3D11UnorderedAccessView** uavs, int uavCount) {
	immContext->CSSetShader(computeShader, nullptr, 0);
	immContext->CSSetUnorderedAccessViews(0, uavCount, uavs, nullptr);
	immContext->Dispatch(3, 3, 3);
	immContext->CSSetShader(nullptr, nullptr, 0);
	ID3D11UnorderedAccessView* uavnullptr[1] = { nullptr };
	immContext->CSSetUnorderedAccessViews(0, 1, uavnullptr, nullptr);
}

ID3D11Buffer* CreateAndCopyToDebugBuf(ID3D11Device* device, ID3D11DeviceContext* immContext, ID3D11Buffer* buffer) {
	ID3D11Buffer* debugbuf = nullptr;

	D3D11_BUFFER_DESC desc = {};
	buffer->GetDesc(&desc);
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.BindFlags = 0;
	desc.MiscFlags = 0;
	HRESULT hr = device->CreateBuffer(&desc, nullptr, &debugbuf);

	if (FAILED(hr)) {
		printf("Failed to create debug buffer: %08X\n", hr);
		return nullptr;
	}

	immContext->CopyResource(debugbuf, buffer);
	return debugbuf;
}

void RunTestInstance(ID3D11Device* device, ID3D11DeviceContext* context) {
	printf("Parameters:\n");
	if (g_useStructuredBuffers)
		printf("\tUsing StructuredBuffers\n");
	else
		printf("\tUsing raw Buffers\n");
	if (g_useSplitInOutBuffers)
		printf("\tUsing seperate output buffer\n");
	else
		printf("\tUsing single buffer\n");
	printf("Running test...");

	// Compile compute shader
	ID3DBlob* csBlob = nullptr;
	HRESULT hr = CompileComputeShader(L"ComputeShader.hlsl", "CSMain", device, &csBlob);
	if (FAILED(hr)) {
		context->Release();
		device->Release();
		printf("Failed compiling compute shader: %08X\n", hr);
		return;
	}

	// Create shader
	ID3D11ComputeShader* computeShader = nullptr;
	hr = device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &computeShader);
	csBlob->Release();
	if (FAILED(hr)) {
		context->Release();
		device->Release();
		printf("Failed creating compute shader: %08X\n", hr);
		return;
	}

	// Create cpu-side buffer and fill with initial data
#define NUM_ELEMENTS 27*27

	UINT cpuBuffer[NUM_ELEMENTS];
	for (int i = 0; i < NUM_ELEMENTS; i++) {
		cpuBuffer[i] = 1;
	}

	// Create and set up gpu-side buffer
	ID3D11Buffer* gpuBuffer = nullptr, * gpuOutBuffer = nullptr;
	if (g_useStructuredBuffers) {
		hr = CreateStructuredBuffer(device, sizeof(UINT), NUM_ELEMENTS, &cpuBuffer[0], &gpuBuffer);
		if (g_useSplitInOutBuffers)
			CreateStructuredBuffer(device, sizeof(UINT), NUM_ELEMENTS, nullptr, &gpuOutBuffer);
	}
	else {
		hr = CreateRawBuffer(device, sizeof(UINT) * NUM_ELEMENTS, &cpuBuffer[0], &gpuBuffer);
		if (g_useSplitInOutBuffers)
			CreateRawBuffer(device, sizeof(UINT) * NUM_ELEMENTS, nullptr, &gpuOutBuffer);
	}

	if (FAILED(hr)) {
		computeShader->Release();
		context->Release();
		device->Release();
		printf("Failed creating buffers: %08X\n", hr);
		return;
	}

	ID3D11UnorderedAccessView* uav = nullptr, * outUav = nullptr;
	hr = CreateBufferUAV(device, gpuBuffer, &uav);
	if (g_useSplitInOutBuffers)
		CreateBufferUAV(device, gpuOutBuffer, &outUav);

	if (FAILED(hr)) {
		gpuBuffer->Release();
		if (g_useSplitInOutBuffers)
			gpuOutBuffer->Release();
		computeShader->Release();
		context->Release();
		device->Release();
		printf("Failed to create UAV: %08X\n", hr);
		return;
	}

	// Run compute shader
	ID3D11UnorderedAccessView* uavs_single[] = {
		uav
	};
	ID3D11UnorderedAccessView* uavs_both[] = {
		uav, outUav
	};

	if (g_useSplitInOutBuffers)
		RunComputeShader(context, computeShader, uavs_both, 2);
	else
		RunComputeShader(context, computeShader, uavs_single, 1);

	// Copy data back to CPU and output
	ID3D11Buffer* debugbuf;
	if (g_useSplitInOutBuffers)
		debugbuf = CreateAndCopyToDebugBuf(device, context, gpuOutBuffer);
	else
		debugbuf = CreateAndCopyToDebugBuf(device, context, gpuBuffer);

	D3D11_MAPPED_SUBRESOURCE mappedResource;
	context->Map(debugbuf, 0, D3D11_MAP_READ, 0, &mappedResource);
	UINT* resData = (UINT*)mappedResource.pData;

	bool hasError = false;
	for (int i = 0; i < NUM_ELEMENTS; i++) {
		if (resData[i] != 3) {
			hasError = true;
			break;
		}
	}

	if (!hasError) {
		printf("Success\n");
	}
	else {
		printf("Error\n");
		printf("Received output from compute shader (expected output: all 3's):\n");
		for (int i = 0; i < NUM_ELEMENTS; i++) {
			printf("%u ", resData[i]);
			if ((i+1) % 27 == 0) {
				printf("\n");
			}
		}
	}
	
	context->Unmap(debugbuf, 0);
	debugbuf->Release();

	uav->Release();
	gpuBuffer->Release();
	if (g_useSplitInOutBuffers) {
		outUav->Release();
		gpuOutBuffer->Release();
	}
	computeShader->Release();

	printf("\n\n");
}

int main(int argc, char** argv) {

	// Basic D3D11 init
	const D3D_FEATURE_LEVEL lvl[] = {
		D3D_FEATURE_LEVEL_11_1
	};

	UINT createDeviceFlags = 0;
#ifdef _DEBUG
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	ID3D11Device* device = nullptr;
	ID3D11DeviceContext* context = nullptr;
	HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, lvl, _countof(lvl),
		D3D11_SDK_VERSION, &device, nullptr, &context);

	if (FAILED(hr)) {
		printf("Failed creating Direct3D 11 device: %08X\n", hr);
		return -1;
	}

	// Print GPU model
	IDXGIDevice* dxgiDevice;
	IDXGIAdapter* adapter;
	device->QueryInterface(__uuidof(IDXGIDevice), (void**)& dxgiDevice);
	dxgiDevice->GetAdapter(&adapter);
	DXGI_ADAPTER_DESC adapterDesc = {};
	adapter->GetDesc(&adapterDesc);
	printf("Using GPU: %u, vendor %u: %ws\n\n", adapterDesc.DeviceId, adapterDesc.VendorId, adapterDesc.Description);

	g_useStructuredBuffers = true;
	g_useSplitInOutBuffers = true;
	RunTestInstance(device, context);

	g_useStructuredBuffers = true;
	g_useSplitInOutBuffers = false;
	RunTestInstance(device, context);

	g_useStructuredBuffers = false;
	g_useSplitInOutBuffers = true;
	RunTestInstance(device, context);

	g_useStructuredBuffers = false;
	g_useSplitInOutBuffers = false;
	RunTestInstance(device, context);

	context->Release();
	device->Release();

	printf("Done.");

	return 0;
}