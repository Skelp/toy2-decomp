#ifndef BUZZINTERNAL_H
#define BUZZINTERNAL_H

#include "Toy2/Buzz.h"
#include "Toy2/Actor.h"
#include "Numerics.h"
#include "Nu3D/Particles.h"

// Declarations the units of the character share but that no public header
// states: the action state flags, the masks that say which actions block which,
// and the movement state one unit writes and another reads.
namespace Toy2
{
	extern Vector4I g_aimTargetPosition;
	extern int32_t g_aimTargetIndex;
	extern int32_t g_gunFireTimer;
	extern int32_t g_gunChargeTimer;
	extern int32_t g_airborneTimer;
	extern int32_t g_turnRecoveryTimer;
	extern int32_t g_movementLockTimer;
	extern int32_t g_movementInputLockTimer;
	extern int32_t g_ledgeClimbTimer;
	extern int32_t g_ziplineState;
	extern int32_t g_swingTimer;
	extern int32_t g_spinCancelRequested;
	extern int32_t g_outOfBoundsLifeGranted;
	extern int32_t g_damageBlinkCounter;
	extern int32_t g_damageRegistered;
	extern uint32_t g_actionStateFlags;
	extern int32_t g_slipperySurfaceAngle;
	namespace Camera
	{
		extern int32_t g_forwardInputDisabled;
	}
	extern uint32_t g_animationEventFlags;
	extern int32_t g_surfaceEffectState;

	// Movement state that Buzz.cpp defines and BuzzMovement.cpp reads.
	extern int32_t g_pendingFootingType;
	extern int32_t g_footingType;
	extern int32_t g_spinCooldownTimer;
	extern int32_t g_spinHoverTimer;
	extern int32_t g_rocketBootsTimer;
	extern int32_t g_environmentEffectType;
	extern int32_t g_environmentSurfaceY;
	extern int32_t g_poleRecordOffset;
	extern int32_t g_jumpHeightControlActive;
	extern int32_t g_poweredLaserCharge;
	extern int32_t g_slipperySurfaceState;
	extern int32_t g_forcedFacingActive;
	extern int32_t g_groundSlamTimer;
	extern int32_t g_poleClimbState;
	extern int32_t g_ziplineCooldown;
	extern int32_t g_ziplineRecordIndex;
	extern int32_t g_forcedFacingAngle;
	extern int32_t g_previousVerticalVelocity;
	extern int32_t g_facingInterpolationTimer;
	extern int32_t g_targetFacingAngle;
	extern int32_t g_jumpStartY;

	namespace MoveableObject
	{
		void Update(Buzz::Toy2BuzzActor* buzz);
	}

	namespace Buzz
	{
		const int16_t ACTOR_FLAG_VISIBLE = 0x1;

		// Entry points the two units of the character call across the split.
		int32_t HandleLedgeClimb(Toy2BuzzActor* buzz);
		int32_t UpdateFacing(Toy2BuzzActor* buzz, MovementRates* movementRates);
		void UpdateJumpAndGravity(Toy2BuzzActor* buzz, int32_t jumpVelocity, int32_t suppressJumpInput);
		int32_t HandleSwing(Toy2BuzzActor* buzz);
		int32_t HandlePoleClimb(Toy2BuzzActor* buzz);
		int32_t HandleZipline(Toy2BuzzActor* buzz);
		void ResolveFooting(Toy2BuzzActor* buzz);
		void UpdateHorizontalMovement(Toy2BuzzActor* buzz, MovementRates* movementRates);
		void TickGunFire(Toy2BuzzActor* buzz);
		int32_t TickGroundSlam(Toy2BuzzActor* buzz);
		void TickSpinHover(Toy2BuzzActor* buzz);
		void HandleGameplay(Toy2BuzzActor* buzz);
		void Launch(int32_t verticalVelocity, int16_t airborneMode);

		const uint32_t MOVEMENT_UPDATE_WITHOUT_INPUT = 0x8;
		const uint32_t MOVEMENT_GRAPPLE_CONTROLLED = 0x10;
		const uint32_t ACTION_STATE_AIR_CONTROL = 0x1;
		const uint32_t ACTION_STATE_SPIN_HOVER = 0x2;
		const uint32_t ACTION_STATE_LEDGE_CLIMB = 0x4;
		const uint32_t ACTION_STATE_ZIPLINE = 0x8;
		const uint32_t ACTION_STATE_POLE_CLIMB = 0x10;
		const uint32_t ACTION_STATE_AIRBORNE_RECOVERY = 0x20;
		const uint32_t ACTION_STATE_GROUND_SLAM = 0x40;
		const uint32_t ACTION_STATE_GUN_FIRE = 0x80;
		const uint32_t ACTION_STATE_CAMERA = 0x100;
		const uint32_t ACTION_STATE_FORCED_FACING = 0x200;
		const uint32_t ACTION_STATE_SWING = 0x400;
		const uint32_t ACTION_STATE_COSMIC_SHIELD = 0x800;
		const uint32_t ACTION_STATE_ROCKET_BOOTS = 0x1000;
		const uint32_t ACTION_STATE_GRAPPLE = 0x2000;
		const uint32_t ACTION_STATE_GRAVITY_BOOTS = 0x4000;
		const uint32_t ACTION_STATE_SPIN_CANCEL = 0x8000;
		const uint32_t ACTION_STATE_SLIPPERY_SURFACE = 0x10000;
		const uint32_t VISOR_START_ACTION_MASK = 0xFFF7F;
		const uint32_t AIRBORNE_TIMER_RESET_ACTIONS = 0x18010;

		enum TraversalState
		{
			TRAVERSAL_INACTIVE = 0,
			TRAVERSAL_UPDATE_MOVEMENT = 1,
			TRAVERSAL_HANDLED = 2,
			TRAVERSAL_UPDATE_WITHOUT_INPUT = 3,
		};

		enum AnimationEventFlag
		{
			ANIMATION_EVENT_RIGHT_FOOT = 0x1,
			ANIMATION_EVENT_LEFT_FOOT = 0x2,
			ANIMATION_EVENT_MOVEMENT = 0x4,
			ANIMATION_EVENT_IDLE = 0x8,
			ANIMATION_EVENT_FORCE_EFFECT = 0x10,
		};

		const uint32_t GROUND_SLAM_BLOCKING_ACTIONS = 0xFFF7F;
		const uint32_t GUN_IDLE_BLOCKING_ACTIONS = 0xFBE3C;
		const uint32_t GUN_START_BLOCKING_ACTIONS = 0xFBEFE;
		const uint32_t GUN_REPEAT_BLOCKING_ACTIONS = 0xFBE7E;
		const int32_t SURFACE_DAMAGE_GROUP = 0;
		const int32_t SURFACE_DEATH_GROUP = 4;
		const int32_t SLIPPERY_SURFACE_QUALITY = 13;
		const uint32_t CLEAR_ACTION_STATE_GUN_FIRE = 0xFF7F;
		const uint32_t SPIN_START_BLOCKING_ACTIONS = 0xFFF7F;
		const uint32_t SPIN_CHARGE_BLOCKING_ACTIONS = 0xFFFFE;
		const uint32_t SPIN_HOVER_BLOCKING_ACTIONS = 0xFFF7E;
		const uint32_t LEDGE_CLIMB_BLOCKING_ACTIONS = 0xFFF7F;
		const uint32_t POLE_CLIMB_BLOCKING_ACTIONS = 0xFFF6E;
		const uint32_t SWING_BLOCKING_ACTIONS = 0xFFB7E;
		const int16_t SWING_START_ANIMATION_STATE = 26;
		const uint32_t TURN_RECOVERY_ACTION_MASK = 0xFF481;
		const int16_t GROUND_SLAM_ANIMATION_STATE = 8;
		const int32_t FACING_SNAP_THRESHOLD = 0x5DC;
		const int32_t JUMP_HEIGHT_TARGET = 0x6A80;
		const int32_t MIN_CONTROLLED_JUMP_VELOCITY = -0x74C;
		const int32_t MAX_CONTROLLED_JUMP_VELOCITY = -0x22A;

		// The two records the level data states for a swing point and a zipline.
		struct SwingRecord
		{
			Vector3I start;
			Vector3I end;
		};
		STATIC_ASSERT(sizeof(SwingRecord) == 0x18);

		struct ZiplineRecord
		{
			Vector3I start;
			Vector3I end;
		};
		STATIC_ASSERT(sizeof(ZiplineRecord) == 0x18);

		enum PoleType
		{
			POLE_TYPE_TOP_EXIT = 1,
			POLE_TYPE_SLIDE = 2,
			POLE_TYPE_DISABLED = 3,
		};

		enum PoleBoundaryState
		{
			POLE_BOUNDARY_NONE = 0,
			POLE_BOUNDARY_BOTTOM = 1,
			POLE_BOUNDARY_TOP = 2,
		};
	}

	namespace MoveableObject
	{
		void Update(Buzz::Toy2BuzzActor* buzz);
	}

	namespace Platform
	{
		extern int16_t g_contactPlatformIndices[32];
		extern int32_t g_contactPlatformCount;
		void AdvancePartialMotion(int32_t frameScale, int32_t collisionScale, int32_t skipMotion);
		void UpdateBuzzPlatformMotion(int32_t queryIndex, int32_t collisionPass);
	}

	namespace Terrain
	{
		struct TerrainEdgeChain
		{
			TerrainEdgeChain* next;
			uint16_t edgeCount;
			uint16_t reserved;
			Vector3I vertices[0];
		};

		STATIC_ASSERT(sizeof(TerrainEdgeChain) == 0x08);
		STATIC_ASSERT(offsetof(TerrainEdgeChain, edgeCount) == 0x04);
		STATIC_ASSERT(offsetof(TerrainEdgeChain, vertices) == 0x08);

		extern uint8_t* g_terrainRelocationHeads[8];
	}

	namespace Buzz
	{
		enum SurfaceEffectStateMask
		{
			SURFACE_EFFECT_TYPE_MASK = 0xFFFF,
		};

		struct AnimationStateDefinition
		{
			uint8_t* eventTrack;
			int32_t primaryAnimationIndex;
			int32_t secondaryAnimationIndex;
			int32_t frameAdvanceRate;
			int32_t unusedFlags;
		};

		extern uint8_t g_animationEventData[0x3B0];
		extern AnimationStateDefinition g_animationStateDefinitions[33];
	}

	// The environment tint and the aim particle that Buzz.cpp holds.
	extern uint8_t g_environmentTintBlue;
	extern uint8_t g_environmentTintGreen;
	extern uint8_t g_environmentTintRed;
	extern Nu3D::Particles::ParticleInstance* g_laserAimParticle;
	extern int32_t g_idleAnimationState;
	extern int32_t g_idleAnimationTimer;

	namespace Buzz
	{
		struct StartPosition
		{
			Vector3I position;
			int16_t yawAngle;
			int16_t reserved;
		};

		STATIC_ASSERT(sizeof(StartPosition) == 0x10);

		extern const StartPosition g_startPositions[17];
	}
}

#endif
