#ifndef BMPDATANODEINTERNAL_H
#define BMPDATANODEINTERNAL_H

#include "Nu3D/BmpDataNode.h"

#include <directx6/ddraw.h>

// State and entry points the bitmap objects share but that no public header
// states. BmpDataNode.cpp holds the definitions; the objects split out of it
// need the same view.
namespace Nu3D
{
	extern int32_t g_minTextureSize;
	extern int32_t g_maxTextureSize;
	extern int32_t g_unusedTexShift;
	extern BmpDataNode* g_bmpDataHead;

	BmpDataNode* AllocateBmpDataNode();
	void ReleaseBmpDataNode(BmpDataNode* bmpDataNode);
	void DestroyBmpDataNode(BmpDataNode* bmpDataNode);
	void FreeTexData(BmpDataNode* bmpDataNode);
	BmpDataNode* GetBmpDataNodeByName(const char* texName);
	void CalculatePixelFormatShifts(PixelFormatInfo* pixelFormatInfo, DDSURFACEDESC2* surfaceDesc);

	int32_t InitialiseTextureSurface(BmpDataNode* bmpDataNode);
	LONG WINAPI FindSuitablePixelFormat(LPDDPIXELFORMAT pixelFormat, void* context);
	int32_t CountAlphaBits(LPDDPIXELFORMAT pixelFormat);
	int32_t CalculateTexSize(HANDLE bmp, int32_t flags);

	int32_t GetBitmapWidth(HANDLE bmpHandle);
	int32_t GetBitmapHeight(HANDLE bmpHandle);
	uint32_t* ProcessBmpPixelData(HANDLE mainBmp, HANDLE alphaBmp, int32_t flags);
}

#endif
