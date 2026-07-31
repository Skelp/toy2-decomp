#pragma once

#include "Common.h"

#include <directx6/d3d.h>
#include <directx6/ddraw.h>

struct DisplayMode
{
	int32_t w;
	int32_t h;
	int32_t bpp;
};

struct D3DTextureFormat
{
	int32_t isPalettized;
	int32_t hasAlphaPixels;
	int16_t indexBPP;
	int16_t rgbBPP;
	int16_t redBPP;
	int16_t blueBPP;
	int16_t greenBPP;
	int16_t alphaBPP;
	DDSURFACEDESC surfaceDesc;
};

struct InterfaceDevice
{
	int32_t valid;
	int32_t hasTexturing;
	int32_t isSquareTexturesOnly;
	int32_t isHardwareAccelerated;
	int32_t hasZBuffer;
	int32_t unkInt1;
	int32_t hasAlphaBlending;
	GUID guid;
	char baseName[64];
	char description[64];
	int32_t textureFormatCount;
	D3DTextureFormat textureFormats[14];
	uint8_t pad0[4];
	D3DDEVICEDESC hwDeviceDesc;
};

struct ExamineDevice
{
	int32_t valid;
	int32_t hasHardwareAccel;
	int32_t has3DSupport;
	int32_t hasVideoDMA;
	int32_t svbCaps;
	int32_t svbcKeyCaps;
	int32_t svbFxCaps;
	int32_t unkInt8;
	int32_t TexVidMemTotal;
	int32_t TexVidMemFree;
	int32_t hasLowVideoMem;
	int32_t isPrimaryDisplay;
	int32_t isPreferredDevice;
	GUID deviceGUID;
	DDCAPS ddCaps;
	char driverName[64];
	char driverDesc[64];
	int32_t displayModeCount;
	int32_t selectedDisplayModeIndex;
	DisplayMode displayModes[128];
	IDirectDraw2* DDDevice;
	IDirect3D2* D3DDevice;
	int32_t deviceCount;
	InterfaceDevice interfaceDevices[6];
};

struct PCProfile
{
	int32_t fullscreenMode;
	int32_t softwareRenderMode;
	int32_t ddDeviceCount;
	ExamineDevice examineDevices[4];
	ExamineDevice* DD;
	InterfaceDevice* D3D;
	DisplayMode* Mode;
	DisplayMode* oldMode;
};

enum RenderMode
{
	RENDERMODE_SOFTWARE = 1,
	RENDERMODE_D3D = 2,
};

extern int32_t g_checkAvailableMem;
extern int32_t g_no32bitColors;
extern PCProfile PC;
extern int16_t g_renderMode;

namespace Toy2
{
	extern int32_t g_skyColorRed;
	extern int32_t g_skyColorGreen;
	extern int32_t g_skyColorBlue;
	extern int32_t g_groundColorRed;
	extern int32_t g_groundColorGreen;
	extern int32_t g_groundColorBlue;
}

int32_t ExamineMachine();
void GetVidMem();
int32_t D3DInit(char* commandLine);
int32_t D3DRestart();
int32_t WINAPI ExamineDDEnumCallback(LPGUID guid, LPSTR driverDesc, LPSTR driverName, LPVOID lpContext);
HRESULT WINAPI ExamineDDModesEnumCallback(LPDDSURFACEDESC surfaceDesc, LPVOID context);
HRESULT WINAPI
ExamineD3DEnumCallback(LPGUID guid, LPSTR deviceDesc, LPSTR deviceName, LPD3DDEVICEDESC d3DHWDeviceDesc, LPD3DDEVICEDESC d3DHELDeviceDesc, LPVOID context);
int32_t SortDisplayModes(const void* modeA, const void* modeB);
LRESULT WINAPI ProfileWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

STATIC_ASSERT(sizeof(DisplayMode) == 0xC);
STATIC_ASSERT(sizeof(D3DTextureFormat) == 0x80);
STATIC_ASSERT(sizeof(InterfaceDevice) == 0x8B0);
STATIC_ASSERT(sizeof(ExamineDevice) == 0x3C74);
STATIC_ASSERT(sizeof(PCProfile) == 0xF1EC);
