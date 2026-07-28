#pragma once

#include "Common.h"
#include <stddef.h>
#include <windows.h>

struct Nu3DFMVInstance
{
	IUnknown* mediaControl;
	IUnknown* mediaEvent;
	IUnknown* videoWindow;
	IUnknown* sourceSurface;
	IUnknown* basicVideo;
	IUnknown* renderSurface;
	uint32_t stateFlags;
	float left;
	float top;
	float right;
	float bottom;
};

int32_t Nu3D_FMV_IsPlaying(Nu3DFMVInstance* instance);
void Nu3D_FMV_Destroy(Nu3DFMVInstance* instance);
void Nu3D_FMV_SetDimensions(Nu3DFMVInstance* instance, int32_t left, int32_t top, int32_t right, int32_t bottom);

STATIC_ASSERT(offsetof(Nu3DFMVInstance, stateFlags) == 0x18);
STATIC_ASSERT(offsetof(Nu3DFMVInstance, left) == 0x1C);
STATIC_ASSERT(sizeof(Nu3DFMVInstance) == 0x2C);
