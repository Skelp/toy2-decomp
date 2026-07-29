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

extern "C"
{
	BOOL D3DAppGetRenderState(D3DAppRenderState* renderState);
	char* D3DAppLastErrorString();
	BOOL D3DAppShowBackBuffer();
	BOOL D3DAppClearBackBuffer();
	BOOL D3DAppCheckForLostSurfaces();
	BOOL D3DAppPause(BOOL pause);
	HRESULT D3DAppLastError();
	BOOL D3DAppDestroy();
	int32_t D3DAppWindowProc(WPARAM* stopProcessing, LPARAM* result, HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	char* D3DAppErrorToString(HRESULT error);
}

STATIC_ASSERT(sizeof(D3DAppRenderState) == 0x38);
