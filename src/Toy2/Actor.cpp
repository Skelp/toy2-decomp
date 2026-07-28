#include "Toy2/Actor.h"
#include "Toy2/Animation.h"
#include "Toy2/Buzz.h"
#include "CharacterLoader.h"
#include "Nu3D/Particles.h"
#include "Random.h"

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

		// GLOBAL: TOY2 0x004E0588
		uint8_t* g_animationFrameSequences[26];

		// GLOBAL: TOY2 0x00529D48
		Toy2Actor* g_renderActors[66];

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
			Animation::EvaluateClip((Animation::ClipHeader*)*(void**)((uint8_t*)entry + 8 + actor->primaryAnimIdx * 4),
				actor->animationFramePosition,
				*(uint16_t*)((uint8_t*)entry + 4),
				0);
		}

		// FUNCTION: TOY2 0x00405C80
		void StepCreatureAnimFrame(Toy2Actor* actor)
		{
			if (actor->animationFrameSequence[2] == 0xff && actor->animationFrameSequence[3] == 0)
			{
				uint8_t* frame = actor->animationFrameSequence;
				actor->animationFramePosition = ((uint32_t)frame[0] << 16) + 0xffff;
				return;
			}

			uint8_t* frame = actor->animationFrameSequence + 1;
			actor->animationFrameSequence = frame;
			if (*frame == 0xff)
			{
				if (actor->animationFrameSequence[1] != 1)
				{
					actor->animationFrameSequence -= actor->animationFrameSequence[1];
				}
				else
				{
					actor->animationFrameSequence--;
					actor->animationFramePosition |= 0xffff;
				}
			}

			actor->animationFramePosition &= 0xffff;
			actor->animationFramePosition += (uint32_t)*actor->animationFrameSequence << 16;
		}

		// FUNCTION: TOY2 0x00405CF0
		void SetAnimation(Toy2Actor* actor, int16_t animationIndex, int32_t frameSequenceIndex)
		{
			actor->primaryAnimIdx = animationIndex;
			actor->animationFrameSequence = g_animationFrameSequences[frameSequenceIndex];
			actor->animationFramePosition = (uint32_t)*actor->animationFrameSequence << 16;
		}

		// FUNCTION: TOY2 0x0049F460
		int32_t IsInsideBounds(const Vector3I* position, int32_t minX, int32_t maxX, int32_t minZ, int32_t maxZ)
		{ return position->x > minX && position->x < maxX && position->z > minZ && position->z < maxZ; }

		// FUNCTION: TOY2 0x004A28B0
		void PopulateActiveActors()
		{
			int32_t actorIndex = 0;
			while (g_activeActors[actorIndex] != 0)
			{
				g_renderActors[actorIndex] = g_activeActors[actorIndex];
				actorIndex++;
			}
			g_renderActors[actorIndex++] = (Toy2Actor*)&Toy2::g_buzzActor;
			g_renderActors[actorIndex] = 0;
		}

		// FUNCTION: TOY2 0x004104D0 [MATCHED]
		void HitType1Particles(int32_t x, int32_t y, int32_t z)
		{
			for (int32_t count = 3; count != 0; count--)
			{
				Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(x, y, z, 0x5E, 9);
				particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
				particle->lifetime = (*g_randDatBufferPtr++ & 0xF) * 2 + 0x18;
			}
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
