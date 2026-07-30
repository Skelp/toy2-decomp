#pragma once

#include "Common.h"
#include "Numerics.h"
#include "Toy2/Actor.h"
#include <stddef.h>

namespace Toy2
{
	extern int32_t g_spinCooldownTimer;
	extern int32_t g_spinHoverTimer;
	extern int32_t g_groundSlamTimer;
	extern int32_t g_footingType;
	extern int32_t g_ledgeClimbPlatformIndex;
	extern int32_t g_previousVerticalVelocity;

	namespace Buzz
	{
		enum DamageFlags
		{
			DAMAGE_KNOCKBACK = 0x1,
			DAMAGE_NORMAL = 0x2,
			DAMAGE_FORCE_DEATH = 0x4,
		};

		enum ActorFlags
		{
			ACTOR_FLAG_DAMAGE_REACTION = 0x2,
			ACTOR_FLAG_LOCK_FACING = 0x4,
			ACTOR_FLAG_RESPAWN_ANCHOR_VALID = 0x8,
			ACTOR_FLAG_DEATH_TRANSITION = 0x10,
			ACTOR_FLAG_STUNNED = 0x20,
			ACTOR_FLAG_UNCONTROLLED_MOMENTUM = 0x40,
			ACTOR_FLAG_BLOCK_EDGE_IDLE = 0x100,
			ACTOR_FLAG_PRESERVE_HORIZONTAL_MOMENTUM = 0x200,
		};

		enum AnimationState
		{
			ANIMATION_STATE_AIRBORNE = 2,
			ANIMATION_STATE_LANDING = 3,
			ANIMATION_STATE_STANDING = 4,
			ANIMATION_STATE_DAMAGE_REACTION = 5,
			ANIMATION_STATE_SWINGING = 8,
			ANIMATION_STATE_FORWARD_EDGE = 10,
			ANIMATION_STATE_REAR_EDGE = 11,
		};

		enum CameraState
		{
			CAMERA_STATE_TARGETING = 3,
			CAMERA_STATE_VISOR = 4,
		};

		enum PoleClimbFlags
		{
			POLE_CLIMB_ASCENDING = 0x2,
			POLE_CLIMB_SLIDING = 0x4,
			POLE_CLIMB_ATTACHING = 0x8,
		};

		enum ZiplineState
		{
			ZIPLINE_APPROACHING = 1,
			ZIPLINE_RIDING = 2,
		};

		enum GrappleState
		{
			GRAPPLE_INACTIVE = 0,
			GRAPPLE_EXTENDING = 1,
			GRAPPLE_PULLING = 2,
			GRAPPLE_BLOCKED = 3,
		};

		enum MovementAxisLocks
		{
			MOVEMENT_LOCK_LATERAL = 0x1,
			MOVEMENT_LOCK_VERTICAL = 0x2,
			MOVEMENT_LOCK_FORWARD = 0x4,
		};

		struct MovementRates
		{
			int32_t lateralDeceleration;
			int32_t forwardDeceleration;
			int32_t forwardAcceleration;
			int32_t verticalAcceleration;
			int32_t lateralSpeedLimit;
			int32_t forwardSpeedLimit;
			int32_t turnRateLimit;
		};

		struct MovementVelocity
		{
			int32_t lateral;
			int32_t vertical;
			int32_t forward;
		};

		struct BeamShot
		{
			Vector3I start;
			Vector3I end;
			Vector3I velocity;
			int16_t fadeTimer;
			int16_t movementTimer;
			RGBA color;
		};

		struct GadgetPickup
		{
			Vector3I position;
			uint8_t linkId;
			uint8_t reservedD[3];
		};

		struct Toy2BuzzActor
		{
			union
			{
				Actor::Toy2Actor boneAttachmentActor;
				struct
				{
					PosAndAngles posAngles;
					int16_t rollAngle;
					int16_t primaryAnimIdx;
					int16_t creatureId;
					int16_t secondaryAnimIdx;
					int32_t animationFramePosition;
					int32_t secondaryAnimationFramePosition;
					int32_t surfaceClampY;
					Vector3I16 lightDirection;
					int16_t lightDistance;
					int16_t scaleX;
					int16_t scaleY;
					int16_t scaleZ;
					int16_t scalePivotHeight;
					RGBA color;
					int16_t unkWord1;
					int16_t unkWord2;
					int16_t unkWord3;
					int16_t unkWord4;
					int16_t actorFlags;
					int16_t visibilityDistance;
					int32_t unkVar22;
					int16_t facingAngle;
					int16_t unusedFrameState;
					Vector3I respawnPos;
					int32_t respawnYawAngle;
					Vector3I motionTargetPos;
					MovementVelocity velocity;
					int32_t forwardSpeed;
					int32_t lateralSpeed;
					int32_t baseAnimationFramePosition;
					int32_t animationEventPosition;
					int32_t floorYPos;
					int32_t isOnWalkableFloor;
					int16_t airborneMode;
					union
					{
						int16_t collisionFlags;
						struct
						{
							uint8_t collisionState;
							uint8_t collisionFlagsHigh;
						};
					};
					int16_t animationState;
					int16_t previousAnimationState;
					int16_t stunTimer;
					int16_t health;
					int16_t cosmicShieldTimer;
					int16_t lives;
					int16_t specialAirState;
					int16_t coinsCollected;
				};
			};
		};

		void Respawn();
		void Init(Toy2BuzzActor* buzz, int32_t levelIndex);
		void RefreshDiscAmmo();
		void ResetGravityBoots();
		void ActivateRocketBoots(GadgetPickup* pickup);
		void DeactivateRocketBoots();
		void CancelGrapple();
		void FireDiscLauncher(int32_t launchPitch);
		void FireGrapple(int32_t aimYaw, int32_t aimPitch);
		void HandleDamage(uint32_t direction, uint32_t damageFlags);
		void ResolveFooting(Toy2BuzzActor* buzz);
		void TickGunFire(Toy2BuzzActor* buzz);
		void HandleCollisions(Toy2BuzzActor* buzz, MovementVelocity* movement, uint8_t* contactState, int32_t queryIndex);
		int32_t TickGroundSlam(Toy2BuzzActor* buzz);
		void TickSpinHover(Toy2BuzzActor* buzz);
		int32_t HandleZipline(Toy2BuzzActor* buzz);
		void TickCosmicShield();
		void TickGrapple();
		void TickBeamShots();
		void TickGadgets();
		void HandleGameplay(Toy2BuzzActor* buzz);
		void CheckParticleCollisions();
		void UpdateAnimationState();
		void UpdateContactEffects();
		void Launch(int32_t verticalVelocity, int16_t airborneMode);
		void UpdateHorizontalMovement(Toy2BuzzActor* buzz, MovementRates* movementRates, int32_t forwardInput);
		void UpdateRespawnAnchor();
		int32_t UpdateFloorHeight(Toy2BuzzActor* buzz);

		STATIC_ASSERT(sizeof(BeamShot) == 0x2C);
		STATIC_ASSERT(sizeof(MovementRates) == 0x1C);
		STATIC_ASSERT(sizeof(MovementVelocity) == 0xC);
		STATIC_ASSERT(offsetof(BeamShot, fadeTimer) == 0x24);
		STATIC_ASSERT(offsetof(BeamShot, movementTimer) == 0x26);
		STATIC_ASSERT(offsetof(BeamShot, color) == 0x28);
		STATIC_ASSERT(sizeof(GadgetPickup) == 0x10);
		STATIC_ASSERT(sizeof(Toy2BuzzActor) == 0xA0);
		STATIC_ASSERT(offsetof(Toy2BuzzActor, animationFramePosition) == 0x18);
		STATIC_ASSERT(offsetof(Toy2BuzzActor, secondaryAnimationFramePosition) == 0x1C);
		STATIC_ASSERT(offsetof(Toy2BuzzActor, velocity) == 0x68);
		STATIC_ASSERT(offsetof(Toy2BuzzActor, baseAnimationFramePosition) == 0x7C);
		STATIC_ASSERT(offsetof(Toy2BuzzActor, animationEventPosition) == 0x80);
		STATIC_ASSERT(offsetof(Toy2BuzzActor, animationState) == 0x90);
	}

	extern Buzz::Toy2BuzzActor g_buzzActor;
	extern int32_t g_slipperySurfaceState;
	extern int32_t g_poweredLaserCharge;
	extern int32_t g_aimTargetLocked;
	extern int32_t g_rocketBootsTimer;
	extern int32_t g_environmentEffectType;
	extern int32_t g_environmentSurfaceY;
	extern int32_t g_previousBuzzEnvironmentY;
	extern Buzz::GadgetPickup* g_activeRocketBootsPickup;
	extern int32_t g_savedRocketBootsPickupY;
	extern Buzz::GadgetPickup* g_activeCosmicShieldPickup;
	extern int32_t g_savedCosmicShieldPickupY;
	extern int32_t g_discLauncherAmmo;
	extern int32_t g_discLauncherShotSlotsAvailable;
	extern int32_t g_grappleTraversalDuration;
	extern int32_t g_grappleElapsedTime;
	extern Vector3I g_grappleEndpoint;
	extern int32_t g_cosmicShieldYaw;
	extern int32_t g_cosmicShieldRoll;
	extern Buzz::BeamShot g_beamShots[4];
	Buzz::BeamShot* SpawnBeamShot(int32_t shotType, int32_t aimYaw, int32_t aimPitch, Vector3I* origin, Vector3I* offset, int32_t range, int32_t autoAim);
}
