#pragma once

#include "D3DApp/d3dapp.h"

#include <stddef.h>

#define ATTEMPT(expression) \
	if (! (expression))     \
	goto exit_with_error

struct InterfaceDevice;

struct TextureDimensions
{
	int16_t width;
	int16_t height;
};

STATIC_ASSERT(sizeof(TextureDimensions) == 4);

extern "C"
{
	HRESULT D3DAppIGetSurfDesc(LPDDSURFACEDESC surfaceDesc, LPDIRECTDRAWSURFACE3 surface);
	BOOL D3DAppICreateBuffers(HWND hwnd, int width, int height, int bpp, BOOL fullscreen, BOOL hardware);
	BOOL D3DAppICheckForPalettized();
	BOOL D3DAppICreateZBuffer(int width, int height);
	BOOL D3DAppISetCoopLevel(HWND hwnd, BOOL fullscreen);
	BOOL D3DAppIRestoreDispMode();
	BOOL D3DAppIRememberWindowsMode();
	BOOL D3DAppIClearBuffers();
	BOOL D3DAppISetRenderState();
	HRESULT CALLBACK CreateD3DEnumTextureFormatsCallback(LPDDSURFACEDESC surfaceDesc, InterfaceDevice* d3dDevice);
	void D3DAppISetDefaults();
	void D3DAppISetClientSize(HWND hwnd, int width, int height, BOOL returnFromFullscreen);
	void D3DAppIGetClientWin(HWND hwnd);
	void D3DAppISetErrorString(char* format, ...);
	BOOL D3DAppICreateSurface(LPDDSURFACEDESC surfaceDesc, LPDIRECTDRAWSURFACE3* surface);
	void __cdecl dpf(char* format, ...);
}

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
	int32_t TextureType[64];
	D3DTEXTUREHANDLE TextureHandle[64];
	LPDIRECTDRAWSURFACE3 lpTextureSurf[64];
	LPDIRECT3DTEXTURE2 lpTexture[64];
	uint8_t pad1[512];
	LPDIRECT3DMATERIAL2 lpTextureMat[64];
	D3DMATERIALHANDLE lpTextureMatHandle[64];
	LPDIRECT3DMATERIAL2 lpSkyMat;
	LPDIRECT3DMATERIAL2 lpGroundMat;
	D3DMATERIALHANDLE lpGroundMatHandle;
	D3DMATERIALHANDLE lpSkyMatHandle;
	uint8_t pad2[51008];
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
extern LOGFONT g_d3dAppLogFont;
extern HFONT g_d3dAppFont;
extern SIZE g_d3dAppStatsTextSize;
extern SIZE g_d3dAppModeTextSize;
extern int32_t g_masterTextureTypes[64];
extern int32_t g_masterTextureStatus[64];
extern LPVOID g_masterTextureData[64];
extern LPVOID g_textureData[64];
extern int32_t g_masterTextureFlags[64];
extern int32_t g_textureFlags[64];
extern TextureDimensions g_textureDimensions[64];
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
STATIC_ASSERT(offsetof(D3DAppInfo, TextureType) == 0x104);
STATIC_ASSERT(offsetof(D3DAppInfo, TextureHandle) == 0x204);
STATIC_ASSERT(offsetof(D3DAppInfo, lpTextureSurf) == 0x304);
STATIC_ASSERT(offsetof(D3DAppInfo, lpTexture) == 0x404);
STATIC_ASSERT(offsetof(D3DAppInfo, lpTextureMat) == 0x704);
STATIC_ASSERT(offsetof(D3DAppInfo, lpTextureMatHandle) == 0x804);
STATIC_ASSERT(offsetof(D3DAppInfo, lpSkyMat) == 0x904);
STATIC_ASSERT(offsetof(D3DAppInfo, lpGroundMat) == 0x908);
STATIC_ASSERT(offsetof(D3DAppInfo, lpGroundMatHandle) == 0x90C);
STATIC_ASSERT(offsetof(D3DAppInfo, lpSkyMatHandle) == 0x910);
STATIC_ASSERT(offsetof(D3DAppInfo, lpD3D) == 0xD054);
STATIC_ASSERT(offsetof(D3DAppInfo, lpD3DDevice) == 0xD058);
STATIC_ASSERT(offsetof(D3DAppInfo, lpD3DViewport) == 0xD05C);
STATIC_ASSERT(offsetof(D3DAppInfo, lpDD) == 0xD060);
STATIC_ASSERT(offsetof(D3DAppInfo, bIsPrimary) == 0xD064);
STATIC_ASSERT(offsetof(D3DAppInfo, lpFrontBuffer) == 0xD068);
STATIC_ASSERT(offsetof(D3DAppInfo, lpBackBuffer) == 0xD06C);
STATIC_ASSERT(offsetof(D3DAppInfo, lpZBuffer) == 0xD070);
STATIC_ASSERT(offsetof(D3DAppInfo, bBackBufferInVideo) == 0xD074);
STATIC_ASSERT(offsetof(D3DAppInfo, bZBufferInVideo) == 0xD075);
STATIC_ASSERT(offsetof(D3DAppInfo, windowsDisplay) == 0xD078);
STATIC_ASSERT(offsetof(D3DAppInfo, szClient) == 0xD088);
STATIC_ASSERT(offsetof(D3DAppInfo, pClientOnPrimary) == 0xD090);
STATIC_ASSERT(offsetof(D3DAppInfo, bPaused) == 0xD098);
STATIC_ASSERT(offsetof(D3DAppInfo, bAppActive) == 0xD099);
STATIC_ASSERT(offsetof(D3DAppInfo, bTexturesDisabled) == 0xD09A);
STATIC_ASSERT(offsetof(D3DAppInfo, bOnlySystemMemory) == 0xD09B);
STATIC_ASSERT(offsetof(D3DAppInfo, bOnlyEmulation) == 0xD09C);
STATIC_ASSERT(offsetof(D3DAppInfo, bMinimized) == 0xD09D);
STATIC_ASSERT(offsetof(D3DAppInfo, bRenderingIsOK) == 0xD09E);
STATIC_ASSERT(sizeof(D3DAppInfo) == 0xD0A0);
