#include "D3DApp/d3dapp.h"
#include "Logger.h"
#include "Toy2/Direct6.h"

// FUNCTION: TOY2 0x0040B290 [MATCHED]
HRESULT D3DAppIGetSurfDesc(LPDDSURFACEDESC surfaceDesc, LPDIRECTDRAWSURFACE surface)
{
	memset(surfaceDesc, 0, sizeof(DDSURFACEDESC));
	surfaceDesc->dwSize = sizeof(DDSURFACEDESC);
	return surface->GetSurfaceDesc(surfaceDesc);
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
