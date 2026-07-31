#include "D3DApp/d3dapp.h"
#include "D3DApp/d3dappi.h"
#include "Logger.h"
#include "Toy2/Direct6.h"

// FUNCTION: TOY2 0x0040B290 [MATCHED]
HRESULT D3DAppIGetSurfDesc(LPDDSURFACEDESC surfaceDesc, LPDIRECTDRAWSURFACE3 surface)
{
	memset(surfaceDesc, 0, sizeof(DDSURFACEDESC));
	surfaceDesc->dwSize = sizeof(DDSURFACEDESC);
	return surface->GetSurfaceDesc(surfaceDesc);
}

// FUNCTION: TOY2 0x0040B2C0 [PROVISIONAL]
BOOL D3DAppICreateBuffers(HWND hwnd, int width, int height, int bpp, BOOL fullscreen, BOOL hardware)
{
	if (lpClipper)
	{
		lpClipper->Release();
		lpClipper = NULL;
	}
	if (d3dappi.lpBackBuffer)
	{
		d3dappi.lpBackBuffer->Release();
		d3dappi.lpBackBuffer = NULL;
	}
	if (d3dappi.lpFrontBuffer)
	{
		d3dappi.lpFrontBuffer->Release();
		d3dappi.lpFrontBuffer = NULL;
	}

	if (width < 50)
		width = 50;
	if (height < 50)
		height = 50;
	szBuffers.cx = width;
	szBuffers.cy = height;

	DDSURFACEDESC surfaceDesc;
	LPDIRECTDRAWSURFACE tempSurface;
	HRESULT result;

	if (fullscreen)
	{
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		surfaceDesc.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
		surfaceDesc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_3DDEVICE | DDSCAPS_COMPLEX;
		surfaceDesc.dwBackBufferCount = 1;
		if (hardware)
			surfaceDesc.ddsCaps.dwCaps |= DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM;
		if (d3dappi.bOnlySystemMemory)
			surfaceDesc.ddsCaps.dwCaps |= DDSCAPS_SYSTEMMEMORY;

		result = d3dappi.lpDD->CreateSurface(&surfaceDesc, &tempSurface, NULL);
		if (result < 0)
			Logger::LogDDError("lpDD->CreateSurface(desc, &tempsurf, punk)", result);
		result = tempSurface->QueryInterface(IID_IDirectDrawSurface3, (void**)&d3dappi.lpFrontBuffer);
		if (result < 0)
			Logger::LogDDError("tempsurf->QueryInterface(IID_IDirectDrawSurface3,(void**) surf)", result);
		if (tempSurface)
			tempSurface->Release();

		DDSCAPS surfaceCaps;
		surfaceCaps.dwCaps = DDSCAPS_BACKBUFFER;
		result = d3dappi.lpFrontBuffer->GetAttachedSurface(&surfaceCaps, &d3dappi.lpBackBuffer);
		if (result < 0)
			Logger::LogDDError("d3dappi.lpFrontBuffer->GetAttachedSurface(&ddscaps, &d3dappi.lpBackBuffer)", result);

		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		result = d3dappi.lpBackBuffer->GetSurfaceDesc(&surfaceDesc);
		if (result < 0)
			Logger::LogDDError("D3DAppIGetSurfDesc(&ddsd, d3dappi.lpBackBuffer)", result);
		d3dappi.bBackBufferInVideo = (surfaceDesc.ddsCaps.dwCaps & DDSCAPS_VIDEOMEMORY) != 0;
	}
	else
	{
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		surfaceDesc.dwFlags = DDSD_CAPS;
		surfaceDesc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

		result = d3dappi.lpDD->CreateSurface(&surfaceDesc, &tempSurface, NULL);
		if (result < 0)
			Logger::LogDDError("lpDD->CreateSurface(desc, &tempsurf, punk)", result);
		result = tempSurface->QueryInterface(IID_IDirectDrawSurface3, (void**)&d3dappi.lpFrontBuffer);
		if (result < 0)
			Logger::LogDDError("tempsurf->QueryInterface(IID_IDirectDrawSurface3,(void**) surf)", result);
		if (tempSurface)
			tempSurface->Release();

		surfaceDesc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS;
		surfaceDesc.dwWidth = width;
		surfaceDesc.dwHeight = height;
		surfaceDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE;
		if (hardware)
			surfaceDesc.ddsCaps.dwCaps |= DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM;
		else
			surfaceDesc.ddsCaps.dwCaps |= DDSCAPS_SYSTEMMEMORY;
		if (d3dappi.bOnlySystemMemory)
			surfaceDesc.ddsCaps.dwCaps |= DDSCAPS_SYSTEMMEMORY;

		result = d3dappi.lpDD->CreateSurface(&surfaceDesc, &tempSurface, NULL);
		if (result < 0)
			Logger::LogDDError("lpDD->CreateSurface(desc, &tempsurf, punk)", result);
		result = tempSurface->QueryInterface(IID_IDirectDrawSurface3, (void**)&d3dappi.lpBackBuffer);
		if (result < 0)
			Logger::LogDDError("tempsurf->QueryInterface(IID_IDirectDrawSurface3,(void**) surf)", result);
		if (tempSurface)
			tempSurface->Release();

		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		result = d3dappi.lpBackBuffer->GetSurfaceDesc(&surfaceDesc);
		if (result < 0)
			Logger::LogDDError("D3DAppIGetSurfDesc(&ddsd, d3dappi.lpBackBuffer)", result);
		d3dappi.bBackBufferInVideo = (surfaceDesc.ddsCaps.dwCaps & DDSCAPS_VIDEOMEMORY) != 0;

		result = d3dappi.lpDD->CreateClipper(0, &lpClipper, NULL);
		if (result < 0)
			Logger::LogDDError("d3dappi.lpDD->CreateClipper(0, &lpClipper, 0)", result);
		result = lpClipper->SetHWnd(0, hwnd);
		if (result < 0)
			Logger::LogDDError("lpClipper->SetHWnd(0, hwnd)", result);
		result = d3dappi.lpFrontBuffer->SetClipper(lpClipper);
		if (result < 0)
			Logger::LogDDError("d3dappi.lpFrontBuffer->SetClipper(lpClipper)", result);
	}

	D3DAppIClearBuffers();
	return TRUE;
}

// FUNCTION: TOY2 0x0040B670 [TOOL]
BOOL D3DAppICheckForPalettized()
{
	LPDIRECTDRAWSURFACE3 backBuffer = d3dappi.lpBackBuffer;
	DDSURFACEDESC surfaceDesc;
	memset(&surfaceDesc, 0, sizeof(surfaceDesc));
	surfaceDesc.dwSize = sizeof(surfaceDesc);
	HRESULT result = backBuffer->GetSurfaceDesc(&surfaceDesc);
	if (result < 0)
		Logger::LogDDError("D3DAppIGetSurfDesc(&ddsd, d3dappi.lpBackBuffer)", result);

	bPrimaryPalettized = (surfaceDesc.ddpfPixelFormat.dwFlags & DDPF_PALETTEINDEXED8) != 0;
	if (bPrimaryPalettized)
	{
		HDC deviceContext = GetDC(NULL);
		GetSystemPaletteEntries(deviceContext, 0, 256, ppe);
		ReleaseDC(NULL, deviceContext);

		int index;
		if (! PC.fullscreenMode)
		{
			for (index = 0; index < 10; ++index)
				ppe[index].peFlags = D3DPAL_READONLY;
			for (index = 10; index < 246; ++index)
				ppe[index].peFlags = D3DPAL_FREE | PC_RESERVED;
			for (index = 246; index < 256; ++index)
				ppe[index].peFlags = D3DPAL_READONLY;
		}
		else
		{
			ppe[0].peFlags = D3DPAL_READONLY;
			for (index = 1; index < 255; ++index)
				ppe[index].peFlags = D3DPAL_FREE | PC_RESERVED;
			ppe[255].peFlags = D3DPAL_READONLY;
		}

		result = d3dappi.lpDD->CreatePalette(0x00000004L | 0x00000008L, ppe, &lpPalette, NULL);
		if (result < 0)
			Logger::LogDDError("d3dappi.lpDD->CreatePalette(0x00000004l | 0x00000008l, ppe, &lpPalette, 0)", result);
		result = d3dappi.lpBackBuffer->SetPalette(lpPalette);
		if (result < 0)
			Logger::LogDDError("d3dappi.lpBackBuffer->SetPalette(lpPalette)", result);
		result = d3dappi.lpFrontBuffer->SetPalette(lpPalette);
		if (result < 0)
			Logger::LogDDError("d3dappi.lpFrontBuffer->SetPalette(lpPalette)", result);
		bPaletteActivate = TRUE;
	}
	return TRUE;
}

// FUNCTION: TOY2 0x0040B7E0 [TOOL]
BOOL D3DAppICreateZBuffer(int width, int height)
{
	if (d3dappi.lpZBuffer)
	{
		d3dappi.lpZBuffer->Release();
		d3dappi.lpZBuffer = NULL;
	}

	if (! PC.D3D->hasZBuffer)
		return TRUE;
	else
	{
		DDSURFACEDESC surfaceDesc;
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		surfaceDesc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS | DDSD_ZBUFFERBITDEPTH;
		surfaceDesc.ddsCaps.dwCaps = DDSCAPS_ZBUFFER;
		surfaceDesc.dwHeight = height;
		surfaceDesc.dwWidth = width;
		surfaceDesc.ddsCaps.dwCaps |= PC.D3D->isHardwareAccelerated ? DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM : DDSCAPS_SYSTEMMEMORY;

		DWORD deviceDepth = PC.D3D->hwDeviceDesc.dwDeviceZBufferBitDepth;
		if (deviceDepth & DDBD_32)
			surfaceDesc.dwZBufferBitDepth = 32;
		else if (deviceDepth & DDBD_24)
			surfaceDesc.dwZBufferBitDepth = 24;
		else if (deviceDepth & DDBD_16)
			surfaceDesc.dwZBufferBitDepth = 16;
		else if (deviceDepth & DDBD_8)
			surfaceDesc.dwZBufferBitDepth = 8;
		else
		{
			Logger::Log("Unsupported Z-buffer depth requested by device.\n");
			return FALSE;
		}

		LPDIRECTDRAWSURFACE surface;
		HRESULT result = d3dappi.lpDD->CreateSurface(&surfaceDesc, &surface, NULL);
		if (result < 0)
			Logger::LogDDError("d3dappi.lpDD->CreateSurface(&ddsd, &surf, 0)", result);
		result = surface->QueryInterface(IID_IDirectDrawSurface3, (void**)&d3dappi.lpZBuffer);
		if (result < 0)
			Logger::LogDDError("surf->QueryInterface(IID_IDirectDrawSurface3,(void**) &d3dappi.lpZBuffer)", result);
		surface->Release();

		result = d3dappi.lpBackBuffer->AddAttachedSurface(d3dappi.lpZBuffer);
		if (result < 0)
			Logger::LogDDError("d3dappi.lpBackBuffer->AddAttachedSurface(d3dappi.lpZBuffer)", result);
		LPDIRECTDRAWSURFACE3 zBuffer = d3dappi.lpZBuffer;
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		result = zBuffer->GetSurfaceDesc(&surfaceDesc);
		if (result < 0)
			Logger::LogDDError("D3DAppIGetSurfDesc(&ddsd, d3dappi.lpZBuffer)", result);
		d3dappi.bZBufferInVideo = (surfaceDesc.ddsCaps.dwCaps & DDSCAPS_VIDEOMEMORY) != 0;

		if (PC.D3D->isHardwareAccelerated && ! d3dappi.bZBufferInVideo)
		{
			D3DAppISetErrorString("Could not fit the Z-buffer in video memory for this hardware device.\n");
			if (d3dappi.lpZBuffer)
			{
				d3dappi.lpZBuffer->Release();
				d3dappi.lpZBuffer = NULL;
			}
			return FALSE;
		}
	}
	return TRUE;
}

// FUNCTION: TOY2 0x0040B9D0 [PROVISIONAL]
BOOL D3DAppISetCoopLevel(HWND hwnd, BOOL fullscreen)
{
	bIgnoreWM_SIZE = TRUE;

	if (fullscreen)
	{
		HRESULT result;
		if ((result = d3dappi.lpDD->SetCooperativeLevel(hwnd, 0x00000010L | 0x00000001L | 0x00000040L)) < 0)
			Logger::LogDDError("d3dappi.lpDD->SetCooperativeLevel(hwnd, 0x00000010l | 0x00000001l | 0x00000040l)", result);
	}
	else
	{
		HRESULT result;
		if ((result = d3dappi.lpDD->SetCooperativeLevel(hwnd, 0x00000008L)) < 0)
			Logger::LogDDError("d3dappi.lpDD->SetCooperativeLevel(hwnd, 0x00000008l)", result);
	}

	bIgnoreWM_SIZE = FALSE;
	return TRUE;
}

// FUNCTION: TOY2 0x0040BA30 [MATCHED]
BOOL D3DAppIRestoreDispMode()
{
	bIgnoreWM_SIZE = TRUE;

	HRESULT result = d3dappi.lpDD->RestoreDisplayMode();
	if (result < 0)
		Logger::LogDDError("d3dappi.lpDD->RestoreDisplayMode()", result);

	bIgnoreWM_SIZE = FALSE;
	return TRUE;
}

// FUNCTION: TOY2 0x0040BA70 [MATCHED]
BOOL D3DAppIRememberWindowsMode()
{
	DDSURFACEDESC surfaceDesc;
	memset(&surfaceDesc, 0, sizeof(surfaceDesc));
	surfaceDesc.dwSize = sizeof(surfaceDesc);

	HRESULT result = d3dappi.lpDD->GetDisplayMode(&surfaceDesc);
	if (result < 0)
		Logger::LogDDError("d3dappi.lpDD->GetDisplayMode(&ddsd)", result);

	d3dappi.windowsDisplay.w = surfaceDesc.dwWidth;
	d3dappi.windowsDisplay.h = surfaceDesc.dwHeight;
	d3dappi.windowsDisplay.bpp = surfaceDesc.ddpfPixelFormat.dwRGBBitCount;
	return TRUE;
}

// FUNCTION: TOY2 0x0040BAE0 [MATCHED]
BOOL D3DAppIClearBuffers()
{
	if (g_renderMode == RENDERMODE_SOFTWARE && PC.Mode->bpp == 8)
		return TRUE;

	DDSURFACEDESC ddsd;
	RECT dst;
	DDBLTFX ddbltfx;
	HRESULT result;

	if (d3dappi.lpFrontBuffer)
	{
		memset(&ddsd, 0, sizeof(ddsd));
		ddsd.dwSize = sizeof(ddsd);
		result = d3dappi.lpFrontBuffer->GetSurfaceDesc(&ddsd);
		if (result < 0)
			Logger::LogDDError("D3DAppIGetSurfDesc(&ddsd, d3dappi.lpFrontBuffer)", result);

		memset(&ddbltfx, 0, sizeof(ddbltfx));
		ddbltfx.dwSize = sizeof(ddbltfx);
		SetRect(&dst, 0, 0, ddsd.dwWidth, ddsd.dwHeight);
		result = d3dappi.lpFrontBuffer->Blt(&dst, 0, 0, DDBLT_COLORFILL | DDBLT_WAIT, &ddbltfx);
		if (result < 0)
			Logger::LogDDError("d3dappi.lpFrontBuffer->Blt(&dst, 0, 0, 0x00000400l | 0x01000000l, &ddbltfx)", result);
	}

	if (d3dappi.lpBackBuffer)
	{
		memset(&ddsd, 0, sizeof(ddsd));
		ddsd.dwSize = sizeof(ddsd);
		result = d3dappi.lpBackBuffer->GetSurfaceDesc(&ddsd);
		if (result < 0)
			Logger::LogDDError("D3DAppIGetSurfDesc(&ddsd, d3dappi.lpBackBuffer)", result);

		memset(&ddbltfx, 0, sizeof(ddbltfx));
		ddbltfx.dwSize = sizeof(ddbltfx);
		SetRect(&dst, 0, 0, ddsd.dwWidth, ddsd.dwHeight);
		result = d3dappi.lpBackBuffer->Blt(&dst, 0, 0, DDBLT_COLORFILL | DDBLT_WAIT, &ddbltfx);
		if (result < 0)
			Logger::LogDDError("d3dappi.lpBackBuffer->Blt(&dst, 0, 0, 0x00000400l | 0x01000000l, &ddbltfx)", result);
	}

	return TRUE;
}
