#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Actor.h"
#include "Toy2/Buzz.h"
#include "Toy2/Camera.h"
#include "Toy2/Collision.h"
#include "Toy2/Collectables.h"
#include "Toy2/HiddenCollectible.h"
#include "Toy2/Levels.h"
#include "Toy2/Lighting.h"
#include "Toy2/Particles.h"
#include "Toy2/Weather.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
#include "Random.h"
#include "Numerics.h"

#include <limits.h>
#include <stdlib.h>

namespace Toy2
{
	extern int32_t g_hudActorAnimationFrame;
	extern uint8_t g_environmentTintBlue;
	extern uint8_t g_environmentTintGreen;
	extern uint8_t g_environmentTintRed;

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
			CLOWN_CHALLENGE_DEFEATED = 3,
			CLOWN_CHALLENGE_COMPLETE = 200,
		};

		enum BridgeStateFlags
		{
			BRIDGE_STATE_ACTIVATED = 1,
			BRIDGE_STATE_SWING_REVERSED = 2,
		};

		enum
		{
			FALLING_WOOD_ANGLE_MASK = 0xFFF,
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
		// GLOBAL: TOY2 0x004F2184
		char* g_rotatingHintSubtitles[] = {
#include "AlleysAndGulliesHint.inc"
		};

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
		Collectables::HiddenCollectibleState g_hiddenCollectibles[5];
		// GLOBAL: TOY2 0x0052F97C
		int32_t g_platform7PathPosition;
		// GLOBAL: TOY2 0x0052F980
		int32_t g_ambientParticlePathPoint;
		// GLOBAL: TOY2 0x0052F984
		int32_t g_unusedState2;
		// GLOBAL: TOY2 0x0052F988
		int32_t g_platform2TiltVelocity;

		STATIC_ASSERT(sizeof(Collectables::HiddenCollectibleState) == 0x8);
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
			g_environmentTintRed = 0x60;
			g_environmentTintGreen = 0x78;
			g_environmentTintBlue = 0x80;
			g_previousClownPhase = Actor::g_creatureActors[3].actorPhase;
			g_unusedState0 = 200;
		}

		// FUNCTION: TOY2 0x0041E880 [PROVISIONAL]
		void Interactions()
		{
			if (g_environmentEffectType == 1 && Nu3D::Camera::g_targetTintFadeSpeed == 0
				&& ((g_environmentSurfaceY - Camera::g_renderCameraTransform.pos.y) >> 4) + 0x80 > 0x80)
			{
				AudioManager::PlaySoundEffect(0x6F, 0);
			}

			if (g_ledgeClimbPlatformIndex == 5 || g_ledgeClimbPlatformIndex == 8)
			{
				Platform::SetVelocity(5, 0, 0, 0);
				Platform::SetVelocity(8, 0, 0, 0);
			}
			else
			{
				UpdateMovingWood(&g_platform8PathPosition, 8, 0, 29, 33, 0x60);
				UpdateMovingWood(&g_platform5PathPosition, 5, 0, 30, 34, 0x60);
			}

			if (g_ledgeClimbPlatformIndex == 7 || g_ledgeClimbPlatformIndex == 6)
			{
				Platform::SetVelocity(7, 0, 0, 0);
				Platform::SetVelocity(6, 0, 0, 0);
			}
			else
			{
				UpdateMovingWood(&g_platform6PathPosition, 6, 5, 31, 35, 0x60);
				UpdateMovingWood(&g_platform7PathPosition, 7, 5, 32, 36, 0x60);
			}

			if (g_ledgeClimbPlatformIndex == 9 || g_ledgeClimbPlatformIndex == 10)
			{
				Platform::SetVelocity(9, 0, 0, 0);
				Platform::SetVelocity(10, 0, 0, 0);
			}
			else
			{
				UpdateMovingWood(&g_platform9PathPosition, 9, 2, 21, 0, 0xC0);
				UpdateMovingWood(&g_platform10PathPosition, 10, 2, 22, 0, 0xC0);
			}

			if (g_ledgeClimbPlatformIndex == 11 || g_ledgeClimbPlatformIndex == 12)
			{
				Platform::SetVelocity(11, 0, 0, 0);
				Platform::SetVelocity(12, 0, 0, 0);
			}
			else
			{
				UpdateMovingWood(&g_platform11PathPosition, 11, 3, 23, 0, 0xC0);
				UpdateMovingWood(&g_platform12PathPosition, 12, 3, 24, 0, 0xC0);
			}

			g_platform2TiltVelocity = Platform::StepTiltPhysics(2, 4, g_platform2TiltVelocity, 2, -0x1C0, 0x1C0, 1);
			g_platform1TiltVelocity = Platform::StepTiltPhysics(1, 6, g_platform1TiltVelocity, 2, -0x1C0, 0x1C0, 1);

			PosAndAngles transform;
			Nu3D::Link::GetRotation8Bit(4, &transform.pos);
			Nu3D::Link::SetRotationRelative8bit(5, transform.pos.x, transform.pos.y, transform.pos.z);
			Nu3D::Link::GetRotation8Bit(6, &transform.pos);
			Nu3D::Link::SetRotationRelative8bit(7, transform.pos.x, transform.pos.y, transform.pos.z);

			if (g_buzzActor.collisionFlags != 0 && (Platform::GetFlags(3) & 3) == Platform::PLATFORM_FLAG_BUZZ_CONTACT
				&& Platform::GetContactFaceNormal(3)->y < -0x3000)
			{
				Levels::DeactivateAmbientEmitter(1, 1);
				if (g_groundSlamTimer != 0)
				{
					g_groundSlamTimer = 0;
					Buzz::Launch(-0xC00, 2);
				}
				else
				{
					Buzz::Launch(-0x980, 2);
				}
				AudioManager::PlaySoundEffect(0x1C, &g_buzzActor.posAngles.pos);
			}

			if (g_buzzActor.specialAirState != 0)
			{
				Levels::g_recordData[61]->data[1].x = 0;
				Levels::g_recordData[61]->data[3].x = 0;
			}

			if (g_buzzActor.collisionFlags != 0 && (g_bridgeStateFlags & BRIDGE_STATE_ACTIVATED) == 0
				&& (Platform::GetFlags(4) & 3) == Platform::PLATFORM_FLAG_BUZZ_CONTACT && Platform::GetContactFaceNormal(4)->y < -0x3000
				&& g_groundSlamTimer != 0)
			{
				g_bridgeStateFlags |= BRIDGE_STATE_ACTIVATED;
				Platform::GetOrigin(4, &transform.pos);
				transform.pos.y += 0xE10;
				Platform::SetOrigin(4, transform.pos.x, transform.pos.y, transform.pos.z);
				Nu3D::Link::SetScaleFromFixedOffsets(16, 0x1000, 0x960, 0x1000);
				Nu3D::Link::SetScaleFromFixedOffsets(17, 0x1000, 0x960, 0x1000);
				Levels::DeactivateAmbientEmitter(0, 1);
			}

			if ((g_bridgeStateFlags & BRIDGE_STATE_ACTIVATED) != 0)
			{
				g_bridgeSoundSpeed += Renderer::g_frameDelta;
				if (g_bridgeSoundSpeed > 0x80)
				{
					g_bridgeSoundSpeed = 0x80;
				}
				AudioManager::g_dynamicSoundFrequencies[0] = ((int16_t)g_bridgeSoundSpeed + 0x20) * 0x20;
				Nu3D::Link::GetCurrentPosFixed(10, &transform.pos);
				AudioManager::PlaySoundEffect(0xA3, &transform.pos);

				g_bridgeRollAngle += Renderer::g_frameDelta * g_bridgeSoundSpeed;
				if ((g_bridgeStateFlags & BRIDGE_STATE_SWING_REVERSED) != 0)
				{
					if (g_bridgeSwingAngle > 0x180)
					{
						g_bridgeSwingSpeed += Renderer::g_frameDelta;
						if (g_bridgeSwingSpeed > 0x20)
						{
							g_bridgeSwingSpeed = 0x20;
						}
						g_bridgeSwingAngle -= g_bridgeSwingSpeed * Renderer::g_frameDelta / 8;
					}
					else
					{
						g_bridgeSwingSpeed -= Renderer::g_frameDelta;
						if (g_bridgeSwingSpeed < 0)
						{
							g_bridgeStateFlags &= ~BRIDGE_STATE_SWING_REVERSED;
							g_bridgeSwingSpeed = 0;
						}
						g_bridgeSwingAngle -= g_bridgeSwingSpeed * Renderer::g_frameDelta / 8;
					}
				}
				else
				{
					if (g_bridgeSwingAngle < 0x200)
					{
						g_bridgeSwingSpeed += Renderer::g_frameDelta;
						if (g_bridgeSwingSpeed > 0x20)
						{
							g_bridgeSwingSpeed = 0x20;
						}
						g_bridgeSwingAngle += g_bridgeSwingSpeed * Renderer::g_frameDelta / 8;
					}
					else
					{
						g_bridgeSwingSpeed -= Renderer::g_frameDelta;
						if (g_bridgeSwingSpeed < 0)
						{
							g_bridgeStateFlags |= BRIDGE_STATE_SWING_REVERSED;
							g_bridgeSwingSpeed = 0;
						}
						g_bridgeSwingAngle += g_bridgeSwingSpeed * Renderer::g_frameDelta / 8;
					}
				}

				Nu3D::Link::SetRotationRelative8bit(10, 0, -g_bridgeSwingAngle, 0);
				Nu3D::Link::SetRotationRelative8bit(11, 0, -g_bridgeSwingAngle, 0);
				Nu3D::Link::SetRotationRelative8bit(20, 0, -g_bridgeSwingAngle, g_bridgeRollAngle * -2);
				g_bridgeEffectAngle = (g_bridgeEffectAngle + Renderer::g_frameDelta) & 0x1FF;
				if (g_bridgeEffectAngle > 0x20 && g_bridgeEffectAngle < 0x28 && g_firstFallingWoodState == 0)
				{
					g_firstFallingWoodState = 1;
					AudioManager::PlaySoundEffect(0xA0, &g_firstFallingWoodPosition);
				}
				if (g_bridgeEffectAngle > 0x120 && g_bridgeEffectAngle < 0x128 && g_secondFallingWoodState == 0)
				{
					g_secondFallingWoodState = 1;
					AudioManager::PlaySoundEffect(0xA0, &g_secondFallingWoodPosition);
				}
			}

			if (g_firstFallingWoodState > 0)
			{
				int32_t scale = Numerics::g_sinCosLUT[(g_firstFallingWoodState * 3) & FALLING_WOOD_ANGLE_MASK] / 0x100 + 0x1000;
				g_firstFallingWoodState += Renderer::g_frameDelta * 0x20;
				if (g_firstFallingWoodState < 0x1000)
				{
					Nu3D::Link::SetScaleFromFixedOffsets(
						12, g_firstFallingWoodState * scale / 0x1000, g_firstFallingWoodState, g_firstFallingWoodState * scale / 0x1000);
					Nu3D::Link::SetScaleFromFixedOffsets(
						13, g_firstFallingWoodState * scale / 0x1000, g_firstFallingWoodState, g_firstFallingWoodState * scale / 0x1000);
					Nu3D::Link::SetRotationRelative8bit(12, 0, Numerics::g_sinCosLUT[g_firstFallingWoodState & FALLING_WOOD_ANGLE_MASK] / 0x80, 0);
					Nu3D::Link::SetRotationRelative8bit(13, 0, Numerics::g_sinCosLUT[g_firstFallingWoodState & FALLING_WOOD_ANGLE_MASK] / 0x80, 0);
					Levels::g_recordData[61]->data[1].x = 3;
				}
				else
				{
					if (g_firstFallingWoodPosition.y > -0xECB4)
					{
						Levels::g_recordData[61]->data[1].x = 3;
					}
					Nu3D::Link::SetScaleFromFixedOffsets(12, scale, 0x1000, scale);
					Nu3D::Link::SetScaleFromFixedOffsets(13, scale, 0x1000, scale);
					Nu3D::Link::SetRotationRelative8bit(12, 0, Numerics::g_sinCosLUT[g_firstFallingWoodState & FALLING_WOOD_ANGLE_MASK] / 0x80, 0);
					Nu3D::Link::SetRotationRelative8bit(13, 0, Numerics::g_sinCosLUT[g_firstFallingWoodState & FALLING_WOOD_ANGLE_MASK] / 0x80, 0);

					if (g_firstFallingWoodPosition.y > -0x1D714)
					{
						g_firstFallingWoodVelocity.y -= Renderer::g_frameDelta * 0x10;
						if (g_firstFallingWoodVelocity.y < -0x200)
						{
							g_firstFallingWoodVelocity.y = -0x200;
						}
					}
					else
					{
						g_firstFallingWoodVelocity.y += Renderer::g_frameDelta * 0x10;
						if (g_firstFallingWoodVelocity.y > 0x200)
						{
							g_firstFallingWoodVelocity.y = 0x200;
						}
					}
					if (abs(g_firstFallingWoodPosition.y + 0x1CCF0) < 20000 && (g_bridgeStateFlags & BRIDGE_STATE_ACTIVATED) != 0)
					{
						g_firstFallingWoodVelocity.x -= Renderer::g_frameDelta * 0x10;
						if (g_firstFallingWoodVelocity.x < -700)
						{
							g_firstFallingWoodVelocity.x = -700;
						}
					}
					g_firstFallingWoodPosition.y += g_firstFallingWoodVelocity.y * Renderer::g_frameDelta;
					g_firstFallingWoodPosition.x += g_firstFallingWoodVelocity.x * Renderer::g_frameDelta;
					g_firstFallingWoodPosition.z += g_firstFallingWoodVelocity.z * Renderer::g_frameDelta;
					if (g_firstFallingWoodPosition.x < -0x108D51)
					{
						Nu3D::Link::SetScaleFromFixedOffsets(12, 0x157C, 0x157C, 0x157C);
						Nu3D::Link::SetScaleFromFixedOffsets(13, 0x157C, 0x157C, 0x157C);
						g_firstFallingWoodState = -1;
						AudioManager::PlaySoundEffect(0xB, &g_firstFallingWoodPosition);
					}
					Nu3D::Link::SetPositionRawAndCommit(
						12, g_firstFallingWoodPosition.x >> 5, g_firstFallingWoodPosition.y >> 5, g_firstFallingWoodPosition.z >> 5);
					Nu3D::Link::SetPositionRawAndCommit(
						13, g_firstFallingWoodPosition.x >> 7, g_firstFallingWoodPosition.y >> 7, g_firstFallingWoodPosition.z >> 7);
				}

				if (g_poleClimbState > 0 && g_poleRecordOffset == 0)
				{
					g_buzzActor.posAngles.pos.y += Levels::g_recordData[61]->data[1].y - Levels::g_recordData[61]->data[0].y + g_firstFallingWoodPosition.y;
					g_buzzActor.posAngles.pos.x = g_firstFallingWoodPosition.x;
					g_buzzActor.posAngles.pos.z = g_firstFallingWoodPosition.z;
					Levels::g_recordData[61]->data[1].x = 3;
				}
				Levels::g_recordData[61]->data[0].x = g_firstFallingWoodPosition.x;
				Levels::g_recordData[61]->data[0].y = Levels::g_recordData[61]->data[1].y + g_firstFallingWoodPosition.y;
				Levels::g_recordData[61]->data[0].z = g_firstFallingWoodPosition.z;
			}
			else if (g_firstFallingWoodState < 0)
			{
				g_firstFallingWoodPosition.x = -0xE4433;
				g_firstFallingWoodPosition.y = -0xC5A4;
				g_firstFallingWoodPosition.z = -0xAB32D;
				g_firstFallingWoodState = 0;
				g_firstFallingWoodVelocity.x = 0;
				g_firstFallingWoodVelocity.y = 0;
				g_firstFallingWoodVelocity.z = 0;
				Nu3D::Link::SetScaleFromFixedOffsets(12, 0, 0, 0);
				Nu3D::Link::SetScaleFromFixedOffsets(13, 0, 0, 0);
				Nu3D::Link::SetPositionRawAndCommit(
					12, g_firstFallingWoodPosition.x >> 5, g_firstFallingWoodPosition.y >> 5, g_firstFallingWoodPosition.z >> 5);
				Nu3D::Link::SetPositionRawAndCommit(
					13, g_firstFallingWoodPosition.x >> 7, g_firstFallingWoodPosition.y >> 7, g_firstFallingWoodPosition.z >> 7);
				Levels::g_recordData[61]->data[1].x = 3;
				if (g_poleClimbState > 0 && g_poleRecordOffset == 0)
				{
					g_poleClimbState = 0;
					g_buzzActor.animationState = 2;
					g_buzzActor.collisionState = 0;
					g_buzzActor.collisionFlagsHigh = 0;
					g_buzzActor.specialAirState = 0;
				}
			}
			else
			{
				Levels::g_recordData[61]->data[1].x = 3;
			}

			if (g_secondFallingWoodState > 0)
			{
				int32_t scale = Numerics::g_sinCosLUT[(g_secondFallingWoodState * 3) & FALLING_WOOD_ANGLE_MASK] / 0x100 + 0x1000;
				g_secondFallingWoodState += Renderer::g_frameDelta * 0x20;
				if (g_secondFallingWoodState < 0x1000)
				{
					Nu3D::Link::SetScaleFromFixedOffsets(
						18, g_secondFallingWoodState * scale / 0x1000, g_secondFallingWoodState, g_secondFallingWoodState * scale / 0x1000);
					Nu3D::Link::SetScaleFromFixedOffsets(
						19, g_secondFallingWoodState * scale / 0x1000, g_secondFallingWoodState, g_secondFallingWoodState * scale / 0x1000);
					Nu3D::Link::SetRotationRelative8bit(18, 0, Numerics::g_sinCosLUT[g_secondFallingWoodState & FALLING_WOOD_ANGLE_MASK] / 0x80, 0);
					Nu3D::Link::SetRotationRelative8bit(19, 0, Numerics::g_sinCosLUT[g_secondFallingWoodState & FALLING_WOOD_ANGLE_MASK] / 0x80, 0);
					Levels::g_recordData[61]->data[3].x = 3;
				}
				else
				{
					if (g_secondFallingWoodPosition.y > -0xECB4)
					{
						Levels::g_recordData[61]->data[3].x = 3;
					}
					Nu3D::Link::SetScaleFromFixedOffsets(18, scale, 0x1000, scale);
					Nu3D::Link::SetScaleFromFixedOffsets(19, scale, 0x1000, scale);
					Nu3D::Link::SetRotationRelative8bit(18, 0, Numerics::g_sinCosLUT[g_secondFallingWoodState & FALLING_WOOD_ANGLE_MASK] / 0x80, 0);
					Nu3D::Link::SetRotationRelative8bit(19, 0, Numerics::g_sinCosLUT[g_secondFallingWoodState & FALLING_WOOD_ANGLE_MASK] / 0x80, 0);

					if (g_secondFallingWoodPosition.y > -0x1D714)
					{
						g_secondFallingWoodVelocity.y -= Renderer::g_frameDelta * 0x10;
						if (g_secondFallingWoodVelocity.y < -0x200)
						{
							g_secondFallingWoodVelocity.y = -0x200;
						}
					}
					else
					{
						g_secondFallingWoodVelocity.y += Renderer::g_frameDelta * 0x10;
						if (g_secondFallingWoodVelocity.y > 0x200)
						{
							g_secondFallingWoodVelocity.y = 0x200;
						}
					}
					if (abs(g_secondFallingWoodPosition.y + 0x1CCF0) < 20000 && (g_bridgeStateFlags & BRIDGE_STATE_ACTIVATED) != 0)
					{
						g_secondFallingWoodVelocity.x -= Renderer::g_frameDelta * 0x10;
						if (g_secondFallingWoodVelocity.x < -700)
						{
							g_secondFallingWoodVelocity.x = -700;
						}
					}
					g_secondFallingWoodPosition.y += g_secondFallingWoodVelocity.y * Renderer::g_frameDelta;
					g_secondFallingWoodPosition.x += g_secondFallingWoodVelocity.x * Renderer::g_frameDelta;
					g_secondFallingWoodPosition.z += g_secondFallingWoodVelocity.z * Renderer::g_frameDelta;
					if (g_secondFallingWoodPosition.x < -0x108D51)
					{
						Nu3D::Link::SetScaleFromFixedOffsets(18, 0x157C, 0x157C, 0x157C);
						Nu3D::Link::SetScaleFromFixedOffsets(19, 0x157C, 0x157C, 0x157C);
						g_secondFallingWoodState = -1;
						AudioManager::PlaySoundEffect(0xB, &g_firstFallingWoodPosition);
					}
					Nu3D::Link::SetPositionRawAndCommit(
						18, g_secondFallingWoodPosition.x >> 5, g_secondFallingWoodPosition.y >> 5, g_secondFallingWoodPosition.z >> 5);
					Nu3D::Link::SetPositionRawAndCommit(
						19, g_secondFallingWoodPosition.x >> 7, g_secondFallingWoodPosition.y >> 7, g_secondFallingWoodPosition.z >> 7);
				}

				if (g_poleClimbState > 0 && g_poleRecordOffset == 6)
				{
					g_buzzActor.posAngles.pos.y += Levels::g_recordData[61]->data[3].y - Levels::g_recordData[61]->data[2].y + g_secondFallingWoodPosition.y;
					g_buzzActor.posAngles.pos.x = g_secondFallingWoodPosition.x;
					g_buzzActor.posAngles.pos.z = g_secondFallingWoodPosition.z;
					Levels::g_recordData[61]->data[3].x = 3;
				}
				Levels::g_recordData[61]->data[2].x = g_secondFallingWoodPosition.x;
				Levels::g_recordData[61]->data[2].y = Levels::g_recordData[61]->data[3].y + g_secondFallingWoodPosition.y;
				Levels::g_recordData[61]->data[2].z = g_secondFallingWoodPosition.z;
			}
			else if (g_secondFallingWoodState < 0)
			{
				g_secondFallingWoodPosition.x = -0xE4433;
				g_secondFallingWoodPosition.y = -0xC5A4;
				g_secondFallingWoodPosition.z = -0xAB32D;
				g_secondFallingWoodState = 0;
				g_secondFallingWoodVelocity.x = 0;
				g_secondFallingWoodVelocity.y = 0;
				g_secondFallingWoodVelocity.z = 0;
				Nu3D::Link::SetScaleFromFixedOffsets(18, 0, 0, 0);
				Nu3D::Link::SetScaleFromFixedOffsets(19, 0, 0, 0);
				Nu3D::Link::SetPositionRawAndCommit(
					18, g_secondFallingWoodPosition.x >> 5, g_secondFallingWoodPosition.y >> 5, g_secondFallingWoodPosition.z >> 5);
				Nu3D::Link::SetPositionRawAndCommit(
					19, g_secondFallingWoodPosition.x >> 7, g_secondFallingWoodPosition.y >> 7, g_secondFallingWoodPosition.z >> 7);
				Levels::g_recordData[61]->data[3].x = 3;
				if (g_poleClimbState > 0 && g_poleRecordOffset == 6)
				{
					g_poleClimbState = 0;
					g_buzzActor.animationState = 2;
					g_buzzActor.collisionState = 0;
					g_buzzActor.collisionFlagsHigh = 0;
					g_buzzActor.specialAirState = 0;
				}
			}
			else
			{
				Levels::g_recordData[61]->data[3].x = 3;
			}

			Nu3D::Link::GetCurrentPosFixed(14, &transform.pos);
			Nu3D::Link::SetPositionRawAndCommit(15, transform.pos.x >> 7, transform.pos.y >> 7, transform.pos.z >> 7);
			g_environmentSurfaceY = g_buzzActor.posAngles.pos.z >= 0xF32A0 ? 0x70000 : 0x10000;

			g_ambientParticleTimer -= Renderer::g_frameDelta;
			if (g_ambientParticleTimer < 0)
			{
				g_ambientParticlePathPoint += 2;
				Levels::RecordData* ambientPath = Levels::g_recordData[62];
				if (g_ambientParticlePathPoint >= ambientPath->recordCount)
				{
					g_ambientParticlePathPoint = 0;
				}
				Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnInstance(ambientPath->data[g_ambientParticlePathPoint].x,
					ambientPath->data[g_ambientParticlePathPoint].y,
					ambientPath->data[g_ambientParticlePathPoint].z,
					(ambientPath->data[g_ambientParticlePathPoint + 1].x - ambientPath->data[g_ambientParticlePathPoint].x) >> 6,
					(ambientPath->data[g_ambientParticlePathPoint + 1].y - ambientPath->data[g_ambientParticlePathPoint].y) >> 6,
					(ambientPath->data[g_ambientParticlePathPoint + 1].z - ambientPath->data[g_ambientParticlePathPoint].z) >> 6,
					0,
					0,
					0,
					0x4C);
				particle->lifetime = 0x80;
				if (particle != &Nu3D::Particles::g_rejectedParticleInstance)
				{
					AudioManager::PlaySoundEffect(0xA4, &particle->pos);
				}
				g_ambientParticleTimer = 0x14;
			}

			Renderer::BlitTextureByIndexOffset(5, 0xC0, 0x80, 0x40, 0x40, 0, g_framePulsePhases.sixtyFourTick, 0, 0x40);
			if (Sector::g_activeSectorIndex == 2 && g_framePulseOutputs.thirtyTwoTick != 0)
			{
				g_fountainParticleTimer--;
				if (g_fountainParticleTimer < 0)
				{
					g_fountainParticleTimer = 10;
				}
				else if (g_fountainParticleTimer < 5)
				{
					Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(0x29630, -0xA5660, -0x2EB06, 0x5D, 0xE);
					if (particle != &Nu3D::Particles::g_rejectedParticleInstance)
					{
						AudioManager::PlaySoundEffect(0xA4, &particle->pos);
					}
				}
			}

			if (g_clownChallengeState == CLOWN_CHALLENGE_IDLE && g_buzzActor.collisionFlags != 0 && Sector::g_activeSectorIndex == 2)
			{
				g_clownChallengeState = CLOWN_CHALLENGE_INTRO;
				Dialogue::Begin(3, 0xD, "ha ha ha ha ... defeat the ^ clown top^ boss to get a pizza planet ^token^!", -1, 0, -1);
			}
			if (g_clownChallengeState == CLOWN_CHALLENGE_INTRO && Nu3D::Camera::g_viewHistoryInitialized == 0)
			{
				Actor::Toy2Actor* clown = &Actor::g_creatureActors[3];
				uint16_t* challengeMovementData = CreatureBehaviour::g_clownMovementData + 14;
				g_levelInteractionTimer = 0xB4;
				g_clownChallengeState = CLOWN_CHALLENGE_ACTIVE;
				clown->movementData = challengeMovementData;
				clown->movementCommandTimer = 0;
				clown->creatureRam->initialFacingAngle = 0;
			}
			if (g_clownChallengeState == CLOWN_CHALLENGE_ACTIVE)
			{
				if (Actor::g_creatureActors[3].creatureId == 0)
				{
					int32_t defeatedState = Renderer::g_frameDelta + CLOWN_CHALLENGE_DEFEATED;
					g_hudActorAnimationFrame = 0;
					g_clownChallengeState = defeatedState;
				}
			}
			else if (g_clownChallengeState >= CLOWN_CHALLENGE_DEFEATED)
			{
				if (g_clownChallengeState < 0x78)
				{
					g_clownChallengeState += Renderer::g_frameDelta;
				}
				else if (g_clownChallengeState != CLOWN_CHALLENGE_COMPLETE)
				{
					Collectables::Activate(4, 0);
					g_clownChallengeState = CLOWN_CHALLENGE_COMPLETE;
				}
			}

			if (g_gateRotationAngle == 0)
			{
				if (MoveableObject::g_objects[2].pathProgress != 0)
				{
					g_gateRotationAngle = 1;
					Platform::DisableCollision(18);
					Collision::MarkPlatformAsMoving(19);
					g_buzzActor.velocity.lateral = 0;
					g_buzzActor.velocity.forward = 0;
					g_forcedFacingActive = 0;
				}
				MoveableObject::g_objects[1].pathProgress = 0;
			}
			else
			{
				if (g_forcedFacingActive == 3)
				{
					g_forcedFacingActive = 0;
				}
				if (g_gateRotationAngle < 0x400)
				{
					g_gateRotationAngle += g_gateRotationSpeed * Renderer::g_frameDelta;
					if (g_gateRotationAngle >= 0x400)
					{
						g_gateRotationAngle = 0x400;
						transform.pos.x = 0x52334;
						transform.pos.y = -0xC56;
						transform.pos.z = 0x5A016;
						for (int32_t particleIndex = 0; particleIndex < 20; particleIndex++)
						{
							Nu3D::Particles::ParticleInstance* particle =
								Nu3D::Particles::SpawnFromPreset(transform.pos.x, transform.pos.y, transform.pos.z, 0xD, 0xE);
							particle->updateParam = (uint8_t)(((*g_randDatBufferPtr++ & 3) + 3) * 2);
							particle->lifetime = (int16_t)(particle->updateParam * 5);
						}
						AudioManager::PlaySoundEffect(0x34, &transform.pos);
						AudioManager::PlaySoundEffect(0x3A, &transform.pos);
						Nu3D::Link::SetScaleFromFixedOffsets(41, 0, 0, 0);
						Nu3D::Link::SetScaleFromFixedOffsets(42, 0, 0, 0);
					}
					Nu3D::Link::SetRotationRelative8bit(2, 0, 0, g_gateRotationAngle);
					Nu3D::Link::SetRotationRelative8bit(3, 0, 0, g_gateRotationAngle);
					g_gateRotationSpeed += Renderer::g_frameDelta;
				}
			}

			Nu3D::Link::GetCurrentPosFixed(0, &transform.pos);
			Nu3D::Link::SetPositionRawAndCommit(1, transform.pos.x >> 7, transform.pos.y >> 7, transform.pos.z >> 7);
			Nu3D::Link::GetCurrentPosFixed(51, &transform.pos);
			Nu3D::Link::SetPositionRawAndCommit(52, transform.pos.x >> 7, transform.pos.y >> 7, transform.pos.z >> 7);

			Actor::CollectQuestReward(20, 15, -1, 0, 0);
			Actor::RotatingHint(34, 20, g_rotatingHintSubtitles);

			Actor::Toy2Actor* duck = &Actor::g_creatureActors[19];
			if ((duck->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0)
			{
				duck->actorFlags &= ~Actor::ACTOR_FLAG_INTERACTION_REQUESTED;
				if (g_levelObjectiveProgress >= 0)
				{
					if (g_levelObjectiveProgress == 5)
					{
						Dialogue::Begin(19, 16, "thanks for finding my ^baby ducklings^ buzz! here is a pizza planet ^token^!", -1, 0, 1);
						g_levelObjectiveProgress = -1;
					}
					else
					{
						Dialogue::Begin(19,
							16,
							"hi buzz! i have lost all my ^baby ducklings^! if you can find all ^five^ of them and come and see me, i will give you a pizza "
							"planet ^token^.",
							-1,
							0,
							-1);
					}
				}
			}

			Actor::PlayPeriodicHintSound(18, 0xB5);
			Actor::Toy2Actor* slinky = &Actor::g_creatureActors[18];
			if ((slinky->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0)
			{
				slinky->actorFlags &= ~Actor::ACTOR_FLAG_INTERACTION_REQUESTED;
				if (HUD::g_challengeState == HUD::CHALLENGE_STATE_INACTIVE)
				{
					AudioManager::Preset::PlayOneShotSound2(0xB6, slinky);
					Dialogue::Begin(
						18, 14, "howdy buzz! if you can bring me ^five^ bones before you run out of time, i will give you a pizza planet ^token^.", -1, 0, -1);
					g_specialPickupCount = 0;
					HUD::g_challengeState = HUD::CHALLENGE_STATE_WAITING_FOR_CAMERA;
					for (int32_t hiddenIndex = 0; hiddenIndex < 5; hiddenIndex++)
					{
						*g_hiddenCollectibles[hiddenIndex].verticalPosition = g_hiddenCollectibles[hiddenIndex].savedVerticalPosition;
						Nu3D::Link::SetScaleFromFixedOffsets(hiddenIndex + 97, 0x1000, 0x1000, 0x1000);
					}
				}
				else if (g_specialPickupCount < 5)
				{
					Dialogue::Begin(18, 14, "quick! you need to find more bones!", -1, 0, -1);
				}
				else if (HUD::g_challengeState != HUD::CHALLENGE_STATE_COMPLETE)
				{
					AudioManager::Preset::PlayOneShotSound2(0xB7, slinky);
					Dialogue::Begin(18, 14, "well done! you found all of my bones! here is your pizza planet ^token^!", -1, 0, 2);
					HUD::g_challengeState = HUD::CHALLENGE_STATE_COMPLETE;
				}
			}

			if (HUD::g_challengeState == HUD::CHALLENGE_STATE_WAITING_FOR_CAMERA && Nu3D::Camera::g_viewHistoryInitialized == 0)
			{
				HUD::g_challengeState = HUD::CHALLENGE_STATE_ACTIVE;
				AndysHouse::g_raceCheckpointPassCount = 0x82;
			}
			if (HUD::g_challengeState == HUD::CHALLENGE_STATE_ACTIVE)
			{
				if (g_framePulseOutputs.sixtyFourTick != 0)
				{
					AndysHouse::g_raceCheckpointPassCount--;
				}
				if (AndysHouse::g_raceCheckpointPassCount < 100)
				{
					AndysHouse::g_raceCheckpointPassCount = 100;
					HUD::g_challengeState = HUD::CHALLENGE_STATE_INACTIVE;
					for (int32_t hiddenIndex = 0; hiddenIndex < 5; hiddenIndex++)
					{
						*g_hiddenCollectibles[hiddenIndex].verticalPosition = INT_MIN;
						Nu3D::Link::SetScaleFromFixedOffsets(hiddenIndex + 97, 0, 0, 0);
					}
				}
			}

			if (g_environmentEffectType == 1)
			{
				if (Camera::g_renderCameraTransform.pos.y > g_environmentSurfaceY)
				{
					Nu3D::Link::SetScaleFromFixedOffsets(39, 0, 0, 0);
					Nu3D::Link::SetScaleFromFixedOffsets(40, 0, 0, 0);
					Nu3D::Link::SetScaleFromFixedOffsets(44, 0, 0, 0);
					Nu3D::Link::SetScaleFromFixedOffsets(49, 0, 0, 0);
					Nu3D::Link::SetScaleFromFixedOffsets(50, 0, 0, 0);
				}
				else
				{
					Nu3D::Link::SetScaleFromFixedOffsets(39, 0x1000, 0x1000, 0x1000);
					Nu3D::Link::SetScaleFromFixedOffsets(40, 0x1000, 0x1000, 0x1000);
					Nu3D::Link::SetScaleFromFixedOffsets(44, 0x1000, 0x1000, 0x1000);
					Nu3D::Link::SetScaleFromFixedOffsets(49, 0x1000, 0x1000, 0x1000);
					Nu3D::Link::SetScaleFromFixedOffsets(50, 0x1000, 0x1000, 0x1000);
				}
			}

			Weather::StepPrecipitation(0x100, 0x1000, 0x34);

			Levels::RecordData* flareRecords = Levels::g_recordData[17];
			int32_t nearestDistanceSquared = INT_MAX;
			int32_t nearestFlareIndex;
			for (int32_t flareIndex = 0; flareIndex < flareRecords->recordCount; flareIndex++)
			{
				int32_t cameraOffsetZ = (Camera::g_renderCameraTransform.pos.z - flareRecords->data[flareIndex].z * 0x20) >> 8;
				int32_t cameraOffsetY = (Camera::g_renderCameraTransform.pos.y - flareRecords->data[flareIndex].y * 0x20) >> 8;
				int32_t cameraOffsetX = (Camera::g_renderCameraTransform.pos.x - flareRecords->data[flareIndex].x * 0x20) >> 8;
				if (cameraOffsetZ * cameraOffsetZ + cameraOffsetY * cameraOffsetY + cameraOffsetX * cameraOffsetX < 1000000)
				{
					Renderer::LensFlare::RegisterLight(flareRecords->data[flareIndex].x * 0x20,
						flareRecords->data[flareIndex].y * 0x20,
						flareRecords->data[flareIndex].z * 0x20,
						0x40,
						0x30,
						0x20,
						0x80);
					int32_t buzzOffsetZ = (g_buzzActor.posAngles.pos.z - flareRecords->data[flareIndex].z * 0x20) >> 8;
					int32_t buzzOffsetY = (g_buzzActor.posAngles.pos.y - flareRecords->data[flareIndex].y * 0x20 - 0x2000) >> 8;
					int32_t buzzOffsetX = (g_buzzActor.posAngles.pos.x - flareRecords->data[flareIndex].x * 0x20) >> 8;
					int32_t distanceSquared = buzzOffsetX * buzzOffsetX + buzzOffsetZ * buzzOffsetZ + buzzOffsetY * buzzOffsetY;
					if (distanceSquared < nearestDistanceSquared)
					{
						nearestDistanceSquared = distanceSquared;
						nearestFlareIndex = flareIndex;
					}
				}
			}

			if (nearestDistanceSquared < 0x10000)
			{
				Vector3I* nearestFlare = &flareRecords->data[nearestFlareIndex];
				Lighting::DynamicLight* light = &Lighting::g_lightingState.dynamicLights[1];
				light->position.x = nearestFlare->x << 5;
				light->position.y = nearestFlare->y << 5;
				light->colour.r = 0x80;
				light->position.z = nearestFlare->z << 5;
				light->colour.g = 0x60;
				light->colour.b = 0x40;
				light->lifetime = 1;
				light->sourceId = reinterpret_cast<int32_t>(nearestFlare);
			}
			else
			{
				Lighting::g_lightingState.dynamicLights[1].lifetime = 0;
			}

			if (g_framePulseOutputs.eightTick != 0 && (g_environmentEffectType != 1 || g_environmentSurfaceY - Camera::g_renderCameraTransform.pos.y >= 0))
			{
				transform.pos.x = (*g_randDatBufferPtr++ - 0x80) * 0x100 + Camera::g_renderCameraTransform.pos.x
					+ Numerics::g_sinCosLUT[Camera::g_renderCameraTransform.rotation.vector.y] * 3;
				transform.pos.y = Camera::g_renderCameraTransform.pos.y - 0x8000;
				transform.pos.z = (*g_randDatBufferPtr++ - 0x80) * 0x100 + Camera::g_renderCameraTransform.pos.z
					+ Numerics::g_sinCosLUT[(Camera::g_renderCameraTransform.rotation.vector.y + 0x400) & 0xFFF] * 3;
				int32_t groundHeight = Nu3D::Collision::GetGroundHeight(&transform, 0);
				if (groundHeight != INT_MIN)
				{
					if (groundHeight > g_environmentSurfaceY)
					{
						groundHeight = g_environmentSurfaceY;
					}
					if (groundHeight > Camera::g_renderCameraTransform.pos.y)
					{
						Nu3D::Particles::SpawnFromPreset(transform.pos.x, groundHeight, transform.pos.z, 0x1B, 2);
					}
				}
			}

			PlayLevelMusic();
		}
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
