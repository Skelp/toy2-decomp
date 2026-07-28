#pragma once

#include "Common.h"
#include "Numerics.h"

namespace Renderer
{
	namespace Shadows
	{
		enum
		{
			MAX_SHADOWS = 48,
		};

		struct ShadowInstance
		{
			Vector3I pos;
			int16_t size;
			int16_t opacity;
		};

		struct ShadowProjection
		{
			int16_t cornerYOffsets[3];
			int16_t opacity;
		};

		extern int16_t g_shadowCount;
		extern int32_t g_unusedShadowVar;
		extern ShadowInstance g_shadowInstances[MAX_SHADOWS];
		extern ShadowProjection g_shadowProjections[MAX_SHADOWS];

		STATIC_ASSERT(sizeof(ShadowInstance) == 0x10);
		STATIC_ASSERT(sizeof(ShadowProjection) == 0x8);
	}
}
