#include "Toy2/Direct6.h"
#include "D3DApp/d3dappi.h"
#include "DrawingDevice.h"
#include "Logger.h"
#include "ModeSelect.h"
#include "Renderer/Renderer.h"
#include "Toy2/Toy2.h"
#include "Toy2/Win95.h"
#include <cstdarg>
#include <cstdio>

// GLOBAL: TOY2 0x004E0690
int32_t g_checkAvailableMem = 1;

// GLOBAL: TOY2 0x00884038
int32_t g_no32bitColors = 0;

// GLOBAL: TOY2 0x0050B650
PCProfile PC;

// GLOBAL: TOY2 0x00534554
int16_t g_renderMode;
// FUNCTION: TOY2 0x004093A0 [PROVISIONAL]
int32_t ExamineMachine()
{
	memset(&PC, 0, sizeof(PC));

	Logger::Log("---------------------\n");
	Logger::Log("BEGIN EXAMINE MACHINE\n\n");

	int32_t count = 0;

	WNDCLASSA wndClass;
	wndClass.style = CS_HREDRAW | CS_VREDRAW;
	wndClass.lpfnWndProc = ProfileWndProc;
	wndClass.cbClsExtra = 0;
	wndClass.cbWndExtra = 0;
	wndClass.hInstance = g_windowData.hInstance;
	wndClass.hIcon = LoadIconA(0, IDI_WINLOGO);
	wndClass.hCursor = LoadCursorA(0, IDC_ARROW);
	wndClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wndClass.lpszMenuName = "";
	wndClass.lpszClassName = "Examining Machine";

	if (! RegisterClassA(&wndClass))
		return 0;

	int32_t curDeviceCount = 1;

	g_windowData.mainHwnd = CreateWindowExA(WS_EX_APPWINDOW,
		"Examining Machine",
		"Examining Machine",
		WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		0,
		0,
		g_windowData.hInstance,
		0);

	ShowWindow(g_windowData.mainHwnd, SW_SHOWNORMAL);
	UpdateWindow(g_windowData.mainHwnd);
	SetFocus(g_windowData.mainHwnd);

	PC.ddDeviceCount = 0;

	HRESULT ddEnumResult = DirectDrawEnumerateA(ExamineDDEnumCallback, 0);
	if (ddEnumResult < 0)
		Logger::LogDDError("DirectDrawEnumerateA(ExamineDDEnumCallback, 0)", ddEnumResult);

	Logger::Log("END EXAMINE MACHINE\n");
	Logger::Log("-------------------\n\n");
	Logger::Log("--------------------\n");
	Logger::Log("BEGIN SELECT DRIVERS\n\n");

	PC.DD = PC.examineDevices;
	PC.examineDevices[0].isPreferredDevice = 1;

	Logger::Log("INFO DirectDraw - Default driver >>>%s,%s<<< selected.\n", PC.examineDevices[0].driverName, PC.examineDevices[0].driverDesc);

	if (PC.ddDeviceCount > 1)
	{
		int32_t deviceIter = 1;

		do
		{
			ExamineDevice* curDevice = &PC.examineDevices[deviceIter];

			if (curDevice->valid && PC.examineDevices[deviceIter].hasHardwareAccel && PC.examineDevices[deviceIter].has3DSupport)
			{
				PC.DD = curDevice;

				Logger::Log("INFO DirectDraw - Override default driver, new driver >>>%s,%s<<< selected.\n", curDevice->driverName, curDevice->driverDesc);
			}

			deviceIter = ++curDeviceCount;
		} while (curDeviceCount < PC.ddDeviceCount);
	}

	if (PC.DD)
	{
		const char* hasHardwareAccel = "has";

		if (! PC.DD->hasHardwareAccel)
			hasHardwareAccel = "has no";

		Logger::Log("INFO DirectDraw - Device %s hardware acceleration.\n", hasHardwareAccel);
	}
	else
	{
		Logger::Log("INFO No available DirectDraw devices.\n");
	}

	Logger::Log("\n");

	PC.softwareRenderMode = 0;

	if (PC.DD->isPrimaryDisplay)
	{
		Logger::Log("INFO Direct3D - Searching for hardware RGB driver.\n");
		ExamineDevice* defaultDriver = PC.DD;

		int32_t hardwareRGBCount = 0;
		int32_t softwareRGBCount = 0;

		InterfaceDevice* hardwareRGBDevice;
		InterfaceDevice* softwareRGBDevice;

		if (PC.DD->deviceCount > 0)
		{
			int32_t curInterfaceDev = 0;

			InterfaceDevice** softwareDevices = &softwareRGBDevice;
			InterfaceDevice** hardwareDevices = &hardwareRGBDevice;

			do
			{
				InterfaceDevice* interfaceDevice = &PC.DD->interfaceDevices[curInterfaceDev];

				if (interfaceDevice->isHardwareAccelerated)
				{
					if ((interfaceDevice->hwDeviceDesc.dcmColorModel & 2) != 0)
					{
						*hardwareDevices++ = interfaceDevice;
						++hardwareRGBCount;
					}
				}
				else if ((interfaceDevice->hwDeviceDesc.dcmColorModel & 2) != 0)
				{
					*softwareDevices = interfaceDevice;
					++softwareRGBCount;
					++softwareDevices;
				}

				curInterfaceDev = ++count;
			} while (count < defaultDriver->deviceCount);
		}

		if (hardwareRGBCount && hardwareRGBDevice->hasTexturing)
		{
			PC.D3D = hardwareRGBDevice;
			PC.fullscreenMode = 1;
			Logger::Log("INFO Direct3D - Hardware RGB driver >>>%s,%s<<< selected\n", hardwareRGBDevice->baseName, hardwareRGBDevice->description);
		}
		else
		{
			if (g_checkAvailableMem)
			{
				PC.fullscreenMode = 0;

				if (softwareRGBCount)
				{
					PC.D3D = softwareRGBDevice;
					Logger::Log("INFO Direct3D - Driver >>>%s,%s<<< selected\n", softwareRGBDevice->baseName, softwareRGBDevice->description);
					defaultDriver = PC.DD;
				}

				defaultDriver->hasHardwareAccel = 0;
			}
			else
			{
				Logger::Log("INFO Direct3D - Found no hardware RGB drivers.\n");
				PC.DD = PC.examineDevices;

				Logger::Log("INFO DirectDraw - Resetting to default driver >>>%s,%s<<<.\n", PC.examineDevices[0].driverName, PC.examineDevices[0].driverDesc);

				PC.fullscreenMode = 1;
				PC.softwareRenderMode = 1;
			}
		}
	}
	else
	{
		int32_t deviceCount = PC.DD->deviceCount;
		int16_t interfaceDeviceIndex = 0;

		if (deviceCount > 0)
		{
			int32_t loopCounter1 = 0;

			while (! PC.DD->interfaceDevices[loopCounter1].isHardwareAccelerated)
			{
				loopCounter1 = ++interfaceDeviceIndex;

				if (interfaceDeviceIndex >= deviceCount)
				{
					deviceCount = -1;
					break;
				}
			}

			if (deviceCount > 0)
			{
				PC.fullscreenMode = 1;
				PC.D3D = &PC.DD->interfaceDevices[interfaceDeviceIndex];

				Logger::Log("INFO Direct3D - Hardware driver >>>%s, %s<<< selected\n",
					PC.DD->interfaceDevices[interfaceDeviceIndex].baseName,
					PC.DD->interfaceDevices[interfaceDeviceIndex].description);
			}
		}

		if (deviceCount <= 0)
		{
			PC.DD = PC.examineDevices;

			Logger::Log("INFO Direct3D - No suitable hardware drivers found, scanning software renderers.\n");
			Logger::Log("INFO DirectDraw - Resetting to default driver >>>%s,%s<<<.\n", PC.DD->driverName, PC.DD->driverDesc);

			if (g_checkAvailableMem && PC.DD->deviceCount)
			{
				PC.D3D = PC.DD->interfaceDevices;
				PC.fullscreenMode = 0;
				PC.softwareRenderMode = 0;

				Logger::Log("INFO Direct3D - Using software renderer.\n");
			}
			else
			{
				PC.fullscreenMode = 1;
				PC.softwareRenderMode = 1;

				Logger::Log("INFO Direct3D - Not using any drivers, resetting to native software renderer.\n");
			}
		}
	}

	if (PC.D3D)
	{
		const char* d3dHardwareStatus = "has";

		if (! PC.D3D->isHardwareAccelerated)
			d3dHardwareStatus = "has no";

		Logger::Log("INFO Direct3D - Device %s hardware acceleration.\n", d3dHardwareStatus);
	}
	else
	{
		Logger::Log("INFO No available Direct3D devices.\n");
	}

	Logger::Log("\n");
	const char* fullscreenStatus = "TRUE";

	if (! PC.fullscreenMode)
		fullscreenStatus = "FALSE";

	Logger::Log("INFO Fullscreen mode is %s.\n", fullscreenStatus);
	const char* softwareRenderStatus = "TRUE";

	if (! PC.softwareRenderMode)
		softwareRenderStatus = "FALSE";

	Logger::Log("INFO Native software render mode is %s.\n", softwareRenderStatus);

	ExamineDevice* defaultDriver = PC.DD;
	int32_t softwareRenderMode = PC.softwareRenderMode;

	PC.Mode = PC.DD->displayModes;

	if (PC.softwareRenderMode)
	{
		int32_t displayModeLoopIndex = 0;

		if (PC.DD->displayModeCount > 0)
		{
			int32_t displayModeCounter = 0;

			do
			{
				if (defaultDriver->displayModes[displayModeCounter].w == 640 && defaultDriver->displayModes[displayModeCounter].h == 480
					&& defaultDriver->displayModes[displayModeCounter].bpp == 16)
				{
					PC.Mode = defaultDriver->displayModes;
					defaultDriver->selectedDisplayModeIndex = displayModeCounter;
					defaultDriver = PC.DD;
				}

				displayModeCounter = ++displayModeLoopIndex;
			} while (displayModeLoopIndex < defaultDriver->displayModeCount);

			softwareRenderMode = PC.softwareRenderMode;
		}
	}
	else
	{
		int32_t deviceLoopIndex = 0;

		if (PC.DD->displayModeCount > 0)
		{
			while (true)
			{
				int32_t defaultWidth = defaultDriver->displayModes[deviceLoopIndex].w;

				if (g_checkAvailableMem)
				{
					if (defaultWidth == 320 && defaultDriver->displayModes[deviceLoopIndex].h == 240 && defaultDriver->displayModes[deviceLoopIndex].bpp == 16)
					{
						PC.Mode = &defaultDriver->displayModes[deviceLoopIndex];
						defaultDriver->selectedDisplayModeIndex = deviceLoopIndex;
						defaultDriver = PC.DD;
					}
				}
				else if (defaultWidth == 640 && defaultDriver->displayModes[deviceLoopIndex].h == 480 && defaultDriver->displayModes[deviceLoopIndex].bpp == 16)
				{
					PC.Mode = &defaultDriver->displayModes[deviceLoopIndex];
					defaultDriver->selectedDisplayModeIndex = deviceLoopIndex;
					defaultDriver = PC.DD;
				}

				if (++deviceLoopIndex >= defaultDriver->displayModeCount)
					break;
			}

			softwareRenderMode = PC.softwareRenderMode;
		}
	}

	g_renderMode = (RenderMode)(2 - (softwareRenderMode != 0));
	const char* renderMode = "native software render";

	if (g_renderMode != RENDERMODE_SOFTWARE)
		renderMode = "Direct3d render";

	Logger::Log("INFO RenderMode set to %s mode.\n", renderMode);
	Logger::Log("INFO Screen mode %dx%dx%d selected.\n", PC.Mode->w, PC.Mode->h, PC.Mode->bpp);
	Logger::Log("\n");

	DestroyWindow(g_windowData.mainHwnd);

	while (GetMessageA(&g_windowData.wndEventMsg, 0, 0, 0))
	{
		if (g_windowData.wndEventMsg.message == WM_QUIT)
			break;

		if (! g_windowData.mainHwnd || ! TranslateAcceleratorA(g_windowData.mainHwnd, g_windowData.hAccTable, &g_windowData.wndEventMsg))
		{
			TranslateMessage(&g_windowData.wndEventMsg);
			DispatchMessageA(&g_windowData.wndEventMsg);
		}
	}

	Logger::Log("END SELECT DRIVERS\n");
	Logger::Log("------------------\n\n");

	return 1;
}

// FUNCTION: TOY2 0x00408D30 [PROVISIONAL]
int32_t WINAPI ExamineDDEnumCallback(LPGUID guid, LPSTR driverDesc, LPSTR driverName, LPVOID lpContext)
{
	memset(&PC.examineDevices[PC.ddDeviceCount], 0, sizeof(PC.examineDevices[PC.ddDeviceCount]));

	ExamineDevice* curDevice = &PC.examineDevices[PC.ddDeviceCount];

	LPDIRECTDRAW lpDD;
	if (DirectDrawCreate(guid, &lpDD, 0) < 0)
	{
		curDevice->DDDevice->Release();
		memset(&PC.examineDevices[PC.ddDeviceCount], 0, sizeof(PC.examineDevices[PC.ddDeviceCount]));
		return 1;
	}

	HRESULT query1Result = lpDD->QueryInterface(IID_IDirectDraw2, (void**)&curDevice->DDDevice);

	if (query1Result < 0)
		Logger::LogDDError("tempDD->QueryInterface(IID_IDirectDraw2, (LPVOID*)&(ddinfo->DDDevice))", query1Result);

	if (lpDD)
	{
		lpDD->Release();
		lpDD = 0;
	}

	g_changingCoopLevel = 1;
	curDevice->DDDevice->SetCooperativeLevel(g_windowData.mainHwnd, 81);
	g_changingCoopLevel = 0;

	memset(&curDevice->ddCaps, 0, sizeof(curDevice->ddCaps));

	curDevice->ddCaps.dwSize = 380;

	if (curDevice->DDDevice->GetCaps(&curDevice->ddCaps, 0) < 0)
	{
		curDevice->DDDevice->Release();
		memset(&PC.examineDevices[PC.ddDeviceCount], 0, sizeof(PC.examineDevices[PC.ddDeviceCount]));
		return 1;
	}

	DDSCAPS ddsCaps;
	ddsCaps.dwCaps = 4096;

	// $TODO: Honestly who knows whats going on here (compiler artifact?)
	if (guid && guid != (GUID*)2 && g_checkAvailableMem != 1)
	{
		HRESULT availMemResult = curDevice->DDDevice->GetAvailableVidMem(&ddsCaps, (LPDWORD)&curDevice->TexVidMemTotal, (LPDWORD)&curDevice->TexVidMemFree);

		if (availMemResult < 0)
			Logger::LogDDError("ddinfo->DDDevice->GetAvailableVidMem(&caps, &ddinfo->TexVidMemTotal, &ddinfo->TexVidMemFree)", availMemResult);

		int32_t texVidMemFree = curDevice->TexVidMemFree;

		if (texVidMemFree < 0x200000)
		{
			Logger::Log("DIRECT DRAW DEVICE : Device %s,%s reports less than %d texture memory free - skipping.\n\n", driverName, driverDesc, 0x200000);

			curDevice->DDDevice->Release();
			memset(&PC.examineDevices[PC.ddDeviceCount], 0, sizeof(PC.examineDevices[PC.ddDeviceCount]));
			return 1;
		}

		curDevice->hasLowVideoMem = texVidMemFree < 0x400000;
	}

	int32_t dwCaps = curDevice->ddCaps.dwCaps;

	curDevice->valid = 1;
	curDevice->has3DSupport = dwCaps & 1;
	curDevice->hasHardwareAccel = (~dwCaps >> 25) & 1;

	int32_t dwSVBCaps = curDevice->ddCaps.dwSVBCaps;

	curDevice->hasVideoDMA = dwSVBCaps >> 31;

	if (dwSVBCaps < 0)
	{
		int32_t dwSVBCKeyCaps = curDevice->ddCaps.dwSVBCKeyCaps;
		int32_t dwSVBFXCaps = curDevice->ddCaps.dwSVBFXCaps;

		curDevice->svbCaps = dwSVBCaps;
		curDevice->svbcKeyCaps = dwSVBCKeyCaps;
		curDevice->svbFxCaps = dwSVBFXCaps;
	}

	strncpy(curDevice->driverName, driverName, sizeof(curDevice->driverName));
	strncpy(curDevice->driverDesc, driverDesc, sizeof(curDevice->driverDesc));

	if (! guid || guid == (GUID*)2)
	{
		curDevice->hasHardwareAccel = 0;
	}
	else
	{
		curDevice->isPrimaryDisplay = 0;
		curDevice->deviceGUID = *guid;
	}

	HRESULT enumModesResult = curDevice->DDDevice->EnumDisplayModes(0, 0, curDevice, ExamineDDModesEnumCallback);

	if (enumModesResult < 0)
		Logger::LogDDError("ddinfo->DDDevice->EnumDisplayModes(0, 0, (LPVOID)ddinfo, ExamineDDModesEnumCallback)", enumModesResult);

	qsort(curDevice->displayModes, curDevice->displayModeCount, sizeof(DisplayMode), SortDisplayModes);

	Logger::Log("DIRECT DRAW DEVICE BASE NAME : %s.\n", curDevice->driverName);
	Logger::Log("DIRECT DRAW DEVICE DESCRIPTION : %s.\n", curDevice->driverDesc);

	const char* has3D = "TRUE";

	if (! curDevice->has3DSupport)
		has3D = "FALSE";

	Logger::Log("DIRECT DRAW DEVICE 3D SUPPORT : %s.\n", has3D);

	const char* hasDDHardware = "TRUE";

	if (! curDevice->hasHardwareAccel)
		hasDDHardware = "FALSE";

	Logger::Log("DIRECT DRAW DEVICE HARDWARE : %s.\n", hasDDHardware);

	const char* hasVideoDMA = "TRUE";

	if (! curDevice->hasVideoDMA)
		hasVideoDMA = "FALSE";

	Logger::Log("DIRECT DRAW SYSTEM-VIDEO DMA : %s.\n", hasVideoDMA);

	if (guid)
	{
		const char* hasLowVidMem = "TRUE";

		if (! curDevice->hasLowVideoMem)
			hasLowVidMem = "FALSE";

		Logger::Log("DIRECT DRAW LOW VIDEO MEMORY STATUS  : %s.\n", hasLowVidMem);
	}

	Logger::Log("DIRECT DRAW DEVICE SCREEN MODES.\n");
	Logger::Log("\n");

	int32_t iter = 0;

	if (curDevice->displayModeCount > 0)
	{
		DisplayMode* item = &curDevice->displayModes[0];

		do
		{
			Logger::Log("DDMODE %i - %dx%dx%d bit.\n", iter++, item->bpp, item->w, item->h);
			++item;
		} while (iter < curDevice->displayModeCount);
	}

	HRESULT query3D2Result = curDevice->DDDevice->QueryInterface(IID_IDirect3D2, (void**)&curDevice->D3DDevice);

	if (query3D2Result < 0)
		Logger::LogDDError("ddinfo->DDDevice->QueryInterface(IID_IDirect3D2, (LPVOID *) &ddinfo->D3DDevice)", query3D2Result);

	HRESULT enumDevicesResult = curDevice->D3DDevice->EnumDevices(ExamineD3DEnumCallback, curDevice);

	if (enumDevicesResult < 0)
		Logger::LogDDError("ddinfo->D3DDevice->EnumDevices(ExamineD3DEnumCallback, (LPVOID*)ddinfo)", enumDevicesResult);

	++PC.ddDeviceCount;

	if (curDevice->D3DDevice)
	{
		curDevice->D3DDevice->Release();
		curDevice->D3DDevice = 0;
	}

	if (curDevice->DDDevice)
	{
		curDevice->DDDevice->Release();
		curDevice->DDDevice = 0;
	}

	return 1;
}

// FUNCTION: TOY2 0x004092F0 [PROVISIONAL]
HRESULT WINAPI ExamineDDModesEnumCallback(LPDDSURFACEDESC surfaceDesc, LPVOID context)
{
	ExamineDevice* examineContext = (ExamineDevice*)context;

	int32_t dwWidth = surfaceDesc->dwWidth;

	if (dwWidth < 320 || surfaceDesc->dwHeight < 200)
		return 1;

	DisplayMode* displayMode = &examineContext->displayModes[examineContext->displayModeCount];

	displayMode->w = dwWidth;
	displayMode->h = surfaceDesc->dwHeight;
	displayMode->bpp = surfaceDesc->ddpfPixelFormat.dwRGBBitCount;

	int32_t curDisplayModeCount = examineContext->displayModeCount + 1;
	examineContext->displayModeCount = curDisplayModeCount;

	return curDisplayModeCount != 128;
}

// FUNCTION: TOY2 0x00409130 [PROVISIONAL]
HRESULT WINAPI
ExamineD3DEnumCallback(LPGUID guid, LPSTR deviceDesc, LPSTR deviceName, LPD3DDEVICEDESC d3DHWDeviceDesc, LPD3DDEVICEDESC d3DHELDeviceDesc, LPVOID context)
{
	LPD3DDEVICEDESC hwDeviceDesc = d3DHWDeviceDesc;
	ExamineDevice* examineContext = (ExamineDevice*)context;

	InterfaceDevice* item = &examineContext->interfaceDevices[examineContext->deviceCount];

	memset(item, 0, sizeof(InterfaceDevice));

	item->isHardwareAccelerated = d3DHWDeviceDesc->dcmColorModel != 0;

	item->guid = *guid;

	lstrcpyA(item->baseName, deviceName);
	lstrcpyA(item->description, deviceDesc);

	if (! d3DHWDeviceDesc->dcmColorModel)
		hwDeviceDesc = d3DHELDeviceDesc;

	memcpy(&item->hwDeviceDesc, hwDeviceDesc, sizeof(item->hwDeviceDesc));

	int32_t dwDeviceZBufferBitDepth = item->hwDeviceDesc.dwDeviceZBufferBitDepth;
	int32_t hasTexturing = item->hwDeviceDesc.dpcTriCaps.dwTextureCaps & 1;

	item->isSquareTexturesOnly = (item->hwDeviceDesc.dpcTriCaps.dwTextureCaps >> 5) & 1;

	int32_t hasAlphaBlending = (item->hwDeviceDesc.dpcTriCaps.dwSrcBlendCaps >> 11) & 1;

	item->hasTexturing = hasTexturing;
	item->hasAlphaBlending = hasAlphaBlending;
	item->hasZBuffer = dwDeviceZBufferBitDepth != 0;

	Logger::Log("\n");
	Logger::Log("DIRECT 3D DEVICE BASE NAME : %s.\n", item->baseName);
	Logger::Log("DIRECT 3D DEVICE DESCRIPTION : %s.\n", item->description);

	const char* texFlag = "TRUE";
	if (! item->hasTexturing)
		texFlag = "FALSE";

	Logger::Log("DIRECT 3D DEVICE TextureFlag\t:\t%s.\n", texFlag);

	const char* zBufferFlag = "TRUE";
	if (! item->hasZBuffer)
		zBufferFlag = "FALSE";

	Logger::Log("DIRECT 3D DEVICE ZBufferFlag : %s.\n", zBufferFlag);

	const char* hardwareAccelFlag = "TRUE";
	if (! item->isHardwareAccelerated)
		hardwareAccelFlag = "FALSE";

	Logger::Log("DIRECT 3D DEVICE HardwareAccel : %s.\n", hardwareAccelFlag);

	const char* squareOnlyFlag = "TRUE";
	if (! item->isSquareTexturesOnly)
		squareOnlyFlag = "FALSE";

	Logger::Log("DIRECT 3D DEVICE SquareOnly : %s.\n", squareOnlyFlag);

	const char* alphaBlending = "ON";
	if (! item->hasAlphaBlending)
		alphaBlending = "OFF";

	Logger::Log("DIRECT 3D DEVICE Alpha blending is %s.\n", alphaBlending);
	Logger::Log("\n");

	item->valid = 1;
	++examineContext->deviceCount;

	return 1;
}

// FUNCTION: TOY2 0x00408CD0 [PROVISIONAL]
int32_t SortDisplayModes(const void* modeA, const void* modeB)
{
	DisplayMode* displayModeA = (DisplayMode*)modeA;
	DisplayMode* displayModeB = (DisplayMode*)modeB;

	int32_t bitCountA = displayModeA->bpp;
	int32_t bitCountB = displayModeB->bpp;

	if (bitCountB > bitCountA)
		return -1;

	if (bitCountB < bitCountA)
		return 1;

	if (displayModeB->w > displayModeA->w)
		return -1;

	if (displayModeB->w < displayModeA->w)
		return 1;

	int32_t heightA = displayModeA->h;
	int32_t heightB = displayModeB->h;

	if (heightA >= heightB)
		return heightB < heightA;
	else
		return -1;
}

// FUNCTION: TOY2 0x00409360 [PROVISIONAL]
LRESULT WINAPI ProfileWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_DESTROY)
	{
		g_windowData.mainHwnd = 0;
		::PostQuitMessage(0);
	}

	return DefWindowProcA(hWnd, msg, wParam, lParam);
}

namespace Toy2
{
	// FUNCTION: TOY2 0x00412B50 [PROVISIONAL]
	void RunModeSelect()
	{
		if (! g_modeSelectFinished)
		{
			atexit(DrawingDevice::Quit);
			ModeSelect::SetForceFullscreen_T(0);

			if (ModeSelect::EnumerateDrivers_T(ModeSelect::DeviceFilterCallback) < 0)
				Logger::GetErrorHandler("C:\\projects\\toy2\\direct6.cpp", 103)("Unable to enumerate a suitable device");

			ModeSelect::Show();

			DrawingDevice::DDAppDevice* primaryDevice;
			DrawingDevice::DDAppDevice::App* ddApp;

			if (DrawingDevice::GetChosenDevice_T(&ddApp, &primaryDevice))
				Logger::GetErrorHandler("C:\\projects\\toy2\\direct6.cpp", 111)("Unable to create D3D device\r\n try a lower resolution or screen depth");

			int32_t canDoWindowed = primaryDevice->canRenderWindowedOnPrimary;
			int32_t fullscreenExclusive = (ModeSelect::g_unusedFlag1 != 0 ? 2 : 0) | (canDoWindowed == 0) | (ModeSelect::g_unusedFlag2 != 0 ? 4 : 0);

			if (! canDoWindowed)
				Logger::g_showMsgBoxOnThrow = 1;

			if (primaryDevice->isHardwareAccelerated)
			{
				Renderer::SetIsSoftwareRendering(0);
			}
			else
			{
				Renderer::SetIsSoftwareRendering(1);
				while (Graphics::RemoveDetailLevel()) {};
			}

			if (! primaryDevice->isHardwareAccelerated && primaryDevice->canRenderWindowedOnPrimary)
			{
				RECT adjustedRect;
				adjustedRect.top = 0;
				adjustedRect.left = 0;
				adjustedRect.right = 320;
				adjustedRect.bottom = 240;

				AdjustWindowRect(&adjustedRect, 0, 0);
				SetWindowPos(g_windowData.mainHwnd, 0, 0, 0, adjustedRect.right, adjustedRect.bottom, 2);
			}

			ShowWindow(g_windowData.mainHwnd, SW_SHOWMAXIMIZED);

			if (DrawingDevice::CD3DFramework::Build(g_windowData.mainHwnd, &ddApp->guid, primaryDevice, primaryDevice->primaryDisplayMode, fullscreenExclusive)
				>= 0)
				g_modeSelectFinished = 1;
		}
	}
}
