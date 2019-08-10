#define _WIN32_WINNT 0x600
#include <stdio.h>

#include <d3d11.h>
#include <d3dcompiler.h>

#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")

#define STRUCTURED_BUFFERS 1

HRESULT CompileComputeShader(LPCWSTR srcFile, LPCSTR entryPoint, ID3D11Device * device, ID3DBlob * *blob) {
	if (!srcFile || !entryPoint || !device || !blob)
		return E_INVALIDARG;

	*blob = nullptr;

	UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
	flags |= D3DCOMPILE_DEBUG;
#endif

	LPCSTR profile = "cs_5_0";

	const D3D_SHADER_MACRO defines[] = {
#ifdef STRUCTURED_BUFFERS
		"STRUCTURED_BUFFERS", "1",
#endif
		nullptr, nullptr
	};

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
		desc.Format = DXGI_FORMAT_R32_TYPELESS;
		desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
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

void RunComputeShader(ID3D11DeviceContext* immContext, ID3D11ComputeShader* computeShader, ID3D11UnorderedAccessView* uav) {
	immContext->CSSetShader(computeShader, nullptr, 0);
	immContext->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
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

	// Compile compute shader
	ID3DBlob* csBlob = nullptr;
	hr = CompileComputeShader(L"ComputeShader.hlsl", "CSMain", device, &csBlob);
	if (FAILED(hr)) {
		context->Release();
		device->Release();
		printf("Failed compiling compute shader: %08X\n", hr);
		return -1;
	}

	// Create shader
	ID3D11ComputeShader* computeShader = nullptr;
	hr = device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &computeShader);
	csBlob->Release();
	if (FAILED(hr)) {
		context->Release();
		device->Release();
		printf("Failed creating compute shader: %08X\n", hr);
		return -1;
	}

	// Create cpu-side buffer and fill with initial data
#define NUM_ELEMENTS 27*27

	UINT cpuBuffer[NUM_ELEMENTS];
	for (int i = 0; i < NUM_ELEMENTS; i++) {
		cpuBuffer[i] = 1;
	}

	// Create and set up gpu-side buffer
	ID3D11Buffer* gpuBuffer = nullptr;
#ifdef STRUCTURED_BUFFERS
	hr = CreateStructuredBuffer(device, sizeof(UINT), NUM_ELEMENTS, &cpuBuffer[0], &gpuBuffer);
#else
	hr = CreateRawBuffer(device, NUM_ELEMENTS * sizeof(UINT), &cpuBuffer[0], &gpuBuffer);
#endif

	if (FAILED(hr)) {
		computeShader->Release();
		context->Release();
		device->Release();
		printf("Failed creating buffers: %08X\n", hr);
		return -1;
	}

	ID3D11UnorderedAccessView* uav = nullptr;
	hr = CreateBufferUAV(device, gpuBuffer, &uav);

	if (FAILED(hr)) {
		gpuBuffer->Release();
		computeShader->Release();
		context->Release();
		device->Release();
		printf("Failed to create UAV: %08X\n", hr);
		return -1;
	}

	// Run compute shader
	RunComputeShader(context, computeShader, uav);

	// Copy data back to CPU and output
	ID3D11Buffer* debugbuf = CreateAndCopyToDebugBuf(device, context, gpuBuffer);
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	context->Map(debugbuf, 0, D3D11_MAP_READ, 0, &mappedResource);
	UINT* resData = (UINT*)mappedResource.pData;
	printf("Received output from compute shader (expected output: all 3's):\n");
	for (int i = 0; i < NUM_ELEMENTS; i++) {
		printf("%u ", resData[i]);
		if ((i+1) % 27 == 0) {
			printf("\n");
		}
	}
	context->Unmap(debugbuf, 0);
	debugbuf->Release();

	// Exit cleanup
	uav->Release();
	gpuBuffer->Release();
	computeShader->Release();
	context->Release();
	device->Release();

	printf("Done.");

	return 0;
}