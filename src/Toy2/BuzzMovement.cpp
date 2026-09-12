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

// Buzz's movement and the action states that drive it. UpdateJumpAndGravity and
// UpdateHorizontalMovement integrate one frame of motion, ResolveFooting and
// UpdateFacing settle him on the ground, and the Handle and Tick pair per action
// runs the gun, the ground slam, the spin hover, the swing, the pole climb, the
// zipline and the ledge climb. HandleGameplay is the dispatcher over all of them.
//
// Retail run 0x00434090-0x00436220, between the Toy2/Camera.cpp and
// AudioManager.cpp runs.

namespace Toy2
{
	// GLOBAL: TOY2 0x0050A540
	int32_t g_aimTargetLocked;

	// GLOBAL: TOY2 0x0053C5D8
	int32_t g_ziplineEndProgress;

	// GLOBAL: TOY2 0x0053C5F0
	Vector3I g_swingAnchorPosition;

	// GLOBAL: TOY2 0x0053C604
	int32_t g_ziplineTargetYOrSpeed;

	// GLOBAL: TOY2 0x0053C614
	int32_t g_ziplineProgress;

	// GLOBAL: TOY2 0x0053C638
	Vector3I g_ziplineDirection;

	// GLOBAL: TOY2 0x0053C654
	int32_t g_swingFacingAngle;

	// GLOBAL: TOY2 0x0053C814
	int32_t g_slipperySurfaceAngle;

	// GLOBAL: TOY2 0x00559E80
	int32_t g_ledgeClimbPlatformIndex;

	namespace Buzz
	{
		// FUNCTION: TOY2 0x00434090 [MATCHED]
		void Launch(int32_t verticalVelocity, int16_t airborneMode)
		{
			g_buzzActor.airborneMode = airborneMode;
			g_buzzActor.collisionFlags = 0;
			g_buzzActor.specialAirState = 0;
			g_buzzActor.animationState = 2;
			g_buzzActor.velocity.vertical = verticalVelocity;
			g_forcedFacingActive = 0;
			g_turnRecoveryTimer = 0;
			g_jumpHeightControlActive = 0;
		}

		// FUNCTION: TOY2 0x004340D0 [PROVISIONAL]
		void UpdateJumpAndGravity(Toy2BuzzActor* buzz, int32_t jumpVelocity, int32_t suppressJumpInput)
		{
			int32_t gravityDivisor;
			int32_t terminalVelocity;
			if ((g_buzzActor.actorFlags & ACTOR_FLAG_BLOCK_EDGE_IDLE) != 0)
			{
				gravityDivisor = 4;
				if (g_environmentEffectType == 1)
				{
					terminalVelocity = 0x400;
				}
				else
				{
					terminalVelocity = 0x40;
					if (buzz->velocity.vertical >= 0)
						buzz->specialAirState = 6;
				}
			}
			else
			{
				gravityDivisor = 2;
				terminalVelocity = 0x800;
			}

			if ((InputManager::g_directionInputState & INPUT_JUMP) != 0 && suppressJumpInput == 0)
			{
				int16_t airborneMode = buzz->airborneMode;
				int32_t airborneTimer = g_airborneTimer;
				if ((airborneMode == 2 && buzz->velocity.vertical > -0x400 && g_spinHoverTimer >= 0 && buzz->specialAirState == 0
						&& buzz->cosmicShieldTimer <= 0 && airborneTimer >= 0)
					|| (airborneMode == 0 && airborneTimer == 0x50))
				{
					g_airborneTimer = 0;
					int32_t heightDelta = g_jumpStartY - buzz->posAngles.pos.y;
					if (heightDelta > 0 && g_jumpHeightControlActive != 0)
					{
						int32_t gravity = 0x100 / (gravityDivisor * gravityDivisor);
						buzz->velocity.vertical = -(int32_t)sqrt((double)(gravity * (JUMP_HEIGHT_TARGET - heightDelta) * 2));
						if (buzz->velocity.vertical < MIN_CONTROLLED_JUMP_VELOCITY)
							buzz->velocity.vertical = MIN_CONTROLLED_JUMP_VELOCITY;
						if (buzz->velocity.vertical > MAX_CONTROLLED_JUMP_VELOCITY)
							buzz->velocity.vertical = MAX_CONTROLLED_JUMP_VELOCITY;
					}
					else
					{
						buzz->velocity.vertical = -0x900 / gravityDivisor;
					}
					buzz->airborneMode = 5;
					buzz->animationState = GROUND_SLAM_ANIMATION_STATE;
					AudioManager::PlaySoundEffect(0x10, &g_buzzActor.posAngles.pos);
					airborneTimer = g_airborneTimer;
				}

				airborneMode = buzz->airborneMode;
				if (airborneMode == 1 || airborneMode >= 4)
				{
					if (buzz->collisionFlags != 0)
						buzz->airborneMode = 5;
					else if (airborneMode == 6)
						buzz->airborneMode = 3;
				}
				else if (buzz->specialAirState != 0 && g_groundSlamTimer == 0 && airborneTimer != 0x50)
				{
					if (AudioManager::IsActorSoundPlaying(buzz) == 0)
						AudioManager::Preset::PlayOneShotSound2(0x19, buzz);

					g_buzzActor.airborneMode = 1;
					g_buzzActor.collisionFlags = 0;
					g_buzzActor.specialAirState = 0;
					g_buzzActor.animationState = ANIMATION_STATE_AIRBORNE;
					g_buzzActor.velocity.vertical = jumpVelocity;
					g_forcedFacingActive = 0;
					g_turnRecoveryTimer = 0;
					g_jumpHeightControlActive = 0;
					g_jumpStartY = buzz->posAngles.pos.y;
					g_jumpHeightControlActive = 1;
				}
				else if (airborneMode == 2)
				{
					buzz->airborneMode = 3;
				}
			}
			else
			{
				if (buzz->collisionFlags != 0)
					buzz->airborneMode = 0;

				if (buzz->airborneMode == 1)
				{
					if (buzz->velocity.vertical < 0)
					{
						if (buzz->velocity.vertical < -0x320 / gravityDivisor)
							buzz->velocity.vertical += 0x180 / (gravityDivisor * gravityDivisor);
						buzz->velocity.vertical = buzz->velocity.vertical / 2 - 0x100 / (gravityDivisor * gravityDivisor);
					}
					buzz->airborneMode = 2;
				}

				if (buzz->airborneMode == 5)
					buzz->airborneMode = 6;
			}

			if (buzz->collisionFlags == 0)
				buzz->velocity.vertical += (Renderer::g_frameDelta << 8) / (gravityDivisor * gravityDivisor);
			if (buzz->velocity.vertical > terminalVelocity)
				buzz->velocity.vertical = terminalVelocity;
		}

		// FUNCTION: TOY2 0x004343D0 [PROVISIONAL]
		void UpdateHorizontalMovement(Toy2BuzzActor* buzz, MovementRates* movementRates, int32_t forwardInput)
		{
			int32_t yaw = (int16_t)buzz->posAngles.angles.yaw;
			int32_t backwardSine = Numerics::g_sinCosLUT[(yaw - 0x800) & 0xFFF] >> 2;
			const int16_t* cosine = &Numerics::g_sinCosLUT[(yaw + 0x400) & 0xFFF];
			int32_t cosineValue = *cosine >> 2;
			int32_t lateralSpeed = (buzz->velocity.forward * backwardSine + buzz->velocity.lateral * cosineValue) / 0x1000;
			int32_t forwardSpeed = (buzz->velocity.forward * cosineValue - buzz->velocity.lateral * backwardSine) / 0x1000;

			if ((buzz->actorFlags & ACTOR_FLAG_PRESERVE_HORIZONTAL_MOMENTUM) != 0)
			{
				if (buzz->collisionFlags != 0)
				{
					buzz->actorFlags &= ~ACTOR_FLAG_PRESERVE_HORIZONTAL_MOMENTUM;
				}
			}
			else
			{
				if (lateralSpeed < 0)
				{
					lateralSpeed += movementRates->lateralDeceleration;
					if (lateralSpeed > 0)
					{
						lateralSpeed = 0;
					}
				}
				else if (lateralSpeed > 0)
				{
					lateralSpeed -= movementRates->lateralDeceleration;
					if (lateralSpeed < 0)
					{
						lateralSpeed = 0;
					}
				}

				if (forwardSpeed < 0)
				{
					forwardSpeed += movementRates->forwardDeceleration;
					if (forwardSpeed > 0)
					{
						forwardSpeed = 0;
					}
				}
				else if (forwardSpeed > 0)
				{
					forwardSpeed -= movementRates->forwardDeceleration;
					if (forwardSpeed < 0)
					{
						forwardSpeed = 0;
					}
				}

				if (forwardInput > 0 && forwardSpeed < movementRates->lateralSpeedLimit)
				{
					forwardSpeed += movementRates->forwardAcceleration + movementRates->forwardDeceleration;
				}
				if (forwardInput < 0 && forwardSpeed > -movementRates->lateralSpeedLimit)
				{
					forwardSpeed -= movementRates->forwardAcceleration + movementRates->forwardDeceleration;
				}
			}

			if (lateralSpeed > movementRates->lateralSpeedLimit)
			{
				lateralSpeed = movementRates->lateralSpeedLimit;
			}
			if (lateralSpeed < -movementRates->lateralSpeedLimit)
			{
				lateralSpeed = -movementRates->lateralSpeedLimit;
			}
			if (forwardSpeed > movementRates->forwardSpeedLimit)
			{
				forwardSpeed = movementRates->forwardSpeedLimit;
			}
			if (forwardSpeed < -movementRates->forwardSpeedLimit)
			{
				forwardSpeed = -movementRates->forwardSpeedLimit;
			}

			buzz->forwardSpeed = forwardSpeed;
			buzz->lateralSpeed = lateralSpeed;
			yaw = (int16_t)buzz->posAngles.angles.yaw;
			int32_t sine = Numerics::g_sinCosLUT[yaw] >> 2;
			cosineValue = *cosine >> 2;
			buzz->velocity.lateral = (lateralSpeed * cosineValue + forwardSpeed * sine) / 0x1000;
			buzz->velocity.forward = (forwardSpeed * cosineValue - lateralSpeed * sine) / 0x1000;
		}

		// FUNCTION: TOY2 0x00434550 [PROVISIONAL]
		void ResolveFooting(Toy2BuzzActor* buzz)
		{
			int32_t previousLateralVelocity = buzz->velocity.lateral;
			int32_t previousGravityVelocity = buzz->velocity.vertical;
			int32_t previousForwardVelocity = buzz->velocity.forward;

			HandleCollisions(buzz, &buzz->velocity, &buzz->collisionState, 0);
			buzz->floorYPos = UpdateFloorHeight(buzz);
			buzz->isOnWalkableFloor = Nu3D::Collision::IsFloorWalkable();

			if (g_pendingFootingType != -1)
			{
				g_footingType = g_pendingFootingType;
			}
			else
			{
				int32_t previousFootingType = g_footingType;
				g_footingType = Nu3D::Collision::GetSurfaceQuality(0);
				if (buzz->collisionFlags == 0 && buzz->specialAirState != 0)
					g_footingType = previousFootingType;
			}
			g_pendingFootingType = -1;

			if (g_footingType >= 0 && (int16_t)g_levelTransition == 0)
			{
				int32_t surfaceGroup = g_footingType >> 2;
				switch (surfaceGroup)
				{
					case SURFACE_DAMAGE_GROUP:
						if (buzz->cosmicShieldTimer == 0 && buzz->stunTimer == 0)
							HandleDamage(0, DAMAGE_NORMAL);
						break;

					case SURFACE_DEATH_GROUP:
						HandleDamage(0, DAMAGE_FORCE_DEATH);
						buzz->posAngles.pos.x = buzz->motionTargetPos.x + previousLateralVelocity;
						buzz->posAngles.pos.y = buzz->motionTargetPos.y + previousGravityVelocity;
						buzz->posAngles.pos.z = buzz->motionTargetPos.z + previousForwardVelocity;
						buzz->velocity.lateral = previousLateralVelocity;
						buzz->velocity.vertical = previousGravityVelocity;
						buzz->velocity.forward = previousForwardVelocity;
						buzz->collisionFlags = 0;
						buzz->specialAirState = 0;
						break;
				}
			}

			if (g_slipperySurfaceState == 0)
			{
				if (g_footingType == SLIPPERY_SURFACE_QUALITY && buzz->specialAirState != 0)
					g_slipperySurfaceState = 2;
			}
			else if (g_footingType != SLIPPERY_SURFACE_QUALITY && buzz->specialAirState != 0)
			{
				g_slipperySurfaceState = 0;
			}

			if (abs(buzz->velocity.vertical) < 2)
				buzz->velocity.vertical = 0;
		}

		// FUNCTION: TOY2 0x004346C0 [MATCHED]
		int32_t UpdateFacing(Toy2BuzzActor* buzz, MovementRates* movementRates)
		{
			int32_t forwardInput = 0;
			if ((InputManager::g_directionInputState & (INPUT_UP | INPUT_RIGHT | INPUT_DOWN | INPUT_LEFT)) != 0)
			{
				if (g_facingInterpolationTimer != -1)
				{
					if (g_facingInterpolationTimer == 0)
					{
						g_targetFacingAngle = (int16_t)buzz->posAngles.angles.yaw;
					}
					else if (g_facingInterpolationTimer >= 0x10)
					{
						goto update_input_facing;
					}

					g_facingInterpolationTimer += Renderer::g_frameDelta;
					if (g_facingInterpolationTimer >= 0x10)
						g_facingInterpolationTimer = 0x10;
				}

			update_input_facing:
				if (Camera::g_forwardInputDisabled == 0)
					forwardInput = 1;

				movementRates->lateralSpeedLimit =
					::Camera::CalculateMaxTurnAngle(InputManager::g_directionInputState) * movementRates->lateralSpeedLimit >> 14;
			}
			else
			{
				if (g_facingInterpolationTimer > 0 && g_facingInterpolationTimer < 0x10 && buzz->specialAirState != 0)
				{
					int32_t angleDelta = (g_targetFacingAngle - (uint16_t)buzz->facingAngle) & 0xFFF;
					if (angleDelta > 0x800)
						angleDelta -= 0xFFF;

					if ((g_gameplayStateFlags & GAMEPLAY_STATE_CUTSCENE_ACTIVE) == 0)
					{
						buzz->facingAngle = (int16_t)(g_targetFacingAngle - g_facingInterpolationTimer * angleDelta / 0x10) & 0xFFF;
					}
					g_facingInterpolationTimer = -1;
				}

				if (g_buzzActor.forwardSpeed < 0x200 && abs(g_buzzActor.lateralSpeed) < 0x200 && buzz->specialAirState != 0)
					g_facingInterpolationTimer = 0;
			}

			int16_t facingAngle = buzz->facingAngle;
			uint16_t yawAngle = buzz->posAngles.angles.yaw;
			int32_t angleDelta = (yawAngle - facingAngle) & 0xFFF;
			if (angleDelta > 0x800)
			{
				int32_t turnAmount = 0x1000 - angleDelta;
				if (turnAmount > FACING_SNAP_THRESHOLD && g_slipperySurfaceState == 0)
				{
					buzz->posAngles.angles.yaw = (int16_t)facingAngle;
					g_targetFacingAngle = facingAngle;
					if ((g_actionStateFlags & TURN_RECOVERY_ACTION_MASK) == 1)
					{
						g_turnRecoveryTimer = 0x1A;
						buzz->previousAnimationState = -1;
						AudioManager::PlaySoundEffect(0x12, &buzz->posAngles.pos);
					}
				}
				else
				{
					if (turnAmount > movementRates->turnRateLimit)
						turnAmount = movementRates->turnRateLimit;
					buzz->posAngles.angles.yaw = (int16_t)(yawAngle + Renderer::g_frameDelta * turnAmount / 8);
				}
			}
			else if (angleDelta > FACING_SNAP_THRESHOLD && g_slipperySurfaceState == 0)
			{
				buzz->posAngles.angles.yaw = (int16_t)facingAngle;
				g_targetFacingAngle = facingAngle;
				if ((g_actionStateFlags & TURN_RECOVERY_ACTION_MASK) == 1)
				{
					g_turnRecoveryTimer = 0x1A;
					buzz->previousAnimationState = -1;
					AudioManager::PlaySoundEffect(0x12, &buzz->posAngles.pos);
				}
			}
			else
			{
				if (angleDelta > movementRates->turnRateLimit)
					angleDelta = movementRates->turnRateLimit;
				buzz->posAngles.angles.yaw = (int16_t)(yawAngle - Renderer::g_frameDelta * angleDelta / 8);
			}

			buzz->posAngles.angles.yaw &= 0xFFF;
			if (g_forcedFacingActive != 0)
			{
				if (((g_forcedFacingAngle - (uint16_t)buzz->facingAngle + 0x400) & 0xFFF) > 0x800)
				{
					g_forcedFacingActive = 0;
					return forwardInput;
				}
				buzz->posAngles.angles.yaw = (int16_t)g_forcedFacingAngle;
			}
			return forwardInput;
		}

		// FUNCTION: TOY2 0x00434990 [PROVISIONAL]
		void TickGunFire(Toy2BuzzActor* buzz)
		{
			if ((g_actionStateFlags & GUN_IDLE_BLOCKING_ACTIONS) != 0)
			{
				g_gunFireTimer = 0;
				g_gunChargeTimer = 0;
			}
			else if (g_gunFireTimer == 0)
			{
				g_gunChargeTimer = 0;
			}

			int32_t fireRequested = 0;
			int32_t laserMode = 0;
			bool fireHeld = (InputManager::g_directionInputState & INPUT_FIRE) != 0;
			bool firePressed = fireHeld && (InputManager::g_prevDirectionInputState & INPUT_FIRE) == 0;

			if (firePressed && (g_actionStateFlags & GUN_START_BLOCKING_ACTIONS) == 0)
			{
				g_actionStateFlags |= ACTION_STATE_GUN_FIRE;
				g_gunChargeTimer = 0;
				g_spinHoverTimer = 0;
				g_spinCooldownTimer = 0;
				g_gunFireTimer = 0;
				g_gunFireTimer += Renderer::g_frameDelta;
				if (g_gunFireTimer < 12)
					return;
				fireRequested = 1;
				laserMode = 1;
			}
			else
			{
				if (g_gunFireTimer == 0)
					return;
				if (g_gunFireTimer < 12)
				{
					if (g_gunFireTimer == -1)
						g_gunFireTimer = 0;
					g_gunFireTimer += Renderer::g_frameDelta;
					if (g_gunFireTimer < 12)
						return;
					fireRequested = 1;
					laserMode = 1;
				}
				else
				{
					g_gunFireTimer += Renderer::g_frameDelta;
					if (fireHeld)
					{
						g_gunChargeTimer += Renderer::g_frameDelta;
						if (g_discLauncherAmmo != 0)
						{
							if (g_gunChargeTimer >= 14)
							{
								fireRequested = 1;
								laserMode = 1;
								g_gunChargeTimer = 0;
							}
						}
						else if (g_poweredLaserCharge != 0)
						{
							if (g_gunChargeTimer >= 6)
							{
								fireRequested = 1;
								laserMode = 1;
								g_gunChargeTimer = 0;
							}
						}
						else
						{
							if (g_gunChargeTimer > 63)
								g_gunChargeTimer = 64;
							if ((InputManager::g_prevDirectionInputState & INPUT_FIRE) == 0 && (g_actionStateFlags & GUN_REPEAT_BLOCKING_ACTIONS) == 0)
							{
								if (g_gunFireTimer < 52)
								{
									fireRequested = 1;
									laserMode = 1;
								}
								else
								{
									g_gunFireTimer = 64 - g_gunFireTimer;
								}
							}
							else if (g_gunFireTimer > 51)
							{
								g_gunFireTimer -= 40;
							}

							if (g_gunChargeTimer > 12)
							{
								AudioManager::g_dynamicSoundFrequencies[3] = (int16_t)(g_gunChargeTimer * 0x50 + 0x800);
								AudioManager::PlaySoundEffect(0x27, &buzz->posAngles.pos);
							}
						}

						if (g_poweredLaserCharge != 0 || g_discLauncherAmmo != 0)
						{
							if (g_gunFireTimer > 51)
								g_gunFireTimer -= 40;
						}
					}
					else
					{
						if (g_gunChargeTimer > 36)
							g_gunFireTimer = 52;
						if (g_gunChargeTimer == 64)
						{
							fireRequested = 2;
							laserMode = 2;
						}
						g_gunChargeTimer = 0;
						if (g_poweredLaserCharge != 0)
							g_gunChargeTimer = 6;
						if (g_discLauncherAmmo != 0)
							g_gunChargeTimer = 14;
					}

					if (g_gunFireTimer >= 64)
						g_gunFireTimer = 0;
					if (fireRequested == 0)
						return;
				}
			}

			if (fireRequested == 1 && g_poweredLaserCharge != 0)
			{
				g_poweredLaserCharge -= 10;
				laserMode = 3;
				if (g_poweredLaserCharge < 0)
					g_poweredLaserCharge = 0;
			}

			g_gunFireTimer = 12;
			int32_t aimPitch;
			int32_t aimYaw;
			int32_t autoAim;
			if (Camera::g_scriptedCameraState == CAMERA_STATE_VISOR)
			{
				aimPitch = -(int16_t)Camera::g_gameplayCamera.target.view.visorAimAngles.pitch;
				aimYaw = (int16_t)Camera::g_gameplayCamera.target.view.visorAimAngles.yaw;
				autoAim = 0;
			}
			else
			{
				aimPitch = 0;
				aimYaw = (int16_t)buzz->posAngles.angles.yaw;
				autoAim = 1;
			}

			Vector3I origin = buzz->posAngles.pos;
			origin.y -= 0x2C00;
			Vector3I aimOffset = { g_aimTargetPosition.x - origin.x, g_aimTargetPosition.y - origin.y, g_aimTargetPosition.z - origin.z };

			if (g_grappleCharges != 0 && Camera::g_scriptedCameraState >= CAMERA_STATE_TARGETING && g_aimTargetLocked != 0 && g_aimTargetIndex >= 1000)
			{
				FireGrapple(aimYaw, aimPitch);
				return;
			}
			if (g_discLauncherAmmo != 0)
			{
				FireDiscLauncher(aimPitch);
				return;
			}

			AudioManager::PlaySoundEffect(laserMode == 1 ? 2 : 6, &g_buzzActor.posAngles.pos);
			BeamShot* shot = SpawnBeamShot(laserMode - 1, aimYaw, aimPitch, &origin, &aimOffset, 4, autoAim);
			if (shot != 0)
			{
				aimOffset.x = 0;
				aimOffset.y = 0;
				aimOffset.z = 0;
				uint8_t randomAngle = *g_randDatBufferPtr;
				g_randDatBufferPtr += 2;
				SpawnBeamShot(laserMode - 1, (aimYaw + 0x600 + randomAngle * 4) & 0xFFF, (randomAngle * 4 - 0x200) & 0xFFF, &shot->end, &aimOffset, 4, 0);
				AudioManager::PlaySoundEffect(7, &shot->end);
			}
		}

		// FUNCTION: TOY2 0x00434D20 [MATCHED]
		int32_t TickGroundSlam(Toy2BuzzActor* buzz)
		{
			if ((InputManager::g_directionInputState & INPUT_SPIN) != 0 && (InputManager::g_prevDirectionInputState & INPUT_SPIN) == 0
				&& (g_actionStateFlags & GROUND_SLAM_BLOCKING_ACTIONS) == 0 && g_spinHoverTimer == 0 && g_spinCooldownTimer == 0
				&& (buzz->airborneMode == 1 || buzz->airborneMode == 2 || buzz->animationState == GROUND_SLAM_ANIMATION_STATE))
			{
				g_groundSlamTimer = 1;
				AudioManager::PlaySoundEffect(0x17, &buzz->posAngles.pos);
			}

			int32_t groundSlamTime = g_groundSlamTimer;
			if (groundSlamTime != 0)
			{
				g_actionStateFlags |= ACTION_STATE_GROUND_SLAM;
				if (groundSlamTime > 0)
				{
					if (buzz->collisionFlags != 0)
					{
						g_groundSlamTimer = -40;
						Camera::g_shakeTimer = 40;
						Nu3D::Particles::SpawnFromPreset(buzz->posAngles.pos.x, buzz->posAngles.pos.y - 0x400, buzz->posAngles.pos.z, 0x12, 0xB);
						Nu3D::Particles::SpawnFromPreset(buzz->posAngles.pos.x, buzz->posAngles.pos.y - 0x400, buzz->posAngles.pos.z, 0x13, 0xC);
						AudioManager::PlaySoundEffect(0xF, &g_buzzActor.posAngles.pos);
					}
					else
					{
						groundSlamTime += Renderer::g_frameDelta;
						g_groundSlamTimer = groundSlamTime;
						if (groundSlamTime > 14)
						{
							buzz->velocity.vertical = 0x800;
							g_groundSlamTimer = 14;
							return MOVEMENT_LOCK_LATERAL | MOVEMENT_LOCK_FORWARD;
						}
						return MOVEMENT_LOCK_LATERAL | MOVEMENT_LOCK_VERTICAL | MOVEMENT_LOCK_FORWARD;
					}
				}
				else
				{
					int32_t frameDelta = Renderer::g_frameDelta;
					groundSlamTime += frameDelta;
					g_groundSlamTimer = groundSlamTime;
					if (groundSlamTime > 0)
					{
						g_groundSlamTimer = 0;
						return 0;
					}
					if (groundSlamTime < -14)
					{
						if (buzz->collisionFlags == 0)
							buzz->velocity.vertical += (frameDelta * 0x100) / 4;
						if (buzz->velocity.vertical > 0x800)
							buzz->velocity.vertical = 0x800;
						return MOVEMENT_LOCK_LATERAL | MOVEMENT_LOCK_FORWARD;
					}
				}
			}
			return 0;
		}

		// FUNCTION: TOY2 0x00434EB0 [PROVISIONAL]
		void TickSpinHover(Toy2BuzzActor* buzz)
		{
			if (g_spinCooldownTimer > 0)
			{
				g_spinCooldownTimer -= Renderer::g_frameDelta;
				if (g_spinCooldownTimer < 0)
					g_spinCooldownTimer = 0;
				if (g_framePulseOutputs.twoTickCount != 0 && g_spinCooldownTimer > 20)
				{
					Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(
						buzz->posAngles.pos.x, buzz->posAngles.pos.y, buzz->posAngles.pos.z, (*g_randDatBufferPtr++ & 1) + 0x16, 2);
					particle->groundAlignRot = ((-g_spinCooldownTimer) & 0xF) << 7;
				}
			}

			if ((InputManager::g_directionInputState & INPUT_SPIN) != 0 && (InputManager::g_prevDirectionInputState & INPUT_SPIN) == 0
				&& (g_actionStateFlags & SPIN_START_BLOCKING_ACTIONS) == 1 && g_spinHoverTimer == 0 && g_spinCooldownTimer == 0 && g_turnRecoveryTimer == 0)
			{
				g_spinCooldownTimer = 48;
				if (g_gunFireTimer != 0)
				{
					g_gunFireTimer = 0;
					g_actionStateFlags &= CLEAR_ACTION_STATE_GUN_FIRE;
				}
				g_spinHoverTimer = 1;
				AudioManager::PlaySoundEffect(0x11, &buzz->posAngles.pos);
			}

			if ((InputManager::g_directionInputState & INPUT_SPIN) != 0 && (g_actionStateFlags & SPIN_CHARGE_BLOCKING_ACTIONS) == 0 && g_spinHoverTimer > 0)
			{
				g_spinHoverTimer += Renderer::g_frameDelta;
				if (g_spinHoverTimer > 60)
				{
					g_spinHoverTimer = 60;
				}
				else if (g_spinHoverTimer <= 12)
				{
					return;
				}
				AudioManager::g_dynamicSoundFrequencies[3] = (int16_t)(g_spinHoverTimer * 0x50 + 0x800);
				AudioManager::PlaySoundEffect(0x27, &buzz->posAngles.pos);
				return;
			}

			if (g_spinHoverTimer >= 0)
			{
				if (g_spinHoverTimer >= 60 && (g_actionStateFlags & SPIN_HOVER_BLOCKING_ACTIONS) == 0)
				{
					g_spinHoverTimer = -300;
					g_actionStateFlags |= ACTION_STATE_SPIN_HOVER;
					return;
				}
				g_spinHoverTimer = 0;
				return;
			}

			int32_t previousSpinHoverTime = g_spinHoverTimer;
			int32_t updatedSpinHoverTime = previousSpinHoverTime + Renderer::g_frameDelta;
			g_spinHoverTimer = updatedSpinHoverTime;
			if (updatedSpinHoverTime <= -120)
			{
				AudioManager::PlaySoundEffect(0x26, &buzz->posAngles.pos);
				if (g_framePulseOutputs.twoTickCount != 0)
				{
					Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(
						buzz->posAngles.pos.x, buzz->posAngles.pos.y - 0x3000, buzz->posAngles.pos.z, (*g_randDatBufferPtr++ & 1) + 0x14, 2);
					particle->groundAlignRot = ((-g_spinHoverTimer) & 0xF) * 0xC0;
				}
			}
			else
			{
				if (previousSpinHoverTime <= -120)
					AudioManager::PlaySoundEffect(0x18, &buzz->posAngles.pos);
				buzz->actorFlags |= ACTOR_FLAG_LOCK_FACING;
			}
			if (g_spinCancelRequested != 0 || g_spinHoverTimer >= 0)
			{
				g_spinHoverTimer = 0;
				buzz->actorFlags &= ~ACTOR_FLAG_LOCK_FACING;
			}
		}

		// FUNCTION: TOY2 0x00435100 [PROVISIONAL]
		int32_t HandleSwing(Toy2BuzzActor* buzz)
		{
			int32_t result = 0;
			Vector3I normalizedDirection;
			normalizedDirection.y = 0;
			if (Levels::g_recordData[60] == 0)
				return 0;

			if ((g_actionStateFlags & SWING_BLOCKING_ACTIONS) != 0 || g_damageRegistered != 0)
			{
				g_swingTimer = 0;
				g_actionStateFlags &= ~ACTION_STATE_SWING;
				return 0;
			}

			if (g_swingTimer == 0)
			{
				SwingRecord* swing = reinterpret_cast<SwingRecord*>(Levels::g_recordData[60]->data);
				for (int32_t swingIndex = 0; swingIndex < Levels::g_recordData[60]->recordCount / 2; swingIndex++, swing++)
				{
					int32_t heightAboveSwing = buzz->posAngles.pos.y - swing->start.y;
					if (heightAboveSwing <= 0 || heightAboveSwing >= 0x4000)
						continue;

					int32_t directionX = (swing->end.x - swing->start.x) >> 5;
					int32_t directionZ = (swing->end.z - swing->start.z) >> 5;
					normalizedDirection.x = directionX;
					normalizedDirection.z = directionZ;
					Nu3D::Math::NormalizeToFixedPoint(&normalizedDirection, &normalizedDirection);

					int32_t offsetX = (buzz->posAngles.pos.x - swing->start.x) >> 5;
					int32_t offsetZ = (buzz->posAngles.pos.z - swing->start.z) >> 5;
					int32_t distanceAlongSwing = (offsetZ * normalizedDirection.z + offsetX * normalizedDirection.x) >> 12;
					int32_t swingLength = (normalizedDirection.z * directionZ + normalizedDirection.x * directionX) >> 12;
					if (distanceAlongSwing <= 0 || distanceAlongSwing > swingLength)
						continue;

					offsetX -= (distanceAlongSwing * normalizedDirection.x) >> 12;
					offsetZ -= (distanceAlongSwing * normalizedDirection.z) >> 12;
					if (offsetZ * offsetZ + offsetX * offsetX >= 0x1000)
						continue;

					AudioManager::PlaySoundEffect(0x1B, &buzz->posAngles.pos);
					g_swingTimer = 0x45;
					buzz->animationState = SWING_START_ANIMATION_STATE;
					buzz->previousAnimationState = -1;
					g_swingAnchorPosition.x = swing->start.x + ((distanceAlongSwing * normalizedDirection.x) >> 7);
					g_swingAnchorPosition.y = swing->start.y + 0x4000;
					g_swingAnchorPosition.z = swing->start.z + ((distanceAlongSwing * normalizedDirection.z) >> 7);
					g_swingFacingAngle = (Nu3D::Math::CartesianToFixedAngle(normalizedDirection.x, normalizedDirection.z) + 0x400) & 0xFFF;
					if (((g_swingFacingAngle - buzz->posAngles.angles.yaw + 0x400) & 0xFFF) > 0x800)
						g_swingFacingAngle = (g_swingFacingAngle - 0x800) & 0xFFF;
					g_actionStateFlags |= ACTION_STATE_SWING;
				}

				if (g_swingTimer == 0)
					return 0;
			}

			g_swingTimer -= Renderer::g_frameDelta;
			if (g_swingTimer + Renderer::g_frameDelta > 15 && g_swingTimer <= 15)
			{
				g_jumpHeightControlActive = 0;
				buzz->velocity.vertical = -0x600;
				int32_t yaw = (int16_t)buzz->posAngles.angles.yaw;
				buzz->velocity.lateral = Numerics::g_sinCosLUT[yaw] >> 1;
				buzz->velocity.forward = Numerics::g_sinCosLUT[(yaw + 0x400) & 0xFFF] >> 1;
				if ((InputManager::g_directionInputState & INPUT_JUMP) == 0)
					buzz->airborneMode = 2;
				AudioManager::Preset::PlayOneShotSound2(0x19, buzz);
				result = 1;
			}

			if (g_swingTimer < 0)
			{
				if (buzz->animationState == SWING_START_ANIMATION_STATE)
					buzz->animationState = ANIMATION_STATE_LANDING;
				g_swingTimer = 0;
				return result;
			}
			if (g_swingTimer <= 15)
				return result;

			buzz->posAngles.pos.x -= (buzz->posAngles.pos.x - g_swingAnchorPosition.x) * Renderer::g_frameDelta / 16;
			buzz->posAngles.pos.y -= (buzz->posAngles.pos.y - g_swingAnchorPosition.y) * Renderer::g_frameDelta / 16;
			buzz->posAngles.pos.z -= (buzz->posAngles.pos.z - g_swingAnchorPosition.z) * Renderer::g_frameDelta / 16;
			buzz->velocity.lateral = 0;
			buzz->velocity.vertical = 0;
			buzz->velocity.forward = 0;

			int32_t facingDelta = (buzz->posAngles.angles.yaw - g_swingFacingAngle) & 0xFFF;
			if (facingDelta >= 0x800)
				facingDelta -= 0x1000;
			buzz->posAngles.angles.yaw -= Renderer::g_frameDelta * facingDelta / 16;
			Camera::g_gameplayCamera.roll = (Camera::g_gameplayCamera.roll - Renderer::g_frameDelta * facingDelta / 16) & 0xFFF;
			buzz->posAngles.angles.yaw &= 0xFFF;
			buzz->facingAngle = buzz->posAngles.angles.yaw;
			return MOVEMENT_LOCK_VERTICAL;
		}

		// FUNCTION: TOY2 0x004354E0 [PROVISIONAL]
		int32_t HandlePoleClimb(Toy2BuzzActor* buzz)
		{
			Levels::RecordData* records = Levels::g_recordData[61];
			if (records == 0 || g_damageRegistered != 0 || (g_actionStateFlags & POLE_CLIMB_BLOCKING_ACTIONS) != 0)
				return 0;
			PoleRecord* poles = reinterpret_cast<PoleRecord*>(records->data);

			PoleRecord* pole;
			int32_t poleBoundaryState = POLE_BOUNDARY_NONE;
			if (g_poleClimbState == 0)
			{
				pole = poles;
				int32_t poleIndex = 0;
				int32_t poleCount = records->recordCount >> 1;

				while (poleIndex < poleCount)
				{
					if (pole->type != POLE_TYPE_DISABLED)
					{
						int32_t distanceX = (buzz->posAngles.pos.x - pole->position.x) >> 8;
						int32_t distanceZ = (buzz->posAngles.pos.z - pole->position.z) >> 8;
						if (distanceX * distanceX + distanceZ * distanceZ < 0x200)
						{
							int32_t heightDelta = pole->position.y - buzz->posAngles.pos.y;
							if (heightDelta >= -0x1E00 && heightDelta <= pole->height - 0x3600)
								break;
						}
					}

					poleIndex++;
					pole++;
				}
				if (poleIndex >= poleCount)
					return 0;

				g_poleRecordOffset = poleIndex * 6;
				g_poleClimbState = POLE_CLIMB_ATTACHING;
				g_movementInputLockTimer = 10;
				buzz->posAngles.pos.x -= (buzz->posAngles.pos.x - pole->position.x) >> 2;
				buzz->posAngles.pos.z -= (buzz->posAngles.pos.z - pole->position.z) >> 2;
				buzz->velocity.vertical = 0;
				goto updatePolePosition;
			}

			pole = reinterpret_cast<PoleRecord*>(reinterpret_cast<int32_t*>(poles) + g_poleRecordOffset);
			if (g_poleClimbState < 0)
			{
				int32_t distanceX = (buzz->posAngles.pos.x - pole->position.x) >> 5;
				int32_t distanceZ = (buzz->posAngles.pos.z - pole->position.z) >> 5;
				if (distanceX * distanceX + distanceZ * distanceZ > 0x8400)
				{
					g_poleClimbState = 0;
					g_poleRecordOffset = 0;
					return 0;
				}
			}
			else if (g_poleClimbState > 0)
			{
				int32_t heightDelta = pole->position.y - buzz->posAngles.pos.y;
				if (heightDelta < -0x1E00)
				{
					g_poleClimbState = -1;
					buzz->airborneMode = 5;
					buzz->animationState = ANIMATION_STATE_LANDING;
					g_jumpHeightControlActive = 0;
					return MOVEMENT_LOCK_LATERAL;
				}

				if (heightDelta >= pole->height - 0x3600)
				{
					buzz->velocity.vertical = 0;
					buzz->posAngles.pos.y = pole->position.y - pole->height + 0x3600;
					poleBoundaryState = POLE_BOUNDARY_TOP;
				}
			}

			if (g_poleClimbState <= 0)
				return 0;

		updatePolePosition:
			pole = reinterpret_cast<PoleRecord*>(reinterpret_cast<int32_t*>(poles) + g_poleRecordOffset);
			buzz->posAngles.pos.x -= (buzz->posAngles.pos.x - pole->position.x) >> 2;
			buzz->posAngles.pos.z -= (buzz->posAngles.pos.z - pole->position.z) >> 2;
			buzz->velocity.lateral = 0;
			buzz->velocity.forward = 0;

			if (pole->type == POLE_TYPE_SLIDE)
			{
				g_poleClimbState = 1;
				if (buzz->specialAirState != 0)
				{
					g_poleClimbState = -1;
					return MOVEMENT_LOCK_LATERAL;
				}

				buzz->velocity.vertical += Renderer::g_frameDelta * 0x40 / 4;
				if (buzz->velocity.vertical > 0x800)
					buzz->velocity.vertical = 0x800;
				g_poleClimbState |= POLE_CLIMB_SLIDING;
			}
			else if (g_poleClimbState != POLE_CLIMB_ATTACHING || (InputManager::g_directionInputState & INPUT_DOWN) == 0)
			{
				g_poleClimbState = 1;
				if ((InputManager::g_directionInputState & INPUT_UP) != 0)
				{
					buzz->collisionFlags = 0;
					buzz->specialAirState = 0;
					if (poleBoundaryState != POLE_BOUNDARY_TOP)
					{
						buzz->velocity.vertical = -0x100;
						g_poleClimbState |= POLE_CLIMB_ASCENDING;
					}
				}
				else if ((InputManager::g_directionInputState & INPUT_DOWN) != 0)
				{
					if (buzz->specialAirState != 0)
					{
						g_poleClimbState = -1;
						return MOVEMENT_LOCK_LATERAL;
					}

					if (poleBoundaryState != POLE_BOUNDARY_BOTTOM)
					{
						buzz->velocity.vertical += Renderer::g_frameDelta * 0x80 / 4;
						if (buzz->velocity.vertical > 0x400)
							buzz->velocity.vertical = 0x400;
						buzz->posAngles.angles.yaw += buzz->velocity.vertical * Renderer::g_frameDelta / 16;
						g_poleClimbState |= POLE_CLIMB_SLIDING;
					}
				}
				else
				{
					if (buzz->velocity.vertical > 0)
						buzz->velocity.vertical -= Renderer::g_frameDelta * 0x100 / 4;
					if (buzz->velocity.vertical < 0)
						buzz->velocity.vertical = 0;
				}
			}

			if ((InputManager::g_directionInputState & INPUT_LEFT) != 0)
				buzz->posAngles.angles.yaw += Renderer::g_frameDelta * -0x40 / 2;
			if ((InputManager::g_directionInputState & INPUT_RIGHT) != 0)
				buzz->posAngles.angles.yaw += Renderer::g_frameDelta * 0x40 / 2;
			buzz->posAngles.angles.yaw &= 0xFFF;
			buzz->facingAngle = buzz->posAngles.angles.yaw;

			bool exitAtTop = false;
			if (poleBoundaryState == POLE_BOUNDARY_TOP && pole->type == POLE_TYPE_TOP_EXIT)
				exitAtTop = true;

			if ((InputManager::g_directionInputState & INPUT_JUMP) == 0 || (InputManager::g_prevDirectionInputState & INPUT_JUMP) != 0)
			{
				if (! exitAtTop)
					return MOVEMENT_LOCK_VERTICAL;
			}
			else if (! exitAtTop)
			{
				if (g_movementInputLockTimer != 0)
					return MOVEMENT_LOCK_VERTICAL;

				buzz->velocity.vertical = -0x400;
				if ((InputManager::g_directionInputState & (INPUT_UP | INPUT_RIGHT | INPUT_DOWN | INPUT_LEFT)) != 0)
				{
					::Camera::CalculateMaxTurnAngle(InputManager::g_directionInputState);
					buzz->posAngles.angles.yaw = buzz->facingAngle;
				}
				int32_t yaw = buzz->posAngles.angles.yaw;
				buzz->velocity.lateral = Numerics::g_sinCosLUT[yaw & 0xFFF] >> 1;
				buzz->velocity.forward = Numerics::g_sinCosLUT[(yaw + 0x400) & 0xFFF] >> 1;
				buzz->airborneMode = 1;
				g_jumpHeightControlActive = 0;
				AudioManager::Preset::PlayOneShotSound2(0x19, buzz);
			}
			else
			{
				buzz->actorFlags |= ACTOR_FLAG_PRESERVE_HORIZONTAL_MOMENTUM;
				buzz->velocity.vertical = -0x600;
				buzz->velocity.lateral = 0;
				buzz->velocity.forward = 0;
				buzz->airborneMode = 2;
				AudioManager::Preset::PlayOneShotSound2(0x19, buzz);
				g_jumpHeightControlActive = 0;
			}

			g_poleClimbState = -1;
			buzz->collisionFlags = 0;
			buzz->specialAirState = 0;
			buzz->animationState = ANIMATION_STATE_AIRBORNE;
			buzz->facingAngle = buzz->posAngles.angles.yaw;
			return MOVEMENT_LOCK_LATERAL;
		}

		// FUNCTION: TOY2 0x004359D0 [PROVISIONAL]
		int32_t HandleZipline(Toy2BuzzActor* buzz)
		{
			if (Levels::g_recordData[62] == 0 || g_damageRegistered != 0)
				return 0;
			ZiplineRecord* zipline = reinterpret_cast<ZiplineRecord*>(Levels::g_recordData[62]->data);

			if (g_ziplineCooldown > 0)
			{
				g_ziplineCooldown -= Renderer::g_frameDelta;
				if (g_ziplineCooldown < 0)
					g_ziplineCooldown = 0;
			}

			if ((g_actionStateFlags & LEDGE_CLIMB_BLOCKING_ACTIONS) == 0 && g_ziplineCooldown == 0)
			{
				int32_t ziplineIndex = 0;
				if (ziplineIndex < Levels::g_recordData[62]->recordCount / 2)
				{
					do
					{
						int32_t startX = zipline->start.x;
						int32_t directionX = zipline->end.x - startX;
						int32_t buzzX = buzz->posAngles.pos.x;
						if (directionX < 0)
						{
							if ((uint32_t)(buzzX - zipline->end.x + 0x800) > (uint32_t)(0x1000 - directionX))
								continue;
						}
						else if ((uint32_t)(buzzX - startX + 0x800) > (uint32_t)(directionX + 0x1000))
						{
							continue;
						}

						int32_t startZ = zipline->start.z;
						int32_t directionZ = zipline->end.z - startZ;
						int32_t buzzZ = buzz->posAngles.pos.z;
						if (directionZ < 0)
						{
							if ((uint32_t)(buzzZ - zipline->end.z + 0x800) > (uint32_t)(0x1000 - directionZ))
								continue;
						}
						else if ((uint32_t)(buzzZ - startZ + 0x800) > (uint32_t)(directionZ + 0x1000))
						{
							continue;
						}

						int32_t directionY = zipline->end.y - zipline->start.y;
						if (directionY < 0)
						{
							if ((uint32_t)(buzz->posAngles.pos.y - zipline->end.y + 0x1000) > (uint32_t)(0x4E00 - directionY))
								continue;
						}
						else if ((uint32_t)(buzz->posAngles.pos.y - zipline->start.y + 0x1000) > (uint32_t)(directionY + 0x4E00))
						{
							continue;
						}

						directionX >>= 5;
						directionY >>= 5;
						directionZ >>= 5;
						int32_t offsetX = (buzzX - startX) >> 5;
						int32_t offsetZ = (buzzZ - startZ) >> 5;
						int32_t dominantOffset;
						int32_t dominantDirection;
						if (abs(offsetX) < abs(offsetZ))
						{
							dominantOffset = offsetZ;
							dominantDirection = directionZ;
						}
						else
						{
							dominantOffset = offsetX;
							dominantDirection = directionX;
						}

						int32_t verticalOffset = dominantOffset * directionY / dominantDirection;
						int32_t projectedX = ((startX - buzzX) >> 5) + dominantOffset * directionX / dominantDirection;
						int32_t projectedZ = ((startZ - buzzZ) >> 5) + dominantOffset * directionZ / dominantDirection;
						if (projectedX * projectedX + projectedZ * projectedZ >= 0x2000)
							continue;

						int32_t heightDelta = (verticalOffset + 0x1F0) * 0x20 - buzz->posAngles.pos.y + zipline->start.y;
						if (heightDelta <= -0x1000 || heightDelta >= 0x3E00)
							continue;

						g_ziplineState = ZIPLINE_APPROACHING;
						g_ziplineRecordIndex = ziplineIndex * 2;
						g_ziplineTargetYOrSpeed = verticalOffset * 0x20 + zipline->start.y;
						g_ziplineDirection.x = directionX;
						g_ziplineDirection.y = directionY;
						g_ziplineDirection.z = directionZ;
						Nu3D::Math::NormalizeToFixedPoint(&g_ziplineDirection, &g_ziplineDirection);
						g_ziplineEndProgress = (int32_t)sqrt((double)(directionX * directionX + directionY * directionY + directionZ * directionZ));
						g_ziplineProgress = (int32_t)sqrt((double)(projectedX * projectedX + verticalOffset * verticalOffset + projectedZ * projectedZ));
						if (g_ziplineEndProgress - g_ziplineProgress >= 200)
							break;
						g_ziplineState = 0;
					} while (++zipline, ++ziplineIndex < Levels::g_recordData[62]->recordCount / 2);
				}
			}

			if (g_ziplineState == ZIPLINE_APPROACHING)
			{
				if (g_ziplineTargetYOrSpeed - buzz->posAngles.pos.y + 0x3E00 < 0)
				{
					g_ziplineState = ZIPLINE_RIDING;
					g_ziplineTargetYOrSpeed = 0;
					g_movementInputLockTimer = 10;
				}
				else
				{
					buzz->velocity.lateral = 0;
					buzz->velocity.forward = 0;
				}
			}

			if (g_ziplineState != ZIPLINE_RIDING)
				return 0;

			AudioManager::PlaySoundEffect(0x23, &buzz->posAngles.pos);
			zipline = reinterpret_cast<ZiplineRecord*>(&Levels::g_recordData[62]->data[g_ziplineRecordIndex]);
			g_ziplineTargetYOrSpeed += Renderer::g_frameDelta;
			if (g_ziplineTargetYOrSpeed > 0x30)
				g_ziplineTargetYOrSpeed = 0x30;

			buzz->posAngles.pos.x = (g_ziplineProgress * g_ziplineDirection.x >> 7) + zipline->start.x;
			buzz->posAngles.pos.y = (g_ziplineProgress * g_ziplineDirection.y >> 7) + 0x3E00 + zipline->start.y;
			buzz->posAngles.pos.z = (g_ziplineProgress * g_ziplineDirection.z >> 7) + zipline->start.z;
			buzz->velocity.lateral = 0;
			buzz->velocity.vertical = 0;
			buzz->velocity.forward = 0;

			int32_t ziplineAngle = Nu3D::Math::CartesianToFixedAngle(g_ziplineDirection.x, g_ziplineDirection.z);
			int32_t facingDelta = (buzz->posAngles.angles.yaw - ziplineAngle) & 0xFFF;
			if (facingDelta >= 0x800)
				facingDelta -= 0x1000;
			buzz->posAngles.angles.yaw -= (((facingDelta >> 3) * Renderer::g_frameDelta) / 2);
			buzz->posAngles.angles.yaw &= 0xFFF;
			buzz->facingAngle = buzz->posAngles.angles.yaw;

			g_ziplineProgress += g_ziplineTargetYOrSpeed * Renderer::g_frameDelta;
			if (g_ziplineProgress < g_ziplineEndProgress
				&& ((InputManager::g_directionInputState & INPUT_JUMP) == 0 || (InputManager::g_prevDirectionInputState & INPUT_JUMP) != 0
					|| g_movementInputLockTimer != 0))
			{
				return MOVEMENT_LOCK_VERTICAL;
			}

			g_ziplineState = 0;
			g_ziplineCooldown = 30;
			buzz->velocity.lateral = (Numerics::g_sinCosLUT[buzz->posAngles.angles.yaw & 0xFFF] >> 5) * g_ziplineTargetYOrSpeed;
			buzz->velocity.forward = (Numerics::g_sinCosLUT[(buzz->posAngles.angles.yaw + 0x400) & 0xFFF] >> 5) * g_ziplineTargetYOrSpeed;
			buzz->velocity.vertical = -0x5C0;
			buzz->airborneMode = 1;
			g_jumpHeightControlActive = 0;
			buzz->collisionFlags = 0;
			buzz->specialAirState = 0;
			buzz->animationState = ANIMATION_STATE_LANDING;
			if ((InputManager::g_directionInputState & INPUT_JUMP) != 0)
				AudioManager::Preset::PlayOneShotSound2(0x19, buzz);
			return MOVEMENT_LOCK_LATERAL;
		}

		// FUNCTION: TOY2 0x00435F30 [PROVISIONAL]
		int32_t HandleLedgeClimb(Toy2BuzzActor* buzz)
		{
			if (buzz->velocity.vertical > 0 && (g_actionStateFlags & LEDGE_CLIMB_BLOCKING_ACTIONS) == 0)
			{
				int32_t floorY = buzz->floorYPos;
				PosAndAngles groundProbe;
				groundProbe.pos.y = buzz->posAngles.pos.y;
				if (floorY - groundProbe.pos.y > 0x2000 && floorY != (int32_t)0x80000000)
				{
					groundProbe.pos.y -= 0x3600;
					int32_t forwardOffsetX;
					groundProbe.pos.x = buzz->posAngles.pos.x + (forwardOffsetX = Numerics::g_sinCosLUT[buzz->posAngles.angles.yaw & 0xFFF] / 3);
					int32_t forwardOffsetZ;
					groundProbe.pos.z = buzz->posAngles.pos.z + (forwardOffsetZ = Numerics::g_sinCosLUT[(buzz->posAngles.angles.yaw + 0x400) & 0xFFF] / 3);

					int32_t groundY = Nu3D::Collision::GetGroundHeight(&groundProbe, 0) - 200;
					if (Collision::g_groundNormal.y < -15000 && groundY < buzz->posAngles.pos.y - 0x3600 && groundY >= buzz->motionTargetPos.y - 0x3600)
					{
						PosAndAngles collisionPosition;
						collisionPosition.pos.x = buzz->posAngles.pos.x - forwardOffsetX / 4;
						collisionPosition.pos.y = buzz->posAngles.pos.y;
						collisionPosition.pos.z = buzz->posAngles.pos.z - forwardOffsetZ / 4;
						PosAndAngles collisionMovement;
						collisionMovement.pos.x = 0;
						collisionMovement.pos.y = groundY - buzz->posAngles.pos.y;
						collisionMovement.pos.z = 0;
						if (Collision::SweepAndSlide(&collisionPosition.pos, &collisionMovement.pos, 0x8000, 0, 0xFA0) == 0)
						{
							collisionPosition.pos.y = groundY - 6000;
							collisionMovement.pos.x = forwardOffsetX + forwardOffsetX / 3;
							collisionMovement.pos.y = 0;
							collisionMovement.pos.z = forwardOffsetZ + forwardOffsetZ / 3;
							if (Collision::SweepAndSlide(&collisionPosition.pos, &collisionMovement.pos, 0x8000, 0, 0xFA0) == 0)
							{
								AudioManager::PlaySoundEffect(0x17, &buzz->posAngles.pos);
								Portal::UpdateActiveSectorAt(buzz->posAngles.pos.x, groundY, buzz->posAngles.pos.z);
								buzz->posAngles.pos.x += forwardOffsetX;
								buzz->posAngles.pos.y = groundY;
								buzz->posAngles.pos.z += forwardOffsetZ;
								g_ledgeClimbTimer = 0x52;
								g_ledgeClimbPlatformIndex = Collision::GetGroundContactIdx();

								collisionPosition.pos.y = groundY;
								if (Collision::SweepAndSlide(&collisionPosition.pos, &collisionMovement.pos, 0x8000, 0, 0xFA0) != 0)
								{
									buzz->facingAngle =
										(Nu3D::Math::CartesianToFixedAngle(Collision::g_groundNormal.x, Collision::g_groundNormal.z) - 0x800) & 0xFFF;
								}
							}
						}
					}
				}
			}

			if (g_ledgeClimbTimer != 0)
			{
				int32_t facingDelta = (buzz->posAngles.angles.yaw - buzz->facingAngle) & 0xFFF;
				if (facingDelta > 0x800)
					facingDelta -= 0x1000;
				buzz->posAngles.angles.yaw -= Renderer::g_frameDelta * facingDelta / 16;
				buzz->posAngles.angles.yaw &= 0xFFF;
				Camera::g_gameplayCamera.roll = (Camera::g_gameplayCamera.roll - Renderer::g_frameDelta * facingDelta / 16) & 0xFFF;
				g_ledgeClimbTimer -= Renderer::g_frameDelta;
				if (g_ledgeClimbTimer < 0)
					g_ledgeClimbTimer = 0;
				return MOVEMENT_LOCK_LATERAL | MOVEMENT_LOCK_VERTICAL | MOVEMENT_LOCK_FORWARD;
			}

			g_ledgeClimbPlatformIndex = -1;
			return 0;
		}

		// FUNCTION: TOY2 0x00436220 [PROVISIONAL]
		void HandleGameplay(Toy2BuzzActor* buzz)
		{
			g_animationEventFlags = 0;
			RefreshDiscAmmo();

			MovementRates movementRates;
			movementRates.lateralSpeedLimit = 0x380;
			movementRates.forwardSpeedLimit = 0x400;
			movementRates.verticalAcceleration = -0x600;
			movementRates.turnRateLimit = 0x300;
			movementRates.forwardAcceleration = Renderer::g_frameDelta * 0xA0 / 4;
			movementRates.lateralDeceleration = Renderer::g_frameDelta * 0x200 / 4;
			movementRates.forwardDeceleration = Renderer::g_frameDelta * 0xC0 / 4;

			if (g_rocketBootsTimer != 0)
				movementRates.verticalAcceleration = -0x640;

			if (buzz->cosmicShieldTimer > 0)
			{
				movementRates.verticalAcceleration = -0x3C0;
				movementRates.lateralDeceleration = Renderer::g_frameDelta * 0x40 / 4;
				movementRates.forwardDeceleration = Renderer::g_frameDelta * 0x20 / 4;
				movementRates.forwardAcceleration = Renderer::g_frameDelta * 0x50 / 4;
			}

			buzz->actorFlags &= ~ACTOR_FLAG_BLOCK_EDGE_IDLE;
			if (g_environmentEffectType == 1)
			{
				if (buzz->posAngles.pos.y > g_environmentSurfaceY + 0x2000)
					buzz->actorFlags |= ACTOR_FLAG_BLOCK_EDGE_IDLE;
			}
			else if (g_environmentEffectType > 1 && g_environmentEffectType < 4 && buzz->posAngles.pos.y > g_environmentSurfaceY)
			{
				buzz->actorFlags |= ACTOR_FLAG_BLOCK_EDGE_IDLE;
			}

			if ((g_footingType & ~3) == 4)
			{
				movementRates.lateralDeceleration = Renderer::g_frameDelta * 0x40 / 4;
				movementRates.forwardDeceleration = Renderer::g_frameDelta * 0x20 / 4;
				movementRates.forwardAcceleration = Renderer::g_frameDelta * 0x50 / 4;
			}

			if (g_slipperySurfaceState != 0)
			{
				g_buzzActor.actorFlags &= ~ACTOR_FLAG_LOCK_FACING;
				movementRates.turnRateLimit = 0;
				movementRates.lateralDeceleration = Renderer::g_frameDelta * 0x10 / 4;

				uint32_t facingDelta = g_slipperySurfaceAngle - (int16_t)buzz->posAngles.angles.yaw;
				int32_t accelerationScale;
				if (((facingDelta + 0x400) & 0xFFF) < 0x801)
					accelerationScale = Renderer::g_frameDelta * 5;
				else
					accelerationScale = -Renderer::g_frameDelta;
				movementRates.forwardAcceleration = accelerationScale * 8 / 4;

				if ((InputManager::g_directionInputState & (INPUT_UP | INPUT_RIGHT | INPUT_DOWN | INPUT_LEFT)) == 0)
				{
					facingDelta &= 0xFFF;
					if (facingDelta > 0x800)
						facingDelta -= 0x1000;
					uint16_t facingAngle = ((int32_t)facingDelta >> 3) + buzz->posAngles.angles.yaw;
					facingAngle &= 0xFFF;
					buzz->posAngles.angles.yaw = facingAngle;
					buzz->facingAngle = facingAngle;
				}
				else
				{
					movementRates.turnRateLimit = 0x100;
				}

				movementRates.forwardDeceleration = movementRates.lateralDeceleration;
				if (buzz->specialAirState != 0)
					g_animationEventFlags |= ANIMATION_EVENT_MOVEMENT;
			}

			if (g_forcedFacingActive != 0)
				movementRates.lateralSpeedLimit = 0x180;

			buzz->surfaceClampY = (int32_t)0x80000000;
			if ((buzz->actorFlags & ACTOR_FLAG_BLOCK_EDGE_IDLE) != 0)
			{
				if (g_environmentEffectType == 1)
				{
					movementRates.lateralSpeedLimit = 0x280;
					movementRates.verticalAcceleration = -0x300;
					movementRates.lateralDeceleration = Renderer::g_frameDelta * 0x200 / 16;
					movementRates.forwardDeceleration = Renderer::g_frameDelta * 0x80 / 16;
					if (movementRates.forwardAcceleration >= 0)
						movementRates.forwardAcceleration = Renderer::g_frameDelta * 0xA0 / 16;
				}
				else if (g_environmentEffectType > 1)
				{
					movementRates.lateralSpeedLimit = 0x100;
					movementRates.verticalAcceleration = -0x300;
					movementRates.lateralDeceleration = Renderer::g_frameDelta * 0x40 / 4;
					movementRates.forwardDeceleration = Renderer::g_frameDelta * 0x20 / 4;
					movementRates.forwardAcceleration = Renderer::g_frameDelta * 0x50 / 4;
					buzz->surfaceClampY = g_environmentSurfaceY;
				}
			}

			g_actionStateFlags = buzz->specialAirState != 0;
			if (g_spinHoverTimer < 0)
				g_actionStateFlags |= ACTION_STATE_SPIN_HOVER;
			if (g_ledgeClimbTimer != 0)
				g_actionStateFlags |= ACTION_STATE_LEDGE_CLIMB;
			if (g_ziplineState != 0)
				g_actionStateFlags |= ACTION_STATE_ZIPLINE;
			if (g_poleClimbState > 0)
				g_actionStateFlags |= ACTION_STATE_POLE_CLIMB;
			if (g_airborneTimer < 0 || g_airborneTimer == 0x50)
				g_actionStateFlags |= ACTION_STATE_AIRBORNE_RECOVERY;
			if (g_groundSlamTimer != 0)
				g_actionStateFlags |= ACTION_STATE_GROUND_SLAM;
			if (g_gunFireTimer != 0)
				g_actionStateFlags |= ACTION_STATE_GUN_FIRE;
			if (Camera::g_scriptedCameraState != 0)
				g_actionStateFlags |= ACTION_STATE_CAMERA;
			if (g_forcedFacingActive != 0)
				g_actionStateFlags |= ACTION_STATE_FORCED_FACING;
			if (g_swingTimer > 15)
				g_actionStateFlags |= ACTION_STATE_SWING;
			if (buzz->cosmicShieldTimer > 0)
				g_actionStateFlags |= ACTION_STATE_COSMIC_SHIELD;
			if (g_rocketBootsTimer != 0)
				g_actionStateFlags |= ACTION_STATE_ROCKET_BOOTS;
			if (g_grappleState != 0)
				g_actionStateFlags |= ACTION_STATE_GRAPPLE;
			if (g_gravityBootsTimer != 0)
				g_actionStateFlags |= ACTION_STATE_GRAVITY_BOOTS;
			if (g_spinCancelRequested != 0)
				g_actionStateFlags |= ACTION_STATE_SPIN_CANCEL;
			if (g_slipperySurfaceState != 0)
				g_actionStateFlags |= ACTION_STATE_SLIPPERY_SURFACE;

			if (buzz->specialAirState == 0)
			{
				movementRates.forwardDeceleration = Renderer::g_frameDelta * 0x40 / 4;
			}
			else if (g_spinCancelRequested != 0)
			{
				g_spinCancelRequested = 0;
				g_actionStateFlags &= ~ACTION_STATE_SPIN_CANCEL;
			}

			if (g_turnRecoveryTimer > 0)
			{
				g_turnRecoveryTimer -= Renderer::g_frameDelta;
				if (g_turnRecoveryTimer < 0)
					g_turnRecoveryTimer = 0;
				if ((InputManager::g_directionInputState & (INPUT_UP | INPUT_RIGHT | INPUT_DOWN | INPUT_LEFT)) != 0)
				{
					movementRates.forwardDeceleration = Renderer::g_frameDelta * 0x10 / 4;
					movementRates.lateralDeceleration = Renderer::g_frameDelta * 0x40 / 4;
				}
			}

			if (buzz->stunTimer != 0)
			{
				if (buzz->collisionFlags != 0)
					buzz->actorFlags &= ~ACTOR_FLAG_DAMAGE_REACTION;
				if ((buzz->actorFlags & ACTOR_FLAG_DAMAGE_REACTION) != 0)
				{
					movementRates.lateralDeceleration = 0;
					movementRates.forwardDeceleration = 0;
				}

				if (buzz->stunTimer > 0)
				{
					buzz->stunTimer -= (int16_t)Renderer::g_frameDelta;
					if (buzz->stunTimer < 1)
					{
						buzz->stunTimer = 0;
						buzz->actorFlags &= ~ACTOR_FLAG_STUNNED;
					}
				}
				if (buzz->stunTimer < 0)
				{
					buzz->stunTimer += (int16_t)Renderer::g_frameDelta;
					if (buzz->stunTimer >= 0)
					{
						buzz->actorFlags &= ~ACTOR_FLAG_STUNNED;
						buzz->stunTimer = 0;
					}
				}
			}

			if ((buzz->actorFlags & ACTOR_FLAG_STUNNED) == 0)
			{
				buzz->actorFlags |= ACTOR_FLAG_VISIBLE;
			}
			else
			{
				g_damageBlinkCounter++;
				if (g_damageBlinkCounter < 3)
					buzz->actorFlags |= ACTOR_FLAG_VISIBLE;
				else
				{
					g_damageBlinkCounter = 0;
					buzz->actorFlags &= ~ACTOR_FLAG_VISIBLE;
				}
			}

			if ((buzz->actorFlags & ACTOR_FLAG_UNCONTROLLED_MOMENTUM) != 0)
			{
				if (buzz->collisionFlags == 0)
				{
					movementRates.lateralDeceleration = 0;
					movementRates.lateralSpeedLimit = 0xB80;
					movementRates.forwardSpeedLimit = 0xB80;
					movementRates.forwardDeceleration = 0;
				}
				else
				{
					buzz->actorFlags &= ~(ACTOR_FLAG_UNCONTROLLED_MOMENTUM | ACTOR_FLAG_LOCK_FACING);
				}
			}

			if (buzz->cosmicShieldTimer > 0)
			{
				buzz->cosmicShieldTimer -= (int16_t)Renderer::g_frameDelta;
				if (buzz->cosmicShieldTimer < 1)
				{
					RespawnCosmicShield();
					buzz->cosmicShieldTimer = 0;
				}
			}

			if (buzz->specialAirState != 0 || (buzz->stunTimer < 1 && buzz->health >= 0))
				g_damageRegistered = 0;

			g_movementInputLockTimer -= Renderer::g_frameDelta;
			if (g_movementInputLockTimer < 0)
				g_movementInputLockTimer = 0;

			if ((InputManager::g_directionInputState & INPUT_VISOR_TOGGLE) != 0 && (InputManager::g_prevDirectionInputState & INPUT_VISOR_TOGGLE) == 0
				&& (g_actionStateFlags & VISOR_START_ACTION_MASK) == ACTION_STATE_AIR_CONTROL)
			{
				Camera::g_scriptedCameraState = CAMERA_STATE_TARGETING;
				g_actionStateFlags |= ACTION_STATE_CAMERA;
				InputManager::g_prevDirectionInputState |= INPUT_VISOR_TOGGLE;
				buzz->velocity.lateral = 0;
				buzz->velocity.vertical = 0;
				buzz->velocity.forward = 0;
				g_spinHoverTimer = 0;
			}

			uint32_t cameraLock = Camera::g_scriptedCameraState != 0 ? MOVEMENT_UPDATE_WITHOUT_INPUT : 0;
			TickGunFire(buzz);
			uint32_t movementLocks = TickGroundSlam(buzz) | cameraLock;
			TickSpinHover(buzz);

			// Traversal handlers: the first active one owns this frame's movement.
			int32_t traversalState = HandleZipline(buzz);
			if (traversalState == TRAVERSAL_INACTIVE)
				traversalState = HandleSwing(buzz);
			if (traversalState == TRAVERSAL_INACTIVE)
				traversalState = HandlePoleClimb(buzz);

			if (traversalState == TRAVERSAL_INACTIVE)
			{
				movementLocks |= HandleLedgeClimb(buzz);
				if (g_airborneTimer < 0)
				{
					buzz->actorFlags |= ACTOR_FLAG_LOCK_FACING;
					g_airborneTimer += Renderer::g_frameDelta;
					if (g_airborneTimer >= 0)
					{
						g_airborneTimer = 0;
						buzz->actorFlags &= ~ACTOR_FLAG_LOCK_FACING;
					}
				}

				if (g_grappleState == GRAPPLE_PULLING)
					movementLocks = MOVEMENT_GRAPPLE_CONTROLLED;

				if (movementLocks != 0)
				{
					// Locked movement: clear the locked axes; apply gravity only when input is ignored.
					if ((movementLocks & MOVEMENT_LOCK_LATERAL) != 0)
						buzz->velocity.lateral = 0;
					if ((movementLocks & MOVEMENT_LOCK_VERTICAL) != 0)
						buzz->velocity.vertical = 0;
					if ((movementLocks & MOVEMENT_LOCK_FORWARD) != 0)
						buzz->velocity.forward = 0;
					if ((movementLocks & MOVEMENT_UPDATE_WITHOUT_INPUT) != 0)
					{
						UpdateJumpAndGravity(buzz, movementRates.verticalAcceleration, 0);
						traversalState = TRAVERSAL_UPDATE_WITHOUT_INPUT;
					}
					else
					{
						traversalState = TRAVERSAL_HANDLED;
					}
				}
				else
				{
					// Free movement: jump, gravity and facing from player input.
					if (g_movementLockTimer > 0)
					{
						g_movementLockTimer -= Renderer::g_frameDelta;
						buzz->velocity.lateral = 0;
						buzz->velocity.forward = 0;
					}
					if (g_gravityBootsTimer != 0)
						Camera::UpdateGravityBoots(buzz);
					else
						UpdateJumpAndGravity(buzz, movementRates.verticalAcceleration, 0);
					traversalState = TRAVERSAL_UPDATE_MOVEMENT;
				}
			}

			// Horizontal movement: facing input (or no input) plus slippery-surface drift.
			if (traversalState != TRAVERSAL_HANDLED)
			{
				int32_t forwardInput;
				if (traversalState == TRAVERSAL_UPDATE_WITHOUT_INPUT)
					forwardInput = 0;
				else if (g_rocketBootsTimer != 0)
					forwardInput = Camera::UpdateRocketBoots(buzz, &movementRates);
				else
					forwardInput = UpdateFacing(buzz, &movementRates);

				if (g_slipperySurfaceState != 0 && movementRates.forwardAcceleration != 0)
				{
					forwardInput = 1;
					if (movementRates.forwardAcceleration < 0)
					{
						forwardInput = -1;
						movementRates.forwardAcceleration = -movementRates.forwardAcceleration;
					}
				}
				UpdateHorizontalMovement(buzz, &movementRates, forwardInput);
			}

			buzz->motionTargetPos = buzz->posAngles.pos;
			g_previousVerticalVelocity = buzz->velocity.vertical;
			buzz->velocity.lateral *= Renderer::g_frameDelta;
			buzz->velocity.vertical *= Renderer::g_frameDelta;
			buzz->velocity.forward *= Renderer::g_frameDelta;
			MoveableObject::Update(buzz);
			ResolveFooting(buzz);
			buzz->velocity.lateral /= Renderer::g_frameDelta;
			buzz->velocity.vertical /= Renderer::g_frameDelta;
			buzz->velocity.forward /= Renderer::g_frameDelta;

			if (buzz->collisionFlags == 0)
			{
				if (buzz->specialAirState > 0)
				{
					buzz->specialAirState -= (int16_t)Renderer::g_frameDelta;
					if (buzz->specialAirState < 1)
						buzz->specialAirState = 0;
				}
			}
			else
			{
				buzz->specialAirState = 6;
			}

			Shadow::QueueStretchedForBuzz(buzz->posAngles.pos.x, buzz->floorYPos, buzz->posAngles.pos.z, 100);
			if (g_slipperySurfaceState != 0 && buzz->collisionFlags != 0 && abs(buzz->floorYPos - buzz->posAngles.pos.y) < 0x1000)
			{
				g_slipperySurfaceAngle = Nu3D::Math::CartesianToFixedAngle(Collision::g_groundNormal.x, Collision::g_groundNormal.z) & 0xFFF;
			}

			if ((g_actionStateFlags & AIRBORNE_TIMER_RESET_ACTIONS) != 0)
			{
				g_airborneTimer = 0;
			}
			else if (g_airborneTimer < 0)
			{
				goto check_world_bounds;
			}

			if (buzz->velocity.vertical < 0x81 || buzz->collisionFlags != 0)
			{
				if (buzz->specialAirState == 0)
				{
					if (g_airborneTimer != 0x50)
						g_airborneTimer = 0;
				}
				else
				{
					if (g_airborneTimer == 0x50)
					{
						g_animationEventFlags |= ANIMATION_EVENT_FORCE_EFFECT;
						g_airborneTimer = -0x46;
						AudioManager::PlaySoundEffect(0x2F, &buzz->posAngles.pos);
						buzz->velocity.lateral = 0;
						buzz->velocity.vertical = 0;
						buzz->velocity.forward = 0;
					}
					if (g_airborneTimer > 0)
					{
						g_airborneTimer = 0;
						g_animationEventFlags |= ANIMATION_EVENT_FORCE_EFFECT;
					}
				}
			}
			else
			{
				g_airborneTimer += Renderer::g_frameDelta;
				if (g_airborneTimer > 0x3C)
				{
					AudioManager::PlaySoundEffect(0x16, &buzz->posAngles.pos);
					g_buzzActor.actorFlags &= ~ACTOR_FLAG_LOCK_FACING;
					g_airborneTimer = 0x50;
					g_spinHoverTimer = 0;
					g_groundSlamTimer = 0;
					DeactivateRocketBoots();
				}
			}

		check_world_bounds:
			if (buzz->collisionFlags != 0)
				g_outOfBoundsLifeGranted = 0;
			if (buzz->posAngles.pos.y > Collision::g_collisionWorldMaxX + 0x2000 && g_levelTransition == 0)
			{
				if (g_outOfBoundsLifeGranted < 1)
				{
					g_outOfBoundsLifeGranted++;
					if (buzz->lives < 9)
						buzz->lives++;
				}
				HandleDamage(0, DAMAGE_FORCE_DEATH);
			}
		}

	}
}
