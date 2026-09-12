#include "Nu3D/BmpDataNode.h"
#include "DrawingDevice.h"
#include "NGNLoader/NGNLoader.h"
#include "Renderer/Renderer.h"
#include "Logger.h"

// The texture surface and the raw bitmap nodes built on it: retail holds
// 0x004AFF80 through 0x004B06D0 as one object, in the order of this file.
#include "Nu3D/BmpDataNodeInternal.h"
namespace Nu3D
{
	// FUNCTION: TOY2 0x004AFF80 [MATCHED]
	void CopyTextureToSurface(BmpDataNode* bmpDataNode)
	{
		DDSURFACEDESC2 surfaceDesc;
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(DDSURFACEDESC2);

		HRESULT lockResult;
		do
		{
			lockResult = bmpDataNode->surface->Lock(0, &surfaceDesc, DDLOCK_NOSYSLOCK, 0);
		} while (lockResult == DDERR_WASSTILLDRAWING);

		if (lockResult < 0)
		{
			Logger::LogD3DError(lockResult);
			return;
		}

		PixelFormatInfo pixelFormatInfo;
		CalculatePixelFormatShifts(&pixelFormatInfo, &surfaceDesc);

		for (int32_t row = 0; row < bmpDataNode->textureHeight; ++row)
		{
			int32_t texHeight = bmpDataNode->textureHeight;
			int32_t texWidth = bmpDataNode->textureWidth;

			uint8_t* sourcePixel = (uint8_t*)&bmpDataNode->texData[texWidth * (texHeight - row - 1)];
			uint16_t* destPixel = (uint16_t*)((uint8_t*)surfaceDesc.lpSurface + row * surfaceDesc.lPitch);

			int32_t col = 0;

			if (texWidth > 0)
			{
				do
				{
					int32_t shiftedValue;

					if (pixelFormatInfo.greenShift >= 0)
						shiftedValue = sourcePixel[1] << (int8_t)pixelFormatInfo.greenShift;
					else
						shiftedValue = sourcePixel[1] >> -(int8_t)pixelFormatInfo.greenShift;

					uint16_t pixel = (uint16_t)pixelFormatInfo.greenMask & shiftedValue;

					if (pixelFormatInfo.blueShift >= 0)
						shiftedValue = sourcePixel[0] << (int8_t)pixelFormatInfo.blueShift;
					else
						shiftedValue = sourcePixel[0] >> -(int8_t)pixelFormatInfo.blueShift;

					pixel = ((uint16_t)pixelFormatInfo.blueMask & shiftedValue) | pixel;

					if (pixelFormatInfo.redShift >= 0)
						shiftedValue = sourcePixel[2] << (int8_t)pixelFormatInfo.redShift;
					else
						shiftedValue = sourcePixel[2] >> -(int8_t)pixelFormatInfo.redShift;

					pixel = ((uint16_t)pixelFormatInfo.redMask & shiftedValue) | pixel;

					if ((bmpDataNode->flags & BMP_TEXTURE_ALPHA_MASK) != 0)
					{
						if (pixelFormatInfo.alphaShift >= 0)
							shiftedValue = sourcePixel[3] << (int8_t)pixelFormatInfo.alphaShift;
						else
							shiftedValue = sourcePixel[3] >> -(int8_t)pixelFormatInfo.alphaShift;

						pixel |= (uint16_t)pixelFormatInfo.alphaMask & shiftedValue;
					}

					*destPixel++ = pixel;
					sourcePixel += 4;
					++col;

				} while (col < bmpDataNode->textureWidth);
			}
		}

		bmpDataNode->surface->Unlock(0);
	}

	// FUNCTION: TOY2 0x004B0200 [PROVISIONAL]
	int32_t InitialiseTextureSurface(BmpDataNode* bmpDataNode)
	{
		FindPixelFormat findPixelFormat;
		DDSURFACEDESC2 surfaceDesc;
		D3DDEVICEDESC deviceDesc2;
		D3DDEVICEDESC deviceDesc;

		if ((bmpDataNode->flags & BMP_TEXTURE_BORROWED_SURFACE) != 0)
			return 0;

		DestroyBmpDataNode(bmpDataNode);

		LPDIRECTDRAW4 ddraw4 = DrawingDevice::GetDDraw4();

		// yes, this just gets called
		DrawingDevice::GetD3D();

		LPDIRECT3DDEVICE3 d3dDevice = DrawingDevice::GetD3DDevice();

		deviceDesc.dwSize = 252;
		deviceDesc2.dwSize = 252;

		if (d3dDevice->GetCaps(&deviceDesc, &deviceDesc2))
			return 0;

		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		uint32_t width = bmpDataNode->textureWidth;
		uint32_t height = bmpDataNode->textureHeight;

		surfaceDesc.ddsCaps.dwCaps2 = 16;

		findPixelFormat.bpp = 16;

		surfaceDesc.dwSize = 124;
		findPixelFormat.out = &surfaceDesc.ddpfPixelFormat;

		int32_t flags = bmpDataNode->flags;

		surfaceDesc.ddpfPixelFormat.dwSize = 32;
		surfaceDesc.dwFlags = 0x101007;
		surfaceDesc.ddsCaps.dwCaps = 4096;
		surfaceDesc.dwTextureStage = 0;
		surfaceDesc.dwWidth = width;
		surfaceDesc.dwHeight = height;
		memset(&findPixelFormat.minAlphaBits, 0, 12);

		if ((flags & BMP_TEXTURE_ALPHA_MASK) != 0)
		{
			findPixelFormat.needAlpha = 1;
			findPixelFormat.minAlphaBits = (flags & BMP_TEXTURE_ALPHA_BITMAP) != 0 ? 4 : 1;
		}

		d3dDevice->EnumTextureFormats(FindSuitablePixelFormat, &findPixelFormat);

		if (! findPixelFormat.valid || ddraw4->CreateSurface(&surfaceDesc, &bmpDataNode->surface, 0)
			|| bmpDataNode->surface->QueryInterface(IID_IDirect3DTexture2, (LPVOID*)&bmpDataNode->d3dTexture))
		{
			return 0;
		}

		uint32_t* texData = bmpDataNode->texData;

		memcpy(&bmpDataNode->surfaceDesc, &surfaceDesc, sizeof(bmpDataNode->surfaceDesc));

		if (texData)
			CopyTextureToSurface(bmpDataNode);

		return 1;
	}

	// FUNCTION: TOY2 0x004B0380 [MATCHED]
	LONG WINAPI FindSuitablePixelFormat(LPDDPIXELFORMAT pixelFormat, void* context)
	{
		if (! pixelFormat)
			return 1;

		if (! context)
			return 1;

		DWORD flags = pixelFormat->dwFlags;

		if ((flags & 0xE0000) != 0)
			return 1;

		DWORD bitCount = pixelFormat->dwRGBBitCount;

		if (bitCount < 16)
			return 1;

		if (pixelFormat->dwFourCC)
			return 1;

		FindPixelFormat* findPixelFormat = (FindPixelFormat*)context;

		uint32_t needAlpha = findPixelFormat->needAlpha;

		if (needAlpha == 1 && (flags & 1) == 0)
			return 1;

		if (! needAlpha && (flags & 1) != 0 || bitCount != findPixelFormat->bpp || needAlpha && CountAlphaBits(pixelFormat) < findPixelFormat->minAlphaBits)
		{
			return 1;
		}

		memcpy(findPixelFormat->out, pixelFormat, sizeof(DDPIXELFORMAT));

		findPixelFormat->valid = 1;

		return 0;
	}

	// FUNCTION: TOY2 0x004B0400 [MATCHED]
	int32_t CountAlphaBits(LPDDPIXELFORMAT pixelFormat)
	{
		DWORD alphaBitMask = pixelFormat->dwRGBAlphaBitMask;

		int32_t result;

		for (result = 0; alphaBitMask; alphaBitMask >>= 1)
		{
			if ((alphaBitMask & 1) != 0)
				++result;
		}

		return result;
	}

	// FUNCTION: TOY2 0x004B0440 [PROVISIONAL]
	BmpDataNode* BuildRawBmpNodeFromSlot(int32_t slotIndex, const char* textureName, int32_t alphaFlag)
	{
		BmpDataNode* bmpDataNode = AllocateBmpDataNode();
		if (! bmpDataNode)
			return bmpDataNode;

		int32_t textureWidth;
		int32_t textureHeight;
		uint32_t surfaceCaps;
		LPDIRECTDRAWSURFACE4 slotSurface;

		DrawingDevice::GetSlotTexSize(slotIndex, &textureWidth, &textureHeight);
		DrawingDevice::GetSlotSurfaceCaps(slotIndex, &surfaceCaps);
		DrawingDevice::GetSlotSurfaceByIndex(slotIndex, &slotSurface);

		bmpDataNode->sourceSlotIndex = slotIndex;
		bmpDataNode->sourceSurface = slotSurface;
		bmpDataNode->fallbackSurface = 0;

		if ((surfaceCaps & DDSCAPS_TEXTURE) == 0)
			bmpDataNode->slotSurfaceMode = BmpDataNode::SLOT_SURFACE_COPY;
		else
			bmpDataNode->slotSurfaceMode = BmpDataNode::SLOT_SURFACE_DIRECT;

		bmpDataNode->texData = 0;
		bmpDataNode->textureWidth = textureWidth;
		bmpDataNode->textureHeight = textureHeight;
		bmpDataNode->flags = alphaFlag;
		strcpy(bmpDataNode->texName, textureName);

		if (bmpDataNode->slotSurfaceMode == BmpDataNode::SLOT_SURFACE_DIRECT)
		{
			bmpDataNode->flags |= BMP_TEXTURE_BORROWED_SURFACE;
			bmpDataNode->surface = slotSurface;
			slotSurface->QueryInterface(IID_IDirect3DTexture2, (LPVOID*)&bmpDataNode->d3dTexture);
			bmpDataNode->surfaceDesc.dwSize = sizeof(DDSURFACEDESC2);
			slotSurface->GetSurfaceDesc(&bmpDataNode->surfaceDesc);
			return bmpDataNode;
		}

		InitialiseTextureSurface(bmpDataNode);

		DDBLTFX bltfx;
		bltfx.dwSize = sizeof(DDBLTFX);
		bltfx.dwROP = SRCCOPY;
		if (bmpDataNode->surface->Blt(NULL, bmpDataNode->sourceSurface, NULL, DDBLT_ROP | DDBLT_ASYNC, &bltfx) < 0)
		{
			DDSURFACEDESC2 surfaceDesc;
			memset(&surfaceDesc, 0, sizeof(surfaceDesc));
			surfaceDesc.dwSize = sizeof(DDSURFACEDESC2);
			surfaceDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
			surfaceDesc.ddsCaps.dwCaps = DDSCAPS_SYSTEMMEMORY;
			surfaceDesc.dwWidth = bmpDataNode->textureWidth;
			surfaceDesc.dwHeight = bmpDataNode->textureHeight;
			LPDIRECTDRAW4 ddraw4 = DrawingDevice::GetDDraw4();
			HRESULT result = ddraw4->CreateSurface(&surfaceDesc, &bmpDataNode->fallbackSurface, NULL);
			bmpDataNode->slotSurfaceMode = result >= 0 ? BmpDataNode::SLOT_SURFACE_FALLBACK : BmpDataNode::SLOT_SURFACE_DIRECT;
		}

		return bmpDataNode;
	}

	// FUNCTION: TOY2 0x004B0620 [MATCHED]
	BmpDataNode* BuildRawBmpNode(HBITMAP bitmap, HBITMAP alphaBitmap, const char* textureName, int32_t flags)
	{
		uint32_t* texData = ProcessBmpPixelData(bitmap, alphaBitmap, flags);
		if (texData)
		{
			BmpDataNode* bmpDataNode = AllocateBmpDataNode();
			if (bmpDataNode)
			{
				bmpDataNode->texData = texData;
				bmpDataNode->textureWidth = bmpDataNode->textureHeight = CalculateTexSize(bitmap, flags);
				bmpDataNode->bitmapWidth = GetBitmapWidth(bitmap);
				bmpDataNode->bitmapHeight = GetBitmapHeight(bitmap);
				bmpDataNode->flags = flags;
				strcpy(bmpDataNode->texName, textureName);
				InitialiseTextureSurface(bmpDataNode);

				return bmpDataNode;
			}

			free(texData);
		}

		return 0;
	}

	// FUNCTION: TOY2 0x004B06D0 [EFFECTIVE]
	int32_t CalculateTexSize(HANDLE bmp, int32_t flags)
	{
		BITMAP bitmap;
		GetObjectA(bmp, sizeof(BITMAP), &bitmap);

		int32_t height = bitmap.bmHeight;

		if (bitmap.bmWidth > bitmap.bmHeight)
			height = bitmap.bmWidth;

		int32_t textureSize;

		if (g_minTextureSize > Numerics::RoundUpToPowerOf2(height))
			textureSize = g_minTextureSize;
		else
			textureSize = Numerics::RoundUpToPowerOf2(height);

		if (textureSize >= g_maxTextureSize)
			textureSize = g_maxTextureSize;

		if (((flags == 0) & 16) != 0)
			textureSize >>= g_unusedTexShift;

		return textureSize;
	}
}
