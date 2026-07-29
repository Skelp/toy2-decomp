#include "D3DApp/d3dapp.h"
#include "D3DApp/d3dappi.h"
#include "Logger.h"
#include "Toy2/Direct6.h"
#include "Toy2/Win95.h"

#include <cstdarg>
#include <cstdio>

// FUNCTION: TOY2 0x0040F740 [PROVISIONAL]
BOOL D3DAppICreateFontSurfaces()
{
	char modeText[] = "000x000x00 (MONO) 0000";
	char statsText[] = "000.00 fps 00000000.00 tps 0000.00 mppps";

	if (g_windowData.fontMaskSurface)
	{
		g_windowData.fontMaskSurface->Release();
		g_windowData.fontMaskSurface = NULL;
	}
	if (g_windowData.fontSurface)
	{
		g_windowData.fontSurface->Release();
		g_windowData.fontSurface = NULL;
	}
	if (g_d3dAppFont)
	{
		DeleteObject(g_d3dAppFont);
		g_d3dAppFont = NULL;
	}

	HDC deviceContext = GetDC(g_windowData.mainHwnd);
	g_d3dAppFont = NULL;
	if (deviceContext)
	{
		char faceName[64];
		LoadStringA(g_windowData.hInstance, 104, faceName, sizeof(faceName));
		memset(&g_d3dAppLogFont, 0, sizeof(g_d3dAppLogFont));
		g_d3dAppLogFont.lfHeight = -MulDiv(7, GetDeviceCaps(deviceContext, LOGPIXELSY), 72);
		g_d3dAppLogFont.lfWeight = FW_NORMAL;
		g_d3dAppLogFont.lfItalic = FALSE;
		strcpy(g_d3dAppLogFont.lfFaceName, faceName);
		g_d3dAppFont = CreateFontIndirectA(&g_d3dAppLogFont);
	}

	deviceContext = GetDC(NULL);
	SelectObject(deviceContext, g_d3dAppFont);
	GetTextExtentPointA(deviceContext, statsText, strlen(statsText), &g_d3dAppStatsTextSize);
	GetTextExtentPointA(deviceContext, modeText, strlen(modeText), &g_d3dAppModeTextSize);
	ReleaseDC(NULL, deviceContext);

	DDSURFACEDESC surfaceDesc;
	memset(&surfaceDesc, 0, sizeof(surfaceDesc));
	surfaceDesc.dwSize = sizeof(surfaceDesc);
	surfaceDesc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
	surfaceDesc.dwHeight = g_d3dAppStatsTextSize.cy;
	surfaceDesc.dwWidth = g_d3dAppStatsTextSize.cx;
	surfaceDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
	if (d3dappi.bOnlySystemMemory)
		surfaceDesc.ddsCaps.dwCaps |= DDSCAPS_SYSTEMMEMORY;

	LPDIRECTDRAWSURFACE tempSurface;
	HRESULT result = d3dappi.lpDD->CreateSurface(&surfaceDesc, &tempSurface, NULL);
	if (result < 0)
		Logger::LogDDError("lpDD->CreateSurface(desc, &tempsurf, punk)", result);
	result = tempSurface->QueryInterface(IID_IDirectDrawSurface3, (void**)&g_windowData.fontSurface);
	if (result < 0)
		Logger::LogDDError("tempsurf->QueryInterface(IID_IDirectDrawSurface3,(void**) surf)", result);
	if (tempSurface)
		tempSurface->Release();

	{
		DDCOLORKEY colorKey;
		colorKey.dwColorSpaceLowValue = 0;
		colorKey.dwColorSpaceHighValue = 0;
		g_windowData.fontSurface->SetColorKey(DDCKEY_SRCBLT, &colorKey);
	}

	memset(&surfaceDesc, 0, sizeof(surfaceDesc));
	surfaceDesc.dwSize = sizeof(surfaceDesc);
	surfaceDesc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
	surfaceDesc.dwHeight = g_d3dAppModeTextSize.cy;
	surfaceDesc.dwWidth = g_d3dAppModeTextSize.cx;
	surfaceDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
	if (d3dappi.bOnlySystemMemory)
		surfaceDesc.ddsCaps.dwCaps |= DDSCAPS_SYSTEMMEMORY;

	result = d3dappi.lpDD->CreateSurface(&surfaceDesc, &tempSurface, NULL);
	if (result < 0)
		Logger::LogDDError("lpDD->CreateSurface(desc, &tempsurf, punk)", result);
	result = tempSurface->QueryInterface(IID_IDirectDrawSurface3, (void**)&g_windowData.fontMaskSurface);
	if (result < 0)
		Logger::LogDDError("tempsurf->QueryInterface(IID_IDirectDrawSurface3,(void**) surf)", result);
	if (tempSurface)
		tempSurface->Release();

	{
		DDCOLORKEY colorKey;
		colorKey.dwColorSpaceLowValue = 0;
		colorKey.dwColorSpaceHighValue = 0;
		g_windowData.fontMaskSurface->SetColorKey(DDCKEY_SRCBLT, &colorKey);
	}
	return TRUE;
}

// FUNCTION: TOY2 0x0040BEC0 [MATCHED]
void D3DAppISetDefaults()
{
	d3dappi.bAppActive = TRUE;
	memset(&d3dapprs, 0, sizeof(d3dapprs));
	d3dapprs.bZBufferOn = TRUE;
	d3dapprs.bPerspCorrect = FALSE;
	d3dapprs.ShadeMode = D3DSHADE_GOURAUD;
	d3dapprs.TextureFilter = D3DFILTER_NEAREST;
	d3dapprs.FillMode = D3DFILL_SOLID;
	d3dapprs.bDithering = FALSE;
	d3dapprs.bSpecular = TRUE;
	d3dapprs.bAntialiasing = FALSE;
	d3dapprs.bFogEnabled = FALSE;
	d3dapprs.FogColor = 0;
	d3dapprs.FogMode = D3DFOG_LINEAR;
	d3dapprs.FogStart = 6.0f;
	d3dapprs.FogEnd = 11.0f;

	lpClipper = NULL;
	lpPalette = NULL;
	bPrimaryPalettized = FALSE;
	bPaletteActivate = FALSE;
	bIgnoreWM_SIZE = FALSE;
	memset(ppe, 0, sizeof(ppe));
	memset(Originalppe, 0, sizeof(Originalppe));
	LastError = DD_OK;
	memset(LastErrorString, 0, sizeof(LastErrorString));
	D3DDeviceDestroyCallback = NULL;
	D3DDeviceDestroyCallbackContext = NULL;
	D3DDeviceCreateCallback = NULL;
	D3DDeviceCreateCallbackContext = NULL;
}

// FUNCTION: TOY2 0x0040BFA0 [MATCHED]
void D3DAppISetClientSize(HWND hwnd, int width, int height, BOOL returnFromFullscreen)
{
	RECT windowRect;

	bIgnoreWM_SIZE = TRUE;
	if (returnFromFullscreen)
	{
		SetRect(&windowRect, 0, 0, width, height);
		AdjustWindowRectEx(&windowRect, GetWindowLongA(hwnd, GWL_STYLE), GetMenu(hwnd) != NULL, GetWindowLongA(hwnd, GWL_EXSTYLE));
		SetWindowPos(hwnd, NULL, 0, 0, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
		SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
	}
	else
	{
		SendMessageA(hwnd, WM_SIZE, SIZE_RESTORED, (width + height) << 16);
		GetWindowRect(hwnd, &windowRect);
		SetWindowPos(hwnd, NULL, 0, 0, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
		SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
	}
	bIgnoreWM_SIZE = FALSE;
	d3dappi.pClientOnPrimary.x = d3dappi.pClientOnPrimary.y = 0;
	ClientToScreen(hwnd, &d3dappi.pClientOnPrimary);
	d3dappi.szClient.cx = width;
	d3dappi.szClient.cy = height;
}

// FUNCTION: TOY2 0x0040C0B0 [MATCHED]
void D3DAppIGetClientWin(HWND hwnd)
{
	if (! PC.fullscreenMode)
	{
		RECT clientRect;
		d3dappi.pClientOnPrimary.x = d3dappi.pClientOnPrimary.y = 0;
		ClientToScreen(hwnd, &d3dappi.pClientOnPrimary);
		GetClientRect(hwnd, &clientRect);
		d3dappi.szClient.cx = clientRect.right;
		d3dappi.szClient.cy = clientRect.bottom;
	}
	else
	{
		d3dappi.pClientOnPrimary.x = d3dappi.pClientOnPrimary.y = 0;
		d3dappi.szClient.cx = PC.Mode->w;
		d3dappi.szClient.cy = PC.Mode->h;
	}
}

// FUNCTION: TOY2 0x0040C130 [MATCHED]
void D3DAppISetErrorString(char* format, ...)
{
	char buffer[256];
	va_list arguments;

	va_start(arguments, format);
	buffer[0] = '\0';
	sprintf(buffer, format, arguments);
	lstrcatA(buffer, "\r\n");
	lstrcpyA(LastErrorString, buffer);
	Logger::Log(buffer);
}

// FUNCTION: TOY2 0x0040C190 [MATCHED]
void __cdecl dpf(char* format, ...)
{
	char buffer[256];
	va_list arguments;

	lstrcpyA(buffer, "D3DApp: ");
	va_start(arguments, format);
	sprintf(&buffer[lstrlenA(buffer)], format, arguments);
	lstrcatA(buffer, "\r\n");
	OutputDebugStringA(buffer);
}
