#pragma once

#include "D3DApp/d3dapp.h"

#include <stddef.h>

#define ATTEMPT(expression) \
	if (! (expression))     \
	goto exit_with_error

struct D3DAppMode
{
	int32_t w;
	int32_t h;
	int32_t bpp;
	BOOL bThisDriverCanDo;
};

struct D3DAppInfo
{
	HWND hwnd;
	int32_t TextureStatus[64];
	D3DTEXTUREHANDLE TextureHandle[64];
	uint8_t pad1[256];
	LPDIRECTDRAWSURFACE3 lpTextureSurf[64];
	uint8_t pad2[1288];
	D3DMATERIALHANDLE lpGroundMatHandle;
	uint8_t pad3[50984];
	int32_t unkInt1;
	int32_t unkInt2;
	int32_t unkInt3;
	int32_t unkInt4;
	int32_t unkInt5;
	int32_t unkInt6;
	int32_t unkInt7;
	LPDIRECT3D2 lpD3D;
	LPDIRECT3DDEVICE2 lpD3DDevice;
	LPDIRECT3DVIEWPORT2 lpD3DViewport;
	LPDIRECTDRAW2 lpDD;
	uint8_t bIsPrimary;
	uint8_t pad4[3];
	LPDIRECTDRAWSURFACE3 lpFrontBuffer;
	LPDIRECTDRAWSURFACE3 lpBackBuffer;
	LPDIRECTDRAWSURFACE3 lpZBuffer;
	uint8_t bBackBufferInVideo;
	uint8_t bZBufferInVideo;
	uint8_t pad5[2];
	D3DAppMode windowsDisplay;
	SIZE szClient;
	POINT pClientOnPrimary;
	uint8_t bPaused;
	uint8_t bAppActive;
	uint8_t bTexturesDisabled;
	uint8_t bOnlySystemMemory;
	uint8_t bOnlyEmulation;
	uint8_t bMinimized;
	uint8_t bRenderingIsOK;
};

extern D3DAppInfo d3dappi;
extern D3DAppRenderState d3dapprs;
extern BOOL bIgnoreWM_SIZE;
extern int32_t g_readyForRender;
extern BOOL bPaletteActivate;
extern BOOL bPrimaryPalettized;
extern LPDIRECTDRAWCLIPPER lpClipper;
extern LPDIRECTDRAWPALETTE lpPalette;
extern PALETTEENTRY ppe[256];
extern PALETTEENTRY Originalppe[256];
extern SIZE szBuffers;
extern BOOL (*D3DDeviceDestroyCallback)(LPVOID);
extern LPVOID D3DDeviceDestroyCallbackContext;
extern BOOL (*D3DDeviceCreateCallback)(int, int, LPDIRECT3DVIEWPORT2*, LPVOID);
extern LPVOID D3DDeviceCreateCallbackContext;
extern HRESULT LastError;
extern char LastErrorString[256];
extern uint16_t g_surfacesLost;
extern RECT g_frontBufferRects[30];
extern HFONT g_d3dAppFont;
extern D3DTEXTUREHANDLE g_masterTextureHandles[64];
extern int32_t g_masterTextureStatus[64];
extern LPVOID g_masterTextureData[64];
extern LPVOID g_textureData[64];
extern int32_t g_masterTextureFlags[64];
extern int32_t g_textureFlags[64];
extern int32_t g_masterTexturePaletteState[32];
extern int32_t g_texturePaletteState[32];

BOOL D3DAppIReleaseAllTextures();
BOOL D3DAppICreateFontSurfaces();
BOOL D3DAppCreate(DWORD flags, HWND hwnd, D3DAppInfo** d3dApp);

namespace Toy2
{
	int16_t InitDirect3DMaterials();
}

STATIC_ASSERT(sizeof(D3DAppMode) == 0x10);
STATIC_ASSERT(offsetof(D3DAppInfo, TextureStatus) == 0x4);
STATIC_ASSERT(offsetof(D3DAppInfo, TextureHandle) == 0x104);
STATIC_ASSERT(offsetof(D3DAppInfo, lpTextureSurf) == 0x304);
STATIC_ASSERT(offsetof(D3DAppInfo, lpGroundMatHandle) == 0x90C);
STATIC_ASSERT(sizeof(D3DAppInfo) == 0xD0A0);
