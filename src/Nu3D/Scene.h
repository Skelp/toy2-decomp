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
		enum WorldRenderFlags
		{
			WORLD_RENDER_BYPASS_SECONDARY_PORTALS = 0x1,
			WORLD_RENDER_BYPASS_PRIMARY_PORTALS = 0x2,
			WORLD_RENDER_BYPASS_ALL_PORTALS = 0x3,
		};

		extern float g_primaryFarClip;
		extern float g_primaryNearClip;
		extern float g_secondaryNearClip;
		extern float g_secondaryPortalNearClip;
		extern float g_primaryFogFarClip;

		void RenderCellsInRadius(int32_t cellRadius, int32_t scalerType, NGNLoader::NGNImage* image);
		void RenderWorldGeometry(int32_t areaIndex, int32_t renderFlags);
	}
}
