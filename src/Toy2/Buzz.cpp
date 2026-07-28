#include "Toy2/Buzz.h"
#include "Toy2/Camera.h"
#include "Toy2/Collision.h"
#include "Toy2/Toy2.h"
#include "AudioManager/AudioManager.h"
#include "InputManager.h"
#include "Nu3D/Link.h"
#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
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

		// STUB: TOY2 0x004A5170
		void TickCosmicShield() {}

		// STUB: TOY2 0x004A5540
		void TickGrapple() {}

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
