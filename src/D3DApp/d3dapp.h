#pragma once

#include "Common.h"

#include <directx6/d3d.h>
#include <directx6/ddraw.h>

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

struct InterfaceDevice;

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
	BOOL D3DAppGetRenderState(D3DAppRenderState* renderState);
	void D3DAppISetDefaults();
	void D3DAppISetClientSize(HWND hwnd, int width, int height, BOOL returnFromFullscreen);
	void D3DAppIGetClientWin(HWND hwnd);
	void D3DAppISetErrorString(char* format, ...);
	void __cdecl dpf(char* format, ...);
	char* D3DAppLastErrorString();
	BOOL D3DAppShowBackBuffer();
	BOOL D3DAppClearBackBuffer();
	BOOL D3DAppCheckForLostSurfaces();
	BOOL D3DAppPause(BOOL pause);
	BOOL D3DAppICreateSurface(LPDDSURFACEDESC surfaceDesc, LPDIRECTDRAWSURFACE3* surface);
	HRESULT D3DAppLastError();
	int32_t D3DAppWindowProc(WPARAM* wParamPtr, LPARAM* lParamPtr, HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	char* D3DAppErrorToString(HRESULT error);
}

STATIC_ASSERT(sizeof(D3DAppRenderState) == 0x38);
