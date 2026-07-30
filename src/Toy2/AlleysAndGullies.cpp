#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Actor.h"
#include "Toy2/Buzz.h"
#include "Toy2/Collision.h"
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
	extern uint8_t g_environmentTintRed;
	extern uint8_t g_environmentTintGreen;
	extern uint8_t g_environmentTintBlue;

	// FUNCTION: TOY2 0x0041E020 [PROVISIONAL]
	void InterpolatePathPoint(int32_t pathRecordType, int32_t pathPosition, Vector3I* position)
	{
		int32_t pointIndex = pathPosition / 0x1000;
		int32_t fraction = pathPosition & 0xFFF;

		position->x =
			(Levels::g_recordData[pathRecordType]->data[pointIndex + 1].x - Levels::g_recordData[pathRecordType]->data[pointIndex].x) * fraction / 0x1000
			+ Levels::g_recordData[pathRecordType]->data[pointIndex].x;
		position->y =
			(Levels::g_recordData[pathRecordType]->data[pointIndex + 1].y - Levels::g_recordData[pathRecordType]->data[pointIndex].y) * fraction / 0x1000
			+ Levels::g_recordData[pathRecordType]->data[pointIndex].y;
		position->z =
			(Levels::g_recordData[pathRecordType]->data[pointIndex + 1].z - Levels::g_recordData[pathRecordType]->data[pointIndex].z) * fraction / 0x1000
			+ Levels::g_recordData[pathRecordType]->data[pointIndex].z;
	}

	namespace AlleysAndGullies
	{
		struct MoveableObjectInitTable
		{
			MoveableObject::InitEntry entries[4];
			int16_t terminator;
		};

		enum ClownChallengeState
		{
			CLOWN_CHALLENGE_IDLE = 0,
			CLOWN_CHALLENGE_INTRO = 1,
			CLOWN_CHALLENGE_ACTIVE = 2,
			CLOWN_CHALLENGE_COMPLETE = 200,
		};

		struct HiddenCollectibleState
		{
			int32_t* verticalPosition;
			int32_t savedVerticalPosition;
		};

		// GLOBAL: TOY2 0x004F215C
		MoveableObjectInitTable g_moveableObjectInitTable = {
			{
				{ 14, 0, 12 },
				{ 0, 17, 6 },
				{ -2, 18, 7 },
				{ 51, 26, 21 },
			},
			-1,
		};
		// GLOBAL: TOY2 0x004F2178
		int16_t g_tokenLinkIds[] = { 0x59, 0x57, 0x56, 0x58, 0x55, 0 };

		// GLOBAL: TOY2 0x0052F8A8
		int32_t g_platform5PathPosition;
		// GLOBAL: TOY2 0x0052F8AC
		int32_t g_platform6PathPosition;

		// GLOBAL: TOY2 0x0052F8B0
		int32_t g_clownPhaseTimer;
		// GLOBAL: TOY2 0x0052F8B4
		int32_t g_platform8PathPosition;
		// GLOBAL: TOY2 0x0052F8B8
		int32_t g_previousClownPhase;
		// GLOBAL: TOY2 0x0052F8C0
		Vector3I g_firstFallingWoodVelocity;
		// GLOBAL: TOY2 0x0052F8D0
		int32_t g_bridgeSoundSpeed;
		// GLOBAL: TOY2 0x0052F8D4
		int32_t g_bridgeSwingAngle;
		// GLOBAL: TOY2 0x0052F8D8
		Vector3I g_secondFallingWoodVelocity;
		// GLOBAL: TOY2 0x0052F8E8
		int32_t g_clownChallengeState;
		// GLOBAL: TOY2 0x0052F8EC
		int32_t g_gateRotationAngle;
		// GLOBAL: TOY2 0x0052F8F0
		int32_t g_unusedState0;
		// GLOBAL: TOY2 0x0052F8F4
		int32_t g_platform12PathPosition;
		// GLOBAL: TOY2 0x0052F8F8
		int32_t g_bridgeEffectAngle;
		// GLOBAL: TOY2 0x0052F8FC
		int32_t g_clownTintFlashToggle;
		// GLOBAL: TOY2 0x0052F900
		int32_t g_platform11PathPosition;
		// GLOBAL: TOY2 0x0052F904
		int32_t g_bridgeRollAngle;
		// GLOBAL: TOY2 0x0052F908
		int32_t g_platform10PathPosition;
		// GLOBAL: TOY2 0x0052F90C
		int32_t g_bridgeSwingSpeed;
		// GLOBAL: TOY2 0x0052F910
		Vector3I g_firstFallingWoodPosition;
		// GLOBAL: TOY2 0x0052F91C
		int32_t g_firstFallingWoodState;
		// GLOBAL: TOY2 0x0052F920
		Vector3I g_secondFallingWoodPosition;
		// GLOBAL: TOY2 0x0052F92C
		int32_t g_secondFallingWoodState;
		// GLOBAL: TOY2 0x0052F930
		int32_t g_ambientParticleTimer;
		// GLOBAL: TOY2 0x0052F934
		int32_t g_fountainParticleTimer;
		// GLOBAL: TOY2 0x0052F938
		int32_t g_bridgeStateFlags;
		// GLOBAL: TOY2 0x0052F93C
		int32_t g_platform1TiltVelocity;
		// GLOBAL: TOY2 0x0052F940
		int32_t g_unusedPathPosition0;
		// GLOBAL: TOY2 0x0052F944
		int32_t g_platform9PathPosition;
		// GLOBAL: TOY2 0x0052F948
		int32_t g_unusedPathPosition1;
		// GLOBAL: TOY2 0x0052F94C
		int32_t g_unusedState1;
		// GLOBAL: TOY2 0x0052F950
		int32_t g_gateRotationSpeed;
		// GLOBAL: TOY2 0x0052F954
		HiddenCollectibleState g_hiddenCollectibles[5];
		// GLOBAL: TOY2 0x0052F97C
		int32_t g_platform7PathPosition;
		// GLOBAL: TOY2 0x0052F980
		int32_t g_ambientParticlePathPoint;
		// GLOBAL: TOY2 0x0052F984
		int32_t g_unusedState2;
		// GLOBAL: TOY2 0x0052F988
		int32_t g_platform2TiltVelocity;

		STATIC_ASSERT(sizeof(HiddenCollectibleState) == 0x8);
		STATIC_ASSERT(sizeof(MoveableObjectInitTable) == 0x1A);

		// FUNCTION: TOY2 0x0041E150 [PROVISIONAL]
		void UpdateMovingWood(
			int32_t* pathPosition, int32_t platformIndex, int32_t pathRecordType, int32_t primaryLinkIndex, int32_t secondaryLinkIndex, int32_t speed)
		{
			Vector3I direction;
			Vector3I platformOrigin;
			Vector3I targetPosition;

			if (*pathPosition > (Levels::g_recordData[pathRecordType]->recordCount - 2) * 0x1000)
			{
				*pathPosition = 0;
				InterpolatePathPoint(pathRecordType, 0, &targetPosition);
				Platform::SetOrigin(platformIndex, targetPosition.x << 5, targetPosition.y << 5, targetPosition.z << 5);
				Platform::GetOrigin(platformIndex, &platformOrigin);
			}
			else
			{
				InterpolatePathPoint(pathRecordType, *pathPosition, &targetPosition);
				Platform::GetOrigin(platformIndex, &platformOrigin);

				direction.x = targetPosition.x * 0x20 - platformOrigin.x;
				direction.y = targetPosition.y * 0x20 - platformOrigin.y;
				direction.z = targetPosition.z * 0x20 - platformOrigin.z;

				if (direction.x < 0x1000)
					*pathPosition += 0x1000;

				while (abs(direction.x) > 0x4000 || abs(direction.y) > 0x4000 || abs(direction.z) > 0x4000)
				{
					direction.x >>= 2;
					direction.y >>= 2;
					direction.z >>= 2;
				}

				Nu3D::Math::NormalizeToFixedPoint(&direction, &direction);
				Platform::SetVelocity(platformIndex,
					Renderer::g_frameDelta * direction.x * speed >> 10,
					Renderer::g_frameDelta * direction.y * speed >> 10,
					Renderer::g_frameDelta * direction.z * speed >> 10);
			}

			if (secondaryLinkIndex != 0)
			{
				Nu3D::Link::SetPositionRawAndCommit(secondaryLinkIndex, platformOrigin.x >> 5, platformOrigin.y >> 5, platformOrigin.z >> 5);
			}
			Nu3D::Link::SetPositionRawAndCommit(primaryLinkIndex, platformOrigin.x >> 5, platformOrigin.y >> 5, platformOrigin.z >> 5);
		}

		// FUNCTION: TOY2 0x0041E300 [TOOL]
		void InitHiddenCollectibles()
		{
			Levels::RecordData* pickupRecords = Levels::g_recordData[63];
			Collectables::PickupRecord* pickup = reinterpret_cast<Collectables::PickupRecord*>(pickupRecords + 1);
			for (int32_t pickupIndex = 0; pickupIndex < pickupRecords->recordCount; pickup++, pickupIndex++)
			{
				if (pickup->objectIndex == 97)
					g_hiddenCollectibles[0].verticalPosition = &pickup->position.y;
				if (pickup->objectIndex == 98)
					g_hiddenCollectibles[1].verticalPosition = &pickup->position.y;
				if (pickup->objectIndex == 99)
					g_hiddenCollectibles[2].verticalPosition = &pickup->position.y;
				if (pickup->objectIndex == 100)
					g_hiddenCollectibles[3].verticalPosition = &pickup->position.y;
				if (pickup->objectIndex == 101)
					g_hiddenCollectibles[4].verticalPosition = &pickup->position.y;
			}

			for (int32_t hiddenIndex = 0; hiddenIndex < 5; hiddenIndex++)
			{
				g_hiddenCollectibles[hiddenIndex].savedVerticalPosition = *g_hiddenCollectibles[hiddenIndex].verticalPosition;
				*g_hiddenCollectibles[hiddenIndex].verticalPosition = INT_MIN;
				Nu3D::Link::SetScaleFromFixedOffsets(hiddenIndex + 97, 0, 0, 0);
			}
		}

		// FUNCTION: TOY2 0x0041E390 [PROVISIONAL]
		void Init()
		{
			Weather::Init();
			MoveableObject::InitTable(g_moveableObjectInitTable.entries);
			Collectables::Init(g_tokenLinkIds, 0x41);
			Collectables::Activate(3, 1);
			InitHiddenCollectibles();

			g_platform5PathPosition = 0x3C000;
			g_platform7PathPosition = 0x3C000;
			g_unusedPathPosition1 = 0x3C000;
			g_unusedPathPosition0 = 0x3C000;
			g_platform10PathPosition = 0x3C000;
			g_platform12PathPosition = 0x3C000;
			g_platform8PathPosition = 0;
			g_platform6PathPosition = 0;
			g_unusedState2 = 0;
			g_unusedState1 = 0;
			g_platform9PathPosition = 0;
			g_platform11PathPosition = 0;

			Vector3I pathPosition;
			InterpolatePathPoint(0, 0, &pathPosition);
			Platform::SetOrigin(8, pathPosition.x << 5, pathPosition.y << 5, pathPosition.z << 5);
			Nu3D::Link::SetPositionRawAndCommit(33, pathPosition.x, pathPosition.y, pathPosition.z);
			Nu3D::Link::SetPositionRawAndCommit(29, pathPosition.x, pathPosition.y, pathPosition.z);

			InterpolatePathPoint(0, g_platform5PathPosition, &pathPosition);
			Platform::SetOrigin(5, pathPosition.x << 5, pathPosition.y << 5, pathPosition.z << 5);
			Nu3D::Link::SetPositionRawAndCommit(34, pathPosition.x, pathPosition.y, pathPosition.z);
			Nu3D::Link::SetPositionRawAndCommit(30, pathPosition.x, pathPosition.y, pathPosition.z);

			InterpolatePathPoint(5, g_platform6PathPosition, &pathPosition);
			Platform::SetOrigin(6, pathPosition.x << 5, pathPosition.y << 5, pathPosition.z << 5);
			Nu3D::Link::SetPositionRawAndCommit(35, pathPosition.x, pathPosition.y, pathPosition.z);
			Nu3D::Link::SetPositionRawAndCommit(31, pathPosition.x, pathPosition.y, pathPosition.z);

			InterpolatePathPoint(5, g_platform7PathPosition, &pathPosition);
			Platform::SetOrigin(7, pathPosition.x << 5, pathPosition.y << 5, pathPosition.z << 5);
			Nu3D::Link::SetPositionRawAndCommit(36, pathPosition.x, pathPosition.y, pathPosition.z);
			Nu3D::Link::SetPositionRawAndCommit(32, pathPosition.x, pathPosition.y, pathPosition.z);

			InterpolatePathPoint(2, g_platform9PathPosition, &pathPosition);
			Platform::SetOrigin(9, pathPosition.x << 5, pathPosition.y << 5, pathPosition.z << 5);
			Nu3D::Link::SetPositionRawAndCommit(21, pathPosition.x, pathPosition.y, pathPosition.z);

			InterpolatePathPoint(2, g_platform10PathPosition, &pathPosition);
			Platform::SetOrigin(10, pathPosition.x << 5, pathPosition.y << 5, pathPosition.z << 5);
			Nu3D::Link::SetPositionRawAndCommit(22, pathPosition.x, pathPosition.y, pathPosition.z);

			InterpolatePathPoint(3, g_platform11PathPosition, &pathPosition);
			Platform::SetOrigin(11, pathPosition.x << 5, pathPosition.y << 5, pathPosition.z << 5);
			Nu3D::Link::SetPositionRawAndCommit(23, pathPosition.x, pathPosition.y, pathPosition.z);

			InterpolatePathPoint(3, g_platform12PathPosition, &pathPosition);
			Platform::SetOrigin(12, pathPosition.x << 5, pathPosition.y << 5, pathPosition.z << 5);
			Nu3D::Link::SetPositionRawAndCommit(24, pathPosition.x, pathPosition.y, pathPosition.z);

			g_gateRotationAngle = 0;
			g_gateRotationSpeed = 0;
			Platform::DisableCollision(19);
			g_bridgeStateFlags = 0;
			g_platform1TiltVelocity = 0;
			g_platform2TiltVelocity = 0;
			g_bridgeRollAngle = 0;
			g_bridgeSwingAngle = 0;
			g_bridgeSwingSpeed = 0;
			g_bridgeSoundSpeed = 0;
			g_bridgeEffectAngle = 0;

			g_firstFallingWoodPosition.x = -0xE4433;
			g_firstFallingWoodPosition.y = -0xC5A4;
			g_firstFallingWoodPosition.z = -0xAB32D;
			g_firstFallingWoodState = 1;
			g_firstFallingWoodVelocity.x = 0;
			g_firstFallingWoodVelocity.y = 0;
			g_firstFallingWoodVelocity.z = 0;
			Nu3D::Link::SetPositionRawAndCommit(12, -0x7222, -0x62E, -0x559A);
			Nu3D::Link::SetScaleFromFixedOffsets(12, 0, 0, 0);
			Nu3D::Link::SetPositionRawAndCommit(13, g_firstFallingWoodPosition.x >> 7, g_firstFallingWoodPosition.y >> 7, g_firstFallingWoodPosition.z >> 7);
			Nu3D::Link::SetScaleFromFixedOffsets(13, 0, 0, 0);

			g_secondFallingWoodPosition.x = -0xE4433;
			g_secondFallingWoodPosition.y = -0xC5A4;
			g_secondFallingWoodPosition.z = -0xAB32D;
			g_secondFallingWoodState = 0;
			g_secondFallingWoodVelocity.x = 0;
			g_secondFallingWoodVelocity.y = 0;
			g_secondFallingWoodVelocity.z = 0;
			Nu3D::Link::SetPositionRawAndCommit(18, -0x7222, -0x62E, -0x559A);
			Nu3D::Link::SetScaleFromFixedOffsets(18, 0, 0, 0);
			Nu3D::Link::SetPositionRawAndCommit(19, g_secondFallingWoodPosition.x >> 7, g_secondFallingWoodPosition.y >> 7, g_secondFallingWoodPosition.z >> 7);
			Nu3D::Link::SetScaleFromFixedOffsets(19, 0, 0, 0);

			g_environmentSurfaceY = 0x10000;
			g_previousBuzzEnvironmentY = 0x10000;
			g_ambientParticleTimer = 0;
			g_fountainParticleTimer = 0;
			g_ambientParticlePathPoint = 0;
			g_clownChallengeState = CLOWN_CHALLENGE_IDLE;
			g_clownTintFlashToggle = 0;
			g_clownPhaseTimer = 0;
			HUD::g_challengeState = 0;
			AndysHouse::g_raceCheckpointPassCount = 0;
			g_environmentEffectType = 1;
			g_environmentTintBlue = 0x60;
			g_environmentTintGreen = 0x78;
			g_environmentTintRed = 0x80;
			g_previousClownPhase = Actor::g_creatureActors[3].actorPhase;
			g_unusedState0 = 200;
		}

		// STUB: TOY2 0x0041E880
		void Interactions() {}
	}
}

namespace Toy2
{
	namespace CreatureBehaviour
	{
		// FUNCTION: TOY2 0x0041DDB0 [MATCHED]
		void Clown(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			AlleysAndGullies::g_clownTintFlashToggle = (AlleysAndGullies::g_clownTintFlashToggle - 1) & 1;

			if (actor->actorPhase != AlleysAndGullies::g_previousClownPhase)
			{
				AlleysAndGullies::g_previousClownPhase = actor->actorPhase;
				AlleysAndGullies::g_clownPhaseTimer = 60;
				actor->creatureRam->defenseMode = 4;
			}

			if ((actor->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0)
			{
				actor->actorFlags &= ~Actor::ACTOR_FLAG_INTERACTION_REQUESTED;
				AudioManager::PlaySoundEffect(0xA1, &actor->pos);
			}

			if (AlleysAndGullies::g_clownChallengeState > AlleysAndGullies::CLOWN_CHALLENGE_INTRO)
			{
				AlleysAndGullies::g_clownPhaseTimer -= Renderer::g_frameDelta;
				if (AlleysAndGullies::g_clownPhaseTimer < 0)
				{
					AlleysAndGullies::g_clownPhaseTimer = 0;
					actor->creatureRam->defenseMode = 7;
					actor->useTint = 0;
				}
				else if (AlleysAndGullies::g_clownTintFlashToggle != 0)
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

			if (AlleysAndGullies::g_clownChallengeState == AlleysAndGullies::CLOWN_CHALLENGE_ACTIVE)
			{
				g_hudActorAnimationFrame = actor->actorPhase * 54 / 20;
				if (Sector::g_activeSectorIndex == 2)
				{
					HUD::g_slideTimers[HUD::SLIDE_BOSS_STATUS] = 90;
				}
			}
		}

		// FUNCTION: TOY2 0x0041DEC0 [MATCHED]
		void Ducks(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
			if (actor->actorPhase == 0x66 && actor->previousActorPhase <= 0)
			{
				actor->previousActorPhase = (*g_randDatBufferPtr++ & 0x7F) + 0x3C;
				AudioManager::PlaySoundEffect(0xA2, &actor->pos);
			}

			if ((actor->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0 && actor->actorPhase == 0x66)
			{
				g_levelObjectiveProgress++;
				Particles::SpawnCollectSparkle(actor->pos.x, actor->pos.y - 0x2000, actor->pos.z, 0x32);
				AudioManager::PlaySoundEffect(0xA2, &actor->pos);
				Actor::Kill(actor, 2);
			}
		}

		// FUNCTION: TOY2 0x0041DF70 [EFFECTIVE]
		void ZBoat(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
			if (actor->previousActorPhase < 0)
			{
				actor->previousActorPhase = 200;
				Vector4I particlePosition;
				particlePosition.x = 0;
				particlePosition.y = -500;
				particlePosition.z = -300;
				Actor::ResolveBoneAttachmentPos(&particlePosition, actor, 0);

				int32_t sine = Numerics::g_sinCosLUT[actor->yawAngle] >> 2;
				int32_t cosine = Numerics::g_sinCosLUT[(actor->yawAngle + 0x400) & 0xFFF] >> 2;
				Nu3D::Particles::SpawnInstance(particlePosition.x, particlePosition.y, particlePosition.z, sine, -0xC00, cosine, 0x80, 0, 0, 0x5C);
			}
		}
	}
}
