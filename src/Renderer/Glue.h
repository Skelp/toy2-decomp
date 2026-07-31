#pragma once

#include "Common.h"
#include <WINDOWS.H>

namespace Renderer
{
	namespace Glue
	{
		int32_t BackdropBltFast();
		int32_t SetBackdrop(int32_t textureIndex);
		void ReleaseBackdrop();
	}
}
