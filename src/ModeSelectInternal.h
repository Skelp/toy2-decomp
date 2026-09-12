#ifndef MODESELECTINTERNAL_H
#define MODESELECTINTERNAL_H

#include "ModeSelect.h"

#include <directx6/ddraw.h>

// Declarations the two objects of the mode selection share but that no public
// header states: the bits that say which device the selection may pick, the
// state the enumeration fills, and the entry points the menu calls.
namespace ModeSelect
{
	enum DeviceSelectionFlags
	{
		DEVICE_SELECTION_WINDOWED_ONLY = 0x2,
		DEVICE_SELECTION_RGB_SOFTWARE = 0x4,
		DEVICE_SELECTION_REFERENCE = 0x8,
		DEVICE_SELECTION_PRIMARY_HARDWARE = 0x10,
		DEVICE_SELECTION_SECONDARY_HARDWARE = 0x20,
		DEVICE_SELECTION_EXPLICIT_MASK = 0x3C,
	};

	extern int32_t g_forceFullscreen;
	extern DeviceFilterCallback_t g_ddDeviceFilterCallback;
	extern int32_t g_foundRefDevice;
	extern int32_t g_foundAnyD3DDevice;
	extern LPDIRECTDRAW4 g_ddraw4;

	int32_t EnumerateDrivers(DeviceFilterCallback_t callback);
	int32_t SelectPrimaryDevice(uint8_t selectionFlags);
	void MarkCompatibleBitDepthDevices();
	void SetForceFullscreen(int32_t forceFullscreen);
	BOOL WINAPI DDrawEnumCallback(GUID* lpGUID, LPSTR lpDriverDescription, LPSTR lpDriverName, LPVOID lpContext);
}

#endif
