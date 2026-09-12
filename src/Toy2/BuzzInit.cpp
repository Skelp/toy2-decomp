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

// The set-up and the state reset of Buzz: retail holds 0x00433D50 and
// 0x00433ED0 as one object, in the order of this file.
namespace Toy2
{
	namespace Buzz
	{
		// FUNCTION: TOY2 0x00433D50 [PROVISIONAL]
		void Init(Toy2BuzzActor* buzz, int32_t levelIndex)
		{
			int32_t lives = buzz->lives;
			int32_t health = buzz->health;
			memset(buzz, 0, sizeof(*buzz));

			const StartPosition* start = &g_startPositions[levelIndex];
			const int32_t* startPosition = &start->position.x;
			buzz->posAngles.pos.x = *startPosition++;
			buzz->posAngles.pos.y = *startPosition++;
			buzz->posAngles.pos.z = *startPosition;
			int32_t groundY = UpdateFloorHeight(buzz) - 0x100;
			int16_t yawAngle = start->yawAngle;

			buzz->motionTargetPos.x = buzz->posAngles.pos.x;
			buzz->posAngles.pos.y = groundY;
			buzz->motionTargetPos.y = groundY;
			buzz->motionTargetPos.z = buzz->posAngles.pos.z;
			buzz->respawnPos.y = groundY;
			buzz->respawnPos.x = buzz->posAngles.pos.x;
			buzz->respawnYawAngle = yawAngle;
			buzz->posAngles.angles.yaw = yawAngle;
			buzz->facingAngle = yawAngle;
			buzz->surfaceClampY = (int32_t)0x80000000;
			buzz->lives = lives;
			buzz->health = health;
			buzz->respawnPos.z = buzz->posAngles.pos.z;
			buzz->isOnWalkableFloor = 0;
			buzz->primaryAnimIdx = -1;
			buzz->secondaryAnimIdx = -1;
			buzz->visibilityDistance = 0x500;

			g_ledgeClimbTimer = 0;
			g_airborneTimer = 0;
			g_poleClimbState = 0;
			g_movementInputLockTimer = 0;
			g_poleRecordOffset = 0;
			g_turnRecoveryTimer = 0;
			g_ziplineState = 0;
			g_ziplineCooldown = 0;
			g_ziplineRecordIndex = 0;
			g_spinHoverTimer = 0;
			g_spinCooldownTimer = 0;
			g_groundSlamTimer = 0;
			g_actionStateFlags = 0;
			g_gunFireTimer = 0;
			g_gunChargeTimer = 0;
			g_forcedFacingActive = 0;
			g_forcedFacingAngle = 0;
			g_swingTimer = 0;
			g_damageRegistered = 0;
			g_damageBlinkCounter = 0;
			g_footingType = -1;
			g_pendingFootingType = -1;
			g_animationEventFlags = 0;
			g_surfaceEffectState = 0;
			g_previousVerticalVelocity = 0;
			g_environmentSurfaceY = 0;
			g_environmentEffectType = 0;
			g_outOfBoundsLifeGranted = 0;
			g_poweredLaserCharge = 0;
			g_spinCancelRequested = 0;
			g_slipperySurfaceState = 0;
			InputManager::g_directionInputState2Frames = 0;
			InputManager::g_directionInputState3Frames = 0;
			g_idleAnimationState = 0;
			g_facingInterpolationTimer = 0;
			g_targetFacingAngle = 0;
			g_jumpStartY = buzz->posAngles.pos.y;
			g_jumpHeightControlActive = 0;
			g_idleAnimationTimer = 0;
			g_movementLockTimer = 0;
		}

	}
}

namespace Toy2
{
	// FUNCTION: TOY2 0x00433ED0 [MATCHED]
	void ResetBuzzState()
	{
		g_ledgeClimbTimer = 0;
		g_airborneTimer = 0;
		g_poleClimbState = 0;
		g_poleRecordOffset = 0;
		g_ziplineState = 0;
		g_turnRecoveryTimer = 0;
		g_spinHoverTimer = 0;
		g_spinCooldownTimer = 0;
		g_groundSlamTimer = 0;
		g_gunFireTimer = 0;
		g_gunChargeTimer = 0;
		g_forcedFacingActive = 0;
		g_swingTimer = 0;
		g_spinCancelRequested = 0;
		Buzz::DeactivateRocketBoots();
		Buzz::CancelGrapple();
		Buzz::ResetGravityBoots();
		g_buzzActor.actorFlags &= ~(Buzz::ACTOR_FLAG_LOCK_FACING | Buzz::ACTOR_FLAG_UNCONTROLLED_MOMENTUM | Buzz::ACTOR_FLAG_PRESERVE_HORIZONTAL_MOMENTUM);
	}
}
