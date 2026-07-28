#include "Nu3D/FMV.h"
#include "Logger.h"
#include "DrawingDevice.h"
#include <string.h>
#include <stdlib.h>

enum Nu3DFMVStateFlags
{
	NU3D_FMV_REPEAT = 2,
	NU3D_FMV_PAUSED = 4
};

// FUNCTION: TOY2 0x004DB8C0 [MATCHED]
void Nu3D_FMV_Destroy(Nu3DFMVInstance* instance)
{
	if (instance->videoSample != NULL)
	{
		instance->videoSample->Release();
	}
	if (instance->directDrawStream != NULL)
	{
		instance->directDrawStream->Release();
	}
	if (instance->primaryVideoStream != NULL)
	{
		instance->primaryVideoStream->Release();
	}
	if (instance->mediaStream != NULL)
	{
		instance->mediaStream->Release();
	}
	free(instance);
}

// FUNCTION: TOY2 0x004DB910 [MATCHED]
void Nu3D_FMV_SetDimensions(Nu3DFMVInstance* instance, int32_t left, int32_t top, int32_t width, int32_t height)
{
	instance->left = (float)left;
	instance->top = (float)top;
	instance->width = (float)width;
	instance->height = (float)height;
}

// FUNCTION: TOY2 0x004DB940 [MATCHED]
int32_t Nu3D_FMV_IsPlaying(Nu3DFMVInstance* instance) { return (instance->stateFlags & NU3D_FMV_PAUSED) == 0; }

// FUNCTION: TOY2 0x004DB950
void Nu3D_FMV_UpdateAndRenderFMV(Nu3DFMVInstance* instance)
{
	HRESULT result = instance->videoSample->Update(2, NULL, NULL, 0);
	switch (result)
	{
		case MS_S_PENDING:
			Logger::DebugLog("FMV - Update PENDING\r\n");
			break;
		case MS_S_ENDOFSTREAM:
			Logger::DebugLog("FMV - Update ENDOFSTREAM\r\n");
			break;
		case E_INVALIDARG:
			Logger::DebugLog("FMV - Update INVALIDARG\r\n");
			break;
		case E_POINTER:
			Logger::DebugLog("FMV - Update POINTER\r\n");
			break;
		case E_ABORT:
			Logger::DebugLog("FMV - Update ABORT\r\n");
			break;
		case MS_E_BUSY:
			Logger::DebugLog("FMV - Update BUSY\r\n");
			break;
		case S_OK:
			break;
		default:
			Logger::DebugLog("FMV - Update UNKNOWN\r\n");
			break;
	}

	if (result == MS_S_ENDOFSTREAM)
	{
		if ((instance->stateFlags & NU3D_FMV_REPEAT) == 0)
		{
			instance->mediaStream->SetState(STREAMSTATE_STOP);
			instance->mediaStream->Seek(0);
			instance->stateFlags |= NU3D_FMV_PAUSED;
		}
		else
		{
			instance->mediaStream->Seek(0);
		}
	}

	RECT destination;
	destination.left = (int32_t)instance->left;
	destination.right = (int32_t)(instance->left + instance->width) - 1;
	destination.top = (int32_t)instance->top;
	destination.bottom = (int32_t)(instance->top + instance->height) - 1;

	DDSURFACEDESC2 surfaceDesc;
	memset(&surfaceDesc, 0, sizeof(surfaceDesc));
	surfaceDesc.dwSize = sizeof(surfaceDesc);
	instance->renderSurface->GetSurfaceDesc(&surfaceDesc);

	RECT source;
	source.left = 0;
	source.top = 0;
	source.right = surfaceDesc.dwWidth - 1;
	source.bottom = surfaceDesc.dwHeight - 1;
	DrawingDevice::GetBackBuffer()->Blt(&destination, instance->renderSurface, &source, DDBLT_WAIT, NULL);
}

// FUNCTION: TOY2 0x004DBB80
void Nu3D_FMV_Seek(Nu3DFMVInstance* instance, int32_t seconds) { instance->mediaStream->Seek((STREAM_TIME)seconds * 10000000); }

// FUNCTION: TOY2 0x004DBB00 [MATCHED]
void Nu3D_FMV_SetPaused(Nu3DFMVInstance* instance, int32_t paused)
{
	HRESULT result;
	if (paused != 0)
	{
		instance->stateFlags |= NU3D_FMV_PAUSED;
		result = instance->mediaStream->SetState(STREAMSTATE_STOP);
		if (FAILED(result))
		{
			Logger::GetErrorHandler("C:\\projects\\nu3d\\fmv.c", 0x158)("Failed with HRESULT(0x%8.8X)\n", result);
		}
	}
	else
	{
		instance->stateFlags &= ~NU3D_FMV_PAUSED;
		result = instance->mediaStream->SetState(STREAMSTATE_RUN);
		if (FAILED(result))
		{
			Logger::GetErrorHandler("C:\\projects\\nu3d\\fmv.c", 0x15E)("Failed with HRESULT(0x%8.8X)\n", result);
		}
	}
}
