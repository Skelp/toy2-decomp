#pragma once

#include "NGNLoader/NGNTypes.h"
#include <windows.h>
#include <cstdio>

namespace NGNLoader
{
	extern NGNImage* g_ngnImage;
	extern NGNTextureData g_textureDataFreeList[2000];
	extern Vector3F g_vertexScaleVector;

	void SetNewImage(char* fileName);
	void Init();
	void DetectBackdropTextures();
	void BuildTex14(int32_t unused);
	void BuildGrid(int32_t gridWidth, int32_t gridHeight, int32_t type, NGNImage* ngnImage);
	void BuildScalerEntries(NGNImage* ngnImage);
	int32_t ExtractAnimations(FILE* stream, Nu3D::Creature* creature, uint32_t dataSize);
	int32_t ExtractShapePatch(FILE* stream, Nu3D::Creature* creature);
	int32_t GetTextureDataIndex(uint32_t textureIndex);
	uint32_t GetTextureDataIndexByName(char* textureName);
	NGNTextureData* GetTextureDataByIndex(uint32_t texDataIndex);
	void ReleaseTextureData(NGNTextureData* textureData);
	void ReleaseAllTextures();
	void ReleaseTextureCache(NGNTextureCache* textureCache);
	void RetrieveTextureData(
		int32_t texDataIndex, uint32_t* bitmapWidthOut, uint32_t* bitmapHeightOut, uint32_t* textureWidth, uint32_t* textureHeight, uint32_t** textureData);
	HBITMAP GetBmpHandle(int32_t index);
	int32_t CopyToDDSurfaceByIndex(int32_t texIndex, LPDIRECTDRAWSURFACE4 ddSurface);
	uint32_t GetOrAllocateTexture(NGNTextureParams* texParams);
}
