#include "Nu3D/BmpDataNode.h"
#include "DrawingDevice.h"
#include "NGNLoader/NGNLoader.h"
#include "Renderer/Renderer.h"
#include "Logger.h"

// The bitmap node pool and the pixel format it serves: retail holds
// 0x004AFAF0 through 0x004AFCB0 as one object, in the order of this file.
#include "Nu3D/BmpDataNodeInternal.h"
namespace Nu3D
{
	// FUNCTION: TOY2 0x004AFAF0 [MATCHED]
	void SetMinTexSize(int32_t minTexSize) { g_minTextureSize = minTexSize; }

	// FUNCTION: TOY2 0x004AFB20 [MATCHED]
	void ReleaseBmpDataNode(BmpDataNode* bmpDataNode)
	{
		int32_t refCount = bmpDataNode->refCount;

		if (refCount)
		{
			bmpDataNode->refCount = refCount - 1;
		}
		else
		{
			BmpDataNode* next = bmpDataNode->next;

			if (next)
				next->prev = bmpDataNode->prev;

			BmpDataNode* prev = bmpDataNode->prev;

			if (prev)
				prev->next = bmpDataNode->next;
			else
				g_bmpDataHead = bmpDataNode->next;

			DestroyBmpDataNode(bmpDataNode);
			FreeTexData(bmpDataNode);

			if (bmpDataNode->bitmapHandle)
				DeleteObject(bmpDataNode->bitmapHandle);

			free(bmpDataNode);
		}
	}

	// FUNCTION: TOY2 0x004AFBA0 [MATCHED]
	void DestroyBmpDataNode(BmpDataNode* bmpDataNode)
	{
		LPDIRECT3DTEXTURE2 d3dTexture = bmpDataNode->d3dTexture;

		if (d3dTexture)
		{
			d3dTexture->Release();
			bmpDataNode->d3dTexture = 0;
		}

		if (bmpDataNode->surface)
		{
			if ((bmpDataNode->flags & BMP_TEXTURE_BORROWED_SURFACE) == 0)
			{
				bmpDataNode->surface->Release();
				bmpDataNode->surface = 0;
			}
		}
	}

	// FUNCTION: TOY2 0x004AFBE0 [MATCHED]
	void FreeTexData(BmpDataNode* bmpDataNode)
	{
		if (bmpDataNode->texData)
			free(bmpDataNode->texData);

		bmpDataNode->texData = 0;
	}

	// FUNCTION: TOY2 0x004AFC10 [MATCHED]
	BmpDataNode* AllocateBmpDataNode()
	{
		BmpDataNode* bmpDataStruct = (BmpDataNode*)malloc(sizeof(BmpDataNode));

		if (bmpDataStruct)
		{
			memset(bmpDataStruct, 0, sizeof(BmpDataNode));
			bmpDataStruct->next = g_bmpDataHead;

			if (g_bmpDataHead)
				g_bmpDataHead->prev = bmpDataStruct;

			bmpDataStruct->prev = 0;

			g_bmpDataHead = bmpDataStruct;

			bmpDataStruct->refCount = 0;
		}

		return bmpDataStruct;
	}

	// FUNCTION: TOY2 0x004AFC70 [MATCHED]
	BmpDataNode* GetBmpDataNodeByName(const char* texName)
	{
		BmpDataNode* nodeIter;

		for (nodeIter = g_bmpDataHead; nodeIter; nodeIter = nodeIter->next)
		{
			if (! strcmpi(texName, nodeIter->texName))
				break;
		}

		return nodeIter;
	}

	// FUNCTION: TOY2 0x004AFCB0 [MATCHED]
	void CalculatePixelFormatShifts(PixelFormatInfo* pixelFormatInfo, DDSURFACEDESC2* surfaceDesc)
	{
		uint32_t mask;

		pixelFormatInfo->alphaShift = -8;
		mask = surfaceDesc->ddpfPixelFormat.dwRGBAlphaBitMask;

		for (pixelFormatInfo->alphaMask = mask; mask; ++pixelFormatInfo->alphaShift)
			mask >>= 1;

		pixelFormatInfo->redShift = -8;
		mask = surfaceDesc->ddpfPixelFormat.dwRBitMask;

		for (pixelFormatInfo->redMask = mask; mask; ++pixelFormatInfo->redShift)
			mask >>= 1;

		pixelFormatInfo->greenShift = -8;
		mask = surfaceDesc->ddpfPixelFormat.dwGBitMask;

		for (pixelFormatInfo->greenMask = mask; mask; ++pixelFormatInfo->greenShift)
			mask >>= 1;

		pixelFormatInfo->blueShift = -8;
		mask = surfaceDesc->ddpfPixelFormat.dwBBitMask;

		for (pixelFormatInfo->blueMask = mask; mask; ++pixelFormatInfo->blueShift)
			mask >>= 1;
	}
}
