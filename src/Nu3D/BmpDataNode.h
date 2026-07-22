#pragma once

#include "Common.h"
#include "Numerics.h"
#include <directx6/ddraw.h>
#include <directx6/d3d.h>
#include <windows.h>
#include <stdio.h>

namespace Nu3D
{
	struct BmpDataNode
	{
		LPDIRECTDRAWSURFACE4 surface;
		LPDIRECT3DTEXTURE2 d3dTexture;
		DDSURFACEDESC2 surfaceDesc;
		uint32_t* texData;
		int32_t textureWidth;
		int32_t textureHeight;
		uint32_t bitmapWidth;
		uint32_t bitmapHeight;
		HBITMAP bitmapHandle;
		int32_t unkVar1;
		int32_t unkVar2;
		int32_t unkVar3;
		int32_t unkVar4;
		char texName[80];
		int32_t unkVar5;
		int32_t flags;
		int32_t refCount;
		BmpDataNode* next;
		BmpDataNode* prev;
	};

	struct PixelFormatInfo
	{
		int32_t alphaShift;
		int32_t redShift;
		int32_t greenShift;
		int32_t blueShift;
		int32_t alphaMask;
		int32_t redMask;
		int32_t greenMask;
		int32_t blueMask;
	};

	struct FindPixelFormat
	{
		uint32_t bpp;
		int32_t minAlphaBits;
		uint32_t needAlpha;
		uint32_t valid;
		DDPIXELFORMAT* out;
	};

	extern BmpDataNode* g_currentBmpDataNode;

	void SetMinTexSize(int32_t minTexSize);
	HRESULT BuildBmpNode(HBITMAP bitmap, const char* textureName, int32_t unused, int32_t flags);
	HRESULT BuildBmpNodeWithAlpha(HBITMAP bitmap, const char* textureName, int32_t unused,
		int32_t flags, HBITMAP alphaBitmap);
	BmpDataNode* BuildRawBmpNode(HBITMAP bitmap, HBITMAP alphaBitmap, const char* textureName,
		int32_t flags);
	BmpDataNode* GetBmpDataNodeByName_T(const char* textureName);
	LPDIRECT3DTEXTURE2 GetTexture(BmpDataNode* bmpDataNode);
	int32_t InitBmpNodeSurface(BmpDataNode* bmpDataNode, int32_t unused);
	BmpDataNode* LoadTextureByStream(FILE* handle, const char* rawTexStr, int32_t flags);
	Nu3D::BmpDataNode* LoadLocalBmpTexture(const char* rawTexStr, int32_t flags);
	HRESULT SetTexture(int32_t stageIndex, BmpDataNode* bmpDataNode);
	int32_t CopyToDDSurface(BmpDataNode* bmpDataNode, LPDIRECTDRAWSURFACE4 ddSurface);
	int32_t ReleaseBmpDataNode_T(BmpDataNode* bmpDataNode);
	void FreeAllBmpDataNodes_T();

	STATIC_ASSERT(sizeof(BmpDataNode) == 0x110);
}
