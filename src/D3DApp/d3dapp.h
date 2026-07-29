#pragma once

#include "D3DApp/d3dappi.h"

extern "C"
{
	BOOL D3DAppIClearBuffers();
	void D3DAppISetErrorString(char* format, ...);
	char* D3DAppLastErrorString();
	BOOL D3DAppShowBackBuffer();
	int32_t D3DAppWindowProc(WPARAM* wParamPtr, LPARAM* lParamPtr, HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	char* D3DAppErrorToString(HRESULT error);
}
