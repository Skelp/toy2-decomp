#include "D3DApp/d3dapp.h"
#include "Logger.h"
#include "Toy2/Direct6.h"

// FUNCTION: TOY2 0x0040BD70 [MATCHED]
BOOL D3DAppISetRenderState()
{
	LPDIRECT3DDEVICE2 device = d3dappi.lpD3DDevice;

	if (g_renderMode != RENDERMODE_D3D || ! d3dappi.lpD3DViewport)
		return TRUE;

	HRESULT result = device->BeginScene();
	if (result < 0)
		Logger::LogDDError("pDev->BeginScene()", result);

	result = device->SetCurrentViewport(d3dappi.lpD3DViewport);
	if (result < 0)
		Logger::LogDDError("pDev->SetCurrentViewport(d3dappi.lpD3DViewport)", result);

	device->SetRenderState(D3DRENDERSTATE_SHADEMODE, d3dapprs.ShadeMode);
	device->SetRenderState(D3DRENDERSTATE_TEXTUREPERSPECTIVE, d3dapprs.bPerspCorrect);
	device->SetRenderState(D3DRENDERSTATE_ZENABLE, d3dapprs.bZBufferOn && PC.D3D->hasZBuffer);
	device->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, d3dapprs.bZBufferOn);
	device->SetRenderState(D3DRENDERSTATE_ZFUNC, D3DCMP_LESSEQUAL);
	device->SetRenderState(D3DRENDERSTATE_TEXTUREMAG, d3dapprs.TextureFilter);
	device->SetRenderState(D3DRENDERSTATE_TEXTUREMIN, d3dapprs.TextureFilter);
	device->SetRenderState(D3DRENDERSTATE_FILLMODE, d3dapprs.FillMode);
	device->SetRenderState(D3DRENDERSTATE_DITHERENABLE, d3dapprs.bDithering);
	device->SetRenderState(D3DRENDERSTATE_SPECULARENABLE, d3dapprs.bSpecular);
	device->SetRenderState(D3DRENDERSTATE_ANTIALIAS, d3dapprs.bAntialiasing);
	device->SetRenderState(D3DRENDERSTATE_FOGCOLOR, d3dapprs.FogColor);
	device->SetLightState(D3DLIGHTSTATE_AMBIENT, 0xFFFFFFFF);

	result = device->EndScene();
	if (result < 0)
		Logger::LogDDError("pDev->EndScene()", result);

	return TRUE;
}

// FUNCTION: TOY2 0x0040C8F0 [MATCHED]
HRESULT CALLBACK CreateD3DEnumTextureFormatsCallback(LPDDSURFACEDESC surfaceDesc, InterfaceDevice* d3dDevice)
{
	D3DTextureFormat* textureFormat = &d3dDevice->textureFormats[d3dDevice->textureFormatCount];
	DWORD mask;
	int redBits;
	int greenBits;
	int blueBits;
	int alphaBits = 0;

	memset(textureFormat, 0, sizeof(*textureFormat));
	memcpy(&textureFormat->surfaceDesc, surfaceDesc, sizeof(*surfaceDesc));

	if (surfaceDesc->ddpfPixelFormat.dwFlags & DDPF_PALETTEINDEXED8)
	{
		textureFormat->isPalettized = TRUE;
		textureFormat->indexBPP = 8;
		textureFormat->rgbBPP = alphaBits;
		textureFormat->hasAlphaPixels = alphaBits;
	}
	else if (surfaceDesc->ddpfPixelFormat.dwFlags & DDPF_PALETTEINDEXED4)
	{
		textureFormat->isPalettized = TRUE;
		textureFormat->indexBPP = 4;
		textureFormat->rgbBPP = alphaBits;
		textureFormat->hasAlphaPixels = alphaBits;
	}
	else
	{
		textureFormat->isPalettized = alphaBits;
		textureFormat->indexBPP = alphaBits;
		textureFormat->rgbBPP = (int16_t)surfaceDesc->ddpfPixelFormat.dwRGBBitCount;
		textureFormat->hasAlphaPixels = surfaceDesc->ddpfPixelFormat.dwFlags & DDPF_ALPHAPIXELS;

		mask = surfaceDesc->ddpfPixelFormat.dwRBitMask;
		while (! (mask & 1))
			mask >>= 1;
		redBits = 0;
		while (mask & 1)
		{
			mask >>= 1;
			++redBits;
		}

		mask = surfaceDesc->ddpfPixelFormat.dwGBitMask;
		while (! (mask & 1))
			mask >>= 1;
		greenBits = 0;
		while (mask & 1)
		{
			mask >>= 1;
			++greenBits;
		}

		mask = surfaceDesc->ddpfPixelFormat.dwBBitMask;
		while (! (mask & 1))
			mask >>= 1;
		blueBits = 0;
		while (mask & 1)
		{
			mask >>= 1;
			++blueBits;
		}

		textureFormat->redBPP = redBits;
		textureFormat->greenBPP = greenBits;
		textureFormat->blueBPP = blueBits;
		textureFormat->alphaBPP = alphaBits;

		if (textureFormat->hasAlphaPixels)
		{
			mask = surfaceDesc->ddpfPixelFormat.dwRGBAlphaBitMask;
			while (! (mask & 1))
				mask >>= 1;
			alphaBits = 0;
			while (mask & 1)
			{
				mask >>= 1;
				++alphaBits;
			}
			textureFormat->alphaBPP = alphaBits;
		}
	}

	Logger::Log("\n");
	Logger::Log("DIRECT 3D Texture Format %d.\n", d3dDevice->textureFormatCount);
	Logger::Log("Type %s", textureFormat->indexBPP ? "PAL" : "RGB");
	Logger::Log("%d ", textureFormat->indexBPP ? textureFormat->indexBPP : textureFormat->rgbBPP);
	if (! textureFormat->indexBPP)
	{
		Logger::Log("ARGB %dx%dx%dx%d ", textureFormat->alphaBPP, textureFormat->redBPP, textureFormat->greenBPP, textureFormat->blueBPP);
	}
	if (textureFormat->hasAlphaPixels)
		Logger::Log("ALPHAPIXELS\n\n");
	else
		Logger::Log("PIXELS\n\n");

	++d3dDevice->textureFormatCount;
	return DDENUMRET_OK;
}
