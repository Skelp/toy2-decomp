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

		STATIC_ASSERT(sizeof(g_nodeAngles) == 0x6000);

		// FUNCTION: TOY2 0x004CD120 [MATCHED]
		void ResetNodeAngles()
		{
			g_actorAnimList = 0;
			memset(g_nodeAngles, 0, sizeof(g_nodeAngles));
		}
	}
}
