#include "D3DApp/d3dapp.h"
#include "D3DApp/d3dappi.h"
#include "Logger.h"
#include "Toy2/Direct6.h"
#include "Toy2/Win95.h"

// Releases one interface and clears the pointer. The create and destroy paths
// state this form twenty two times.
#define RELEASE_AND_CLEAR(pointer) \
	if (pointer)                   \
	{                              \
		pointer->Release();        \
		pointer = NULL;            \
	}

// The window the Direct3D application draws into: retail holds 0x0040C1F0 as
// one object.
// FUNCTION: TOY2 0x0040C1F0 [PROVISIONAL]
BOOL D3DAppCreate(DWORD flags, HWND hwnd, D3DAppInfo** d3dApp)
{
	int32_t width;
	int32_t height;

	if (PC.softwareRenderMode)
	{
		d3dappi.bOnlySystemMemory = ! PC.DD->hasHardwareAccel;
	}
	else
	{
		d3dappi.bOnlySystemMemory = TRUE;
		if (PC.DD->hasHardwareAccel || PC.D3D->isHardwareAccelerated)
			d3dappi.bOnlySystemMemory = FALSE;
	}

	Logger::Log("SYSTEM : OnlySystemMemory status %s.\n", d3dappi.bOnlySystemMemory ? "SYSTEM MEMORY ONLY" : "VIDEO MEMORY ONLY");
	d3dappi.bIsPrimary = PC.DD->isPrimaryDisplay;

	RELEASE_AND_CLEAR(d3dappi.lpDD);

	HRESULT result;
	{
		LPDIRECTDRAW directDraw;
		result = DirectDrawCreate(&PC.DD->deviceGUID, &directDraw, NULL);
		if (result < 0)
			Logger::LogDDError("DirectDrawCreate(&PC.DD->Guid, &tempDD, 0)", result);

		result = directDraw->QueryInterface(IID_IDirectDraw2, (LPVOID*)&d3dappi.lpDD);
		if (result < 0)
			Logger::LogDDError("tempDD->QueryInterface(IID_IDirectDraw2, (LPVOID*)&d3dappi.lpDD)", result);
		if (directDraw)
			directDraw->Release();
	}

	HDC deviceContext = GetDC(NULL);
	GetSystemPaletteEntries(deviceContext, 0, 256, Originalppe);
	memcpy(ppe, Originalppe, sizeof(ppe));
	ReleaseDC(NULL, deviceContext);

	DDSURFACEDESC displayMode;
	memset(&displayMode, 0, sizeof(displayMode));
	displayMode.dwSize = sizeof(displayMode);
	result = d3dappi.lpDD->GetDisplayMode(&displayMode);
	if (result < 0)
		Logger::LogDDError("d3dappi.lpDD->GetDisplayMode(&ddsd)", result);
	d3dappi.windowsDisplay.w = displayMode.dwWidth;
	d3dappi.windowsDisplay.h = displayMode.dwHeight;
	d3dappi.windowsDisplay.bpp = displayMode.ddpfPixelFormat.dwRGBBitCount;

	if (! PC.softwareRenderMode)
	{
		result = d3dappi.lpDD->QueryInterface(IID_IDirect3D2, (LPVOID*)&d3dappi.lpD3D);
		if (result < 0)
			Logger::LogDDError("d3dappi.lpDD->QueryInterface(IID_IDirect3D2, (LPVOID *) &d3dappi.lpD3D)", result);
	}

	if (! PC.fullscreenMode)
	{
		d3dappi.pClientOnPrimary.x = d3dappi.pClientOnPrimary.y = 0;
		ClientToScreen(hwnd, &d3dappi.pClientOnPrimary);

		RECT clientRect;
		GetClientRect(hwnd, &clientRect);
		d3dappi.szClient.cx = clientRect.right;
		d3dappi.szClient.cy = clientRect.bottom;

		bIgnoreWM_SIZE = TRUE;
		result = d3dappi.lpDD->SetCooperativeLevel(hwnd, DDSCL_NORMAL);
		if (result < 0)
			Logger::LogDDError("d3dappi.lpDD->SetCooperativeLevel(hwnd, 0x00000008l)", result);
		bIgnoreWM_SIZE = FALSE;

		width = d3dappi.szClient.cx;
		height = d3dappi.szClient.cy;
		ATTEMPT(D3DAppICreateBuffers(hwnd, width, height, -100, FALSE, PC.DD->hasHardwareAccel));
	}
	else
	{
		width = PC.Mode->w;
		height = PC.Mode->h;
		Logger::Log("D3DAPPCREATE : Mode set to %dx%d.\n", width, height);

		d3dappi.pClientOnPrimary.x = d3dappi.pClientOnPrimary.y = 0;
		d3dappi.szClient.cx = width;
		d3dappi.szClient.cy = height;

		bIgnoreWM_SIZE = TRUE;
		result = d3dappi.lpDD->SetCooperativeLevel(hwnd, DDSCL_ALLOWMODEX | DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN);
		if (result < 0)
			Logger::LogDDError("d3dappi.lpDD->SetCooperativeLevel(hwnd, 0x00000010l | 0x00000001l | 0x00000040l)", result);

		szBuffers.cx = width;
		szBuffers.cy = height;
		result = d3dappi.lpDD->SetDisplayMode(width, height, PC.Mode->bpp, 0, 0);
		if (result < 0)
			Logger::LogDDError("d3dappi.lpDD->SetDisplayMode(w, h, PC.Mode->bpp, 0, 0)", result);
		Logger::Log("MODE: Width = %d Height = %d Bpp = %d.\n", width, height, PC.Mode->bpp);
		bIgnoreWM_SIZE = FALSE;

		ATTEMPT(D3DAppICreateBuffers(hwnd, width, height, PC.Mode->bpp, TRUE, PC.DD->hasHardwareAccel));
	}

	ATTEMPT(D3DAppICheckForPalettized());

	if (! PC.softwareRenderMode)
	{
		ATTEMPT(D3DAppICreateZBuffer(width, height));

		RELEASE_AND_CLEAR(d3dappi.lpD3DDevice);

		if (PC.D3D->isHardwareAccelerated && ! d3dappi.bBackBufferInVideo)
			Logger::LogLn("Could not fit the rendering surfaces in video memory for this hardware device.\n");

		PC.D3D->textureFormatCount = 0;
		{
			LPDIRECTDRAWSURFACE renderSurface;
			result = d3dappi.lpBackBuffer->QueryInterface(IID_IDirectDrawSurface, (LPVOID*)&renderSurface);
			if (result < 0)
				Logger::LogDDError("d3dappi.lpBackBuffer->QueryInterface(IID_IDirectDrawSurface, (void**)&tempsurf)", result);

			result = d3dappi.lpD3D->CreateDevice(PC.D3D->guid, renderSurface, &d3dappi.lpD3DDevice);
			if (result < 0)
				Logger::LogDDError("d3dappi.lpD3D->CreateDevice(PC.D3D->Guid, tempsurf, &d3dappi.lpD3DDevice)", result);
			if (renderSurface)
				renderSurface->Release();
		}

		if (PC.D3D->hasTexturing)
		{
			result = d3dappi.lpD3DDevice->EnumTextureFormats(reinterpret_cast<LPD3DENUMTEXTUREFORMATSCALLBACK>(CreateD3DEnumTextureFormatsCallback), PC.D3D);
			if (result < 0)
				Logger::LogDDError("d3dappi.lpD3DDevice->EnumTextureFormats(&CreateD3DEnumTextureFormatsCallback, (LPVOID) PC.D3D)", result);
		}

		LPDIRECT3DVIEWPORT2 viewport;
		result = d3dappi.lpD3D->CreateViewport(&viewport, NULL);
		if (result != D3D_OK)
		{
			Logger::LogLn("Create D3D viewport failed.\n%s", D3DAppErrorToString(result));
			Nullsub8();
		}
		else
		{
			result = d3dappi.lpD3DDevice->AddViewport(viewport);
			if (result != D3D_OK)
			{
				Logger::LogLn("Add D3D viewport failed.\n%s", D3DAppErrorToString(result));
				Nullsub8();
			}
			else
			{
				D3DVIEWPORT2 viewportData;
				memset(&viewportData, 0, sizeof(viewportData));
				viewportData.dwSize = sizeof(viewportData);
				viewportData.dwWidth = width;
				viewportData.dwHeight = height;
				viewportData.dvClipX = -1.0f;
				viewportData.dvClipWidth = 2.0f;
				viewportData.dvClipHeight = ((D3DVALUE)height * 2.0f) / (D3DVALUE)width;
				viewportData.dvClipY = viewportData.dvClipHeight * 0.5f;
				viewportData.dvMinZ = 0.0f;
				viewportData.dvMaxZ = 1.0f;

				result = viewport->SetViewport2(&viewportData);
				if (result != D3D_OK)
				{
					Logger::LogLn("SetViewport failed.\n%s", D3DAppErrorToString(result));
					Nullsub8();
				}
				else
				{
					d3dappi.lpD3DViewport = viewport;
					D3DAppICreateFontSurfaces();
				}
			}
		}
	}

	D3DAppIReleaseAllTextures();
	ATTEMPT(D3DAppISetRenderState());

	g_readyForRender = TRUE;
	d3dappi.bRenderingIsOK = TRUE;
	return TRUE;

exit_with_error:
	RELEASE_AND_CLEAR(g_windowData.fontMaskSurface);
	RELEASE_AND_CLEAR(g_windowData.fontSurface);
	if (g_d3dAppFont)
	{
		DeleteObject(g_d3dAppFont);
		g_d3dAppFont = NULL;
	}
	if (d3dappi.lpD3DViewport)
	{
		d3dappi.lpD3DDevice->DeleteViewport(d3dappi.lpD3DViewport);
		d3dappi.lpD3DViewport->Release();
		d3dappi.lpD3DViewport = NULL;
	}
	RELEASE_AND_CLEAR(d3dappi.lpD3DDevice);
	RELEASE_AND_CLEAR(d3dappi.lpZBuffer);
	RELEASE_AND_CLEAR(lpPalette);
	RELEASE_AND_CLEAR(lpClipper);
	RELEASE_AND_CLEAR(d3dappi.lpBackBuffer);
	RELEASE_AND_CLEAR(d3dappi.lpFrontBuffer);
	if (PC.fullscreenMode)
	{
		bIgnoreWM_SIZE = TRUE;
		result = d3dappi.lpDD->RestoreDisplayMode();
		if (result < 0)
			Logger::LogDDError("d3dappi.lpDD->RestoreDisplayMode()", result);
		bIgnoreWM_SIZE = TRUE;
		result = d3dappi.lpDD->SetCooperativeLevel(hwnd, DDSCL_NORMAL);
		if (result < 0)
			Logger::LogDDError("d3dappi.lpDD->SetCooperativeLevel(hwnd, 0x00000008l)", result);
		bIgnoreWM_SIZE = FALSE;
	}
	RELEASE_AND_CLEAR(d3dappi.lpD3D);
	RELEASE_AND_CLEAR(d3dappi.lpDD);
	return FALSE;
}
