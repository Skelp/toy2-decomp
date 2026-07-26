#pragma once

#include "Common.h"
#include <windows.h>
#include <directx6/ddraw.h>

namespace Nu3D
{
	namespace DrawSurface
	{
		int32_t GetDC(LPDIRECTDRAWSURFACE4 surface, HDC* hdcOut);
		int32_t ReleaseDC(LPDIRECTDRAWSURFACE4 surface, HDC hdc);
	}
}
