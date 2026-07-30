#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Actor.h"
#include "Toy2/Buzz.h"
#include "Toy2/Collectables.h"
#include "Toy2/Levels.h"
#include "Toy2/Particles.h"
#include "Toy2/Weather.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
#include "Random.h"
#include "Numerics.h"

#include <limits.h>

namespace Toy2
{
	extern int32_t g_hudActorAnimationFrame;

	namespace AlsPenthouse
	{
		struct ObjectGroup
		{
			int16_t mask;
			int16_t primaryLinkId;
			int16_t alternateLinkId;
			int16_t rotationLinkId;
			int16_t rotationX;
			int16_t rotationY;
			int16_t rotationZ;
		};
		enum ObjectGroupPrimaryCursor
		{
			PRIMARY_CURSOR_MASK = -1,
			PRIMARY_CURSOR_LINK = 0,
			PRIMARY_CURSOR_ALTERNATE_LINK = 1,
		};
		enum ObjectGroupAlternateCursor
		{
			ALTERNATE_CURSOR_MASK = -2,
			ALTERNATE_CURSOR_PRIMARY_LINK = -1,
			ALTERNATE_CURSOR_LINK = 0,
		};

		enum GunslingerEncounterState
		{
			GUNSLINGER_ENCOUNTER_ACTIVE = 2,
			GUNSLINGER_ENCOUNTER_DEFEATED = 3,
			GUNSLINGER_ENCOUNTER_COMPLETE = 200,
		};

		struct HiddenCollectibleState
		{
			int32_t* verticalPosition;
			int32_t savedVerticalPosition;
		};

		// GLOBAL: TOY2 0x0052FD10
		int32_t g_gunslingerPhaseTimer;
		// GLOBAL: TOY2 0x0052FD2C
		int32_t g_previousGunslingerPhase;
		// GLOBAL: TOY2 0x0052FD40
		int32_t g_gunslingerEncounterState;
		// GLOBAL: TOY2 0x0052FD70
		int32_t g_gunslingerTintToggle;
		// GLOBAL: TOY2 0x0052FD08
		int32_t g_objectGroupFlashPhase;
		// GLOBAL: TOY2 0x0052FD14
		int32_t g_objectGroupMask;
		// GLOBAL: TOY2 0x004F3F48
		ObjectGroup g_objectGroups[7] = {
			{ 1, 36, 17, 84, -350, 0, 0 },
			{ 2, 37, 18, 84, 0, 0, 0 },
			{ 4, 31, 12, 86, 175, 0, 0 },
			{ 8, 32, 13, 86, 350, 0, 0 },
			{ 16, 33, 14, 86, 0, 0, 0 },
			{ 32, 34, 15, 85, 0, 0, -350 },
			{ 64, 35, 16, 85, 0, 0, 0 },
		};
		// GLOBAL: TOY2 0x0052FDC8
		HiddenCollectibleState g_hiddenCollectibles[5];

		STATIC_ASSERT(sizeof(HiddenCollectibleState) == 0x8);

		// FUNCTION: TOY2 0x00429CE0 [TOOL]
		void InitHiddenCollectibles()
		{
			Levels::RecordData* pickupRecords = Levels::g_recordData[63];
			Collectables::PickupRecord* pickup = reinterpret_cast<Collectables::PickupRecord*>(pickupRecords + 1);
			for (int32_t pickupIndex = 0; pickupIndex < pickupRecords->recordCount; pickup++, pickupIndex++)
			{
				if (pickup->objectIndex == 101)
					g_hiddenCollectibles[0].verticalPosition = &pickup->position.y;
				if (pickup->objectIndex == 102)
					g_hiddenCollectibles[1].verticalPosition = &pickup->position.y;
				if (pickup->objectIndex == 103)
					g_hiddenCollectibles[2].verticalPosition = &pickup->position.y;
				if (pickup->objectIndex == 104)
					g_hiddenCollectibles[3].verticalPosition = &pickup->position.y;
				if (pickup->objectIndex == 105)
					g_hiddenCollectibles[4].verticalPosition = &pickup->position.y;
			}

			for (int32_t hiddenIndex = 0; hiddenIndex < 5; hiddenIndex++)
			{
				g_hiddenCollectibles[hiddenIndex].savedVerticalPosition = *g_hiddenCollectibles[hiddenIndex].verticalPosition;
				*g_hiddenCollectibles[hiddenIndex].verticalPosition = INT_MIN;
				Nu3D::Link::SetScaleFromFixedOffsets(hiddenIndex + 101, 0, 0, 0);
			}
		}

		// FUNCTION: TOY2 0x00428BA0 [PROVISIONAL]
		void UpdateObjectGroupFlash()
		{
			int32_t phase = Renderer::g_frameDelta;
			phase += g_objectGroupFlashPhase;
			g_objectGroupFlashPhase = phase & 0x1F;

			if (g_objectGroupFlashPhase < 24 && (g_objectGroupMask & 0x800) == 0)
			{
				g_objectGroupMask |= 0x800;
				int16_t* group = &g_objectGroups[0].primaryLinkId;
				do
				{
					if ((g_objectGroupMask & group[PRIMARY_CURSOR_MASK]) != 0)
					{
						Nu3D::Link::SetScaleFromFixedOffsets(group[PRIMARY_CURSOR_LINK], 0x1000, 0x1000, 0x1000);
						Nu3D::Link::SetScaleFromFixedOffsets(group[PRIMARY_CURSOR_ALTERNATE_LINK], 0, 0, 0);
					}
					group += sizeof(ObjectGroup) / sizeof(int16_t);
				} while ((int32_t)group < (int32_t)&g_objectGroups[7].primaryLinkId);
				return;
			}

			if (g_objectGroupFlashPhase > 24 && (g_objectGroupMask & 0x800) != 0)
			{
				g_objectGroupMask &= ~0x800;
				int16_t* group = &g_objectGroups[0].alternateLinkId;
				do
				{
					if ((g_objectGroupMask & group[ALTERNATE_CURSOR_MASK]) != 0)
					{
						Nu3D::Link::SetScaleFromFixedOffsets(group[ALTERNATE_CURSOR_LINK], 0x1000, 0x1000, 0x1000);
						Nu3D::Link::SetScaleFromFixedOffsets(group[ALTERNATE_CURSOR_PRIMARY_LINK], 0, 0, 0);
					}
					group += sizeof(ObjectGroup) / sizeof(int16_t);
				} while ((int32_t)group < (int32_t)&g_objectGroups[7].alternateLinkId);
			}
		}

		// STUB: TOY2 0x00429D70
		void Init() {}

		// STUB: TOY2 0x0042A130
		void Interactions() {}

		STATIC_ASSERT(sizeof(ObjectGroup) == 0xE);
	}
}

namespace Toy2
{
	namespace CreatureBehaviour
	{
		// FUNCTION: TOY2 0x004282D0 [MATCHED]
		void GunsLLevel11(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			AlsPenthouse::g_gunslingerTintToggle = (AlsPenthouse::g_gunslingerTintToggle - 1) & 1;

			if (actor->actorPhase != AlsPenthouse::g_previousGunslingerPhase)
			{
				AlsPenthouse::g_previousGunslingerPhase = actor->actorPhase;
				AlsPenthouse::g_gunslingerPhaseTimer = 60;
				actor->creatureRam->defenseMode = 4;
				AudioManager::PlaySoundEffect(0xA5, &actor->pos);
			}

			if (AlsPenthouse::g_gunslingerEncounterState == AlsPenthouse::GUNSLINGER_ENCOUNTER_ACTIVE)
			{
				AlsPenthouse::g_gunslingerPhaseTimer -= Renderer::g_frameDelta;
				if (AlsPenthouse::g_gunslingerPhaseTimer < 0)
				{
					AlsPenthouse::g_gunslingerPhaseTimer = 0;
					actor->creatureRam->defenseMode = 7;
					actor->useTint = 0;
				}
				else if (AlsPenthouse::g_gunslingerTintToggle != 0)
				{
					actor->useTint = 1;
					actor->actorTint.r = 0x2000;
					actor->actorTint.g = 0x2000;
					actor->actorTint.b = 0x2000;
				}
				else
				{
					actor->useTint = 0;
				}
			}
			else
			{
				actor->useTint = 0;
			}

			if (g_buzzActor.posAngles.pos.y < 0x1BCA1 || g_buzzActor.posAngles.pos.y > 0x22405)
			{
				actor->motionTargetPos.x = actor->boundary.x;
				actor->motionTargetPos.z = actor->boundary.z;
				actor->creatureRam->defenseMode = 4;
			}
			else if (AlsPenthouse::g_gunslingerPhaseTimer == 0)
			{
				actor->creatureRam->defenseMode = 7;
			}

			if (actor->previousActorPhase != 0)
			{
				int32_t fireProjectile = 0;
				Vector4I projectilePosition;
				if (actor->previousActorPhase > 20)
				{
					fireProjectile = 1;
					projectilePosition.x = 0;
					projectilePosition.y = 0;
					projectilePosition.z = 0;
					Actor::ResolveBoneAttachmentPos(&projectilePosition, actor, 15);
					actor->previousActorPhase = 10;
					AudioManager::PlaySoundEffect(0x93, &actor->pos);
				}

				actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
				if (actor->previousActorPhase <= 0)
				{
					projectilePosition.x = 0;
					projectilePosition.y = 0;
					projectilePosition.z = 0;
					Actor::ResolveBoneAttachmentPos(&projectilePosition, actor, 16);
					actor->previousActorPhase = 0;
					AudioManager::PlaySoundEffect(0x94, &actor->pos);
					fireProjectile = 1;
				}

				if (fireProjectile != 0)
				{
					int32_t projectileAngle =
						Nu3D::Math::CartesianToFixedAngle(
							g_buzzActor.posAngles.pos.x - projectilePosition.x, g_buzzActor.posAngles.pos.z - projectilePosition.z)
						& 0xFFF;
					if (((projectileAngle - actor->yawAngle + 0x100) & 0xFFF) > 0x200)
						projectileAngle = actor->yawAngle;

					Nu3D::Particles::SpawnInstance(projectilePosition.x,
						projectilePosition.y,
						projectilePosition.z,
						Numerics::g_sinCosLUT[projectileAngle] >> 2,
						0x200,
						Numerics::g_sinCosLUT[(projectileAngle + 0x400) & 0xFFF] >> 2,
						0,
						0,
						0,
						0x61);

					if ((actor->actorFlags & Actor::ACTOR_FLAG_TARGETABLE) != 0)
					{
						for (int32_t particleCount = 5; particleCount != 0; particleCount--)
						{
							Nu3D::Particles::ParticleInstance* particle =
								Nu3D::Particles::SpawnFromPreset(projectilePosition.x, projectilePosition.y, projectilePosition.z, 100, 15);
							particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
						}
					}
				}
			}

			if (AlsPenthouse::g_gunslingerEncounterState == AlsPenthouse::GUNSLINGER_ENCOUNTER_ACTIVE)
			{
				g_hudActorAnimationFrame = (actor->actorPhase - 9) * 54 / 20;
				if (Sector::g_activeSectorIndex == 2 && g_buzzActor.posAngles.pos.y < 0x22405)
					HUD::g_slideTimers[HUD::SLIDE_BOSS_STATUS] = 90;
			}

			if (actor->actorPhase < 10 && AlsPenthouse::g_gunslingerEncounterState == AlsPenthouse::GUNSLINGER_ENCOUNTER_ACTIVE)
			{
				actor->movementData = g_gunslingerMovementData + 52;
				actor->creatureRam->defenseMode = 4;
				actor->actorFlags &= ~Actor::ACTOR_FLAG_DAMAGES_BUZZ;
				AudioManager::PlaySoundEffect(-2, &actor->pos);
				AlsPenthouse::g_gunslingerEncounterState = AlsPenthouse::GUNSLINGER_ENCOUNTER_DEFEATED;
				g_hudActorAnimationFrame = 0;
				actor->movementCommandTimer = 0;
			}

			if (AlsPenthouse::g_gunslingerEncounterState >= AlsPenthouse::GUNSLINGER_ENCOUNTER_ACTIVE)
			{
				if (actor->pos.z < -0x5B4F0)
				{
					actor->boundary.x = -0x9D020;
					actor->boundary.z = -0x62040;
					actor->creatureRam->boundHalfX = 0x12B;
					actor->creatureRam->boundHalfZ = 0x70;
				}
				else
				{
					actor->boundary.x = -0xA85A0;
					actor->boundary.z = -0x43FC0;
					actor->creatureRam->boundHalfX = 0x60;
					actor->creatureRam->boundHalfZ = 0x17E;
				}
			}

			if (AlsPenthouse::g_gunslingerEncounterState == AlsPenthouse::GUNSLINGER_ENCOUNTER_COMPLETE)
			{
				actor->pos.x = actor->boundary.x;
				actor->pos.z = actor->boundary.z;
			}
		}

		// FUNCTION: TOY2 0x00428650 [MATCHED]
		void Rabid(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
			if (actor->previousActorPhase <= 0)
			{
				actor->previousActorPhase = (*g_randDatBufferPtr++ & 0x7F) + 0x3C;
				AudioManager::PlaySoundEffect(0x96, &actor->pos);
			}

			if ((actor->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0 && actor->actorPhase == 0x66)
			{
				g_levelObjectiveProgress++;
				Particles::SpawnCollectSparkle(actor->pos.x, actor->pos.y - 0x2000, actor->pos.z, 0x32);
				AudioManager::PlaySoundEffect(0x96, &actor->pos);
				Actor::Kill(actor, 2);
			}
		}
	}
}
