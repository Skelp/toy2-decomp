#include "Nu3D/BmpDataNode.h"
#include "DrawingDevice.h"
#include "NGNLoader/NGNLoader.h"
#include "Renderer/Renderer.h"
#include "Logger.h"
#include "Nu3D/BmpDataNodeInternal.h"

namespace Nu3D
{
	// GLOBAL: TOY2 0x00884044
	uint8_t g_lastBmpPalette[1024];

	// GLOBAL: TOY2 0x00884448
	int32_t g_minTextureSize;

	// GLOBAL: TOY2 0x00508214
	int32_t g_maxTextureSize = 256;

	// GLOBAL: TOY2 0x00508210
	int32_t g_unusedTexShift = 1;

	// GLOBAL: TOY2 0x00884444
	BmpDataNode* g_bmpDataHead;

	// GLOBAL: TOY2 0x0088400C
	BmpDataNode* g_currentBmpDataNode;

	// FUNCTION: TOY2 0x004B0760 [MATCHED]
	int32_t GetBitmapWidth(HANDLE bmpHandle)
	{
		BITMAP bmp;
		GetObjectA(bmpHandle, sizeof(BITMAP), &bmp);
		return bmp.bmWidth;
	}

	// FUNCTION: TOY2 0x004B0780 [MATCHED]
	int32_t GetBitmapHeight(HANDLE bmpHandle)
	{
		BITMAP bmp;
		GetObjectA(bmpHandle, sizeof(BITMAP), &bmp);
		return bmp.bmHeight;
	}

	// FUNCTION: TOY2 0x004B0870 [PROVISIONAL]
	RGBA SampleBitmapPixel(BITMAP* mainBmp, BITMAP* alphaBmp, int32_t x, int32_t y, int32_t textureSize, uint8_t flags)
	{
		RGBA color;
		int32_t bitsPerPixel = mainBmp->bmBitsPixel;

		if (bitsPerPixel == 8)
		{
			int32_t scaledY2 = y * mainBmp->bmHeight / textureSize;

			color.a = -1;

			int32_t paletteIndex = 4 * *((uint8_t*)mainBmp->bmBits + x * mainBmp->bmWidth / textureSize + mainBmp->bmWidthBytes * scaledY2);

			color.b = g_lastBmpPalette[paletteIndex];
			color.g = g_lastBmpPalette[paletteIndex + 1];
			color.r = g_lastBmpPalette[paletteIndex + 2];

			if ((flags & BMP_TEXTURE_TRANSPARENT_WHITE) != 0)
			{
				color.a = -(color.value != -1);
				return color;
			}

			if ((flags & BMP_TEXTURE_TRANSPARENT_BLACK) != 0)
			{
				color.a = -(color.value != 0xFF000000);
				return color;
			}

			if ((flags & BMP_TEXTURE_TRANSPARENT_GREEN) != 0 && color.value == 0xFF00FF00)
			{
				RGBA empty;
				empty.value = 0;

				return empty;
			}
		}
		else if (bitsPerPixel == 24)
		{
			int32_t scaledY = y * mainBmp->bmHeight / textureSize;

			color.a = 0xFF;

			uint8_t* pixelOffset = (uint8_t*)mainBmp->bmBits + 3 * (x * mainBmp->bmWidth / textureSize) + mainBmp->bmWidthBytes * scaledY;

			color.b = pixelOffset[0];
			color.g = pixelOffset[1];
			color.r = pixelOffset[2];

			if ((flags & BMP_TEXTURE_TRANSPARENT_WHITE) != 0)
			{
				color.a = -(color.value != -1);
			}
			else if ((flags & BMP_TEXTURE_TRANSPARENT_BLACK) != 0)
			{
				color.a = -(color.value != 0xFF000000);
			}
			else if ((flags & BMP_TEXTURE_TRANSPARENT_GREEN) != 0 && color.value == 0xFF00FF00)
			{
				color.value = 0;
			}

			if (alphaBmp && alphaBmp->bmBitsPixel == 8)
			{
				color.a = *((uint8_t*)alphaBmp->bmBits + x * alphaBmp->bmWidth / textureSize + alphaBmp->bmWidthBytes * (y * alphaBmp->bmHeight / textureSize));
				return color;
			}
		}

		return color;
	}

	// FUNCTION: TOY2 0x004B0B60 [PROVISIONAL]
	HBITMAP ProcessBmpInfoFromStream(FILE* stream)
	{
		int32_t offset = ftell(stream);

		BITMAPFILEHEADER bmpHeader;

		struct
		{
			BITMAPINFOHEADER bmiHeader;
			RGBQUAD bmiColors[256];
		} bmpInfo;

		fread(&bmpHeader, 1, sizeof(BITMAPFILEHEADER), stream);
		fread(&bmpInfo, 1, sizeof(BITMAPINFOHEADER), stream);

		size_t pixelDataSize;
		uint32_t calcHeight;

		switch (bmpInfo.bmiHeader.biBitCount)
		{
			case 1:
				fread(bmpInfo.bmiColors, 2, 4, stream);
				calcHeight = bmpInfo.bmiHeader.biHeight;
				break;
			case 4:
				fread(bmpInfo.bmiColors, 16, 4, stream);
				pixelDataSize = bmpInfo.bmiHeader.biWidth * bmpInfo.bmiHeader.biHeight / 2;
				goto READ_BMDATA;
			case 8:
				fread(bmpInfo.bmiColors, 256, 4, stream);

				memcpy(g_lastBmpPalette, bmpInfo.bmiColors, sizeof(g_lastBmpPalette));
				pixelDataSize = bmpInfo.bmiHeader.biWidth * bmpInfo.bmiHeader.biHeight;

				goto READ_BMDATA;
			default:
				calcHeight = bmpInfo.bmiHeader.biHeight * bmpInfo.bmiHeader.biBitCount;
				break;
		}

		pixelDataSize = bmpInfo.bmiHeader.biWidth * calcHeight / 8;

	READ_BMDATA:

		fseek(stream, offset + bmpHeader.bfOffBits, 0);

		void* bitmapBits;
		HBITMAP hBitmap = CreateDIBSection(0, (BITMAPINFO*)&bmpInfo, 0, &bitmapBits, 0, 0);

		if (hBitmap)
			fread(bitmapBits, pixelDataSize, 1, stream);

		return hBitmap;
	}

	// FUNCTION: TOY2 0x004B07A0 [PROVISIONAL]
	uint32_t* ProcessBmpPixelData(HANDLE mainBmp, HANDLE alphaBmp, int32_t flags)
	{
		int32_t texSize = CalculateTexSize(mainBmp, flags);

		BITMAP mainBmpObj;
		BITMAP alphaBmpObj;

		GetObjectA(mainBmp, sizeof(BITMAP), &mainBmpObj);

		if (alphaBmp)
			GetObjectA(alphaBmp, sizeof(BITMAP), &alphaBmpObj);

		uint32_t* buffer = (uint32_t*)malloc(4 * texSize * texSize);

		if (! buffer)
			return buffer;

		int32_t row = 0;

		if (texSize > 0)
		{
			int32_t rowOffset = 0;

			do
			{
				for (int32_t col = 0; col < texSize; ++col)
				{
					uint32_t pixelColor;

					if (alphaBmp)
						pixelColor = SampleBitmapPixel(&mainBmpObj, &alphaBmpObj, col, row, texSize, flags).value;
					else
						pixelColor = SampleBitmapPixel(&mainBmpObj, 0, col, row, texSize, flags).value;

					buffer[col + rowOffset] = pixelColor;
				}

				++row;
				rowOffset += texSize;

			} while (row < texSize);
		}

		return buffer;
	}

	// FUNCTION: TOY2 0x004AD030 [MATCHED]
	HRESULT BuildBmpNodeFromSlot(int32_t slotIndex, const char* textureName, int32_t alphaFlag)
	{
		return BuildRawBmpNodeFromSlot(slotIndex, textureName, alphaFlag) ? S_OK : E_OUTOFMEMORY;
	}

	// FUNCTION: TOY2 0x004AD060 [MATCHED]
	HRESULT BuildBmpNode(HBITMAP bitmap, const char* textureName, int32_t unused, int32_t flags)
	{
		return BuildRawBmpNode(bitmap, 0, textureName, flags) ? S_OK : E_OUTOFMEMORY;
	}

	// FUNCTION: TOY2 0x004AD090 [MATCHED]
	HRESULT BuildBmpNodeWithAlpha(HBITMAP bitmap, const char* textureName, int32_t unused, int32_t flags,
		HBITMAP alphaBitmap)
	{
		return BuildRawBmpNode(bitmap, alphaBitmap, textureName, flags) ? S_OK : E_OUTOFMEMORY;
	}

	// FUNCTION: TOY2 0x004B0A30 [PROVISIONAL]
	BmpDataNode* LoadTextureByStream(FILE* handle, const char* rawTexStr, int32_t flags)
	{
		HBITMAP mainBmp = ProcessBmpInfoFromStream(handle);
		HBITMAP alphaBmp = 0;

		if (! mainBmp)
			return 0;

		if ((flags & BMP_TEXTURE_ALPHA_BITMAP) != 0)
			alphaBmp = ProcessBmpInfoFromStream(handle);

		uint32_t* texDataBuffer = ProcessBmpPixelData(mainBmp, alphaBmp, flags);

		if (! texDataBuffer)
		{
			if (alphaBmp)
				DeleteObject(alphaBmp);

			DeleteObject(mainBmp);
			return 0;
		}

		BmpDataNode* bmpDataNode = AllocateBmpDataNode();

		if (! bmpDataNode)
		{
			free(texDataBuffer);

			if (alphaBmp)
				DeleteObject(alphaBmp);

			DeleteObject(mainBmp);
			return 0;
		}

		bmpDataNode->texData = texDataBuffer;

		int32_t texSize = CalculateTexSize(mainBmp, flags);

		bmpDataNode->textureHeight = texSize;
		bmpDataNode->textureWidth = texSize;
		bmpDataNode->bitmapWidth = GetBitmapWidth(mainBmp);
		bmpDataNode->bitmapHeight = GetBitmapHeight(mainBmp);
		bmpDataNode->flags = flags;

		strcpy(bmpDataNode->texName, rawTexStr);

		if (alphaBmp)
			DeleteObject(alphaBmp);

		if ((flags & BMP_TEXTURE_KEEP_BITMAP) != 0)
			bmpDataNode->bitmapHandle = mainBmp;
		else
			DeleteObject(mainBmp);

		InitialiseTextureSurface(bmpDataNode);

		return bmpDataNode;
	}

	// FUNCTION: TOY2 0x004AD0C0 [MATCHED]
	BmpDataNode* GetBmpDataNodeByName_T(const char* textureName)
	{
		return GetBmpDataNodeByName(textureName);
	}

	// FUNCTION: TOY2 0x004B0CF0 [MATCHED]
	void AddRef(BmpDataNode* bmpDataNode) { ++bmpDataNode->refCount; }

	// FUNCTION: TOY2 0x004AD130 [MATCHED]
	LPDIRECT3DTEXTURE2 GetTexture(BmpDataNode* bmpDataNode)
	{
		return bmpDataNode ? bmpDataNode->d3dTexture : 0;
	}

	// FUNCTION: TOY2 0x004AD1C0 [MATCHED]
	int32_t InitBmpNodeSurface(BmpDataNode* bmpDataNode, LPDIRECT3DDEVICE3 d3dDevice)
	{
		if (bmpDataNode)
		{
			InitialiseTextureSurface(bmpDataNode);
			return 0;
		}

		return -1;
	}

	// FUNCTION: TOY2 0x004B0C90 [MATCHED]
	BmpDataNode* LoadLocalBmpTexture(const char* rawTexStr, int32_t flags)
	{
		BmpDataNode* dataNode = GetBmpDataNodeByName(rawTexStr);

		if (dataNode)
		{
			AddRef(dataNode);
			return dataNode;
		}
		else
		{
			FILE* fileHandle = fopen(rawTexStr, "rb");

			if (fileHandle)
			{
				BmpDataNode* loadedNode = LoadTextureByStream(fileHandle, rawTexStr, flags);
				fclose(fileHandle);

				return loadedNode;
			}
			else
			{
				return 0;
			}
		}
	}

	// FUNCTION: TOY2 0x004AC1A0 [MATCHED]
	HRESULT SetTexture(int32_t stageIndex, BmpDataNode* bmpDataNode)
	{
		g_currentBmpDataNode = bmpDataNode;

		if (! Renderer::g_isSoftwareRendering)
		{
			if (bmpDataNode)
				return DrawingDevice::g_drawingDevice->m_pd3dDevice->SetTexture(stageIndex, bmpDataNode->d3dTexture);

			return DrawingDevice::g_drawingDevice->m_pd3dDevice->SetTexture(stageIndex, 0);
		}
		return 0;
	}

	// FUNCTION: TOY2 0x004B0D90 [PROVISIONAL]
	void GetDIBPixelColor(BGRA* color, DIBSECTION* dibSection, RGBQUAD* colorTables, int32_t* rowBasePtr, int32_t xOffset)
	{
		color->a = 255;

		WORD bitDepth = dibSection->dsBmih.biBitCount;

		if (bitDepth == 8)
		{
			uint8_t index = ((uint8_t*)rowBasePtr)[xOffset];
			color->b = colorTables[index].rgbBlue;
			color->g = colorTables[index].rgbGreen;
			color->r = colorTables[index].rgbRed;
		}
		else if (bitDepth == 24)
		{
			uint8_t* pixelPtr = (uint8_t*)rowBasePtr + 2 * xOffset + xOffset;

			color->b = *pixelPtr++;
			color->g = *pixelPtr;
			color->r = pixelPtr[1];
		}
	}

	// FUNCTION: TOY2 0x004B0E00 [PROVISIONAL]
	int32_t CopyToDDSurface(BmpDataNode* bmpDataNode, LPDIRECTDRAWSURFACE4 ddSurface)
	{
		int32_t result = 0;

		if (! bmpDataNode)
			return result;

		HBITMAP bmpHandle = bmpDataNode->bitmapHandle;
		if (! bmpHandle)
			return result;

		DIBSECTION dibSection;
		if (! GetObjectA(bmpHandle, sizeof(DIBSECTION), &dibSection))
			return result;

		DDSURFACEDESC2 surfaceDesc;
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(DDSURFACEDESC2);

		HRESULT lockResult;
		do
		{
			lockResult = ddSurface->Lock(0, &surfaceDesc, DDLOCK_WAIT, 0);
		} while (lockResult == DDERR_WASSTILLDRAWING);

		if (lockResult < 0)
		{
			Logger::LogD3DError(lockResult);
			return 0;
		}

		HDC dc = CreateCompatibleDC(0);

		if (dc)
		{
			RGBQUAD colorTables[256];
			HGDIOBJ oldObject = SelectObject(dc, bmpDataNode->bitmapHandle);

			if (dibSection.dsBmih.biBitCount == 8)
				GetDIBColorTable(dc, 0, 256, colorTables);

			PixelFormatInfo pixelFormat;
			CalculatePixelFormatShifts(&pixelFormat, &surfaceDesc);

			for (uint32_t currentRow = 0; currentRow < surfaceDesc.dwHeight; ++currentRow)
			{
				uint32_t width = surfaceDesc.dwWidth;
				uint32_t bitCount = surfaceDesc.ddpfPixelFormat.dwRGBBitCount;

				int32_t* srcRowPtr = (int32_t*)((uint8_t*)dibSection.dsBm.bmBits
					+ dibSection.dsBm.bmWidthBytes * (dibSection.dsBm.bmHeight - currentRow * dibSection.dsBm.bmHeight / surfaceDesc.dwHeight - 1));

				uint8_t* destRow = static_cast<uint8_t*>(surfaceDesc.lpSurface) + currentRow * surfaceDesc.lPitch;

				if (bitCount >= 15)
				{
					if (bitCount > 16)
					{
						if (bitCount == 32)
						{
							uint32_t* destPixel = reinterpret_cast<uint32_t*>(destRow);
							for (uint32_t xPixel = 0; xPixel < surfaceDesc.dwWidth; ++xPixel)
							{
								BGRA color;
								GetDIBPixelColor(&color, &dibSection, colorTables, srcRowPtr, xPixel * dibSection.dsBm.bmWidth / width);

								int32_t shiftedGreen;
								if (pixelFormat.greenShift < 0)
									shiftedGreen = color.g >> -(int8_t)pixelFormat.greenShift;
								else
									shiftedGreen = color.g << (int8_t)pixelFormat.greenShift;

								int32_t maskedGreen = pixelFormat.greenMask & shiftedGreen;

								int32_t shiftedBlue;
								if (pixelFormat.blueShift < 0)
									shiftedBlue = color.b >> -(int8_t)pixelFormat.blueShift;
								else
									shiftedBlue = color.b << (int8_t)pixelFormat.blueShift;

								int32_t maskedBlue = (pixelFormat.blueMask & shiftedBlue) | maskedGreen;

								int32_t shiftedRed;
								if (pixelFormat.redShift < 0)
									shiftedRed = color.r >> -(int8_t)pixelFormat.redShift;
								else
									shiftedRed = color.r << (int8_t)pixelFormat.redShift;

								*destPixel++ = maskedBlue | (pixelFormat.redMask & shiftedRed);

								width = surfaceDesc.dwWidth;
							}
						}
					}
					else
					{
						// 15 or 16 bit
						uint16_t* destPixel = reinterpret_cast<uint16_t*>(destRow);
						for (uint32_t xPixel = 0; xPixel < surfaceDesc.dwWidth; ++xPixel)
						{
							BGRA color;
							GetDIBPixelColor(&color, &dibSection, colorTables, srcRowPtr, xPixel * dibSection.dsBm.bmWidth / width);

							int32_t shiftedGreen;
							if (pixelFormat.greenShift < 0)
								shiftedGreen = color.g >> -(int8_t)pixelFormat.greenShift;
							else
								shiftedGreen = color.g << (int8_t)pixelFormat.greenShift;

							int16_t maskedGreen = (uint16_t)pixelFormat.greenMask & shiftedGreen;

							int32_t shiftedBlue;
							if (pixelFormat.blueShift < 0)
								shiftedBlue = color.b >> -(int8_t)pixelFormat.blueShift;
							else
								shiftedBlue = color.b << (int8_t)pixelFormat.blueShift;

							int16_t maskedBlue = ((uint16_t)pixelFormat.blueMask & shiftedBlue) | maskedGreen;

							int32_t shiftedRed;
							if (pixelFormat.redShift < 0)
								shiftedRed = color.r >> -(int8_t)pixelFormat.redShift;
							else
								shiftedRed = color.r << (int8_t)pixelFormat.redShift;

							*destPixel++ = maskedBlue | ((uint16_t)pixelFormat.redMask & shiftedRed);

							width = surfaceDesc.dwWidth;
						}

						bitCount = surfaceDesc.ddpfPixelFormat.dwRGBBitCount;
					}
				}

				result = 1;
			}

			SelectObject(dc, oldObject);
			DeleteDC(dc);
		}

		ddSurface->Unlock(0);

		return result;
	}

	// FUNCTION: TOY2 0x004AC260 [MATCHED]
	int32_t ReleaseBmpDataNode_T(BmpDataNode* bmpDataNode)
	{
		ReleaseBmpDataNode(bmpDataNode);
		return 0;
	}

	// FUNCTION: TOY2 0x004B1150 [MATCHED]
	void FreeAllBmpDataNodes()
	{
		for (BmpDataNode* curNode = g_bmpDataHead; g_bmpDataHead; curNode = g_bmpDataHead)
		{
			curNode->refCount = 0;
			ReleaseBmpDataNode(g_bmpDataHead);
		}
	}

	// FUNCTION: TOY2 0x004B1180 [MATCHED]
	void FreeAllBmpDataNodes_T() { FreeAllBmpDataNodes(); }

	// FUNCTION: TOY2 0x004BB1F0 [MATCHED]
	int32_t CreateTextureResourceFromSlot(int32_t slotIndex, const char* textureName, uint32_t flags)
	{
		NGNLoader::NGNTextureData* textureData = NGNLoader::AllocateTextureData();
		if (textureData)
		{
			textureData->textureFlags = 0;
			textureData->color.b = 0;
			textureData->color.g = 0;
			textureData->color.r = 0;
			textureData->bmpDataNode = 0;
			LPDIRECT3DDEVICE3 d3dDevice = DrawingDevice::GetD3DDevice();
			if (d3dDevice)
			{
				if (BuildBmpNodeFromSlot(slotIndex, textureName, (flags >> 1) & 1) == 0)
				{
					BmpDataNode* bmpDataNode = GetBmpDataNodeByName_T(textureName);
					textureData->bmpDataNode = bmpDataNode;
					if (bmpDataNode)
					{
						InitBmpNodeSurface(bmpDataNode, d3dDevice);
						return textureData->textureIndex;
					}
				}
			}
			NGNLoader::ReleaseTextureData(textureData);
		}
		return 0;
	}

	// FUNCTION: TOY2 0x004BB270 [MATCHED]
	int32_t CreateTextureResource(HBITMAP bitmapHandle, const char* textureName, int32_t flags)
	{
		NGNLoader::NGNTextureData* textureData = NGNLoader::AllocateTextureData();
		if (textureData)
		{
			textureData->textureFlags = 0;
			textureData->color.b = 0;
			textureData->color.g = 0;
			textureData->color.r = 0;
			textureData->bmpDataNode = 0;
			LPDIRECT3DDEVICE3 d3dDevice = DrawingDevice::GetD3DDevice();
			if (d3dDevice)
			{
				if (BuildBmpNode(bitmapHandle, textureName, 0, flags) == 0)
				{
					BmpDataNode* bmpDataNode = GetBmpDataNodeByName_T(textureName);
					textureData->bmpDataNode = bmpDataNode;
					if (bmpDataNode)
					{
						InitBmpNodeSurface(bmpDataNode, d3dDevice);
						return textureData->textureIndex;
					}
				}
			}
			NGNLoader::ReleaseTextureData(textureData);
		}
		return 0;
	}
}
