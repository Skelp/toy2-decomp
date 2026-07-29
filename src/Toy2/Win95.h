#pragma once

#include "Common.h"

#include <directx6/ddraw.h>
#include <windows.h>

struct RenderStateCache
{
	int32_t zWriteEnable;
	int32_t texturePerspective;
	int32_t shadeMode;
	int32_t textureFilter;
	int32_t cullMode;
	int32_t fillMode;
	int32_t ditherEnable;
	int32_t specularEnable;
	int32_t antiAlias;
	int32_t fogEnable;
	int32_t fogColor;
	int32_t fogTableMode;
	float fogStart;
	float fogEnd;
};

struct WindowData
{
	MSG wndEventMsg;
	HACCEL hAccTable;
	HINSTANCE hInstance;
	HINSTANCE hPrev;
	char* lpCmdLine;
	HWND mainHwnd;
	WNDCLASSEXA wndClass;
	int32_t nShowCmd;
	RenderStateCache stateCache;
	int32_t unkInt1;
	int32_t unkInt2;
	int32_t unkInt3;
	int32_t unkInt4;
	int32_t unkInt5;
	int32_t unkInt6;
	int32_t unkInt7;
	int32_t wndIsExiting;
	int32_t unkInt8;
	LPDIRECTDRAWSURFACE3 fontSurface;
	LPDIRECTDRAWSURFACE3 fontMaskSurface;
};

extern WindowData g_windowData;
extern int32_t g_sysParamsInfo;

int32_t BuildWindow();
int32_t ProcessWndEvents();
int32_t PostQuitMessage();
LRESULT WINAPI NormalWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void SysParmsOnExit();
void LogErrorNotSet();
void Nullsub8();

STATIC_ASSERT(sizeof(RenderStateCache) == 0x38);
STATIC_ASSERT(sizeof(WindowData) == 0xC8);
