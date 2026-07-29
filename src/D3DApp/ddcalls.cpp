#include "D3DApp/d3dapp.h"
#include "Logger.h"
#include "Toy2/Direct6.h"

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
