#include "Nu3D/FMV.h"
#include <stdlib.h>

enum Nu3DFMVStateFlags
{
	NU3D_FMV_PAUSED = 4
};

// FUNCTION: TOY2 0x004DB8C0 [MATCHED]
void Nu3D_FMV_Destroy(Nu3DFMVInstance* instance)
{
	if (instance->basicVideo != NULL)
	{
		instance->basicVideo->Release();
	}
	if (instance->videoWindow != NULL)
	{
		instance->videoWindow->Release();
	}
	if (instance->mediaEvent != NULL)
	{
		instance->mediaEvent->Release();
	}
	if (instance->mediaControl != NULL)
	{
		instance->mediaControl->Release();
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
