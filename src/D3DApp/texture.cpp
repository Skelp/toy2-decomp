#include "D3DApp/d3dappi.h"
#include "D3DApp/d3dtextr.h"
#include "Logger.h"
#include "Nu3D/Nu3D.h"
#include "SaveManager.h"
#include "SoftwareRenderer.h"
#include "Toy2/Direct6.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

namespace SoftwareRenderer
{
	extern void* g_softwareTextureData[64];
}

// GLOBAL: TOY2 0x0051A840
int32_t g_masterTextureTypes[64];

// GLOBAL: TOY2 0x0051AACC
int32_t g_masterTextureStatus[64];

// GLOBAL: TOY2 0x0050A590
LPVOID g_masterTextureData[64];

// GLOBAL: TOY2 0x0051A9C8
LPVOID g_textureData[64];

// GLOBAL: TOY2 0x0051AFB4
int32_t g_masterTextureFlags[64];

// GLOBAL: TOY2 0x0050A778
int32_t g_textureFlags[64];

// GLOBAL: TOY2 0x0051A944
int16_t g_masterTexturePaletteState[64];

// GLOBAL: TOY2 0x0050A690
int16_t g_texturePaletteState[64];

// GLOBAL: TOY2 0x0050AA64
TextureDimensions g_textureDimensions[64];

// GLOBAL: TOY2 0x00884474
TextureContainer* g_textureList;

// GLOBAL: TOY2 0x00508288
char g_texturePath[512] = "MEDIA\\";

// GLOBAL: TOY2 0x00508488
char* g_textureRegistryValueName = "DX6SDK Samples Path";

// FUNCTION: TOY2 0x00497DB0 [MATCHED]
void AllocateTexturePixelBuffer(int16_t textureIndex)
{
	int32_t index = textureIndex;
	if (g_textureData[index] != NULL || textureIndex >= 0x40)
		return;

	switch (g_renderMode)
	{
		case RENDERMODE_SOFTWARE:
			if (SoftwareRenderer::g_bitsPerPixel != 8)
			{
				if (SoftwareRenderer::g_bitsPerPixel > 14 && SoftwareRenderer::g_bitsPerPixel <= 16)
				{
					uint16_t* pixelData = (uint16_t*)malloc(0x10000 * sizeof(uint16_t));
					g_textureData[index] = pixelData;
					SoftwareRenderer::g_softwareTextureData[index] = pixelData;

					int32_t pixelsRemaining = 0x10000;
					do
					{
						*pixelData++ = (0 << SoftwareRenderer::g_redShift) | (31 << SoftwareRenderer::g_greenShift);
					} while (--pixelsRemaining != 0);
				}
			}
			else
			{
				uint16_t* pixelData = (uint16_t*)malloc(g_textureDimensions[index].height * g_textureDimensions[index].width * sizeof(uint16_t));
				g_textureData[index] = pixelData;
				SoftwareRenderer::g_softwareTextureData[index] = pixelData;

				for (int32_t pixelIndex = 0; pixelIndex < g_textureDimensions[index].height * g_textureDimensions[index].width; ++pixelIndex)
				{
					*pixelData++ = 0;
				}
			}

			Logger::Log("MEM : SOFT Pixel buffer for tpage %d created.\n", index);
			break;

		case RENDERMODE_D3D:
			switch (g_textureFlags[index])
			{
				case 0: {
					uint8_t* pixelData = (uint8_t*)malloc(g_textureDimensions[index].height * g_textureDimensions[index].width * 3);
					g_textureData[index] = pixelData;
					SoftwareRenderer::g_softwareTextureData[index] = pixelData;

					for (int32_t pixelIndex = 0; pixelIndex < g_textureDimensions[index].height * g_textureDimensions[index].width; ++pixelIndex)
					{
						*pixelData++ = 0;
						*pixelData++ = 0xFF;
						*pixelData++ = 0;
					}
					break;
				}

				case 1: {
					uint8_t* pixelData = (uint8_t*)malloc(g_textureDimensions[index].height * g_textureDimensions[index].width);
					g_textureData[index] = pixelData;
					SoftwareRenderer::g_softwareTextureData[index] = pixelData;

					for (int32_t pixelIndex = 0; pixelIndex < g_textureDimensions[index].height * g_textureDimensions[index].width; ++pixelIndex)
					{
						*pixelData++ = 0;
					}
					break;
				}
			}

			Logger::Log("MEM : D3D Pixel buffer for tpage %d created.\n", index);
			break;
	}
}

// FUNCTION: TOY2 0x00409C80 [MATCHED]
BOOL D3DAppIReleaseAllTextures() { return TRUE; }

// FUNCTION: TOY2 0x004B1890 [PROVISIONAL]
TextureContainer::~TextureContainer()
{
	if (rgbaData != NULL)
	{
		free(rgbaData);
	}

	if (next != NULL)
	{
		delete next;
		next = NULL;
	}

	if (texture != NULL)
	{
		texture->Release();
		texture = NULL;
	}

	if (surface != NULL)
	{
		surface->Release();
		surface = NULL;
	}

	DeleteObject(bitmap);
}

// FUNCTION: TOY2 0x004B1980 [MATCHED]
LPDIRECTDRAWSURFACE4 D3DTextr_GetSurface(const char* name)
{
	TextureContainer* texture = D3DTextr_FindTexture(name);
	if (texture != NULL)
		return texture->surface;
	return NULL;
}

// FUNCTION: TOY2 0x004B19A0 [MATCHED]
TextureContainer* D3DTextr_FindTexture(const char* name)
{
	TextureContainer* texture = g_textureList;
	while (texture != NULL)
	{
		if (lstrcmpi(name, texture->name) == 0)
			return texture;
		texture = texture->next;
	}
	return NULL;
}

// FUNCTION: TOY2 0x004B19E0 [MATCHED]
LPDIRECTDRAWSURFACE4 D3DTextr_GetSurface(TextureContainer* texture)
{
	if (texture != NULL)
		return texture->surface;
	return NULL;
}

// FUNCTION: TOY2 0x004B1950 [PROVISIONAL]
void D3DTextr_DestroyAllTextures()
{
	TextureContainer* texture = g_textureList;
	if (texture != NULL)
	{
		delete texture;
		g_textureList = NULL;
	}
}

// FUNCTION: TOY2 0x004B19F0 [MATCHED]
LPDIRECT3DTEXTURE2 D3DTextr_GetTexture(const char* name)
{
	TextureContainer* texture = D3DTextr_FindTexture(name);
	if (texture != NULL)
		return texture->texture;
	return NULL;
}

// FUNCTION: TOY2 0x004B1A10 [MATCHED]
LPDIRECT3DTEXTURE2 D3DTextr_GetTexture(TextureContainer* texture)
{
	if (texture != NULL)
		return texture->texture;
	return NULL;
}

// FUNCTION: TOY2 0x004B1A20 [MATCHED]
uint32_t* D3DTextr_GetRGBAData(TextureContainer* texture)
{
	if (texture != NULL)
		return texture->rgbaData;
	return NULL;
}

// FUNCTION: TOY2 0x004B1A40 [MATCHED]
void D3DTextr_GetSurfaceDesc(TextureContainer* texture, DDSURFACEDESC2** surfaceDesc)
{
	*surfaceDesc = &texture->surfaceDesc;
}

// FUNCTION: TOY2 0x004B1A50 [MATCHED]
TextureContainer* D3DTextr_GetTextureContainer(const char* name)
{
	return D3DTextr_FindTexture(name);
}

// FUNCTION: TOY2 0x004B1A60 [MATCHED]
void D3DTextr_SetTexturePath(const char* path)
{
	if (path == NULL)
		path = &SaveManager::g_emptyString;

	strcpy(g_texturePath, path);
}

inline BOOL FileExists(char* name)
{
	FILE* file = fopen(name, "rb");
	return file != NULL ? fclose(file) == 0 : FALSE;
}

// FUNCTION: TOY2 0x004B1C50 [MATCHED]
static HRESULT FindTextureFile(char* filename, char* texturePath, char* fullPath)
{
	strcpy(fullPath, filename);
	if (FileExists(fullPath))
		return DD_OK;

	char* path = getenv("D3DPATH");
	if (path != NULL)
	{
		sprintf(fullPath, "%s\\%s", path, filename);
		if (FileExists(fullPath))
			return DD_OK;
	}

	HKEY key;
	LONG result = RegOpenKeyEx(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\DirectX", 0, KEY_READ, &key);
	if (result == ERROR_SUCCESS)
	{
		char path[512];
		DWORD type;
		DWORD size = sizeof(path);
		result = RegQueryValueEx(key, g_textureRegistryValueName, NULL, &type, (BYTE*)path, &size);
		RegCloseKey(key);

		if (result == ERROR_SUCCESS)
		{
			sprintf(fullPath, "%s\\D3DIM\\Media\\%s", path, filename);
			if (FileExists(fullPath))
				return DD_OK;

			sprintf(fullPath, "%s\\%s%s", path, texturePath, filename);
			if (FileExists(fullPath))
				return DD_OK;
		}
	}

	return DDERR_NOTFOUND;
}

// FUNCTION: TOY2 0x004B1B60 [MATCHED]
static HRESULT LoadTextureImage(TextureContainer* texture)
{
	char* filename = texture->name;
	char* extension = strrchr(filename, '.');
	char pathname[256];

	if (extension == NULL)
		return DDERR_UNSUPPORTED;

	texture->bitmap = (HBITMAP)LoadImage(GetModuleHandle(NULL), filename, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
	if (texture->bitmap != NULL)
		return S_OK;

	if (FAILED(FindTextureFile(filename, g_texturePath, pathname)))
		return DDERR_NOTFOUND;

	if (lstrcmpi(extension, ".bmp") == 0)
	{
		texture->bitmap = (HBITMAP)LoadImage(GetModuleHandle(NULL), pathname, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
		if (texture->bitmap == NULL)
		{
			texture->bitmap = (HBITMAP)LoadImage(NULL, pathname, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
		}

		return texture->bitmap != NULL ? DD_OK : DDERR_NOTFOUND;
	}

	return DDERR_UNSUPPORTED;
}

// FUNCTION: TOY2 0x004B1AA0 [PROVISIONAL]
HRESULT D3DTextr_CreateTexture(char* name, DWORD stage, DWORD flags)
{
	if (D3DTextr_FindTexture(name) != NULL)
		return S_OK;

	TextureContainer* texture = new TextureContainer;
	if (texture == NULL)
		return E_OUTOFMEMORY;

	ZeroMemory(texture, sizeof(TextureContainer));
	lstrcpy(texture->name, name);
	texture->stage = stage;
	texture->flags = flags;
	if (Nu3D::g_isSoftwareRendering)
		texture->flags |= 4;

	if (FAILED(LoadTextureImage(texture)))
	{
		delete texture;
		return E_FAIL;
	}

	if (g_textureList != NULL)
		g_textureList->prev = texture;
	texture->next = g_textureList;
	g_textureList = texture;

	return S_OK;
}
