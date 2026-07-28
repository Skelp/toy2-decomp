#pragma once

#include "Common.h"
#include <directx6/ddraw.h>
#include <amstream.h>
#include <ddstream.h>
#include <stddef.h>

struct Nu3DFMVInstance
{
	IAMMultiMediaStream* mediaStream;
	IMediaStream* primaryVideoStream;
	IDirectDrawMediaStream* directDrawStream;
	IDirectDrawSurface* sourceSurface;
	IDirectDrawStreamSample* videoSample;
	IDirectDrawSurface4* renderSurface;
	uint32_t stateFlags;
	float left;
	float top;
	float width;
	float height;
};

int32_t Nu3D_FMV_IsPlaying(Nu3DFMVInstance* instance);
void Nu3D_FMV_Destroy(Nu3DFMVInstance* instance);
void Nu3D_FMV_Seek(Nu3DFMVInstance* instance, int32_t seconds);
void Nu3D_FMV_SetPaused(Nu3DFMVInstance* instance, int32_t paused);
void Nu3D_FMV_UpdateAndRenderFMV(Nu3DFMVInstance* instance);
void Nu3D_FMV_SetDimensions(Nu3DFMVInstance* instance, int32_t left, int32_t top, int32_t width, int32_t height);

STATIC_ASSERT(offsetof(Nu3DFMVInstance, stateFlags) == 0x18);
STATIC_ASSERT(offsetof(Nu3DFMVInstance, left) == 0x1C);
STATIC_ASSERT(sizeof(Nu3DFMVInstance) == 0x2C);
