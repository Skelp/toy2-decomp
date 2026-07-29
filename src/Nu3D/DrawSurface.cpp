#include "Nu3D/DrawSurface.h"

namespace Nu3D
{
	namespace DrawSurface
	{
		// FUNCTION: TOY2 0x004ABE90 [MATCHED]
		int32_t GetDC(LPDIRECTDRAWSURFACE4 surface, HDC* hdcOut) { return surface->GetDC(hdcOut); }

		// FUNCTION: TOY2 0x004ABEA0 [MATCHED]
		int32_t ReleaseDC(LPDIRECTDRAWSURFACE4 surface, HDC hdc) { return surface->ReleaseDC(hdc); }
	}
}
