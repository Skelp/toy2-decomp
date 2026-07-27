#include "Toy2/Actor.h"
#include "Toy2/Animation.h"
#include "CharacterLoader.h"

#include <string.h>

namespace Toy2
{
	namespace Game
	{
		void InitActor(Actor::Toy2Actor* actor, int32_t param);
	}

	namespace Actor
	{
		// GLOBAL: TOY2 0x0052F1D0
		Toy2Actor* g_activeActors[65];

		// GLOBAL: TOY2 0x0052c840
		Toy2Actor g_creatureActors[64];

		// GLOBAL: TOY2 0x0050A54C
		int32_t g_unk50A54C;

		// GLOBAL: TOY2 0x0052ADD8
		int32_t g_unk52ADD8[0x80];

		// GLOBAL: TOY2 0x0052EF48
		int32_t g_unk52EF48;

		// GLOBAL: TOY2 0x0052EF88
		int32_t g_unk52EF88;

		// FUNCTION: TOY2 0x00407150
		void InitCreatureRam()
		{
			memset(g_creatureActors, 0, sizeof(g_creatureActors));
			memset(g_unk52ADD8, 0, sizeof(g_unk52ADD8));
			g_unk50A54C = -1;
			g_activeActors[0] = 0;
			g_unk52EF48 = 0;
			g_unk52EF88 = 0;

			Toy2Actor* actor = g_creatureActors;
			RawLoader::CreatureListRam* creature = RawLoader::g_creatureListRam;
			// CAP-18: MSVC anchors the creature cursor on the entCtrl store target;
			// retail anchors on the creatureId read source. The actor side matches.
			for (int32_t i = 0x40; i != 0; i--)
			{
				actor->secondaryAnimIdx = -1;
				uint8_t creatureId = creature->creatureId;
				if (creatureId != 0)
				{
					actor->creatureRam = creature;
					if (creatureId == 0x0e)
					{
						creature->entCtrl.actorPhase = 4;
					}
					Game::InitActor(actor, 1);
				}
				actor++;
				creature++;
			}
		}

		// FUNCTION: TOY2 0x004019D0
		void UpdatePrimaryAnimation(Toy2Actor* actor)
		{
			void* entry = CharacterLoader::g_unk547CD4[actor->creatureId];
			Animation::EvaluateClip(*(void**)((uint8_t*)entry + 8 + actor->primaryAnimIdx * 4), actor->unkVar7, *(uint16_t*)((uint8_t*)entry + 4), 0);
		}

		// FUNCTION: TOY2 0x00414A80
		void GetCreatureList(uint8_t* creatureIdList)
		{
			InitCreatureRam();
			int32_t index = 1;
			int16_t* creatureId = &g_creatureActors[0].creatureId;
			creatureIdList[0] = 0;
			do
			{
				if (*creatureId != 0)
				{
					creatureIdList[index] = (uint8_t)*creatureId;
					index++;
				}
				creatureId += sizeof(Toy2Actor) / sizeof(int16_t);
			} while (creatureId < &g_creatureActors[64].creatureId);
			creatureIdList[index] = 0xff;
		}

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