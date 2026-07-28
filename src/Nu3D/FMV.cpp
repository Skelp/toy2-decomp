#include "Nu3D/FMV.h"
#include "Logger.h"
#include "DrawingDevice.h"
#include "InputManager.h"
#include "Renderer/Renderer.h"
#include "Toy2/Toy2.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// GLOBAL: TOY2 0x004DD9C8
EXTERN_C const CLSID CLSID_AMMultiMediaStream = { 0x49C47CE5, 0x9BA4, 0x11D0, { 0x82, 0x12, 0x00, 0xC0, 0x4F, 0xC3, 0x2C, 0x45 } };
// GLOBAL: TOY2 0x004DD9D8
EXTERN_C const IID IID_IAMMultiMediaStream = { 0xBEBE595C, 0x9A6F, 0x11D0, { 0x8F, 0xDE, 0x00, 0xC0, 0x4F, 0xD9, 0x18, 0x9D } };
// GLOBAL: TOY2 0x004DD9A8
EXTERN_C const MSPID MSPID_PrimaryAudio = { 0xA35FF56B, 0x9FDA, 0x11D0, { 0x8F, 0xDF, 0x00, 0xC0, 0x4F, 0xD9, 0x18, 0x9D } };
// GLOBAL: TOY2 0x004DD9B8
EXTERN_C const MSPID MSPID_PrimaryVideo = { 0xA35FF56A, 0x9FDA, 0x11D0, { 0x8F, 0xDF, 0x00, 0xC0, 0x4F, 0xD9, 0x18, 0x9D } };
// GLOBAL: TOY2 0x004DD9E8
EXTERN_C const IID IID_IDirectDrawMediaStream = { 0xF4104FCE, 0x9A70, 0x11D0, { 0x8F, 0xDE, 0x00, 0xC0, 0x4F, 0xD9, 0x18, 0x9D } };

enum Nu3DFMVStateFlags
{
	NU3D_FMV_HAS_PRIMARY_VIDEO = 1,
	NU3D_FMV_REPEAT = 2,
	NU3D_FMV_PAUSED = 4
};

// GLOBAL: TOY2 0x00B62668
int32_t g_fmvComInitialized;

// FUNCTION: TOY2 0x004DB600
Nu3DFMVInstance* Nu3D_FMV_CreateFMVInstance(char* filename)
{
	Nu3DFMVInstance* instance = (Nu3DFMVInstance*)malloc(sizeof(Nu3DFMVInstance));
	if (instance != NULL)
	{
		memset(instance, 0, sizeof(Nu3DFMVInstance));

		if (g_fmvComInitialized == 0)
		{
			CoInitialize(NULL);
			g_fmvComInitialized = 1;
		}

		IAMMultiMediaStream* mediaStream;
		CoCreateInstance(CLSID_AMMultiMediaStream, NULL, CLSCTX_INPROC_SERVER, IID_IAMMultiMediaStream, (void**)&mediaStream);

		HRESULT result = mediaStream->Initialize(STREAMTYPE_READ, 0, NULL);
		if (FAILED(result))
		{
			Logger::GetErrorHandler("C:\\projects\\nu3d\\fmv.c", 0x82)("Failed ot initialize HRESULT(0x%8.8X)\n", result);
		}

		result = mediaStream->AddMediaStream(DrawingDevice::GetDDraw4(), &MSPID_PrimaryVideo, 0, NULL);
		if (FAILED(result))
		{
			Logger::GetErrorHandler("C:\\projects\\nu3d\\fmv.c", 0x87)("Failed to add video media stream HRESULT(0x%8.8X)\n", result);
		}

		result = mediaStream->AddMediaStream(NULL, &MSPID_PrimaryAudio, AMMSF_ADDDEFAULTRENDERER, NULL);
		if (FAILED(result))
		{
			Logger::GetErrorHandler("C:\\projects\\nu3d\\fmv.c", 0x8D)("Failed to add audio media stream HRESULT(0x%8.8X)\n", result);
		}

		WCHAR wideFilename[MAX_PATH];
		MultiByteToWideChar(CP_ACP, 0, filename, -1, wideFilename, MAX_PATH);
		result = mediaStream->OpenFile(wideFilename, 0);
		if (FAILED(result))
		{
			mediaStream->Release();
			free(instance);
			return NULL;
		}

		instance->mediaStream = mediaStream;
		mediaStream->AddRef();
		if (mediaStream == NULL)
		{
			printf("Could not create a CLSID_MultiMediaStream object\nCheck you have run regsvr32 amstream.dll\n");
		}
		mediaStream->Release();

		result = instance->mediaStream->GetMediaStream(MSPID_PrimaryVideo, &instance->primaryVideoStream);
		if (FAILED(result))
		{
			Logger::GetErrorHandler("C:\\projects\\nu3d\\fmv.c", 0xA7)("Failed to get media stream HRESULT(0x%8.8X)\n", result);
		}

		result = instance->primaryVideoStream->QueryInterface(IID_IDirectDrawMediaStream, (void**)&instance->directDrawStream);
		if (FAILED(result))
		{
			Logger::GetErrorHandler("C:\\projects\\nu3d\\fmv.c", 0xAD)("Failed to query DirectDrawMediaStream interface HRESULT(0x%8.8X)\n", result);
		}

		result = instance->directDrawStream->CreateSample(NULL, NULL, 0, &instance->videoSample);
		if (FAILED(result))
		{
			Logger::GetErrorHandler("C:\\projects\\nu3d\\fmv.c", 0xB3)("Failed to create sample HRESULT(0x%8.8X)\n", result);
		}

		RECT sourceRect;
		result = instance->videoSample->GetSurface(&instance->sourceSurface, &sourceRect);
		if (FAILED(result))
		{
			Logger::GetErrorHandler("C:\\projects\\nu3d\\fmv.c", 0xB9)("Failed get surface HRESULT(0x%8.8X)\n", result);
		}

		result = instance->sourceSurface->QueryInterface(IID_IDirectDrawSurface4, (void**)&instance->renderSurface);
		if (FAILED(result))
		{
			Logger::GetErrorHandler("C:\\projects\\nu3d\\fmv.c", 0xBF)("Failed to query DirectDrawSurface4 interface HRESULT(0x%8.8X)\n", result);
		}

		DWORD streamFlags = 2;
		if (instance->mediaStream->GetInformation(&streamFlags, NULL) == S_OK)
		{
			instance->stateFlags |= NU3D_FMV_HAS_PRIMARY_VIDEO;
		}
		else
		{
			instance->stateFlags &= ~NU3D_FMV_HAS_PRIMARY_VIDEO;
		}
	}
	return instance;
}

// FUNCTION: TOY2 0x004CE5F0 [MATCHED]
int32_t Nu3D_FMV_PlayMovie(char* filename)
{
	int32_t interrupted = 0;
	Renderer::ShowBlackFrames();
	Nu3DFMVInstance* instance = Nu3D_FMV_CreateFMVInstance(filename);
	if (instance != NULL)
	{
		Nu3D_FMV_SetDimensions(instance, 0, 0, DrawingDevice::GetDestWidth(), DrawingDevice::GetDestHeight());
		Nu3D_FMV_Seek(instance, 0);
		Nu3D_FMV_SetPaused(instance, 0);

		do
		{
			Toy2::ProcessMiscEvents();
			Nu3D_FMV_UpdateAndRenderFMV(instance);
			if (Renderer::BeginScene())
			{
				int32_t wasSoftwareRendering = Renderer::g_isSoftwareRendering;
				Renderer::g_isSoftwareRendering = 0;
				Renderer::EndScene(1);
				Renderer::g_isSoftwareRendering = wasSoftwareRendering;
			}

			if (InputManager::FindKeyReleased() != 0 || (InputManager::GetCurButtonsPressed() & 0xF000) != 0)
			{
				interrupted = 1;
			}
			Sleep(1);
		} while (Nu3D_FMV_IsPlaying(instance) != 0 && interrupted == 0);

		Nu3D_FMV_Destroy(instance);
	}
	return interrupted;
}

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
