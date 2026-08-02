#include "D3DApp/d3dappi.h"
#include "D3DApp/d3dtextr.h"
#include "DrawingDevice.h"
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

// GLOBAL: TOY2 0x0050848C
int32_t g_minTextureAlphaBits = 4;

struct TextureSearchInfo
{
	DWORD desiredBpp;
	BOOL useAlpha;
	BOOL usePalette;
	BOOL useFourCC;
	BOOL found;
	LPDDPIXELFORMAT output;
};

static HRESULT CALLBACK D3DTextr_FindSuitablePixelFormat(LPDDPIXELFORMAT pixelFormat, LPVOID context);
static int32_t D3DTextr_CountAlphaBits(LPDDPIXELFORMAT pixelFormat);
static HRESULT CopyBitmapToTextureSurface(LPDIRECTDRAWSURFACE4 surface, HBITMAP bitmap, DWORD flags, HBITMAP alphaBitmap);

static LPDIRECTDRAW4 GetDirectDrawFromDevice(LPDIRECT3DDEVICE3 device)
{
	if (device == NULL)
		return NULL;

	LPDIRECTDRAWSURFACE4 renderTarget;
	if (FAILED(device->GetRenderTarget(&renderTarget)))
		return NULL;

	LPDIRECTDRAW4 directDraw = NULL;
	renderTarget->GetDDInterface((LPVOID*)&directDraw);
	renderTarget->Release();
	return directDraw;
}

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

// FUNCTION: TOY2 0x004B1E10 [PROVISIONAL]
HRESULT D3DTextr_CreateTextureFromBitmap(HBITMAP bitmap, HBITMAP alphaBitmap, char* name, DWORD stage, DWORD flags)
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

	texture->bitmap = bitmap;
	texture->alphaBitmap = alphaBitmap;
	if ((flags & 0xB) != 0 || alphaBitmap != NULL)
		texture->hasAlpha = TRUE;

	if (g_textureList != NULL)
		g_textureList->prev = texture;
	texture->next = g_textureList;
	g_textureList = texture;

	return S_OK;
}

// FUNCTION: TOY2 0x004B1EC0 [MATCHED]
HRESULT D3DTextr_RestoreTexture(char* name, LPDIRECT3DDEVICE3 device)
{
	if (device == NULL)
		return E_INVALIDARG;

	TextureContainer* texture = D3DTextr_FindTexture(name);
	if (texture == NULL)
		return DDERR_NOTFOUND;

	if (texture->texture != NULL)
	{
		texture->texture->Release();
		texture->texture = NULL;
	}
	if (texture->surface != NULL)
	{
		texture->surface->Release();
		texture->surface = NULL;
	}

	return D3DTextr_RestoreTextureContainer(texture, device);
}

// FUNCTION: TOY2 0x004B1F30 [PROVISIONAL]
HRESULT D3DTextr_RestoreTextureContainer(TextureContainer* texture, LPDIRECT3DDEVICE3 device)
{
	LPDIRECTDRAW4 directDraw = GetDirectDrawFromDevice(device);
	if (directDraw == NULL)
		return E_FAIL;
	directDraw->Release();

	D3DDEVICEDESC hardwareDesc;
	D3DDEVICEDESC softwareDesc;
	hardwareDesc.dwSize = sizeof(hardwareDesc);
	softwareDesc.dwSize = sizeof(softwareDesc);
	if (FAILED(device->GetCaps(&hardwareDesc, &softwareDesc)))
		return E_FAIL;

	DWORD textureCaps;
	if (hardwareDesc.dwFlags != 0)
		textureCaps = hardwareDesc.dpcTriCaps.dwTextureCaps;
	else
		textureCaps = softwareDesc.dpcTriCaps.dwTextureCaps;

	TextureSearchInfo searchInfo;
	HBITMAP bitmap = texture->bitmap;
	HBITMAP alphaBitmap = texture->alphaBitmap;
	BITMAP bitmapInfo;
	GetObject(bitmap, sizeof(bitmapInfo), &bitmapInfo);
	DWORD bitmapWidth = bitmapInfo.bmWidth;
	DWORD bitmapHeight = bitmapInfo.bmHeight;

	DDSURFACEDESC2 surfaceDesc;
	DrawingDevice::CD3DFramework::InitSurfaceDesc(&surfaceDesc, 0, 0);
	surfaceDesc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_TEXTURESTAGE;
	surfaceDesc.ddsCaps.dwCaps = DDSCAPS_TEXTURE;
	if (Nu3D::g_isSoftwareRendering)
		surfaceDesc.ddsCaps.dwCaps |= DDSCAPS_SYSTEMMEMORY;
	else
		surfaceDesc.ddsCaps.dwCaps2 = DDSCAPS2_TEXTUREMANAGE;

	surfaceDesc.dwTextureStage = texture->stage;
	surfaceDesc.dwWidth = bitmapWidth;
	surfaceDesc.dwHeight = bitmapHeight;

	if ((textureCaps & D3DPTEXTURECAPS_POW2) != 0)
	{
		for (surfaceDesc.dwWidth = 1; bitmapWidth > surfaceDesc.dwWidth; surfaceDesc.dwWidth <<= 1) {}
		for (surfaceDesc.dwHeight = 1; bitmapHeight > surfaceDesc.dwHeight; surfaceDesc.dwHeight <<= 1) {}
	}

	if ((textureCaps & D3DPTEXTURECAPS_SQUAREONLY) != 0)
	{
		if (surfaceDesc.dwWidth > surfaceDesc.dwHeight)
			surfaceDesc.dwHeight = surfaceDesc.dwWidth;
		else
			surfaceDesc.dwWidth = surfaceDesc.dwHeight;
	}

	BOOL usePalette = bitmapInfo.bmBitsPixel <= 8;
	BOOL useAlpha = FALSE;
	if (texture->hasAlpha)
	{
		useAlpha = TRUE;
		g_minTextureAlphaBits = texture->alphaBitmap != NULL ? 4 : 1;
	}

	if ((texture->flags & 0xB) != 0 && usePalette)
	{
		useAlpha = TRUE;
		if ((textureCaps & D3DPTEXTURECAPS_ALPHAPALETTE) == 0)
			usePalette = FALSE;
	}

	searchInfo.useAlpha = useAlpha;
	searchInfo.usePalette = usePalette;
	searchInfo.useFourCC = surfaceDesc.ddpfPixelFormat.dwFlags & DDPF_FOURCC;
	searchInfo.output = &surfaceDesc.ddpfPixelFormat;
	searchInfo.desiredBpp = 16;
	searchInfo.found = FALSE;
	if ((texture->flags & 4) != 0)
		searchInfo.desiredBpp = 32;
	device->EnumTextureFormats(D3DTextr_FindSuitablePixelFormat, &searchInfo);

	if (! searchInfo.found && usePalette)
	{
		searchInfo.desiredBpp = 16;
		searchInfo.usePalette = FALSE;
		searchInfo.found = FALSE;
		device->EnumTextureFormats(D3DTextr_FindSuitablePixelFormat, &searchInfo);
	}

	if (! searchInfo.found)
		return E_FAIL;

	if (Nu3D::g_isSoftwareRendering)
	{
		surfaceDesc.dwFlags |= DDSD_PITCH | DDSD_LPSURFACE;
		surfaceDesc.lPitch = surfaceDesc.dwWidth * 2;
		surfaceDesc.ddsCaps.dwCaps |= DDSCAPS_SYSTEMMEMORY;
		if ((texture->flags & 4) != 0)
			surfaceDesc.lPitch <<= 1;

		texture->rgbaData = (uint32_t*)malloc(surfaceDesc.lPitch * surfaceDesc.dwHeight);
		if (texture->rgbaData == NULL)
			return E_FAIL;
		surfaceDesc.lpSurface = texture->rgbaData;
	}

	if (FAILED(directDraw->CreateSurface(&surfaceDesc, &texture->surface, NULL)))
		return E_FAIL;

	if (FAILED(texture->surface->QueryInterface(IID_IDirect3DTexture2, (LPVOID*)&texture->texture)))
		return E_FAIL;

	memcpy(&texture->surfaceDesc, &surfaceDesc, sizeof(surfaceDesc));
	return CopyBitmapToTextureSurface(texture->surface, bitmap, texture->flags, alphaBitmap);
}

static HRESULT CALLBACK D3DTextr_FindSuitablePixelFormat(LPDDPIXELFORMAT pixelFormat, LPVOID context)
{
	if (pixelFormat == NULL || context == NULL)
		return D3DENUMRET_OK;

	TextureSearchInfo* searchInfo = (TextureSearchInfo*)context;
	DWORD formatFlags = pixelFormat->dwFlags;
	if ((formatFlags & (DDPF_LUMINANCE | DDPF_BUMPLUMINANCE | DDPF_BUMPDUDV)) != 0)
		return D3DENUMRET_OK;

	if (searchInfo->usePalette)
	{
		if ((formatFlags & DDPF_PALETTEINDEXED8) == 0)
			return D3DENUMRET_OK;
	}
	else
	{
		if (pixelFormat->dwRGBBitCount < 16)
			return D3DENUMRET_OK;

		if (searchInfo->useFourCC)
			return pixelFormat->dwFourCC == 0 ? D3DENUMRET_OK : D3DENUMRET_CANCEL;

		if (pixelFormat->dwFourCC != 0)
			return D3DENUMRET_OK;
		if (searchInfo->useAlpha == 1 && (formatFlags & DDPF_ALPHAPIXELS) == 0)
			return D3DENUMRET_OK;
		if (searchInfo->useAlpha == 0 && (formatFlags & DDPF_ALPHAPIXELS) != 0)
			return D3DENUMRET_OK;
		if (pixelFormat->dwRGBBitCount != searchInfo->desiredBpp)
			return D3DENUMRET_OK;
		if (searchInfo->useAlpha && D3DTextr_CountAlphaBits(pixelFormat) < g_minTextureAlphaBits)
			return D3DENUMRET_OK;
	}

	memcpy(searchInfo->output, pixelFormat, sizeof(DDPIXELFORMAT));
	searchInfo->found = TRUE;
	return D3DENUMRET_CANCEL;
}

static int32_t D3DTextr_CountAlphaBits(LPDDPIXELFORMAT pixelFormat)
{
	DWORD alphaMask = pixelFormat->dwRGBAlphaBitMask;
	int32_t count = 0;
	while (alphaMask != 0)
	{
		if ((alphaMask & 1) != 0)
			++count;
		alphaMask >>= 1;
	}
	return count;
}

static HRESULT CopyBitmapToTextureSurface(LPDIRECTDRAWSURFACE4 surface, HBITMAP bitmap, DWORD flags, HBITMAP alphaBitmap)
{
	LPDIRECTDRAW4 directDraw = NULL;
	surface->GetDDInterface((LPVOID*)&directDraw);
	if (directDraw == NULL)
		return S_OK;

	BITMAP bitmapInfo;
	GetObject(bitmap, sizeof(bitmapInfo), &bitmapInfo);

	DDSURFACEDESC2 textureDesc;
	DrawingDevice::CD3DFramework::InitSurfaceDesc(&textureDesc, 0, 0);
	surface->GetSurfaceDesc(&textureDesc);

	DDSURFACEDESC2 sourceDesc;
	DrawingDevice::CD3DFramework::InitSurfaceDesc(&sourceDesc, 0, 0);
	sourceDesc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_TEXTURESTAGE;
	sourceDesc.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_SYSTEMMEMORY;
	sourceDesc.dwWidth = bitmapInfo.bmWidth;
	sourceDesc.dwHeight = bitmapInfo.bmHeight;
	sourceDesc.ddpfPixelFormat = textureDesc.ddpfPixelFormat;

	LPDIRECTDRAWSURFACE4 sourceSurface;
	if (FAILED(directDraw->CreateSurface(&sourceDesc, &sourceSurface, NULL)))
	{
		directDraw->Release();
		return S_OK;
	}
	directDraw->Release();

	HDC bitmapDC = CreateCompatibleDC(NULL);
	if (bitmapDC == NULL)
	{
		sourceSurface->Release();
		return S_OK;
	}

	SelectObject(bitmapDC, bitmap);
	if (bitmapInfo.bmBitsPixel == 8)
	{
		RGBQUAD colors[256];
		UINT colorCount = GetDIBColorTable(bitmapDC, 0, 256, colors);
		PALETTEENTRY entries[256];
		for (UINT index = 0; index < colorCount; ++index)
		{
			entries[index].peRed = colors[index].rgbRed;
			entries[index].peGreen = colors[index].rgbGreen;
			entries[index].peBlue = colors[index].rgbBlue;
			entries[index].peFlags = 0xFF;
			DWORD color = RGB(colors[index].rgbRed, colors[index].rgbGreen, colors[index].rgbBlue);
			if (((flags & 2) != 0 && color == RGB(0, 0, 0)) || ((flags & 1) != 0 && color == RGB(255, 255, 255))
				|| ((flags & 8) != 0 && color == RGB(0, 255, 0)))
			{
				entries[index].peFlags = 0;
			}
		}

		LPDIRECTDRAWPALETTE palette = NULL;
		DWORD paletteFlags = DDPCAPS_8BIT;
		if ((flags & 3) != 0)
			paletteFlags |= DDPCAPS_ALPHA;
		if (SUCCEEDED(directDraw->CreatePalette(paletteFlags, entries, &palette, NULL)))
		{
			sourceSurface->SetPalette(palette);
			surface->SetPalette(palette);
			palette->Release();
		}
	}

	HDC surfaceDC;
	if (SUCCEEDED(sourceSurface->GetDC(&surfaceDC)))
	{
		BitBlt(surfaceDC, 0, 0, bitmapInfo.bmWidth, bitmapInfo.bmHeight, bitmapDC, 0, 0, SRCCOPY);
		sourceSurface->ReleaseDC(surfaceDC);
	}
	DeleteDC(bitmapDC);
	surface->Blt(NULL, sourceSurface, NULL, DDBLT_WAIT, NULL);
	sourceSurface->Release();

	if (textureDesc.ddpfPixelFormat.dwRGBAlphaBitMask != 0 && ((flags & 0xB) != 0 || alphaBitmap != NULL))
	{
		DDSURFACEDESC2 lockDesc;
		DrawingDevice::CD3DFramework::InitSurfaceDesc(&lockDesc, 0, 0);
		HRESULT lockResult;
		do
		{
			lockResult = surface->Lock(NULL, &lockDesc, 0, NULL);
		} while (lockResult == DDERR_WASSTILLDRAWING);

		if (SUCCEEDED(lockResult))
		{
			DWORD colorMask = lockDesc.ddpfPixelFormat.dwRBitMask | lockDesc.ddpfPixelFormat.dwGBitMask | lockDesc.ddpfPixelFormat.dwBBitMask;
			DWORD transparentColor = 0;
			if ((flags & 1) != 0)
				transparentColor = colorMask;
			if ((flags & 8) != 0)
				transparentColor = lockDesc.ddpfPixelFormat.dwGBitMask;

			DIBSECTION alphaInfo;
			ZeroMemory(&alphaInfo, sizeof(alphaInfo));
			if (alphaBitmap != NULL)
				GetObject(alphaBitmap, sizeof(alphaInfo), &alphaInfo);

			for (DWORD y = 0; y < lockDesc.dwHeight; ++y)
			{
				uint8_t* row = (uint8_t*)lockDesc.lpSurface + y * lockDesc.lPitch;
				uint8_t* alphaRow =
					alphaInfo.dsBm.bmBits != NULL ? (uint8_t*)alphaInfo.dsBm.bmBits + (lockDesc.dwHeight - y - 1) * alphaInfo.dsBm.bmWidthBytes : NULL;
				for (DWORD x = 0; x < lockDesc.dwWidth; ++x)
				{
					if (lockDesc.ddpfPixelFormat.dwRGBBitCount == 16)
					{
						uint16_t* pixel = (uint16_t*)row + x;
						if (alphaRow != NULL)
							*pixel = (*pixel & (uint16_t)colorMask) | ((uint16_t)alphaRow[x] << 8 & (uint16_t)lockDesc.ddpfPixelFormat.dwRGBAlphaBitMask);
						else if ((*pixel & (uint16_t)colorMask) != (uint16_t)transparentColor)
							*pixel |= (uint16_t)lockDesc.ddpfPixelFormat.dwRGBAlphaBitMask;
					}
					else if (lockDesc.ddpfPixelFormat.dwRGBBitCount == 32)
					{
						uint32_t* pixel = (uint32_t*)row + x;
						if (alphaRow != NULL)
							*pixel = (*pixel & colorMask) | ((uint32_t)alphaRow[x] << 24 & lockDesc.ddpfPixelFormat.dwRGBAlphaBitMask);
						else if ((*pixel & colorMask) != transparentColor)
							*pixel |= lockDesc.ddpfPixelFormat.dwRGBAlphaBitMask;
					}
				}
			}
			surface->Unlock(NULL);
		}
	}

	return S_OK;
}
