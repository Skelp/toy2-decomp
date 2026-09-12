#include "ModeSelect.h"
#include "DrawingDevice.h"
#include "Logger.h"
#include "FileUtils.h"
#include "Toy2/Direct6.h"
#include "Toy2/Win95.h"
#include "Toy2/Toy2.h"
#include <directx6/ddraw.h>
#include <directx6/d3d.h>
#include <cstdio>
#include <windows.h>
#include "ModeSelectInternal.h"

// The driver and display-mode enumeration that picks the primary device:
// retail holds 0x004AC4D0 through 0x004ACFB0 as one object, in the order of
// this file.
namespace ModeSelect
{
	HRESULT CALLBACK D3DDeviceEnumCallback(
		GUID* lpGuid, LPSTR lpDeviceDescription, LPSTR lpDeviceName, LPD3DDEVICEDESC lpD3DHWDeviceDesc, LPD3DDEVICEDESC lpD3DHELDeviceDesc, LPVOID lpContext);
	HRESULT CALLBACK EnumDisplayModes(LPDDSURFACEDESC2 lpDDSurfaceDesc2, LPVOID lpContext);
	BOOL WINAPI DDrawEnumCallbackExA(GUID* lpGUID, LPSTR lpDriverName, LPSTR lpDriverDescription, LPVOID lpContext, HMONITOR hm);

	// FUNCTION: TOY2 0x004AC4D0 [MATCHED]
	int32_t EnumerateDrivers(DeviceFilterCallback_t callback)
	{
		typedef HRESULT(WINAPI * DirectDrawEnumerateExA_t)(LPDDENUMCALLBACKEXA lpCallback, LPVOID lpContext, DWORD dwFlags);

		g_ddDeviceFilterCallback = callback;
		g_foundRefDevice = 0;

		HMODULE moduleHandle = GetModuleHandleA("DDRAW.DLL");

		if (! moduleHandle)
			return 0x81000004;

		DirectDrawEnumerateExA_t procAddress = (DirectDrawEnumerateExA_t)GetProcAddress(moduleHandle, "DirectDrawEnumerateExA");

		if (procAddress)
			procAddress(DDrawEnumCallbackExA, 0, 7);
		else
			DirectDrawEnumerateA(DDrawEnumCallback, 0);

		return SelectPrimaryDevice(0);
	}

	// FUNCTION: TOY2 0x004AC540 [PROVISIONAL]
	BOOL WINAPI DDrawEnumCallbackExA(GUID* lpGUID, LPSTR lpDriverName, LPSTR lpDriverDescription, LPVOID lpContext, HMONITOR hm)
	{
		using namespace DrawingDevice;

		DDSCAPS2 ddsCaps;
		LPDIRECTDRAW directDraw;

		if (DirectDrawCreate(lpGUID, &directDraw, 0) < 0)
			return TRUE;

		if (directDraw->QueryInterface(IID_IDirectDraw4, (LPVOID*)&g_ddraw4) < 0)
		{
			directDraw->Release();
			return TRUE;
		}

		directDraw->Release();

		LPDIRECT3D3 d3d3;
		if (g_ddraw4->QueryInterface(IID_IDirect3D3, (LPVOID*)&d3d3) < 0)
		{
			g_ddraw4->Release();
			return TRUE;
		}

		DDAppDevice::App* ddApp = new DDAppDevice::App;

		if (ddApp)
		{
			memset(ddApp, 0, sizeof(DDAppDevice::App));

			if (lpGUID)
			{
				ddApp->guid = *lpGUID;
				ddApp->ref = ddApp;
			}

			strncpy(ddApp->driverName, lpDriverName, sizeof(ddApp->driverName) - 1);
			strncpy(ddApp->driverDesc, lpDriverDescription, sizeof(ddApp->driverDesc) - 1);

			ddApp->hMonitor = hm;
			ddApp->ddCaps1.dwSize = sizeof(ddApp->ddCaps1);
			ddApp->ddCaps2.dwSize = sizeof(ddApp->ddCaps2);

			g_ddraw4->GetCaps(&ddApp->ddCaps1, &ddApp->ddCaps2);

			ddApp->vidMemTotal = ddApp->ddCaps1.dwVidMemTotal;
			ddApp->vidMemFree = ddApp->ddCaps1.dwVidMemFree;

			memset(&ddsCaps.dwCaps2, 0, 12);
			ddsCaps.dwCaps = 16896;

			g_ddraw4->GetAvailableVidMem(&ddsCaps, &ddApp->totalTextureMem, &ddApp->freeTextureMem);

			memset(&ddsCaps.dwCaps2, 0, 12);
			ddsCaps.dwCaps = 4096;

			g_ddraw4->GetAvailableVidMem(&ddsCaps, &ddApp->totalVideoMem, &ddApp->freeVideoMem);
			d3d3->EnumDevices(D3DDeviceEnumCallback, ddApp);

			if (ddApp->deviceListHead)
			{
				DDAppDevice::App* chain = g_ddAppListHead;
				DDAppDevice::App** curChain;

				for (curChain = &g_ddAppListHead; chain; chain = chain->chainDDApp)
					curChain = &chain->chainDDApp;

				*curChain = ddApp;

				if (! lpGUID)
					g_primaryDDApp = ddApp;
			}
			else
			{
				delete ddApp;
			}

			d3d3->Release();
			g_ddraw4->Release();

			return TRUE;
		}

		return reinterpret_cast<BOOL>(ddApp);
	}

	// FUNCTION: TOY2 0x004AC760 [PROVISIONAL]
	HRESULT CALLBACK D3DDeviceEnumCallback(
		GUID* lpGuid, LPSTR lpDeviceDescription, LPSTR lpDeviceName, LPD3DDEVICEDESC lpD3DHWDeviceDesc, LPD3DDEVICEDESC lpD3DHELDeviceDesc, LPVOID lpContext)
	{
		using namespace DrawingDevice;

		DDAppDevice::App* ddApp = reinterpret_cast<DDAppDevice::App*>(lpContext);

		if (! lpGuid || ! lpD3DHWDeviceDesc || ! lpD3DHELDeviceDesc || ! ddApp)
			return D3DENUMRET_CANCEL;

		if (! memcmp(lpGuid, &IID_IDirect3DNullDevice, sizeof(GUID)))
			return D3DENUMRET_OK;

		g_foundAnyD3DDevice = 1;

		int32_t flags = lpD3DHWDeviceDesc->dwFlags != 0;

		LPD3DDEVICEDESC deviceDesc = lpD3DHWDeviceDesc;

		if (! lpD3DHWDeviceDesc->dwFlags)
			deviceDesc = lpD3DHELDeviceDesc;

		if (ddApp->ref && ! lpD3DHWDeviceDesc->dwFlags)
			return D3DENUMRET_OK;

		if (g_ddDeviceFilterCallback && g_ddDeviceFilterCallback(&ddApp->ddCaps1, deviceDesc) < 0)
			return D3DENUMRET_OK;

		DDAppDevice* appDevice = new DDAppDevice;

		if (! appDevice)
			return D3DENUMRET_CANCEL;

		memset(appDevice, 0, sizeof(DDAppDevice));

		appDevice->guid = *lpGuid;
		appDevice->ref = appDevice;

		strncpy(appDevice->deviceName, lpDeviceName, sizeof(appDevice->deviceName) - 1);
		memcpy(&appDevice->deviceDesc, deviceDesc, sizeof(appDevice->deviceDesc));

		appDevice->isHardwareAccelerated = flags;
		appDevice->ddAppParent = ddApp;

		if (flags || ! ddApp->primaryDevice && (lpD3DHELDeviceDesc->dcmColorModel & 2) != 0)
			ddApp->primaryDevice = appDevice;

		g_ddraw4->EnumDisplayModes(0, 0, appDevice, EnumDisplayModes);

		DDSURFACEDESC2 surfaceDesc;
		surfaceDesc.dwSize = sizeof(DDSURFACEDESC2);
		g_ddraw4->GetDisplayMode(&surfaceDesc);

		DWORD bitCount = surfaceDesc.ddpfPixelFormat.dwRGBBitCount;

		if ((ddApp->ddCaps1.dwCaps2 & 0x80000) != 0 && ! g_forceFullscreen)
		{
			for (DDAppDevice::DisplayMode* displayMode = appDevice->displayModeListHead; displayMode; displayMode = displayMode->nextDisplayMode)
			{
				if (displayMode->surfaceDesc.ddpfPixelFormat.dwRGBBitCount == bitCount)
				{
					appDevice->supportsCurrentMode = 1;

					if (! ddApp->ref)
						appDevice->canRenderWindowedOnPrimary = 1;
				}
			}
		}

		if (appDevice->displayModeListHead)
		{
			DDAppDevice* deviceListHead = ddApp->deviceListHead;
			DDAppDevice** nextDeviceLink;

			for (nextDeviceLink = &ddApp->deviceListHead; deviceListHead; deviceListHead = deviceListHead->nextDevice)
				nextDeviceLink = &deviceListHead->nextDevice;

			*nextDeviceLink = appDevice;
		}
		else
		{
			delete appDevice;
		}

		if (! memcmp(lpGuid, &IID_IDirect3DRefDevice, sizeof(GUID)))
			g_foundRefDevice = 1;

		return D3DENUMRET_OK;
	}

	// FUNCTION: TOY2 0x004AC9C0 [PROVISIONAL]
	HRESULT CALLBACK EnumDisplayModes(LPDDSURFACEDESC2 lpDDSurfaceDesc2, LPVOID lpContext)
	{
		using namespace DrawingDevice;

		DDAppDevice* device = reinterpret_cast<DDAppDevice*>(lpContext);

		if (! lpDDSurfaceDesc2 || ! device)
			return D3DENUMRET_CANCEL;

		DWORD deviceRenderBitDepth = device->deviceDesc.dwDeviceRenderBitDepth;
		DWORD rgbBitCount = lpDDSurfaceDesc2->ddpfPixelFormat.dwRGBBitCount;

		if (rgbBitCount == 32)
		{
			if ((deviceRenderBitDepth & 256) == 0)
				return D3DENUMRET_OK;

			if (g_no32bitColors)
				return D3DENUMRET_OK;
		}
		else if (rgbBitCount == 24)
		{
			if ((deviceRenderBitDepth & 512) == 0)
				return D3DENUMRET_OK;

			if (g_no32bitColors)
				return D3DENUMRET_OK;
		}
		else if (rgbBitCount == 16)
		{
			if ((deviceRenderBitDepth & 1024) == 0)
				return D3DENUMRET_OK;
		}
		else if (rgbBitCount == 8)
		{
			return D3DENUMRET_OK;
		}
		else if (rgbBitCount > 16)
		{
			if (g_no32bitColors)
				return D3DENUMRET_OK;
		}

		if (! device->isHardwareAccelerated)
		{
			if (rgbBitCount != 16)
				return D3DENUMRET_OK;

			if (lpDDSurfaceDesc2->dwWidth > 320)
				return D3DENUMRET_OK;
		}

		DWORD texMem;

		if (rgbBitCount == 16)
		{
			texMem = 6 * lpDDSurfaceDesc2->dwHeight * lpDDSurfaceDesc2->dwWidth;
		}
		else if (rgbBitCount == 24)
		{
			texMem = 9 * lpDDSurfaceDesc2->dwHeight * lpDDSurfaceDesc2->dwWidth;
		}
		else
		{
			if (rgbBitCount != 32)
				return D3DENUMRET_OK;

			texMem = 12 * lpDDSurfaceDesc2->dwHeight * lpDDSurfaceDesc2->dwWidth;
		}

		// $BUG: On some systems this VRAM calculation can short-circuit and cause the game
		// to not find a suitable d3d device

		if (device->isHardwareAccelerated)
		{
			DDAppDevice::App* ddAppParent = device->ddAppParent;

			if (ddAppParent->vidMemTotal == ddAppParent->totalTextureMem)
			{
				if (ddAppParent->vidMemFree - texMem < 0x80000)
					return D3DENUMRET_OK;
			}
			else if (ddAppParent->freeTextureMem < texMem)
			{
				return D3DENUMRET_OK;
			}
		}

		DDAppDevice::DisplayMode* displayMode = new DDAppDevice::DisplayMode;

		if (! displayMode)
			return D3DENUMRET_CANCEL;

		memset(displayMode, 0, sizeof(DDAppDevice::DisplayMode));
		memcpy(displayMode, lpDDSurfaceDesc2, sizeof(DDSURFACEDESC2));

		sprintf(displayMode->modeText, "%ld x %ld x %ld", lpDDSurfaceDesc2->dwWidth, lpDDSurfaceDesc2->dwHeight, rgbBitCount);

		DDAppDevice::DisplayMode* displayModeListHead = device->displayModeListHead;
		DDAppDevice::DisplayMode** nextDm;

		for (nextDm = &device->displayModeListHead; displayModeListHead; displayModeListHead = displayModeListHead->nextDisplayMode)
			nextDm = &displayModeListHead->nextDisplayMode;

		*nextDm = displayMode;

		if (lpDDSurfaceDesc2->dwWidth == 640 && lpDDSurfaceDesc2->dwHeight == 480 && lpDDSurfaceDesc2->ddpfPixelFormat.dwRGBBitCount == 16)
			device->primaryDisplayMode = displayMode;

		if (! device->primaryDisplayMode)
			device->primaryDisplayMode = displayMode;

		return D3DENUMRET_OK;
	}

	// FUNCTION: TOY2 0x004ACBC0 [MATCHED]
	BOOL WINAPI DDrawEnumCallback(GUID* lpGUID, LPSTR lpDriverDescription, LPSTR lpDriverName, LPVOID lpContext)
	{ return DDrawEnumCallbackExA(lpGUID, lpDriverDescription, lpDriverName, 0, 0); }

	// FUNCTION: TOY2 0x004ACBE0 [PROVISIONAL]
	int32_t SelectPrimaryDevice(uint8_t selectionFlags)
	{
		using namespace DrawingDevice;

		MarkCompatibleBitDepthDevices();

		uint8_t flags = selectionFlags;

		if (! g_forceFullscreen)
		{
			flags = selectionFlags | DEVICE_SELECTION_WINDOWED_ONLY;
			selectionFlags |= DEVICE_SELECTION_WINDOWED_ONLY;
		}

		if ((flags & DEVICE_SELECTION_EXPLICIT_MASK) != 0)
		{
			DDAppDevice::App* appIter = g_ddAppListHead;

			for (DDAppDevice::App* outerApp = g_ddAppListHead; appIter; outerApp = appIter)
			{
				DDAppDevice* deviceIter = appIter->deviceListHead;

				if (deviceIter)
				{
					while (true)
					{
						BOOL matchesDriverKind = 0;

						if (! memcmp(deviceIter->ref, &IID_IDirect3DRGBDevice, sizeof(GUID)))
						{
							if ((selectionFlags & DEVICE_SELECTION_RGB_SOFTWARE) != 0)
							{
								g_primaryDDApp = outerApp;
								outerApp->primaryDevice = deviceIter;
								return 0;
							}
						}
						else if (! memcmp(deviceIter->ref, &IID_IDirect3DRefDevice, sizeof(GUID)))
						{
							if ((selectionFlags & DEVICE_SELECTION_REFERENCE) != 0)
							{
								g_primaryDDApp = outerApp;
								outerApp->primaryDevice = deviceIter;
								return 0;
							}
						}
						else
						{
							if ((selectionFlags & DEVICE_SELECTION_PRIMARY_HARDWARE) != 0)
								matchesDriverKind = outerApp == g_ddAppListHead;

							if ((selectionFlags & DEVICE_SELECTION_SECONDARY_HARDWARE) != 0 && outerApp != g_ddAppListHead || matchesDriverKind)
							{
								g_primaryDDApp = outerApp;
								outerApp->primaryDevice = deviceIter;
								return 0;
							}
						}

						deviceIter = deviceIter->nextDevice;

						if (! deviceIter)
						{
							appIter = outerApp;
							break;
						}
					}
				}

				appIter = appIter->chainDDApp;
			}

			return 0x81000005;
		}

		DDAppDevice::App* appIter;
		DDAppDevice* device;
		int32_t wantHardware;
		int32_t wantWindowed;
		int16_t attemptIndex = 0;

		while (true)
		{
			wantHardware = (attemptIndex & 1) == 0;
			wantWindowed = (~attemptIndex >> 1) & 1;

			if ((wantHardware != 1 || (flags & 1) == 0) && (wantWindowed != 1 || (flags & 2) == 0))
			{
				appIter = g_ddAppListHead;

				if (g_ddAppListHead)
					break;
			}

		LBL_NEXT_ATTEMPT:

			if (++attemptIndex >= 4)
			{
				if (g_foundAnyD3DDevice)
					return (g_foundRefDevice != 0) + 0x81000002;
				else
					return 0x81000001;
			}
		}

		while (true)
		{
			if (! wantWindowed || (appIter->ddCaps1.dwCaps2 & 0x80000) != 0)
			{
				device = appIter->deviceListHead;
				if (device)
					break;
			}

			appIter = appIter->chainDDApp;

			if (! appIter)
				goto LBL_NEXT_ATTEMPT;
		}

		while (wantHardware != device->isHardwareAccelerated || wantWindowed && ! device->supportsCurrentMode)
		{
			device = device->nextDevice;

			if (! device)
			{
				appIter = appIter->chainDDApp;

				if (! appIter)
					goto LBL_NEXT_ATTEMPT;
			}
		}

		device->canRenderWindowedOnPrimary = wantWindowed;

		g_primaryDDApp = appIter;
		appIter->primaryDevice = device;

		return 0;
	}

	// FUNCTION: TOY2 0x004ACD90 [MATCHED]
	void MarkCompatibleBitDepthDevices()
	{
		using namespace DrawingDevice;

		LPDIRECTDRAW dd;
		if (DirectDrawCreate(0, &dd, 0) >= 0)
		{
			DDSURFACEDESC surfaceDesc;
			surfaceDesc.dwSize = sizeof(DDSURFACEDESC);

			dd->GetDisplayMode(&surfaceDesc);
			dd->Release();

			DDAppDevice::App* listHead = g_ddAppListHead;

			for (DWORD bitCount = surfaceDesc.ddpfPixelFormat.dwRGBBitCount; listHead; listHead = listHead->chainDDApp)
			{
				for (DDAppDevice* device = listHead->deviceListHead; device; device = device->nextDevice)
				{
					DDAppDevice::DisplayMode* modeIter = device->displayModeListHead;

					for (device->supportsCurrentMode = 0; modeIter; modeIter = modeIter->nextDisplayMode)
					{
						if (modeIter->surfaceDesc.ddpfPixelFormat.dwRGBBitCount == bitCount)
							device->supportsCurrentMode = 1;
					}
				}
			}
		}
	}

	// FUNCTION: TOY2 0x004ACFB0 [MATCHED]
	void SetForceFullscreen(int32_t forceFullscreen) { g_forceFullscreen = forceFullscreen; }
}
