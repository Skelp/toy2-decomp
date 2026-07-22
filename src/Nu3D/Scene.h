#pragma once

#include "Common.h"

namespace NGNLoader
{
	struct NGNImage;
}

namespace Nu3D
{
	namespace Scene
	{
		void RenderCellsInRadius(int32_t cellRadius, int32_t scalerType, NGNLoader::NGNImage* image);
	}
}
