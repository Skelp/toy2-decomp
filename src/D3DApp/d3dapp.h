#pragma once

#include "D3DApp/d3dappi.h"

extern "C"
{
	HRESULT D3DAppIGetSurfDesc(LPDDSURFACEDESC surfaceDesc, LPDIRECTDRAWSURFACE surface);
	BOOL D3DAppISetCoopLevel(HWND hwnd, BOOL fullscreen);
	BOOL D3DAppIRestoreDispMode();
	BOOL D3DAppIRememberWindowsMode();
	BOOL D3DAppIClearBuffers();
	void D3DAppISetErrorString(char* format, ...);
	char* D3DAppLastErrorString();
	BOOL D3DAppShowBackBuffer();
	int32_t D3DAppWindowProc(WPARAM* wParamPtr, LPARAM* lParamPtr, HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	char* D3DAppErrorToString(HRESULT error);
}
