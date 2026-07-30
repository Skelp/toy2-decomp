#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Actor.h"
#include "Toy2/Camera.h"
#include "Toy2/Collision.h"
#include "Toy2/Collectables.h"
#include "Toy2/Direct6.h"
#include "Toy2/MainMenu.h"
#include "Toy2/Particles.h"
#include "Toy2/Weather.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
#include "Random.h"
#include "Numerics.h"
#include "Nullsub.h"

namespace Toy2
{
	extern int32_t g_hudActorAnimationFrame;

	namespace Lighting
	{
		void SpawnLight(int32_t x, int32_t y, int32_t z, int32_t colour, int32_t lifetime, int32_t sourceId);
	}

	namespace MoveableObject
	{
		extern State g_objects[10];
	}

	namespace AlsToyBarn
	{
		enum DinoEncounterState
		{
			DINO_ENCOUNTER_ACTIVE = 2,
			DINO_ENCOUNTER_DEFEATED = 3,
		};

		// GLOBAL: TOY2 0x004F2844
		char g_hayBaleRideInstructions[] = {
#include "HayBaleRideInstructions.inc"
		};

		// GLOBAL: TOY2 0x004F294C
		uint16_t g_platform7MotionScript[18] = { 1, 0, 0xF894, 0, 8, 3, 0x7F, 0x60, 1, 0, 0, 0, 8, 3, 0x7F, 0x20, 0, 0x10 };
		// GLOBAL: TOY2 0x004F2970
		uint16_t g_platform8MotionScript[18] = { 1, 0, 0xF574, 0, 8, 3, 0x7F, 0x60, 1, 0, 0, 0, 8, 3, 0x7F, 0x20, 0, 0x10 };
		// GLOBAL: TOY2 0x004F2994
		uint16_t g_platform9MotionScript[18] = { 1, 0, 0xF574, 0, 8, 3, 0x7F, 0x60, 1, 0, 0, 0, 8, 3, 0x7F, 0x20, 0, 0x10 };
		// GLOBAL: TOY2 0x004F29B8
		uint16_t g_platform13MotionScript[18] = { 1, 0x316, 0, 0, 0xC, 3, 0xFF, 0x20, 1, 0, 0, 0, 0xC, 3, 0xFF, 0x20, 0, 0x10 };
		// GLOBAL: TOY2 0x004F29DC
		uint16_t g_platform12MotionScript[18] = { 1, 0x316, 0, 0, 0xC, 3, 0xFF, 0x20, 1, 0, 0, 0, 0xC, 3, 0xFF, 0x20, 0, 0x10 };
		// GLOBAL: TOY2 0x004F2A00
		uint16_t g_platform11MotionScript[18] = { 1, 0x316, 0, 0, 0xC, 3, 0xFF, 0x20, 1, 0, 0, 0, 0xC, 3, 0xFF, 0x20, 0, 0x10 };

		// GLOBAL: TOY2 0x004F2A24
		MoveableObject::InitEntry g_moveableObjectInitTable[] = {
			{ 8, 5, 0 },
			{ 9, 6, 1 },
			{ -1, 0, 0 },
		};

		// GLOBAL: TOY2 0x004F2A48
		int16_t g_tokenLinkIds[] = { 0x31, 0x33, 0x34, 0x32, 0x30, 0 };

		// GLOBAL: TOY2 0x004F2A54
		extern const Collectables::TokenDialogueValue g_tokenDialogueValues[] = {
			{ 0x46 },
			{ 0x14 },
			{ reinterpret_cast<int32_t>(g_hayBaleRideInstructions) },
			{ 0x400 },
			{ -1 },
		};

		// GLOBAL: TOY2 0x0052F9BC
		int32_t g_platform10VerticalVelocity;
		// GLOBAL: TOY2 0x0052F9C0
		int32_t g_groundSlamPlatformTimer;
		// GLOBAL: TOY2 0x0052F9DC
		int32_t g_chick10WasActive;
		// GLOBAL: TOY2 0x0052F9E0
		int32_t g_chick11WasActive;
		// GLOBAL: TOY2 0x0052F9E4
		int32_t g_hayBaleRideState;
		// GLOBAL: TOY2 0x0052F9E8
		int32_t g_platform14MotionSpeed;
		// GLOBAL: TOY2 0x0052F9EC
		int32_t g_launchPadBounceTimer;
		// GLOBAL: TOY2 0x0052F9F0
		uint16_t* g_platform12MotionCursor;
		// GLOBAL: TOY2 0x0052F9F4
		uint16_t* g_platform11MotionCursor;
		// GLOBAL: TOY2 0x0052F9F8
		uint16_t* g_platform9MotionCursor;
		// GLOBAL: TOY2 0x0052F9FC
		uint16_t* g_platform13MotionCursor;
		// GLOBAL: TOY2 0x0052FA00
		uint16_t* g_platform7MotionCursor;
		// GLOBAL: TOY2 0x0052FA04
		uint16_t* g_platform8MotionCursor;
		// GLOBAL: TOY2 0x0052FA08
		int32_t g_eggLiftOffset;
		// GLOBAL: TOY2 0x0052FA0C
		int32_t g_ambientParticlePositionIndex;
		// GLOBAL: TOY2 0x0052FA10
		int32_t g_targetEggLiftOffset;

		// GLOBAL: TOY2 0x0052FA14
		int32_t g_dinoEncounterState;
		// GLOBAL: TOY2 0x0052FA18
		int32_t g_unusedState;
		// GLOBAL: TOY2 0x0052FA1C
		int32_t g_eggChallengeState;
		// GLOBAL: TOY2 0x0052FA20
		int32_t g_dinoTintToggle;
		// GLOBAL: TOY2 0x0052FA24
		int32_t g_hayBaleRideTimer;
		// GLOBAL: TOY2 0x0052FA28
		int32_t g_hayBaleRideSpeed;
		// GLOBAL: TOY2 0x0052FA2C
		int32_t g_previousDinoPhase;
		// GLOBAL: TOY2 0x0052FA30
		int32_t g_hayBaleRideAngle;
		// GLOBAL: TOY2 0x0052FA34
		int32_t g_platform12MotionTimer;
		// GLOBAL: TOY2 0x0052FA38
		int32_t g_platform11MotionTimer;
		// GLOBAL: TOY2 0x0052FA3C
		int32_t g_platform9MotionTimer;
		// GLOBAL: TOY2 0x0052FA40
		int32_t g_platform13MotionTimer;
		// GLOBAL: TOY2 0x0052FA44
		int32_t g_platform7MotionTimer;
		// GLOBAL: TOY2 0x0052FA48
		int32_t g_platform8MotionTimer;
		// GLOBAL: TOY2 0x0052FA4C
		int32_t g_platform10ForwardSpeed;
		// GLOBAL: TOY2 0x0052FA50
		int32_t g_dinoTintTimer;

		// FUNCTION: TOY2 0x00420F70 [MATCHED]
		void ResolveChickObjectCollision(int32_t actorIndex, int32_t objectIndex)
		{
			if ((Actor::g_creatureActors[actorIndex].actorFlags & Actor::ACTOR_FLAG_ACTIVE) != 0)
			{
				int32_t actorY = Actor::g_creatureActors[actorIndex].pos.y;
				if (actorY > MoveableObject::g_objects[1].position.y - 0x9000)
				{
					int32_t deltaX = (MoveableObject::g_objects[objectIndex].position.x - Actor::g_creatureActors[actorIndex].pos.x) >> 5;
					int32_t deltaZ = (MoveableObject::g_objects[objectIndex].position.z - Actor::g_creatureActors[actorIndex].pos.z) >> 5;
					if (deltaZ * deltaZ + deltaX * deltaX < 490000)
					{
						int32_t objectY = MoveableObject::g_objects[objectIndex].position.y;
						if (actorY < objectY - 0x8800)
						{
							Actor::g_creatureActors[actorIndex].pos.y = objectY - 0x9000;
							return;
						}

						int32_t angle = Nu3D::Math::CartesianToFixedAngle(deltaX, deltaZ) & 0xFFF;
						Actor::g_creatureActors[actorIndex].pos.x =
							MoveableObject::g_objects[objectIndex].position.x - (Numerics::g_sinCosLUT[angle] * 700 >> 9);
						Actor::g_creatureActors[actorIndex].pos.z =
							MoveableObject::g_objects[objectIndex].position.z - (Numerics::g_sinCosLUT[(angle + 0x400) & 0xFFF] * 700 >> 9);
					}
				}
			}
		}

		// FUNCTION: TOY2 0x00421090 [PROVISIONAL]
		void Init()
		{
			Collectables::Init(g_tokenLinkIds, 0x41);
			Collectables::LoadTokenTable(g_tokenDialogueValues);
			MoveableObject::InitTable(g_moveableObjectInitTable);
			Collectables::Activate(3, 1);
			Nullsub7(0x13, 0x12);

			g_hayBaleRideState = 0;
			g_hayBaleRideTimer = 0;
			g_hayBaleRideAngle = 0;
			g_hayBaleRideSpeed = 0;
			Platform::AddFlags(0, 0x100);

			Actor::g_creatureActors[6].actorFlags &= ~(Actor::ACTOR_FLAG_TARGETABLE | Actor::ACTOR_FLAG_COLLIDABLE);
			g_ambientParticlePositionIndex = 0;
			g_dinoEncounterState = 0;
			g_dinoTintToggle = 0;
			g_dinoTintTimer = 0;
			g_previousDinoPhase = Actor::g_creatureActors[0].actorPhase;
			g_chick10WasActive = 0;
			g_chick11WasActive = 0;
			g_unusedState = 0;
			g_groundSlamPlatformTimer = 0;
			Nu3D::Link::SetScaleFromFixedOffsets(0x1E, 0x1000, 0, 0x1000);

			g_launchPadBounceTimer = 0;
			Nu3D::Link::SetScaleFromFixedOffsets(3, 0x1000, 0, 0x1000);
			g_platform14MotionSpeed = 0;
			g_platform10VerticalVelocity = 0;
			g_platform10ForwardSpeed = 0;
			Platform::AddFlags(10, 0x100);
			Platform::AddFlags(14, 0x100);

			g_eggLiftOffset = 0;
			g_eggChallengeState = 0;
			g_targetEggLiftOffset = 0;
			HUD::g_challengeState = 0;
			AndysHouse::g_raceCheckpointPassCount = 0;

			g_platform7MotionCursor = g_platform7MotionScript;
			g_platform7MotionTimer = 0;
			g_platform8MotionCursor = g_platform8MotionScript;
			g_platform8MotionTimer = 0;
			g_platform9MotionCursor = g_platform9MotionScript;
			g_platform9MotionTimer = 0;
			Platform::AddFlags(7, 0x100);
			Platform::AddFlags(8, 0x100);
			Platform::AddFlags(9, 0x100);
			g_platform13MotionCursor = g_platform13MotionScript;
			g_platform13MotionTimer = 0;
			g_platform12MotionCursor = g_platform12MotionScript;
			g_platform12MotionTimer = 0;
			g_platform11MotionCursor = g_platform11MotionScript;
			g_platform11MotionTimer = 0;

			Vector3I platformOrigin;
			Platform::GetOrigin(13, &platformOrigin);
			Platform::SetOrigin(13, platformOrigin.x + 0x62C0, platformOrigin.y, platformOrigin.z);
			Nu3D::Link::SetPositionRawAndCommit(12, (platformOrigin.x + 0x62C0) >> 5, platformOrigin.y >> 5, platformOrigin.z >> 5);

			Platform::GetOrigin(12, &platformOrigin);
			Platform::SetOrigin(12, platformOrigin.x + 0x62C0, platformOrigin.y, platformOrigin.z);
			Nu3D::Link::SetPositionRawAndCommit(13, (platformOrigin.x + 0x62C0) >> 5, platformOrigin.y >> 5, platformOrigin.z >> 5);

			Platform::GetOrigin(11, &platformOrigin);
			Platform::SetOrigin(11, platformOrigin.x + 0x62C0, platformOrigin.y, platformOrigin.z);
			Nu3D::Link::SetPositionRawAndCommit(14, (platformOrigin.x + 0x62C0) >> 5, platformOrigin.y >> 5, platformOrigin.z >> 5);

			MainMenu::g_menuClearColor.r = g_skyColorRed;
			MainMenu::g_menuClearColor.g = g_skyColorGreen;
			MainMenu::g_menuClearColor.b = g_skyColorBlue;
		}

		// STUB: TOY2 0x00421340
		void Interactions() {}
	}
}

namespace Toy2
{
	namespace CreatureBehaviour
	{
		// FUNCTION: TOY2 0x00420AF0 [PROVISIONAL]
		void Dino(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			AlsToyBarn::g_dinoTintToggle = (AlsToyBarn::g_dinoTintToggle - 1) & 1;
			if (actor->actorPhase != AlsToyBarn::g_previousDinoPhase)
			{
				AlsToyBarn::g_previousDinoPhase = actor->actorPhase;
				AlsToyBarn::g_dinoTintTimer = 0x3C;
				actor->creatureRam->defenseMode = 4;
				AudioManager::PlaySoundEffect(0x79, &actor->pos);
			}

			if (actor->previousActorPhase != 0)
			{
				if (Sector::g_activeSectorIndex == 4)
				{
					Vector4I particlePosition;
					particlePosition.x = 0x14;
					particlePosition.y = -600;
					particlePosition.z = 0;

					int32_t particleAngle = ((uint16_t)actor->yawAngle + *g_randDatBufferPtr++ - 0x80) & 0xFFF;
					int32_t velocityX = Numerics::g_sinCosLUT[particleAngle] >> 3;
					int32_t velocityZ = Numerics::g_sinCosLUT[(particleAngle + 0x400) & 0xFFF] >> 3;
					Actor::ResolveBoneAttachmentPos(&particlePosition, actor, 1);
					AudioManager::PlaySoundEffect(0x78, &actor->pos);

					int32_t randomValue = *g_randDatBufferPtr;
					g_randDatBufferPtr += 2;
					Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnInstance(
						particlePosition.x, particlePosition.y, particlePosition.z, velocityX, randomValue + 0x200, velocityZ, 0, 0, randomValue - 0x80, 0x55);
					if (actor->previousActorPhase == 0x1E)
					{
						Camera::g_shakeTimer = 0x28;
						particle->renderFlags |= Nu3D::Particles::PARTICLE_INTERACTS_WITH_BUZZ;
					}
				}

				actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
				if (actor->previousActorPhase <= 0)
					actor->previousActorPhase = 0;
			}

			if (AlsToyBarn::g_dinoEncounterState > 1)
			{
				AlsToyBarn::g_dinoTintTimer -= Renderer::g_frameDelta;
				if (AlsToyBarn::g_dinoTintTimer < 0)
				{
					AlsToyBarn::g_dinoTintTimer = 0;
					if (AlsToyBarn::g_dinoEncounterState == AlsToyBarn::DINO_ENCOUNTER_ACTIVE)
						actor->creatureRam->defenseMode = 7;
					actor->useTint = 0;
				}
				else if (AlsToyBarn::g_dinoTintToggle != 0)
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

			if (actor->actorPhase < 10 && AlsToyBarn::g_dinoEncounterState == AlsToyBarn::DINO_ENCOUNTER_ACTIVE)
			{
				actor->movementData = g_dinoMovementData + 58;
				actor->creatureRam->defenseMode = 4;
				actor->actorFlags &= ~Actor::ACTOR_FLAG_DAMAGES_BUZZ;

				int32_t effectX = actor->pos.x + actor->collisionVolumes->offset.x;
				int32_t effectY = actor->pos.y + actor->collisionVolumes->offset.y;
				int32_t effectZ = actor->pos.z + actor->collisionVolumes->offset.z;
				for (int32_t effectCount = 5; effectCount != 0; effectCount--)
				{
					Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(effectX, effectY, effectZ, 0x23, 0xE);
					particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
				}

				Lighting::SpawnLight(effectX, effectY, effectZ, 0xF08000, 0x20, (int32_t)actor);
				AudioManager::PlaySoundEffect(-2, &actor->pos);
				AlsToyBarn::g_dinoEncounterState = AlsToyBarn::DINO_ENCOUNTER_DEFEATED;
				g_hudActorAnimationFrame = 0;
			}

			if (AlsToyBarn::g_dinoEncounterState >= AlsToyBarn::DINO_ENCOUNTER_DEFEATED && Sector::g_activeSectorIndex == 4)
			{
				int32_t yawAngle = actor->yawAngle;
				int32_t sideOffset = Numerics::g_sinCosLUT[(yawAngle - 0x400) & 0xFFF] >> 2;
				int32_t forwardOffset = Numerics::g_sinCosLUT[yawAngle & 0xFFF] >> 2;
				int32_t particleX;
				int32_t particleY;
				int32_t particleZ;
				if (AlsToyBarn::g_dinoTintToggle != 0)
				{
					particleX = actor->pos.x + sideOffset;
					particleY = actor->pos.y - 0x1D00;
					particleZ = actor->pos.z + forwardOffset;
				}
				else
				{
					particleX = actor->pos.x - sideOffset * 3;
					particleY = actor->pos.y - 0x400;
					particleZ = actor->pos.z - forwardOffset * 3;
				}

				if (g_framePulseOutputs.sevenTick != 0)
				{
					Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(particleX, particleY, particleZ, 0x11, 0xA);
					particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
					particle->width = 0x28;
					particle->height = 0x28;
				}

				if ((*g_randDatBufferPtr++ & 3) != 0 && (actor->animationFramePosition & (int32_t)0xFFFF0000) == 0xA0000)
				{
					Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(particleX, particleY, particleZ, 4, 4);
					particle->lifetime = (*g_randDatBufferPtr++ & 0xF) * 2 + 0x18;
				}
			}

			if (AlsToyBarn::g_dinoEncounterState == AlsToyBarn::DINO_ENCOUNTER_ACTIVE)
			{
				g_hudActorAnimationFrame = (actor->actorPhase - 9) * 0x36 / 0x14;
				if (Sector::g_activeSectorIndex == 4)
					HUD::g_slideTimers[HUD::SLIDE_BOSS_STATUS] = 0x5A;
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
	}
}
