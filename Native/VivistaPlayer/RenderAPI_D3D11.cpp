#pragma once
#include "RenderAPI.h"
#include "PlatformBase.h"

// Direct3D 11 implementation of RenderAPI.

#if SUPPORT_D3D11

#include <assert.h>
#include <d3d11.h>
#include "Unity/IUnityGraphicsD3D11.h"
#include <cstdint>
#include <thread>
#include "Logger.h"

class RenderAPI_D3D11 : public RenderAPI
{
public:
	RenderAPI_D3D11();
	virtual ~RenderAPI_D3D11() {}

	virtual void ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces);

	virtual bool GetUsesReverseZ() { return (int)device->GetFeatureLevel() >= (int)D3D_FEATURE_LEVEL_10_0; }

	virtual void Create(int textureWidth, int textureHeight, void** ptry, void** ptru, void** ptrv);
	virtual void UploadYUVFrame(unsigned char* ych, unsigned char* uch, unsigned char* vch);

private:
	void CreateResources();
	void ReleaseResources();

private:
	ID3D11Device* device;
	static const unsigned int TEXTURE_NUM = 3;

	unsigned int widthY;
	unsigned int heightY;
	unsigned int lengthY;

	unsigned int widthUV;
	unsigned int heightUV;
	unsigned int lengthUV;

	ID3D11Texture2D* textures[TEXTURE_NUM];
	ID3D11ShaderResourceView* shaderResourceView[TEXTURE_NUM];
};

RenderAPI* CreateRenderAPI_D3D11()
{
	return new RenderAPI_D3D11();
}

RenderAPI_D3D11::RenderAPI_D3D11()
	: device(NULL)
{
}

void RenderAPI_D3D11::ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces)
{
	switch (type)
	{
		case kUnityGfxDeviceEventInitialize:
		{
			IUnityGraphicsD3D11* d3d = interfaces->Get<IUnityGraphicsD3D11>();
			device = d3d->GetDevice();
			CreateResources();
			break;
		}
		case kUnityGfxDeviceEventShutdown:
			ReleaseResources();
			break;
	}
}

void RenderAPI_D3D11::CreateResources()
{
	widthY = heightY = lengthY = 0;
	widthUV = heightUV = lengthUV = 0;

	for (int i = 0; i < TEXTURE_NUM; i++)
	{
		textures[i] = NULL;
		shaderResourceView[i] = NULL;
	}
}

void RenderAPI_D3D11::ReleaseResources()
{
	widthY = heightY = lengthY = 0;
	widthUV = heightUV = lengthUV = 0;

	for (int i = 0; i < TEXTURE_NUM; i++)
	{
		if (textures[i] != NULL)
		{
			textures[i]->Release();
			textures[i] = NULL;
		}

		if (shaderResourceView[i] != NULL)
		{
			shaderResourceView[i]->Release();
			shaderResourceView[i] = NULL;
		}
	}
}

void RenderAPI_D3D11::Create(int textureWidth, int textureHeight, void** ptry, void** ptru, void** ptrv)
{
	widthY = (unsigned int)(ceil((float)textureWidth / 64) * 64);
	heightY = textureHeight;
	lengthY = widthY * heightY;

	widthUV = widthY / 2;
	heightUV = heightY / 2;
	lengthUV = widthUV * heightUV;

	D3D11_TEXTURE2D_DESC textDesc;
	ZeroMemory(&textDesc, sizeof(D3D11_TEXTURE2D_DESC));
	textDesc.Width = textureWidth;
	textDesc.Height = textureHeight;
	textDesc.MipLevels = textDesc.ArraySize = 1;
	textDesc.Format = DXGI_FORMAT_A8_UNORM;
	textDesc.SampleDesc.Count = 1;
	textDesc.Usage = D3D11_USAGE_DYNAMIC;
	textDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	textDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	textDesc.MiscFlags = 0;

	D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc;
	shaderResourceViewDesc.Format = DXGI_FORMAT_A8_UNORM;
	shaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
	shaderResourceViewDesc.Texture2D.MipLevels = 1;

	HRESULT result = device->CreateTexture2D(&textDesc, NULL, &textures[0]);
	if (FAILED(result)) { LOG("Create texture Y fail. Error code: %x\n", result); }

	result = device->CreateShaderResourceView(textures[0], &shaderResourceViewDesc, &shaderResourceView[0]);
	if (FAILED(result)) { LOG("Create shader resource view Y fail. Error code: %x\n", result); }

	textDesc.Width = textureWidth / 2;
	textDesc.Height = textureHeight / 2;
	result = device->CreateTexture2D(&textDesc, NULL, &textures[1]);
	if (FAILED(result)) { LOG("Create texture U fail. Error code: %x\n", result); }

	result = device->CreateShaderResourceView(textures[1], &shaderResourceViewDesc, &shaderResourceView[1]);
	if (FAILED(result)) { LOG("Create shader resource view U fail. Error code: %x\n", result); }

	result = device->CreateTexture2D(&textDesc, NULL, &textures[2]);
	if (FAILED(result)) { LOG("Create texture V fail. Error code: %x\n", result); }

	result = device->CreateShaderResourceView(textures[2], &shaderResourceViewDesc, &shaderResourceView[2]);
	if (FAILED(result)) { LOG("Create shader resource view V fail. %x\n", result); }

	*ptry = shaderResourceView[0];
	*ptru = shaderResourceView[1];
	*ptrv = shaderResourceView[2];
}

void RenderAPI_D3D11::UploadYUVFrame(unsigned char* ych, unsigned char* uch, unsigned char* vch)
{
	if (device == NULL)
	{
		return;
	}

	ID3D11DeviceContext* ctx = NULL;
	device->GetImmediateContext(&ctx);

	D3D11_MAPPED_SUBRESOURCE mappedResource[TEXTURE_NUM];
	for (int i = 0; i < TEXTURE_NUM; i++)
	{
		ZeroMemory(&(mappedResource[i]), sizeof(D3D11_MAPPED_SUBRESOURCE));
	}


	ctx->Map(textures[0], 0, D3D11_MAP_WRITE_DISCARD, 0, &(mappedResource[0]));
	UINT rowPitchY = mappedResource[0].RowPitch;
	if (widthY == rowPitchY)
	{
		uint8_t* ptrMappedY = (uint8_t*)(mappedResource[0].pData);
		memcpy(ptrMappedY, ych, lengthY);
	}
	else
	{
		uint8_t* ptrMappedY = (uint8_t*)(mappedResource[0].pData);
		uint8_t* end = ych + lengthY;
		while (ych != end)
		{
			memcpy(ptrMappedY, ych, widthY);
			ych += widthY;
			ptrMappedY += rowPitchY;
		}
	}
	ctx->Unmap(textures[0], 0);



	ctx->Map(textures[1], 0, D3D11_MAP_WRITE_DISCARD, 0, &(mappedResource[1]));
	UINT rowPitchU = mappedResource[1].RowPitch;
	if (widthUV == rowPitchU)
	{
		uint8_t* ptrMappedU = (uint8_t*)(mappedResource[1].pData);
		memcpy(ptrMappedU, uch, lengthUV);
	}
	else
	{
		uint8_t* endU = uch + lengthUV;
		uint8_t* ptrMappedU = (uint8_t*)(mappedResource[1].pData);
		while (uch != endU)
		{
			memcpy(ptrMappedU, uch, widthUV);

			uch += widthUV;
			ptrMappedU += rowPitchU;
		}
	}
	ctx->Unmap(textures[1], 0);

	ctx->Map(textures[2], 0, D3D11_MAP_WRITE_DISCARD, 0, &(mappedResource[2]));
	UINT rowPitchV = mappedResource[2].RowPitch;
	if (widthUV == rowPitchV)
	{
		uint8_t* ptrMappedV = (uint8_t*)(mappedResource[2].pData);
		memcpy(ptrMappedV, vch, lengthUV);
	}
	else
	{
		uint8_t* endV = vch + lengthUV;
		uint8_t* ptrMappedV = (uint8_t*)(mappedResource[2].pData);
		while (vch != endV)
		{
			memcpy(ptrMappedV, vch, widthUV);
			vch += widthUV;
			ptrMappedV += rowPitchV;
		}
	}
	ctx->Unmap(textures[2], 0);

	for (int i = 0; i < TEXTURE_NUM; i++)
	{
		//ctx->Unmap(textures[i], 0);
	}

	ctx->Release();
}

#endif // #if SUPPORT_D3D11