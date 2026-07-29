#pragma once

#include "Common.h"

#include <directx6/d3d.h>
#include <directx6/ddraw.h>

struct D3DAppMode
{
	int32_t w;
	int32_t h;
	int32_t bpp;
	BOOL bThisDriverCanDo;
};

struct D3DAppRenderState
{
	BOOL bZBufferOn;
	BOOL bPerspCorrect;
	D3DSHADEMODE ShadeMode;
	D3DTEXTUREFILTER TextureFilter;
	D3DTEXTUREBLEND TextureBlend;
	D3DFILLMODE FillMode;
	BOOL bDithering;
	BOOL bSpecular;
	BOOL bAntialiasing;
	BOOL bFogEnabled;
	D3DCOLOR FogColor;
	D3DFOGMODE FogMode;
	D3DVALUE FogStart;
	D3DVALUE FogEnd;
};

struct D3DAppInfo
{
	HWND hwnd;
	uint8_t pad0[768];
	LPDIRECTDRAWSURFACE lpTextureSurf[15];
	LPDIRECT3DTEXTURE2 lpTexture[15];
	uint8_t pad1[136];
	uint8_t pad2[52276];
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
	int32_t bIsPrimary;
	LPDIRECTDRAWSURFACE3 lpFrontBuffer;
	LPDIRECTDRAWSURFACE3 lpBackBuffer;
	LPDIRECTDRAWSURFACE3 lpZBuffer;
	uint8_t bBackBufferInVideo;
	uint8_t bZBufferInVideo;
	uint8_t pad3[2];
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

STATIC_ASSERT(sizeof(D3DAppMode) == 0x10);
STATIC_ASSERT(sizeof(D3DAppRenderState) == 0x38);
STATIC_ASSERT(sizeof(D3DAppInfo) == 0xD0A0);
