#include "Toy2/Actor.h"
#include "Toy2/Animation.h"

namespace Toy2
{
	namespace Actor
	{
		// GLOBAL: TOY2 0x0052F1D0
		Toy2Actor* g_activeActors[65];

		// STUB: TOY2 0x00414A80
		void GetCreatureList(uint8_t* creatureIdList) {}

		// FUNCTION: TOY2 0x004CDBF0 [MATCHED]
		int32_t FindInActorList(Toy2Actor* actor)
		{
			Toy2Actor** list = Animation::g_actorAnimList;
			if (list != 0)
			{
				int32_t index = 0;
				while (*list != 0)
				{
					if (actor == *list)
						return index;
					list++;
					index++;
				}
			}
			return -1;
		}

		// FUNCTION: TOY2 0x004CDBB0 [MATCHED]
		void SetNodeAngle(Toy2Actor* actor, int32_t nodeIndex, float x, float y, float z)
		{
			int32_t actorIndex = FindInActorList(actor);
			if (actorIndex >= 0)
			{
				Animation::g_nodeAngles[actorIndex][nodeIndex].x = x;
				Animation::g_nodeAngles[actorIndex][nodeIndex].y = y;
				Animation::g_nodeAngles[actorIndex][nodeIndex].z = z;
			}
		}
	}
}