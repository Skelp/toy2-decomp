#include "D3DApp/d3dapp.h"
#include "Logger.h"
#include "Toy2/Direct6.h"
#include "Toy2/Win95.h"

// GLOBAL: TOY2 0x0050AF64
int32_t g_changingCoopLevel = 0;

// GLOBAL: TOY2 0x0051B0B8
D3DAppInfo d3dappi;

// GLOBAL: TOY2 0x0050A770
int32_t g_readyForRender = 0;

// GLOBAL: TOY2 0x0051ABD4
int32_t g_usesPalette;

// GLOBAL: TOY2 0x0051AAC8
int32_t g_backBufferSupportsAlpha;

// GLOBAL: TOY2 0x0050AA58
LPDIRECTDRAWPALETTE g_lpPalette = 0;

// GLOBAL: TOY2 0x0051ABD0
HRESULT LastError;

// GLOBAL: TOY2 0x004E0698
char LastErrorString[256] = "ERROR NOT SET";

// Set when a presentation call reports that the DirectDraw surfaces were lost.
// GLOBAL: TOY2 0x0051A83C
uint16_t g_surfacesLost;

// Destination rectangles on the primary surface. D3DAppShowBackBuffer currently uses
// one entry, but the Direct3D application framework reserves a dirty-rectangle list.
// GLOBAL: TOY2 0x0051ABF0
RECT g_frontBufferRects[30];
// FUNCTION: TOY2 0x0040D2C0 [MATCHED]
char* D3DAppLastErrorString() { return LastErrorString; }

// FUNCTION: TOY2 0x0040CD80 [MATCHED]
BOOL D3DAppShowBackBuffer()
{
	if (! d3dappi.bRenderingIsOK)
	{
		D3DAppISetErrorString("Cannot call D3DAppShowBackBuffer while bRenderingIsOK is FALSE.\n");
		return FALSE;
	}

	if (d3dappi.bPaused)
		return TRUE;

	if (PC.fullscreenMode)
	{
		LastError = d3dappi.lpFrontBuffer->Flip(d3dappi.lpBackBuffer, DDFLIP_WAIT);
		if (LastError == DDERR_SURFACELOST)
		{
			g_surfacesLost = 1;
			d3dappi.lpFrontBuffer->Restore();
			d3dappi.lpBackBuffer->Restore();
			D3DAppIClearBuffers();
		}
		else if (LastError != DD_OK)
		{
			D3DAppISetErrorString("Flipping complex display surface failed.\n%s", D3DAppErrorToString(LastError));
			return FALSE;
		}
	}
	else
	{
		RECT backBufferRects[30];

		SetRect(&backBufferRects[0], 0, 0, d3dappi.szClient.cx, d3dappi.szClient.cy);
		SetRect(&g_frontBufferRects[0],
			d3dappi.pClientOnPrimary.x,
			d3dappi.pClientOnPrimary.y,
			d3dappi.szClient.cx + d3dappi.pClientOnPrimary.x,
			d3dappi.szClient.cy + d3dappi.pClientOnPrimary.y);

		for (int32_t i = 0; i < 1; i++)
		{
			LastError = d3dappi.lpFrontBuffer->Blt(&g_frontBufferRects[i], d3dappi.lpBackBuffer, &backBufferRects[i], DDBLT_WAIT, 0);
			if (LastError == DDERR_SURFACELOST)
			{
				g_surfacesLost = 1;
				d3dappi.lpFrontBuffer->Restore();
				d3dappi.lpBackBuffer->Restore();
				D3DAppIClearBuffers();
			}
			else if (LastError != DD_OK)
			{
				D3DAppISetErrorString("Blt of back buffer to front buffer failed.\n%s", D3DAppErrorToString(LastError));
				return FALSE;
			}
		}
	}

	return TRUE;
}

// FUNCTION: TOY2 0x0040CAC0 [PROVISIONAL]
int32_t D3DAppWindowProc(WPARAM* wParamPtr, LPARAM* lParamPtr, HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	int32_t result;
	PAINTSTRUCT paintStruct;

	*wParamPtr = 0;

	if (! g_readyForRender)
		return 1;

	if (msg > WM_ACTIVATEAPP)
	{
		if (msg > WM_NCPAINT)
		{
			if (msg == WM_MOVING && PC.fullscreenMode)
			{
				GetWindowRect(hWnd, reinterpret_cast<RECT*>(lParam));

				*lParamPtr = 1;
				*wParamPtr = 1;
			}
		}
		else
		{
			switch (msg)
			{
				case WM_NCPAINT:
					if (PC.fullscreenMode && ! d3dappi.bPaused)
					{
						result = 1;
						*lParamPtr = 0;
						*wParamPtr = 1;
						return result;
					}
					break;
				case WM_SETCURSOR:
					if (PC.fullscreenMode && ! d3dappi.bPaused)
					{
						result = 1;
						*lParamPtr = 1;
						*wParamPtr = 1;
						return result;
					}
					break;
				case WM_GETMINMAXINFO:

					MINMAXINFO* minMaxInfo = reinterpret_cast<MINMAXINFO*>(lParam);

					if (PC.fullscreenMode)
					{
						minMaxInfo->ptMaxTrackSize.x = PC.Mode->w;
						minMaxInfo->ptMaxTrackSize.y = PC.Mode->h;
						minMaxInfo->ptMinTrackSize.x = PC.Mode->w;
						minMaxInfo->ptMinTrackSize.y = PC.Mode->h;
					}
					else
					{
						minMaxInfo->ptMaxTrackSize.x = d3dappi.windowsDisplay.w;
						minMaxInfo->ptMaxTrackSize.y = d3dappi.windowsDisplay.h;
					}

					*lParamPtr = 0;
					*wParamPtr = 1;
					return 1;
			}
		}
		return 1;
	}

	if (msg == WM_ACTIVATEAPP)
	{
		d3dappi.bAppActive = 1;
		*wParamPtr = 1;
		return 1;
	}
	else
	{
		switch (msg)
		{
			case WM_MOVE:
				d3dappi.pClientOnPrimary.y = 0;
				d3dappi.pClientOnPrimary.x = 0;

				ClientToScreen(hWnd, &d3dappi.pClientOnPrimary);

				result = 1;
				break;

			case WM_SIZE:
				if (g_changingCoopLevel)
					return 1;

				*wParamPtr = 1;
				result = 1;
				break;

			case WM_ACTIVATE:
				if (! g_usesPalette || ! g_backBufferSupportsAlpha || ! d3dappi.lpFrontBuffer)
					return 1;

				d3dappi.lpFrontBuffer->SetPalette(g_lpPalette);

				result = 1;
				break;

			case WM_PAINT:
				BeginPaint(hWnd, &paintStruct);
				EndPaint(hWnd, &paintStruct);

				result = 1;

				*lParamPtr = 1;
				*wParamPtr = 1;
				break;

			default:
				return 1;
		}
	}
	return result;
}

// STUB: TOY2 0x0040D490
char* D3DAppErrorToString(HRESULT error) { return "Unimplemented"; }
