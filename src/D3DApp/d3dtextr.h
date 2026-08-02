#pragma once

#include "Common.h"

#include <directx6/ddraw.h>
#include <directx6/d3d.h>
#include <stddef.h>
#include <windows.h>

struct TextureContainer
{
	HBITMAP bitmap;
	uint32_t width;
	LPDIRECTDRAWSURFACE4 surface;
	LPDIRECT3DTEXTURE2 texture;
	uint32_t stage;
	char name[80];
	uint32_t colorKey;
	uint32_t flags;
	uint8_t textureMetadata[40];
	uint32_t* rgbaData;
	DDSURFACEDESC2 surfaceDesc;
	uint32_t textureHandle;
	TextureContainer* prev;
	TextureContainer* next;

	~TextureContainer();
};

STATIC_ASSERT(sizeof(TextureContainer) == 0x120);
STATIC_ASSERT(offsetof(TextureContainer, rgbaData) == 0x94);
STATIC_ASSERT(offsetof(TextureContainer, surfaceDesc) == 0x98);
STATIC_ASSERT(offsetof(TextureContainer, prev) == 0x118);
STATIC_ASSERT(offsetof(TextureContainer, next) == 0x11C);
