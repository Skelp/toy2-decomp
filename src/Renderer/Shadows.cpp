#include "Renderer/Shadows.h"

namespace Renderer
{
	namespace Shadows
	{
		// GLOBAL: TOY2 0x0054DEB0
		int16_t g_shadowCount;

		// GLOBAL: TOY2 0x0054DD64
		int32_t g_unusedShadowVar;

		// GLOBAL: TOY2 0x0054F098
		ShadowInstance g_shadowInstances[MAX_SHADOWS];

		// GLOBAL: TOY2 0x0054DEC8
		ShadowProjection g_shadowProjections[MAX_SHADOWS];
	}
}
