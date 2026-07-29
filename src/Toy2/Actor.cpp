#include "Toy2/Actor.h"
#include "Toy2/Animation.h"
#include "Toy2/Buzz.h"
#include "Toy2/Collectables.h"
#include "Toy2/Toy2.h"
#include "AudioManager/AudioManager.h"
#include "CharacterLoader.h"
#include "Nu3D/Particles.h"
#include "Random.h"
#include "Renderer/Renderer.h"

#include <string.h>

namespace Toy2
{
	namespace Dialogue
	{
		// STUB: TOY2 0x004027F0
		void Begin(int32_t actorIndex, int32_t recordIndex, char* subtitle, int32_t actorFacingAngle, int32_t cameraFacingAngle, int32_t duration) {}
	}

	namespace Game
	{
		void InitActor(Actor::Toy2Actor* actor, int32_t param);
	}

	namespace Particles
	{
		// STUB: TOY2 0x00410410
		void SpawnCollectSparkle(int32_t x, int32_t y, int32_t z, int32_t particleSpread) {}
	}

	namespace Actor
	{
		// GLOBAL: TOY2 0x0052F1D0
		Toy2Actor* g_activeActors[65];

		// GLOBAL: TOY2 0x0052c840
		Toy2Actor g_creatureActors[64];

		// GLOBAL: TOY2 0x004E0588
		uint8_t* g_animationFrameSequences[26];

		// GLOBAL: TOY2 0x00830D40
		int32_t g_periodicHintSoundTimer;

		// GLOBAL: TOY2 0x00830C98
		int32_t g_coinQuestHintTimer;

		// GLOBAL: TOY2 0x00830D1C
		int32_t g_coinTokenAwarded;

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

		// STUB: TOY2 0x00405D20
		void Kill(Toy2Actor* actor, uint8_t killFlags) {}

		// FUNCTION: TOY2 0x00407150 [PROVISIONAL]
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

		// FUNCTION: TOY2 0x004019D0 [MATCHED]
		void UpdatePrimaryAnimation(Toy2Actor* actor)
		{
			CharacterLoader::CharacterAnimationData* animationData = CharacterLoader::g_characterAnimationData[actor->creatureId];
			Animation::EvaluateClip(animationData->clips[actor->primaryAnimIdx], actor->animationFramePosition, animationData->baseBoneIndex, 0);
		}

		// FUNCTION: TOY2 0x00405C80 [PROVISIONAL]
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

		// FUNCTION: TOY2 0x00405CF0 [MATCHED]
		void SetAnimation(Toy2Actor* actor, int16_t animationIndex, int32_t frameSequenceIndex)
		{
			actor->primaryAnimIdx = animationIndex;
			actor->animationFrameSequence = g_animationFrameSequences[frameSequenceIndex];
			actor->animationFramePosition = (uint32_t)*actor->animationFrameSequence << 16;
		}

		// FUNCTION: TOY2 0x0049F460 [MATCHED]
		int32_t IsInsideBounds(const Vector3I* position, int32_t minX, int32_t maxX, int32_t minZ, int32_t maxZ)
		{ return position->x > minX && position->x < maxX && position->z > minZ && position->z < maxZ; }

		// FUNCTION: TOY2 0x004A28B0 [MATCHED]
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

		// FUNCTION: TOY2 0x004A1CE0 [MATCHED]
		void CollectQuestReward(int32_t actorIndex, int32_t dialogueRecordIndex, int32_t actorFacingAngle, int32_t cameraFacingAngle, int32_t tokenIndex)
		{
			if (Collectables::g_tokenStates[tokenIndex].active == 0 && (g_creatureActors[actorIndex].actorFlags & ACTOR_FLAG_ACTIVE) != 0
				&& (Toy2::g_gameplayStateFlags & 1) == 0)
			{
				g_coinQuestHintTimer -= Renderer::g_frameDelta;
				if (g_coinQuestHintTimer < 0)
				{
					g_coinQuestHintTimer = *g_randDatBufferPtr++ * 2 + 0xF0;
					AudioManager::PlayOneShotSound3DActor(&g_creatureActors[actorIndex],
						AudioManager::g_oneShotPresets[0xAC].encodedSoundIndex - 1,
						AudioManager::g_oneShotPresets[0xAC].baseFrequency,
						AudioManager::g_oneShotPresets[0xAC].leftVolume,
						&g_creatureActors[actorIndex],
						0);
				}
			}

			if ((g_creatureActors[actorIndex].actorFlags & ACTOR_FLAG_INTERACTION_REQUESTED) == 0)
				return;

			g_creatureActors[actorIndex].actorFlags &= ~ACTOR_FLAG_INTERACTION_REQUESTED;
			if (Collectables::g_tokenStates[tokenIndex].active != 0)
				return;

			if (Toy2::g_buzzActor.coinsCollected >= 50)
			{
				AudioManager::PlayOneShotSound3DActor(&g_creatureActors[actorIndex],
					AudioManager::g_oneShotPresets[0xAE].encodedSoundIndex - 1,
					AudioManager::g_oneShotPresets[0xAE].baseFrequency,
					AudioManager::g_oneShotPresets[0xAE].leftVolume,
					&g_creatureActors[actorIndex],
					0);
				g_coinTokenAwarded = 1;
				Dialogue::Begin(
					actorIndex, dialogueRecordIndex, "well done buzz! here is your pizza planet ^token^.", actorFacingAngle, cameraFacingAngle, tokenIndex);
				return;
			}

			AudioManager::PlayOneShotSound3DActor(&g_creatureActors[actorIndex],
				AudioManager::g_oneShotPresets[0xAD].encodedSoundIndex - 1,
				AudioManager::g_oneShotPresets[0xAD].baseFrequency,
				AudioManager::g_oneShotPresets[0xAD].leftVolume,
				&g_creatureActors[actorIndex],
				0);
			Dialogue::Begin(actorIndex,
				dialogueRecordIndex,
				"hi buzz! if you can bring me ^fifty^ coins, i will give you a pizza planet ^token^.",
				actorFacingAngle,
				cameraFacingAngle,
				-1);
		}

		// FUNCTION: TOY2 0x004A26F0 [PROVISIONAL]
		void PlayPeriodicHintSound(int32_t actorIndex, int32_t soundPresetIndex)
		{
			if (soundPresetIndex == 0xB5)
			{
				if (Toy2::HUD::g_challengeState != 0)
					return;
			}
			else if (Toy2::g_levelObjectiveProgress < 0)
			{
				return;
			}

			if ((g_creatureActors[actorIndex].actorFlags & ACTOR_FLAG_ACTIVE) == 0 || (Toy2::g_gameplayStateFlags & 1) != 0)
				return;

			g_periodicHintSoundTimer -= Renderer::g_frameDelta;
			if (g_periodicHintSoundTimer >= 0)
				return;

			g_periodicHintSoundTimer = *g_randDatBufferPtr++ * 2 + 0xF0;
			AudioManager::OneShotSoundPreset* preset = &AudioManager::g_oneShotPresets[soundPresetIndex];
			AudioManager::PlayOneShotSound3DActor(
				&g_creatureActors[actorIndex], preset->encodedSoundIndex - 1, preset->baseFrequency, preset->leftVolume, &g_creatureActors[actorIndex], 0);
		}

		// FUNCTION: TOY2 0x00414A80 [PROVISIONAL]
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

	namespace CreatureBehaviour
	{
		// GLOBAL: TOY2 0x004E0318
		uint16_t* g_boxMovementData;

		// FUNCTION: TOY2 0x004068E0 [EFFECTIVE]
		void Box(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			if (actor->previousActorPhase != 0)
			{
				actor->previousActorPhase = 0;
				if (actor[1].actorPhase == 0)
				{
					actor[1].respawnDelay = 30;
					uint16_t* movementData = g_boxMovementData + 53;
					actor->movementCommandTimer = 0;
					actor->movementData = movementData;
				}
				else if (actor[2].actorPhase == 0)
				{
					actor[2].respawnDelay = 30;
					uint16_t* movementData = g_boxMovementData + 53;
					actor->movementCommandTimer = 0;
					actor->movementData = movementData;
				}
				else
				{
					uint16_t* movementData = g_boxMovementData;
					actor->movementCommandTimer = 0;
					actor->movementData = movementData + 59;
				}
			}
		}

		// FUNCTION: TOY2 0x00406C70 [MATCHED]
		void Buzzard(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			if (actor->previousActorPhase != actor->actorPhase)
			{
				if (actor->previousActorPhase != 0)
				{
					AudioManager::PlaySoundEffect(0x58, &actor->pos);
				}
				actor->previousActorPhase = actor->actorPhase;
			}

			AudioManager::PlaySoundEffect(0x57, &actor->pos);
			if ((context->targetFlags & 1) != 0)
			{
				actor->primaryAnimIdx = 1;
			}
			else
			{
				actor->primaryAnimIdx = 0;
			}
		}

		// FUNCTION: TOY2 0x00416A60 [MATCHED]
		void Sheep(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			if ((actor->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0)
			{
				g_levelObjectiveProgress++;
				Particles::SpawnCollectSparkle(actor->pos.x, actor->pos.y - 0x2000, actor->pos.z, 0x32);
				AudioManager::PlaySoundEffect(0x20, &actor->pos);
				Actor::Kill(actor, 2);
			}
		}

		// FUNCTION: TOY2 0x0041BB80 [MATCHED]
		void LTyke(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
			if (actor->previousActorPhase <= 0)
			{
				actor->previousActorPhase = (*g_randDatBufferPtr++ & 0x7F) + 0x5A;
				AudioManager::PlaySoundEffect(0x6B, &actor->pos);
			}

			if ((actor->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0 && actor->actorPhase == 0x66)
			{
				g_levelObjectiveProgress++;
				Particles::SpawnCollectSparkle(actor->pos.x, actor->pos.y - 0x2000, actor->pos.z, 0x32);
				AudioManager::PlaySoundEffect(0x6C, &actor->pos);
				Actor::Kill(actor, 2);
			}
		}

		// FUNCTION: TOY2 0x00420ED0 [MATCHED]
		void Chick(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
			if (actor->previousActorPhase <= 0)
			{
				actor->previousActorPhase = (*g_randDatBufferPtr++ & 0x7F) + 0x3C;
				AudioManager::PlaySoundEffect(0x77, &actor->pos);
			}

			if ((actor->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0 && actor->actorPhase == 0x66)
			{
				g_levelObjectiveProgress++;
				Particles::SpawnCollectSparkle(actor->pos.x, actor->pos.y - 0x2000, actor->pos.z, 0x32);
				AudioManager::PlaySoundEffect(0x77, &actor->pos);
				Actor::Kill(actor, 2);
			}
		}

		// STUB: TOY2 0x00416F30
		void RCCarLevel1(Actor::Toy2Actor::ActorBehaviourContext* context) {}

		// STUB: TOY2 0x00418720
		void RCCarLevel2(Actor::Toy2Actor::ActorBehaviourContext* context) {}

		// FUNCTION: TOY2 0x00406A60 [MATCHED]
		void RCCar(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			if (g_levelFileIndex == 1)
			{
				RCCarLevel1(context);
			}
			if (g_levelFileIndex == 2)
			{
				RCCarLevel2(context);
			}
		}
	}
}
