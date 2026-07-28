#include "Nu3D/FMV.h"
#include "Logger.h"
#include <stdlib.h>

enum Nu3DFMVStateFlags
{
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
void Nu3D_FMV_SetDimensions(Nu3DFMVInstance* instance, int32_t left, int32_t top, int32_t right, int32_t bottom)
{
	instance->left = (float)left;
	instance->top = (float)top;
	instance->right = (float)right;
	instance->bottom = (float)bottom;
}

// FUNCTION: TOY2 0x004DB940 [MATCHED]
int32_t Nu3D_FMV_IsPlaying(Nu3DFMVInstance* instance) { return (instance->stateFlags & NU3D_FMV_PAUSED) == 0; }

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
