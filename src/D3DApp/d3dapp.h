#pragma once

#include "D3DApp/d3dappi.h"

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
	void D3DAppISetDefaults();
	void D3DAppISetClientSize(HWND hwnd, int width, int height, BOOL returnFromFullscreen);
	void D3DAppIGetClientWin(HWND hwnd);
	void D3DAppISetErrorString(char* format, ...);
	void __cdecl dpf(char* format, ...);
	char* D3DAppLastErrorString();
	BOOL D3DAppShowBackBuffer();
	int32_t D3DAppWindowProc(WPARAM* wParamPtr, LPARAM* lParamPtr, HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	char* D3DAppErrorToString(HRESULT error);
}
