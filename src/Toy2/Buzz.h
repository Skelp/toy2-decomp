#pragma once

#include "Common.h"
#include "Numerics.h"
#include <stddef.h>

namespace Toy2
{
	namespace Buzz
	{
		enum ActorFlags
		{
			ACTOR_FLAG_LOCK_FACING = 0x4,
			ACTOR_FLAG_RESPAWN_ANCHOR_VALID = 0x8,
			ACTOR_FLAG_STUNNED = 0x20,
			ACTOR_FLAG_UNCONTROLLED_MOMENTUM = 0x40,
			ACTOR_FLAG_PRESERVE_HORIZONTAL_MOMENTUM = 0x200,
		};

		enum GrappleState
		{
			GRAPPLE_INACTIVE = 0,
			GRAPPLE_EXTENDING = 1,
			GRAPPLE_PULLING = 2,
			GRAPPLE_BLOCKED = 3,
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
			PosAndAngles posAngles;
			int16_t rollAngle;
			int16_t primaryAnimIdx;
			int16_t creatureId;
			int16_t secondaryAnimIdx;
			int32_t unkVar7;
			int32_t unkVar8;
			int32_t surfaceClampY;
			Vector3I16 lightDirection;
			int16_t unkVar51;
			int32_t unkVar12;
			int32_t unkVar13;
			RGBA color;
			int16_t unkWord1;
			int16_t unkWord2;
			int16_t unkWord3;
			int16_t unkWord4;
			int16_t actorFlags;
			int16_t visibilityDistance;
			int32_t unkVar22;
			int16_t facingAngle;
			int16_t unkVar23;
			Vector3I respawnPos;
			int32_t respawnYawAngle;
			Vector3I motionTargetPos;
			int32_t velX;
			int32_t gravityVel;
			int32_t velForward;
			int32_t forwardSpeed;
			int32_t lateralSpeed;
			int32_t movementState;
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

		void Respawn();
		void RefreshDiscAmmo();
		void ResetGravityBoots();
		void ActivateRocketBoots(GadgetPickup* pickup);
		void DeactivateRocketBoots();
		void CancelGrapple();
		void FireDiscLauncher(int32_t launchPitch);
		void FireGrapple(int32_t aimYaw, int32_t aimPitch);
		void HandleCollisions(Toy2BuzzActor* buzz, Vector3I* movement, uint8_t* contactState, int32_t queryIndex);
		void TickCosmicShield();
		void TickGrapple();
		void TickBeamShots();
		void TickGadgets();
		void Launch(int32_t verticalVelocity, int16_t airborneMode);
		void UpdateHorizontalMovement(Toy2BuzzActor* buzz, MovementRates* movementRates, int32_t forwardInput);
		void UpdateRespawnAnchor();

		STATIC_ASSERT(sizeof(BeamShot) == 0x2C);
		STATIC_ASSERT(sizeof(MovementRates) == 0x1C);
		STATIC_ASSERT(offsetof(BeamShot, fadeTimer) == 0x24);
		STATIC_ASSERT(offsetof(BeamShot, movementTimer) == 0x26);
		STATIC_ASSERT(offsetof(BeamShot, color) == 0x28);
		STATIC_ASSERT(sizeof(GadgetPickup) == 0x10);
		STATIC_ASSERT(sizeof(Toy2BuzzActor) == 0xA0);
	}

	extern Buzz::Toy2BuzzActor g_buzzActor;
	extern int32_t g_rocketBootsTimer;
	extern int32_t g_environmentSurfaceY;
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
}
