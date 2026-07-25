#include "Toy2/Animation.h"

#include <string.h>

namespace Toy2
{
	namespace Animation
	{
		// GLOBAL: TOY2 0x00B1C3C0
		Vector3F g_nodeAngles[64][32];

		// GLOBAL: TOY2 0x00B223C0
		Actor::Toy2Actor** g_actorAnimList;

		// GLOBAL: TOY2 0x00B223E0
		uint8_t* g_nodeKeyframeOffsets;

		// GLOBAL: TOY2 0x00B423E8
		uint8_t* g_nodeScaleFlags;

		// GLOBAL: TOY2 0x00B423EC
		uint8_t* g_keyframeData;

		STATIC_ASSERT(sizeof(g_nodeAngles) == 0x6000);

		// FUNCTION: TOY2 0x004CD120 [MATCHED]
		void ResetNodeAngles()
		{
			g_actorAnimList = 0;
			memset(g_nodeAngles, 0, sizeof(g_nodeAngles));
		}

		// FUNCTION: TOY2 0x004CD170 [MATCHED]
		void ParseHeader(int16_t* header)
		{
			int32_t headerSize;
			if (*header < 0)
				headerSize = -*header;
			else
				headerSize = 12;

			g_nodeKeyframeOffsets = (uint8_t*)header + headerSize;
			g_nodeScaleFlags = (uint8_t*)header + headerSize + (uint16_t)header[5] * 2;
			g_keyframeData = g_nodeScaleFlags + (uint16_t)header[6];
		}
	}
}
