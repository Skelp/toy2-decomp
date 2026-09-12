#include "Toy2/Buzz.h"
#include "Toy2/BuzzInternal.h"
#include "Toy2/Animation.h"
#include "Toy2/Actor.h"
#include "Toy2/Camera.h"
#include "Toy2/Collision.h"
#include "Toy2/CollisionInternal.h"
#include "Toy2/Collectables.h"
#include "Toy2/Levels.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/PoleRecord.h"
#include "Toy2/Toy2.h"
#include "AudioManager/AudioManager.h"
#include "CharacterLoader.h"
#include "InputManager.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Random.h"
#include "Renderer/Renderer.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace Toy2
{
	// GLOBAL: TOY2 0x005281A4
	int32_t g_pendingFootingType;

	// GLOBAL: TOY2 0x0052B81C
	int32_t g_footingType;

	// GLOBAL: TOY2 0x0053C650
	int32_t g_spinCooldownTimer;

	// GLOBAL: TOY2 0x0053C83C
	int32_t g_spinHoverTimer;

	extern uint8_t g_environmentTintBlue;
	extern uint8_t g_environmentTintGreen;
	extern uint8_t g_environmentTintRed;
	extern Nu3D::Particles::ParticleInstance* g_laserAimParticle;

	namespace Buzz
	{
		int32_t UpdateFacing(Toy2BuzzActor* buzz, MovementRates* movementRates);
		void UpdateJumpAndGravity(Toy2BuzzActor* buzz, int32_t jumpVelocity, int32_t suppressJumpInput);
		int32_t HandleSwing(Toy2BuzzActor* buzz);
		int32_t HandlePoleClimb(Toy2BuzzActor* buzz);
		int32_t HandleLedgeClimb(Toy2BuzzActor* buzz);

	}

	// GLOBAL: TOY2 0x0050A0A0
	Vector4I g_aimTargetPosition;

	// GLOBAL: TOY2 0x0050A4FC
	int32_t g_aimTargetIndex;

	// GLOBAL: TOY2 0x0053C5D4
	int32_t g_turnRecoveryTimer;

	// GLOBAL: TOY2 0x0053C5E0
	int32_t g_rocketBootsTimer;

	// GLOBAL: TOY2 0x0053C5E8
	int32_t g_environmentEffectType;

	// GLOBAL: TOY2 0x0053C628
	int32_t g_environmentSurfaceY;

	// GLOBAL: TOY2 0x00830D30
	int32_t g_previousBuzzEnvironmentY;

	// GLOBAL: TOY2 0x0053C5E4
	int32_t g_poleRecordOffset;

	// GLOBAL: TOY2 0x0053C608
	int32_t g_spinCancelRequested;

	// GLOBAL: TOY2 0x0053C61C
	int32_t g_jumpHeightControlActive;

	// GLOBAL: TOY2 0x0053C618
	int32_t g_ziplineState;

	// GLOBAL: TOY2 0x0053C620
	int32_t g_gunFireTimer;

	// GLOBAL: TOY2 0x0053C824
	int32_t g_poweredLaserCharge;

	// GLOBAL: TOY2 0x0053C818
	int32_t g_slipperySurfaceState;

	// GLOBAL: TOY2 0x0053C81C
	int32_t g_riderPlatformOffsetX;

	// GLOBAL: TOY2 0x0053C820
	int32_t g_riderPlatformOffsetZ;

	// GLOBAL: TOY2 0x0053C648
	int32_t g_forcedFacingActive;

	// GLOBAL: TOY2 0x0053C64C
	int32_t g_groundSlamTimer;

	// GLOBAL: TOY2 0x0053C660
	int32_t g_ledgeClimbTimer;

	// GLOBAL: TOY2 0x0053C668
	int32_t g_poleClimbState;

	// GLOBAL: TOY2 0x0053C66C
	int32_t g_swingTimer;

	// GLOBAL: TOY2 0x0053C838
	int32_t g_airborneTimer;

	// GLOBAL: TOY2 0x0053C840
	int32_t g_gunChargeTimer;

	// GLOBAL: TOY2 0x0053C834
	int32_t g_damageRegistered;

	// GLOBAL: TOY2 0x0053C828
	uint32_t g_actionStateFlags;

	// GLOBAL: TOY2 0x0053C658
	int32_t g_movementInputLockTimer;

	// GLOBAL: TOY2 0x0053C600
	int32_t g_ziplineCooldown;

	// GLOBAL: TOY2 0x0053C610
	int32_t g_ziplineRecordIndex;

	// GLOBAL: TOY2 0x0053C624
	int32_t g_forcedFacingAngle;

	// GLOBAL: TOY2 0x0053C830
	int32_t g_damageBlinkCounter;

	// GLOBAL: TOY2 0x0053C634
	uint32_t g_animationEventFlags;

	// GLOBAL: TOY2 0x0053C60C
	int32_t g_surfaceEffectState;

	// GLOBAL: TOY2 0x0053C810
	int32_t g_previousVerticalVelocity;

	// GLOBAL: TOY2 0x0053C62C
	int32_t g_outOfBoundsLifeGranted;

	// GLOBAL: TOY2 0x0050A098
	int32_t g_idleAnimationState;

	// GLOBAL: TOY2 0x0053C678
	int32_t g_facingInterpolationTimer;

	// GLOBAL: TOY2 0x0053C5D0
	int32_t g_targetFacingAngle;

	// GLOBAL: TOY2 0x0053C674
	int32_t g_jumpStartY;

	// GLOBAL: TOY2 0x0053C664
	int32_t g_idleAnimationTimer;

	// GLOBAL: TOY2 0x0053C5DC
	int32_t g_movementLockTimer;

	namespace Buzz
	{
		// GLOBAL: TOY2 0x004F59A4
		extern const StartPosition g_startPositions[17] = {
			{ { 0, 0, 0 }, 0, 0 },
			{ { 194774, 60044, -361401 }, 0, 0 },
			{ { -466652, 433, -32144 }, 0x500, 0 },
			{ { -47074, 97, -185901 }, 0, 0 },
			{ { 411295, 11, 30702 }, 0xC52, 0 },
			{ { 204615, 75, 114100 }, 0xB82, 0 },
			{ { 264357, -1082, 2748 }, 0xC00, 0 },
			{ { -128033, 76, -332419 }, 0, 0 },
			{ { -4082, 40, 315274 }, 0x7FF, 0 },
			{ { 16532, 70, -141912 }, 0xFE2, 0 },
			{ { -291, -32978, -646 }, 0x400, 0 },
			{ { -262581, 174040, 253369 }, 0x6D7, 0 },
			{ { -121468, -76752, 63129 }, 0x418, 0 },
			{ { 27126, -94, 240599 }, 0xBFC, 0 },
			{ { -320338, 76, 983296 }, 0x791, 0 },
			{ { -36601, 69, -5416 }, 0xBFA, 0 },
			{ { 0, -561600, -844800 }, 0, 0 },
		};

	}

	namespace Buzz
	{
		// GLOBAL: TOY2 0x004DF040
		uint8_t g_animationEventData[0x3B0] = {
#include "BuzzAnimationEvents.inc"
		};

		// GLOBAL: TOY2 0x004DF3F0
		AnimationStateDefinition g_animationStateDefinitions[33] = {
#include "BuzzAnimationStates.inc"
		};

		STATIC_ASSERT(sizeof(AnimationStateDefinition) == 0x14);
		STATIC_ASSERT(sizeof(g_animationEventData) == 0x3B0);
	}

	namespace Animation
	{}

	namespace Buzz
	{}

	namespace Buzz
	{
		// FUNCTION: TOY2 0x004A28F0 [MATCHED]
		void UpdateRespawnAnchor()
		{
			g_buzzActor.actorFlags &= ~ACTOR_FLAG_RESPAWN_ANCHOR_VALID;
			if (g_buzzActor.collisionFlags != 0 && g_levelTransitionTimer == 0 && Collision::IsSafeFooting(0, &g_buzzActor.collisionState)
				&& (g_footingType & ~3) != 0)
			{
				g_buzzActor.actorFlags |= ACTOR_FLAG_RESPAWN_ANCHOR_VALID;
				g_buzzActor.respawnPos = g_buzzActor.posAngles.pos;
				g_buzzActor.respawnYawAngle = (int16_t)g_buzzActor.posAngles.angles.yaw;
			}
		}

		// FUNCTION: TOY2 0x00414110 [PROVISIONAL]
		void Respawn()
		{
			if (g_levelFileIndex == 12)
			{
				EvilEmperorZurg::BuzzRespawn();
			}

			g_buzzActor.motionTargetPos.x = g_buzzActor.posAngles.pos.x = g_buzzActor.respawnPos.x;
			g_buzzActor.floorYPos = g_buzzActor.motionTargetPos.y = g_buzzActor.posAngles.pos.y = g_buzzActor.respawnPos.y;
			g_buzzActor.lives--;
			g_buzzActor.motionTargetPos.z = g_buzzActor.posAngles.pos.z = g_buzzActor.respawnPos.z;
			g_buzzActor.posAngles.angles.pitch = 0;
			g_buzzActor.posAngles.angles.yaw = (uint16_t)g_buzzActor.respawnYawAngle;
			g_buzzActor.rollAngle = 0;
			g_buzzActor.facingAngle = (uint16_t)g_buzzActor.respawnYawAngle;
			g_buzzActor.velocity.lateral = 0;
			g_buzzActor.velocity.vertical = 0;
			g_buzzActor.velocity.forward = 0;
			g_buzzActor.forwardSpeed = 0;
			g_buzzActor.lateralSpeed = 0;
			g_buzzActor.baseAnimationFramePosition = 0;
			g_buzzActor.animationEventPosition = 0;
			g_buzzActor.surfaceClampY = 0x80000000;
			g_buzzActor.cosmicShieldTimer = 0;
			g_buzzActor.airborneMode = 0;
			g_buzzActor.collisionFlags = 0;
			g_buzzActor.specialAirState = 0;
			g_buzzActor.animationState = 0;
			g_buzzActor.previousAnimationState = 0;
			g_buzzActor.actorFlags = ACTOR_FLAG_STUNNED;
			g_buzzActor.stunTimer = -160;
			g_buzzActor.health = 14;
			g_footingType = -1;
			g_pendingFootingType = -1;

			Camera::InitGameplayCamera(&Camera::g_gameplayCamera, &g_buzzActor);
			Nu3D::Particles::Init();
			RespawnCosmicShield();
			AudioManager::g_soundSequenceSlotIndex = 0;
			memset(AudioManager::g_soundSequenceSlots, 0, sizeof(AudioManager::g_soundSequenceSlots));
			AudioManager::g_maxLeftVolume = 0;
			AudioManager::g_maxRightVolume = 0;
			AudioManager::g_maxVolume = 0;
			HUD::g_slideTimers[0] = 180;
			HUD::g_slideAngles[0] = 0x400;
			ResetBuzzState();
		}

		// FUNCTION: TOY2 0x004863A0 [PROVISIONAL]
		int32_t UpdateFloorHeight(Toy2BuzzActor* buzz)
		{
			int32_t previousTriangleCount = Collision::g_collisionTriangleCount;
			Collision::GatherTrianglesAtXZ(&buzz->posAngles.pos);

			PosAndAngles floorProbe = buzz->posAngles;
			floorProbe.pos.y -= 0x400;
			Collision::ResolveGroundCeiling(&floorProbe, 0x10000);
			int32_t floorY = floorProbe.pos.y;

			Collision::g_groundPlatformIndex = -1;
			if (Collision::g_collisionMeshInstances[Collision::g_groundCollisionMeshIndex].typeFlags == Collision::COLLISION_MESH_MOVING)
			{
				int32_t platformIndex = Collision::g_collisionMeshInstances[Collision::g_groundCollisionMeshIndex].platformIdx;
				Collision::g_groundPlatformIndex = platformIndex;
				int16_t platformFlags = Platform::g_platformStates[platformIndex].flags;

				int32_t buzzY;
				if ((platformFlags & Platform::PLATFORM_FLAG_ROTATED) == Platform::PLATFORM_FLAG_ROTATED)
				{
					buzzY = g_buzzActor.posAngles.pos.y;
					if (floorY + 0x200 < buzzY && buzzY < floorY + 0x6000)
					{
						buzzY = floorY - 0x1C0;
						g_buzzActor.posAngles.pos.y = buzzY;
						if (Collision::g_groundNormal.y >= -0x2000)
						{
							g_buzzActor.velocity.lateral = g_buzzActor.velocity.lateral * 3 / 4;
							g_buzzActor.velocity.forward = g_buzzActor.velocity.forward * 3 / 4;
						}
					}
				}
				else
				{
					buzzY = g_buzzActor.posAngles.pos.y;
				}

				int32_t floorDistance = floorY - buzzY;
				if (floorDistance < 0x200 && floorDistance > -0x400 && Collision::g_groundNormal.y < -0x2000)
				{
					Platform::g_platformStates[platformIndex].flags =
						platformFlags | Platform::PLATFORM_FLAG_BUZZ_CONTACT | Platform::PLATFORM_FLAG_BUZZ_GROUNDED;
					Collision::g_collisionQueryResults[0].platformIndex = (int16_t)platformIndex;
					if (g_buzzActor.airborneMode == 0)
					{
						g_buzzActor.collisionState |= 1;
						g_buzzActor.velocity.vertical += 0x20;
					}
				}
			}

			Collision::g_buzzGroundNormal = Collision::g_groundNormal;
			Collision::g_collisionTriangleCount = previousTriangleCount;
			return floorY;
		}

	}
}
