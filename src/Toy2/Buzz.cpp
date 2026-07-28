#include "Toy2/Buzz.h"
#include "Toy2/Actor.h"
#include "Toy2/Camera.h"
#include "Toy2/Collision.h"
#include "Toy2/Levels.h"
#include "Toy2/Toy2.h"
#include "AudioManager/AudioManager.h"
#include "InputManager.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
#include <stdlib.h>
#include <string.h>

namespace Toy2
{
	namespace EvilEmperorZurg
	{
		// FUNCTION: TOY2 0x0042B2D0 [MATCHED]
		void BuzzRespawn()
		{
			g_buzzActor.respawnPos.x = -0x1DA7C;
			g_buzzActor.respawnPos.y = -0x12BD0;
			g_buzzActor.respawnPos.z = 0xF699;
			g_buzzActor.respawnYawAngle = 0x400;
		}
	}

	// GLOBAL: TOY2 0x005281A4
	int32_t g_pendingFootingType;

	// GLOBAL: TOY2 0x0052B81C
	int32_t g_footingType;

	// GLOBAL: TOY2 0x0050A0A0
	Vector4I g_aimTargetPosition;

	// GLOBAL: TOY2 0x0050A4FC
	int32_t g_aimTargetIndex;

	// GLOBAL: TOY2 0x0053C5D4
	int32_t g_turnRecoveryTimer;

	// GLOBAL: TOY2 0x0053C5E0
	int32_t g_rocketBootsTimer;

	// GLOBAL: TOY2 0x0053C628
	int32_t g_environmentSurfaceY;

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

	// GLOBAL: TOY2 0x0053C648
	int32_t g_forcedFacingActive;

	// GLOBAL: TOY2 0x0053C64C
	int32_t g_groundSlamTimer;

	// GLOBAL: TOY2 0x0053C650
	int32_t g_spinCooldownTimer;

	// GLOBAL: TOY2 0x0053C660
	int32_t g_ledgeClimbTimer;

	// GLOBAL: TOY2 0x0053C668
	int32_t g_poleClimbState;

	// GLOBAL: TOY2 0x0053C66C
	int32_t g_swingTimer;

	// GLOBAL: TOY2 0x0053C838
	int32_t g_airborneTimer;

	// GLOBAL: TOY2 0x0053C83C
	int32_t g_spinHoverTimer;

	// GLOBAL: TOY2 0x0053C840
	int32_t g_gunChargeTimer;

	// GLOBAL: TOY2 0x00882924
	Buzz::GadgetPickup* g_activeRocketBootsPickup;

	// GLOBAL: TOY2 0x00882954
	int32_t g_savedRocketBootsPickupY;

	// GLOBAL: TOY2 0x00882934
	Buzz::GadgetPickup* g_activeCosmicShieldPickup;

	// GLOBAL: TOY2 0x00882958
	int32_t g_savedCosmicShieldPickupY;

	// GLOBAL: TOY2 0x00882964
	int32_t g_discLauncherAmmo;

	// GLOBAL: TOY2 0x00882968
	int32_t g_discLauncherShotSlotsAvailable;

	// GLOBAL: TOY2 0x00882970
	Buzz::BeamShot g_beamShots[4];

	// GLOBAL: TOY2 0x0088292C
	int32_t g_grappleTraversalDuration;

	// GLOBAL: TOY2 0x00882930
	int32_t g_grappleElapsedTime;

	// GLOBAL: TOY2 0x00882940
	Vector3I g_grappleEndpoint;

	// GLOBAL: TOY2 0x0088295C
	int32_t g_cosmicShieldYaw;

	// GLOBAL: TOY2 0x00882960
	int32_t g_cosmicShieldRoll;

	static __inline void RestoreCosmicShieldPickup()
	{
		if (g_activeCosmicShieldPickup != 0)
		{
			g_activeCosmicShieldPickup->position.y = g_savedCosmicShieldPickupY;
			Nu3D::Link::SetScaleFromFixedOffsets(g_activeCosmicShieldPickup->linkId, 0x2000, 0x2000, 0x2000);
			Nu3D::Link::SetPositionRawAndCommit(g_activeCosmicShieldPickup->linkId,
				g_activeCosmicShieldPickup->position.x,
				g_activeCosmicShieldPickup->position.y,
				g_activeCosmicShieldPickup->position.z);
			g_activeCosmicShieldPickup = 0;
		}
	}

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

	// FUNCTION: TOY2 0x004A48B0 [MATCHED]
	void ResetGadgets()
	{
		memset(g_beamShots, 0, sizeof(g_beamShots));
		g_activeCosmicShieldPickup = 0;
		g_savedCosmicShieldPickupY = 0;
		g_cosmicShieldYaw = 0;
		g_cosmicShieldRoll = 0;
		g_activeRocketBootsPickup = 0;
		g_rocketBootsTimer = 0;
		g_grappleCharges = 0;
		g_grappleState = 0;
		g_grappleTraversalDuration = 0;
		g_grappleElapsedTime = 0;
		g_discLauncherAmmo = 0;
		g_discLauncherShotSlotsAvailable = 6;
		g_gravityBootsTimer = 0;
	}

	// FUNCTION: TOY2 0x004A4B70 [MATCHED]
	void ActivateGravityBoots()
	{
		ResetBuzzState();
		g_gravityBootsTimer = 600;
		g_gravityBootsHoverHeight = 0x2000;
	}

	// FUNCTION: TOY2 0x004A5070 [MATCHED]
	void RespawnCosmicShield() { RestoreCosmicShieldPickup(); }

	namespace Buzz
	{
		union GrappleBeamVector
		{
			Vector3I direction;
			Vector4I beam;
		};
		STATIC_ASSERT(sizeof(GrappleBeamVector) == 0x10);

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

		// FUNCTION: TOY2 0x00434090 [MATCHED]
		void Launch(int32_t verticalVelocity, int16_t airborneMode)
		{
			g_buzzActor.airborneMode = airborneMode;
			g_buzzActor.collisionFlags = 0;
			g_buzzActor.specialAirState = 0;
			g_buzzActor.animationState = 2;
			g_buzzActor.gravityVel = verticalVelocity;
			g_forcedFacingActive = 0;
			g_turnRecoveryTimer = 0;
			g_jumpHeightControlActive = 0;
		}

		// FUNCTION: TOY2 0x004343D0
		void UpdateHorizontalMovement(Toy2BuzzActor* buzz, MovementRates* movementRates, int32_t forwardInput)
		{
			int32_t forwardSpeed;
			int32_t lateralSpeed;
			{
				int32_t yaw = buzz->posAngles.angles.yaw;
				int32_t backwardSine = Numerics::g_sinCosLUT[(yaw - 0x800) & 0xFFF] >> 2;
				int32_t cosine = Numerics::g_sinCosLUT[(yaw + 0x400) & 0xFFF] >> 2;
				lateralSpeed = (buzz->velForward * backwardSine + buzz->velX * cosine) / 0x1000;
				forwardSpeed = (buzz->velForward * cosine - buzz->velX * backwardSine) / 0x1000;
			}

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
			int32_t yaw = buzz->posAngles.angles.yaw;
			int32_t sine = Numerics::g_sinCosLUT[yaw] >> 2;
			int32_t cosine = Numerics::g_sinCosLUT[(yaw + 0x400) & 0xFFF] >> 2;
			buzz->velX = (lateralSpeed * cosine + forwardSpeed * sine) / 0x1000;
			buzz->velForward = (forwardSpeed * cosine - lateralSpeed * sine) / 0x1000;
		}

		static __inline void StopRocketBoots()
		{
			if (g_rocketBootsTimer != 0)
			{
				g_rocketBootsTimer = 0;
				for (int32_t particleIndex = 0; particleIndex < 64; particleIndex++)
				{
					Nu3D::Particles::ParticleInstance& particle = Nu3D::Particles::g_particleInstances[particleIndex];
					if (particle.typeId == 0x2F)
					{
						particle.lifetime = 1;
					}
				}

				Nu3D::Link::SetScaleFromFixedOffsets(g_activeRocketBootsPickup->linkId, 0x1000, 0x1000, 0x1000);
				g_activeRocketBootsPickup->position.y = g_savedRocketBootsPickupY;
			}
		}

		// FUNCTION: TOY2 0x00414110
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
			g_buzzActor.velX = 0;
			g_buzzActor.gravityVel = 0;
			g_buzzActor.velForward = 0;
			g_buzzActor.forwardSpeed = 0;
			g_buzzActor.lateralSpeed = 0;
			g_buzzActor.movementState = 0;
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

		// FUNCTION: TOY2 0x004A4910 [MATCHED]
		void RefreshDiscAmmo()
		{
			int32_t availableShots = 6;
			g_discLauncherShotSlotsAvailable = availableShots;
			if (g_discLauncherAmmo != 0)
			{
				for (int32_t particleIndex = 0; particleIndex < 64; particleIndex++)
				{
					const Nu3D::Particles::ParticleInstance& particle = Nu3D::Particles::g_particleInstances[particleIndex];
					if ((particle.typeId == 0x47 || particle.typeId == 0x48) && particle.lifetime > 0)
					{
						availableShots--;
					}
				}
				g_discLauncherShotSlotsAvailable = availableShots;
				if (availableShots < 0)
				{
					g_discLauncherShotSlotsAvailable = 0;
				}
			}
		}

		// FUNCTION: TOY2 0x004A4960
		void FireDiscLauncher(int32_t launchPitch)
		{
			int32_t nearestDistanceSquared = 0x7FFFFFFF;
			if (g_discLauncherShotSlotsAvailable <= 0)
				return;

			Actor::Toy2Actor* targetActor;
			if (Camera::g_scriptedCameraState >= 3)
			{
				if (g_aimTargetIndex != -1 && g_aimTargetIndex < 1000)
				{
					targetActor = &Actor::g_creatureActors[g_aimTargetIndex];
					goto spawnHomingDisc;
				}
			}
			else
			{
				Actor::Toy2Actor** actorSlot = Actor::g_activeActors;
				Actor::Toy2Actor* actor = *actorSlot;
				while (actor != 0)
				{
					if (actor->creatureRam->defenseMode != 0 && actor->hitpoints >= 0
						&& (actor->actorFlags & (Actor::ACTOR_FLAG_TARGETABLE | Actor::ACTOR_FLAG_ACTIVE))
							== (Actor::ACTOR_FLAG_TARGETABLE | Actor::ACTOR_FLAG_ACTIVE))
					{
						int32_t deltaX = (actor->pos.x - g_buzzActor.posAngles.pos.x) >> 5;
						int32_t deltaY = (actor->pos.y - g_buzzActor.posAngles.pos.y) >> 5;
						int32_t deltaZ = (actor->pos.z - g_buzzActor.posAngles.pos.z) >> 5;
						int32_t distanceSquared = deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;
						if (distanceSquared < nearestDistanceSquared)
						{
							nearestDistanceSquared = distanceSquared;
							targetActor = actor;
						}
					}
					actorSlot++;
					actor = *actorSlot;
				}
				if (nearestDistanceSquared < 0x1000000)
					goto spawnHomingDisc;
			}

			Nu3D::Particles::SpawnInstance(g_aimTargetPosition.x,
				g_aimTargetPosition.y,
				g_aimTargetPosition.z,
				(Numerics::g_sinCosLUT[(int16_t)g_buzzActor.posAngles.angles.yaw & 0xFFF] * Numerics::g_sinCosLUT[(launchPitch + 0x400) & 0xFFF] >> 14) / 3,
				-Numerics::g_sinCosLUT[launchPitch & 0xFFF] / 3,
				(Numerics::g_sinCosLUT[((int16_t)g_buzzActor.posAngles.angles.yaw + 0x400) & 0xFFF] * Numerics::g_sinCosLUT[(launchPitch + 0x400) & 0xFFF]
					>> 14)
					/ 3,
				0,
				0,
				0,
				0x48);
			goto finishDiscShot;

		spawnHomingDisc: {
			Nu3D::Particles::ParticleInstance* disc = Nu3D::Particles::SpawnInstance(
				g_aimTargetPosition.x, g_aimTargetPosition.y, g_aimTargetPosition.z, 0, -2, 0, (int16_t)g_buzzActor.posAngles.angles.yaw << 2, 0, 0, 0x47);
			disc->targetActor = targetActor;
			disc->discPitchAngle = launchPitch & 0xFFF;
		}

		finishDiscShot:
			AudioManager::PlaySoundEffect(0x54, &g_buzzActor.posAngles.pos);
			g_discLauncherShotSlotsAvailable--;
			g_discLauncherAmmo--;
		}

		// FUNCTION: TOY2 0x004A4B90 [MATCHED]
		void ResetGravityBoots()
		{
			if (g_gravityBootsTimer != 0)
			{
				g_gravityBootsTimer = 0;
			}
		}

		// FUNCTION: TOY2 0x004A4D60 [MATCHED]
		void ActivateRocketBoots(GadgetPickup* pickup)
		{
			StopRocketBoots();
			g_activeRocketBootsPickup = pickup;
			g_savedRocketBootsPickupY = pickup->position.y;
			ResetBuzzState();
			g_rocketBootsTimer = 250;

			Nu3D::Particles::ParticleInstance* leftExhaust =
				Nu3D::Particles::SpawnFromPreset(g_buzzActor.posAngles.pos.x, g_buzzActor.posAngles.pos.y, g_buzzActor.posAngles.pos.z, 0x2F, 2);
			leftExhaust->rotSpeed = -32;
			leftExhaust->groundAlignRot = 128;
			leftExhaust->lifetime = (int16_t)g_rocketBootsTimer;

			Nu3D::Particles::ParticleInstance* rightExhaust =
				Nu3D::Particles::SpawnFromPreset(g_buzzActor.posAngles.pos.x, g_buzzActor.posAngles.pos.y, g_buzzActor.posAngles.pos.z, 0x2F, 2);
			rightExhaust->updateParam = 0x65;
			rightExhaust->rotSpeed = 32;
			rightExhaust->lifetime = (int16_t)g_rocketBootsTimer;

			AudioManager::PlaySoundEffect(0x4C, &g_buzzActor.posAngles.pos);
		}

		// FUNCTION: TOY2 0x004A4E60 [MATCHED]
		void DeactivateRocketBoots() { StopRocketBoots(); }

		// FUNCTION: TOY2 0x004A5340 [MATCHED]
		void CancelGrapple()
		{
			if (g_grappleState != 0)
			{
				if (g_grappleCharges > 0)
				{
					g_grappleCharges--;
				}
				g_grappleState = 0;
			}
		}

		// FUNCTION: TOY2 0x004A5370
		void FireGrapple(int32_t aimYaw, int32_t aimPitch)
		{
			AudioManager::PlaySoundEffect(0x51, &g_buzzActor.posAngles.pos);

			const Levels::RecordData* grappleRecords = Levels::g_recordData[59];
			const Vector3I& grappleAnchor = grappleRecords->data[g_aimTargetIndex - 1000];
			g_grappleEndpoint.x = g_buzzActor.posAngles.pos.x;
			g_grappleEndpoint.y = g_buzzActor.posAngles.pos.y - 0x2C00;
			g_grappleEndpoint.z = g_buzzActor.posAngles.pos.z;

			Vector3I grappleOffset;
			grappleOffset.x = grappleAnchor.x * 0x20 - g_grappleEndpoint.x;
			grappleOffset.y = (grappleAnchor.y - 0xC0) * 0x20 - g_grappleEndpoint.y;
			grappleOffset.z = grappleAnchor.z * 0x20 - g_grappleEndpoint.z;
			g_grappleState = Collision::SweepAndSlide(&g_grappleEndpoint, &grappleOffset, 0x8000, 0, 0x100) != 0 ? 3 : 1;

			g_grappleEndpoint.x += grappleOffset.x;
			g_grappleEndpoint.y += grappleOffset.y;
			g_grappleEndpoint.z += grappleOffset.z;

			grappleOffset.x = (g_grappleEndpoint.x - g_aimTargetPosition.x) >> 2;
			grappleOffset.y = (g_grappleEndpoint.y - g_aimTargetPosition.y) >> 2;
			grappleOffset.z = (g_grappleEndpoint.z - g_aimTargetPosition.z) >> 2;
			Nu3D::Math::NormalizeToFixedPoint(&grappleOffset, &grappleOffset);

			int32_t dominantComponent = grappleOffset.y >> 1;
			int32_t grappleDistance = g_grappleEndpoint.y - g_buzzActor.posAngles.pos.y + 0x1600;
			if (abs(dominantComponent) < abs(grappleOffset.x >> 1))
			{
				dominantComponent = grappleOffset.x >> 1;
				grappleDistance = g_grappleEndpoint.x - g_buzzActor.posAngles.pos.x;
			}
			if (abs(dominantComponent) <= abs(grappleOffset.z >> 1))
			{
				dominantComponent = grappleOffset.z >> 1;
				grappleDistance = g_grappleEndpoint.z - g_buzzActor.posAngles.pos.z;
			}
			if (dominantComponent == 0)
				dominantComponent = 1;

			g_buzzActor.actorFlags |= ACTOR_FLAG_LOCK_FACING;
			g_grappleElapsedTime = 0;
			g_grappleTraversalDuration = abs(grappleDistance / dominantComponent);
			InputManager::g_directionInputState &= INPUT_SECRET_MENU | INPUT_MENU | INPUT_CAMERA_LEFT | INPUT_CAMERA_RIGHT;
		}

		// FUNCTION: TOY2 0x004A5C40 [MATCHED]
		void TickBeamShots()
		{
			BeamShot* shot = g_beamShots;
			int32_t shotsRemaining = 4;
			do
			{
				if (shot->fadeTimer != 0)
				{
					Vector4I position;
					position.x = shot->end.x;
					position.y = shot->end.y;
					position.z = shot->end.z;
					Vector4I direction;
					direction.x = shot->start.x - position.x;
					direction.y = shot->start.y - position.y;
					direction.z = shot->start.z - position.z;

					int32_t brightness = shot->fadeTimer * 4;
					Renderer::Beam::QueueBeam(9,
						shot->color.a,
						400,
						&position,
						&direction,
						shot->color.b * brightness >> 7,
						shot->color.g * brightness >> 7,
						shot->color.r * brightness >> 7);

					shot->fadeTimer -= (int16_t)Renderer::g_frameDelta;
					if (shot->fadeTimer <= 0)
					{
						shot->fadeTimer = 0;
					}

					shot->movementTimer -= (int16_t)Renderer::g_frameDelta;
					if (shot->movementTimer <= 0)
					{
						shot->fadeTimer = 0;
					}
					else
					{
						shot->start.x += shot->velocity.x * Renderer::g_frameDelta;
						shot->start.y += shot->velocity.y * Renderer::g_frameDelta;
						shot->start.z += shot->velocity.z * Renderer::g_frameDelta;
					}
				}
				shot++;
				shotsRemaining--;
			} while (shotsRemaining != 0);
		}

		// FUNCTION: TOY2 0x004A5170
		void TickCosmicShield()
		{
			if (g_activeCosmicShieldPickup == 0)
				return;

			Vector3I shieldPosition;
			shieldPosition.x = (g_buzzActor.posAngles.pos.x - Camera::g_renderCameraTransform.pos.x) >> 5;
			shieldPosition.y = (g_buzzActor.posAngles.pos.y - Camera::g_renderCameraTransform.pos.y - 0x2000) >> 5;
			shieldPosition.z = (g_buzzActor.posAngles.pos.z - Camera::g_renderCameraTransform.pos.z) >> 5;
			Nu3D::Math::NormalizeToFixedPoint(&shieldPosition, &shieldPosition);
			shieldPosition.x = (g_buzzActor.posAngles.pos.x - shieldPosition.x) >> 5;
			shieldPosition.y = (g_buzzActor.posAngles.pos.y - shieldPosition.y - 0x2000) >> 5;
			shieldPosition.z = (g_buzzActor.posAngles.pos.z - shieldPosition.z) >> 5;

			Nu3D::Link::SetPositionRawAndCommit(g_activeCosmicShieldPickup->linkId, shieldPosition.x, shieldPosition.y, shieldPosition.z);
			Nu3D::Link::SetRotationRelative8bit(g_activeCosmicShieldPickup->linkId, 0, g_cosmicShieldYaw, g_cosmicShieldRoll);

			g_cosmicShieldYaw = -g_buzzActor.posAngles.angles.yaw & 0xFFF;
			g_cosmicShieldRoll = (g_cosmicShieldRoll - (g_buzzActor.forwardSpeed >> 4) * Renderer::g_frameDelta) & 0xFFF;

			int32_t playSound = 1;
			int32_t scale;
			if (g_buzzActor.cosmicShieldTimer < 0x80)
			{
				scale = (Numerics::g_sinCosLUT[g_buzzActor.cosmicShieldTimer * 8] >> 2) * 3;
				if (g_sixteenTickPhase < 8)
					playSound = 0;
			}
			else
			{
				scale = (Numerics::g_sinCosLUT[(g_buzzActor.cosmicShieldTimer & 0x3F) * 0x40] >> 4) + 0x3000;
			}

			Nu3D::Link::SetScaleFromFixedOffsets(g_activeCosmicShieldPickup->linkId, scale, scale, scale);
			if (playSound != 0)
			{
				AudioManager::g_dynamicSoundFrequencies[0] = (int16_t)((0x800 - g_buzzActor.cosmicShieldTimer) * 4);
				AudioManager::PlaySoundEffect(0x4D, &g_buzzActor.posAngles.pos);
			}

			Renderer::BlitTextureByIndexOffset(0x10, 0x80, 0xC0, 0x40, 0x40, g_sixteenTickPhase * 4, g_thirtyTwoTickPhase * 2, 0, -0x40);
		}

		// FUNCTION: TOY2 0x004A5540
		void TickGrapple()
		{
			if (g_grappleState == GRAPPLE_INACTIVE)
				return;

			g_airborneTimer = 0;
			GrappleBeamVector grappleVector;
			if ((g_grappleState & GRAPPLE_EXTENDING) != 0)
			{
				g_buzzActor.facingAngle =
					(int16_t)Nu3D::Math::CartesianToFixedAngle(g_grappleEndpoint.x - g_aimTargetPosition.x, g_grappleEndpoint.z - g_aimTargetPosition.z);
				g_grappleElapsedTime += Renderer::g_frameDelta * 2;

				if (g_grappleElapsedTime <= g_grappleTraversalDuration)
				{
					grappleVector.beam.x = (g_grappleEndpoint.x - g_aimTargetPosition.x) * g_grappleElapsedTime / g_grappleTraversalDuration;
					grappleVector.beam.y = (g_grappleEndpoint.y - g_aimTargetPosition.y) * g_grappleElapsedTime / g_grappleTraversalDuration;
					grappleVector.beam.z = (g_grappleEndpoint.z - g_aimTargetPosition.z) * g_grappleElapsedTime / g_grappleTraversalDuration;
					Renderer::Beam::QueueBeam(0x22, 0x1E, 400, &g_aimTargetPosition, &grappleVector.beam, 0x80, 0x60, 0x20);
				}
				else
				{
					AudioManager::PlaySoundEffect(0x52, &g_buzzActor.posAngles.pos);
					if (g_grappleState == GRAPPLE_BLOCKED)
					{
						if (g_grappleCharges > 0)
							g_grappleCharges--;
						g_buzzActor.actorFlags &= ~ACTOR_FLAG_LOCK_FACING;
						g_grappleState = GRAPPLE_INACTIVE;
						return;
					}

					g_grappleState = GRAPPLE_PULLING;
					g_grappleElapsedTime = 0;
					if (Camera::g_scriptedCameraState != 0)
						Camera::SnapBehindBuzz(&Camera::g_gameplayCamera);

					int16_t grappleSide = g_buzzActor.posAngles.angles.yaw & 0x200;
					if (grappleSide != 0)
						Camera::g_gameplayCamera.roll = (Camera::g_gameplayCamera.roll + 0x400) & 0xFFF;
					else
						Camera::g_gameplayCamera.roll = (Camera::g_gameplayCamera.roll - 0x400) & 0xFFF;
				}
			}

			if (g_grappleState != GRAPPLE_PULLING)
				return;

			if (g_grappleElapsedTime <= g_grappleTraversalDuration * 4 / 3 + 10)
			{
				AudioManager::PlaySoundEffect(0x53, &g_buzzActor.posAngles.pos);
				grappleVector.beam.x = g_grappleEndpoint.x - g_aimTargetPosition.x;
				grappleVector.beam.y = g_grappleEndpoint.y - g_aimTargetPosition.y;
				grappleVector.beam.z = g_grappleEndpoint.z - g_aimTargetPosition.z;
				Renderer::Beam::QueueBeam(0x22, 0x1E, 400, &g_aimTargetPosition, &grappleVector.beam, 0x80, 0x60, 0x20);

				grappleVector.direction.y = (g_grappleEndpoint.y - g_buzzActor.posAngles.pos.y + 0x1600) >> 3;
				grappleVector.direction.x = (g_grappleEndpoint.x - g_buzzActor.posAngles.pos.x) >> 3;
				grappleVector.direction.z = (g_grappleEndpoint.z - g_buzzActor.posAngles.pos.z) >> 3;
				Nu3D::Math::NormalizeToFixedPoint(&grappleVector.direction, &grappleVector.direction);
				if (g_grappleElapsedTime >= 10)
				{
					g_buzzActor.velX = grappleVector.direction.x / 3;
					g_buzzActor.gravityVel = grappleVector.direction.y / 3;
					g_buzzActor.velForward = grappleVector.direction.z / 3;
				}
				g_grappleElapsedTime += Renderer::g_frameDelta;
				return;
			}

			if (g_grappleCharges > 0)
				g_grappleCharges--;
			g_buzzActor.actorFlags &= ~ACTOR_FLAG_LOCK_FACING;
			g_grappleState = GRAPPLE_INACTIVE;
			g_buzzActor.posAngles.pos.x = g_grappleEndpoint.x;
			g_buzzActor.posAngles.pos.y = g_grappleEndpoint.y + 0x1600;
			g_buzzActor.posAngles.pos.z = g_grappleEndpoint.z;
			g_buzzActor.velX = 0;
			g_buzzActor.gravityVel = 0;
			g_buzzActor.velForward = 0;
		}

		// FUNCTION: TOY2 0x004A62A0
		void TickGadgets()
		{
			if (g_rocketBootsTimer != 0)
			{
				if (g_environmentSurfaceY != 0 && g_buzzActor.posAngles.pos.y > g_environmentSurfaceY && g_buzzActor.gravityVel > -0x300)
				{
					g_buzzActor.gravityVel -= Renderer::g_frameDelta * 0x80;
				}

				AudioManager::PlaySoundEffect(0x40, &g_buzzActor.posAngles.pos);
				g_rocketBootsTimer -= Renderer::g_frameDelta;
				if (g_rocketBootsTimer <= 0)
				{
					g_rocketBootsTimer = 0;
					for (int32_t particleIndex = 0; particleIndex < 64; particleIndex++)
					{
						Nu3D::Particles::ParticleInstance& particle = Nu3D::Particles::g_particleInstances[particleIndex];
						if (particle.typeId == 0x2F)
							particle.lifetime = 1;
					}

					Nu3D::Link::SetScaleFromFixedOffsets(g_activeRocketBootsPickup->linkId, 0x1000, 0x1000, 0x1000);
					g_activeRocketBootsPickup->position.y = g_savedRocketBootsPickupY;
				}
			}

			TickCosmicShield();
			TickBeamShots();
			TickGrapple();
		}
	}

	namespace Camera
	{
		// FUNCTION: TOY2 0x004A4F80
		int32_t UpdateRocketBoots(Buzz::Toy2BuzzActor* buzz, Buzz::MovementRates* movementRates)
		{
			movementRates->lateralSpeedLimit = 0x800;
			movementRates->forwardSpeedLimit = 0x800;

			movementRates->forwardAcceleration = Renderer::g_frameDelta * 0x280 / 4;
			movementRates->lateralDeceleration = Renderer::g_frameDelta * 0x200 / 4;
			movementRates->forwardDeceleration = Renderer::g_frameDelta * 0x80 / 4;

			if ((InputManager::g_directionInputState & (INPUT_UP | INPUT_RIGHT | INPUT_DOWN | INPUT_LEFT)) != 0)
			{
				::Camera::CalculateMaxTurnAngle(InputManager::g_directionInputState);
			}

			uint16_t yaw = buzz->posAngles.angles.yaw;
			int32_t yawDelta = (yaw - buzz->facingAngle) & 0xFFF;
			if (yawDelta >= 0x800)
			{
				int32_t turnAmount = 0x1000 - yawDelta;
				if (turnAmount > movementRates->turnRateLimit)
				{
					turnAmount = movementRates->turnRateLimit;
				}
				buzz->posAngles.angles.yaw = (uint16_t)(yaw + Renderer::g_frameDelta * turnAmount / 8);
			}
			else
			{
				int32_t turnAmount = yawDelta;
				if (turnAmount > movementRates->turnRateLimit)
				{
					turnAmount = movementRates->turnRateLimit;
				}
				buzz->posAngles.angles.yaw = (uint16_t)(yaw - Renderer::g_frameDelta * turnAmount / 8);
			}
			buzz->posAngles.angles.yaw &= 0xFFF;
			return 1;
		}
	}
}
