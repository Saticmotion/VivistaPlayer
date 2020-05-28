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
	virtual ~RenderAPI_D3D11() { }

	virtual void ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces);

	virtual bool GetUsesReverseZ() { return (int)m_Device->GetFeatureLevel() >= (int)D3D_FEATURE_LEVEL_10_0; }

	virtual void Create(int textureWidth, int textureHeight);
	virtual void Upload(unsigned char* ych, unsigned char* uch, unsigned char* vch);

	virtual void getResourcePointers(void*& ptry, void*& ptru, void*& ptrv);

private:
	void CreateResources();
	void ReleaseResources();

private:
	ID3D11Device* m_Device;
	static const unsigned int TEXTURE_NUM = 3;

	unsigned int m_WidthY;
	unsigned int m_HeightY;
	unsigned int m_LengthY;

	unsigned int m_WidthUV;
	unsigned int m_HeightUV;
	unsigned int m_LengthUV;

	ID3D11Texture2D* m_Textures[TEXTURE_NUM];
	ID3D11ShaderResourceView* m_ShaderResourceView[TEXTURE_NUM];
};


RenderAPI* CreateRenderAPI_D3D11()
{
	return new RenderAPI_D3D11();
}

RenderAPI_D3D11::RenderAPI_D3D11()
	: m_Device(NULL)
{
}


void RenderAPI_D3D11::ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces)
{
	switch (type)
	{
	case kUnityGfxDeviceEventInitialize:
	{
		IUnityGraphicsD3D11* d3d = interfaces->Get<IUnityGraphicsD3D11>();
		m_Device = d3d->GetDevice();
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
	m_WidthY = m_HeightY = m_LengthY = 0;
	m_WidthUV = m_HeightUV = m_LengthUV = 0;

	for (int i = 0; i < TEXTURE_NUM; i++) {
		m_Textures[i] = NULL;
		m_ShaderResourceView[i] = NULL;
	}
}


void RenderAPI_D3D11::ReleaseResources()
{
	m_WidthY = m_HeightY = m_LengthY = 0;
	m_WidthUV = m_HeightUV = m_LengthUV = 0;

	for (int i = 0; i < TEXTURE_NUM; i++) {
		if (m_Textures[i] != NULL)
		{
			m_Textures[i]->Release();
			m_Textures[i] = NULL;
		}

		if (m_ShaderResourceView[i] != NULL)
		{
			m_ShaderResourceView[i]->Release();
			m_ShaderResourceView[i] = NULL;
		}
	}
}

void RenderAPI_D3D11::Create(int textureWidth, int textureHeight)
{
	m_WidthY = (unsigned int)(ceil((float)textureWidth / 64) * 64);
	m_HeightY = textureHeight;
	m_LengthY = m_WidthY * m_HeightY;

	m_WidthUV = m_WidthY / 2;
	m_HeightUV = m_HeightY / 2;
	m_LengthUV = m_WidthUV * m_HeightUV;

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

	HRESULT result = m_Device->CreateTexture2D(&textDesc, NULL, (ID3D11Texture2D**)(&(m_Textures[0])));
	if (FAILED(result))
	{
		LOG("Create texture Y fail. Error code: %x\n", result);
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc;
	shaderResourceViewDesc.Format = DXGI_FORMAT_A8_UNORM;
	shaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
	shaderResourceViewDesc.Texture2D.MipLevels = 1;

	result = m_Device->CreateShaderResourceView((ID3D11Texture2D*)(m_Textures[0]), &shaderResourceViewDesc, &(m_ShaderResourceView[0]));
	if (FAILED(result)) {
		LOG("Create shader resource view Y fail. Error code: %x\n", result);
	}

	textDesc.Width = textureWidth / 2;
	textDesc.Height = textureHeight / 2;
	result = m_Device->CreateTexture2D(&textDesc, NULL, (ID3D11Texture2D**)(&(m_Textures[1])));
	if (FAILED(result))
	{
		LOG("Create texture U fail. Error code: %x\n", result);
	}

	result = m_Device->CreateShaderResourceView((ID3D11Texture2D*)(m_Textures[0]), &shaderResourceViewDesc, &(m_ShaderResourceView[1]));
	if (FAILED(result)) {
		LOG("Create shader resource view U fail. Error code: %x\n", result);
	}

	result = m_Device->CreateTexture2D(&textDesc, NULL, (ID3D11Texture2D**)(&(m_Textures[2])));
	if (FAILED(result))
	{
		LOG("Create texture V fail. Error code: %x\n", result);
	}

	result = m_Device->CreateShaderResourceView((ID3D11Texture2D*)(m_Textures[0]), &shaderResourceViewDesc, &(m_ShaderResourceView[2]));
	if (FAILED(result)) {
		LOG("Create shader resource view V fail. %x\n", result);
	}
}


void RenderAPI_D3D11::Upload(unsigned char* ych, unsigned char* uch, unsigned char* vch)
{
	if (m_Device == NULL)
	{
		return;
	}

	ID3D11DeviceContext* ctx = NULL;
	m_Device->GetImmediateContext(&ctx);

	D3D11_MAPPED_SUBRESOURCE mappedResource[TEXTURE_NUM];
	for (int i = 0; i < TEXTURE_NUM; i++) {
		ZeroMemory(&(mappedResource[i]), sizeof(D3D11_MAPPED_SUBRESOURCE));
		ctx->Map(m_Textures[i], 0, D3D11_MAP_WRITE_DISCARD, 0, &(mappedResource[i]));
	}

	UINT rowPitchY = mappedResource[0].RowPitch;
	UINT rowPitchUV = mappedResource[1].RowPitch;

	uint8_t* ptrMappedY = (uint8_t*)(mappedResource[0].pData);
	uint8_t* ptrMappedU = (uint8_t*)(mappedResource[1].pData);
	uint8_t* ptrMappedV = (uint8_t*)(mappedResource[2].pData);

	std::thread YThread = std::thread([&]() {
		//	Map region has its own row pitch which may different to texture width.
		if (m_WidthY == rowPitchY) {
			memcpy(ptrMappedY, ych, m_LengthY);
		}
		else {
			//	Handle rowpitch of mapped memory.
			uint8_t* end = ych + m_LengthY;
			while (ych != end) {
				memcpy(ptrMappedY, ych, m_WidthY);
				ych += m_WidthY;
				ptrMappedY += rowPitchY;
			}
		}
		});

	std::thread UVThread = std::thread([&]() {
		if (m_WidthUV == rowPitchUV) {
			memcpy(ptrMappedU, uch, m_LengthUV);
			memcpy(ptrMappedV, vch, m_LengthUV);
		}
		else {
			//	Handle rowpitch of mapped memory.
			//	YUV420, length U == length V
			uint8_t* endU = uch + m_LengthUV;
			while (uch != endU) {
				memcpy(ptrMappedU, uch, m_WidthUV);
				memcpy(ptrMappedV, vch, m_WidthUV);
				uch += m_WidthUV;
				vch += m_WidthUV;
				ptrMappedU += rowPitchUV;
				ptrMappedV += rowPitchUV;
			}
		}
		});

	if (YThread.joinable()) {
		YThread.join();
	}
	if (UVThread.joinable()) {
		UVThread.join();
	}

	for (int i = 0; i < TEXTURE_NUM; i++) {
		ctx->Unmap(m_Textures[i], 0);
	}
	ctx->Release();
}

void RenderAPI_D3D11::getResourcePointers(void*& ptry, void*& ptru, void*& ptrv)
{
	if (m_Device == NULL)
	{
		return;
	}

	ptry = m_ShaderResourceView[0];
	ptru = m_ShaderResourceView[1];
	ptrv = m_ShaderResourceView[2];
}

#endif // #if SUPPORT_D3D11