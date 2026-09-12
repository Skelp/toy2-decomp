#include "Toy2/Buzz.h"
#include "Toy2/Actor.h"
#include "Toy2/Animation.h"
#include "Toy2/Camera.h"
#include "Toy2/Collectables.h"
#include "Toy2/Collision.h"
#include "Toy2/Levels.h"
#include "Toy2/Toy2.h"
#include "AudioManager/AudioManager.h"
#include "InputManager.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Random.h"
#include <math.h>
#include <string.h>

// Buzz's gadgets: the disc launcher, the gravity boots, the rocket boots, the
// cosmic shield, the grapple and the beam shots. ResetGadgets clears the state
// of all of them and TickGadgets advances them once per frame.
//
// Retail band 0x004A48B0-0x004A62A0, with its data block 0x00882924-0x00882970.
// Collectables::CosmicShield (0x004A50D0) belongs to the band: it sits between
// RespawnCosmicShield and TickCosmicShield and writes the same pickup state.

namespace Toy2
{
	// Buzz state that other units of the character define.
	extern Vector4I g_aimTargetPosition;
	extern int32_t g_aimTargetIndex;
	extern int32_t g_airborneTimer;
	extern Nu3D::Particles::ParticleInstance* g_laserAimParticle;
}

namespace Toy2
{
	// GLOBAL: TOY2 0x00882924
	Buzz::GadgetPickup* g_activeRocketBootsPickup;

	// GLOBAL: TOY2 0x0088292C
	int32_t g_grappleTraversalDuration;

	// GLOBAL: TOY2 0x00882930
	int32_t g_grappleElapsedTime;

	// GLOBAL: TOY2 0x00882934
	Buzz::GadgetPickup* g_activeCosmicShieldPickup;

	// GLOBAL: TOY2 0x00882940
	Vector3I g_grappleEndpoint;

	// GLOBAL: TOY2 0x00882954
	int32_t g_savedRocketBootsPickupY;

	// GLOBAL: TOY2 0x00882958
	int32_t g_savedCosmicShieldPickupY;

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

	// GLOBAL: TOY2 0x00882964
	int32_t g_discLauncherAmmo;

	// GLOBAL: TOY2 0x00882968
	int32_t g_discLauncherShotSlotsAvailable;

	// GLOBAL: TOY2 0x00882970
	Buzz::BeamShot g_beamShots[4];

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

	namespace Buzz
	{
		union GrappleBeamVector
		{
			Vector3I direction;
			Vector4I beam;
		};
		STATIC_ASSERT(sizeof(GrappleBeamVector) == 0x10);

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

		// FUNCTION: TOY2 0x004A4910 [TOOL]
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

		// FUNCTION: TOY2 0x004A4960 [PROVISIONAL]
		void FireDiscLauncher(int32_t launchPitch)
		{
			int32_t nearestDistanceSquared = 0x7FFFFFFF;
			if (g_discLauncherShotSlotsAvailable <= 0)
				return;

			Actor::Toy2Actor* targetActor = 0;
			if (Camera::g_scriptedCameraState >= 3)
			{
				if (g_aimTargetIndex != -1 && g_aimTargetIndex < 1000)
				{
					targetActor = &Actor::g_creatureActors[g_aimTargetIndex];
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
				if (nearestDistanceSquared >= 0x1000000)
					targetActor = 0;
			}

			if (targetActor != 0)
			{
				Nu3D::Particles::ParticleInstance* disc = Nu3D::Particles::SpawnInstance(
					g_aimTargetPosition.x, g_aimTargetPosition.y, g_aimTargetPosition.z, 0, -2, 0, (int16_t)g_buzzActor.posAngles.angles.yaw << 2, 0, 0, 0x47);
				disc->targetActor = targetActor;
				disc->discPitchAngle = launchPitch & 0xFFF;
			}
			else
			{
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
			}

			AudioManager::PlaySoundEffect(0x54, &g_buzzActor.posAngles.pos);
			g_discLauncherShotSlotsAvailable--;
			g_discLauncherAmmo--;
		}

	}
	// FUNCTION: TOY2 0x004A4B70 [MATCHED]
	void ActivateGravityBoots()
	{
		ResetBuzzState();
		g_gravityBootsTimer = 600;
		g_gravityBootsHoverHeight = 0x2000;
	}

	namespace Buzz
	{
		// FUNCTION: TOY2 0x004A4B90 [MATCHED]
		void ResetGravityBoots()
		{
			if (g_gravityBootsTimer != 0)
			{
				g_gravityBootsTimer = 0;
			}
		}

	}
	namespace Camera
	{
		// FUNCTION: TOY2 0x004A4BB0 [PROVISIONAL]
		void UpdateGravityBoots(Buzz::Toy2BuzzActor* buzz)
		{
			g_airborneTimer = 0;
			buzz->collisionFlags = 0;

			if (buzz->posAngles.pos.y < buzz->floorYPos - g_gravityBootsHoverHeight)
			{
				buzz->velocity.vertical += Renderer::g_frameDelta * 0x20;
				if (buzz->velocity.vertical > 0x180)
					buzz->velocity.vertical = 0x180;
			}
			else
			{
				buzz->velocity.vertical -= Renderer::g_frameDelta * 0x20;
				if (buzz->velocity.vertical < -0x180)
					buzz->velocity.vertical = -0x180;
			}

			int32_t particleVariant;
			if ((InputManager::g_directionInputState & INPUT_JUMP) != 0)
			{
				g_gravityBootsHoverHeight += Renderer::g_frameDelta * 0x400;
				if (g_gravityBootsHoverHeight >= 0xC000)
					g_gravityBootsHoverHeight = 0xC000;
				particleVariant = 1;
			}
			else
			{
				particleVariant = 0;
				g_gravityBootsHoverHeight -= Renderer::g_frameDelta * 0x400;
				if (g_gravityBootsHoverHeight <= 0x2000)
					g_gravityBootsHoverHeight = 0x2000;
			}

			if (g_framePulseOutputs.fourTick != 0)
			{
				if (g_gravityBootsTimer < 120)
				{
					particleVariant = 2;
					if (g_framePulseOutputs.eightTick != 0)
						goto update_timer;
				}
				else if (g_framePulseOutputs.eightTick != 0 && particleVariant != 0)
				{
					particleVariant = 0;
				}
				else if (particleVariant == -1)
				{
					goto update_timer;
				}

				Nu3D::Particles::SpawnInstance(buzz->posAngles.pos.x,
					buzz->posAngles.pos.y,
					buzz->posAngles.pos.z,
					0,
					0,
					0,
					0,
					g_framePulsePhases.thirtyTwoTick << 7,
					0x80,
					particleVariant + 0x51);
				int32_t exhaustRotation = -g_framePulsePhases.thirtyTwoTick;
				exhaustRotation <<= 7;
				exhaustRotation &= 0xFFF;
				Nu3D::Particles::SpawnInstance(
					buzz->posAngles.pos.x, buzz->posAngles.pos.y, buzz->posAngles.pos.z, 0, 0, 0, 0, exhaustRotation, -0x80, particleVariant + 0x51);
			}

		update_timer:
			AudioManager::PlaySoundEffect(0x50, &g_buzzActor.posAngles.pos);
			g_gravityBootsTimer -= Renderer::g_frameDelta;
			if (g_gravityBootsTimer <= 0)
				g_gravityBootsTimer = 0;
		}

	}
	namespace Buzz
	{
		// FUNCTION: TOY2 0x004A4D60 [TOOL]
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

		// FUNCTION: TOY2 0x004A4E60 [TOOL]
		void DeactivateRocketBoots() { StopRocketBoots(); }

	}
	namespace Camera
	{
		// FUNCTION: TOY2 0x004A4F80 [PROVISIONAL]
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
	// FUNCTION: TOY2 0x004A5070 [MATCHED]
	void RespawnCosmicShield() { RestoreCosmicShieldPickup(); }

	namespace Collectables
	{
		// FUNCTION: TOY2 0x004A50D0 [MATCHED]
		void CosmicShield(Buzz::GadgetPickup* pickup)
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

			g_savedCosmicShieldPickupY = pickup->position.y;
			g_activeCosmicShieldPickup = pickup;
			g_buzzActor.cosmicShieldTimer = 900;
			Nu3D::Link::SetScaleFromFixedOffsets(pickup->linkId, 0x3000, 0x3000, 0x3000);
		}

	}
	namespace Buzz
	{
		// FUNCTION: TOY2 0x004A5170 [PROVISIONAL]
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
				if (g_framePulsePhases.sixteenTick < 8)
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

			Renderer::BlitTextureByIndexOffset(
				0x10, 0x80, 0xC0, 0x40, 0x40, g_framePulsePhases.sixteenTick * 4, g_framePulsePhases.thirtyTwoTick * 2, 0, -0x40);
		}

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

		// FUNCTION: TOY2 0x004A5370 [PROVISIONAL]
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

		// FUNCTION: TOY2 0x004A5540 [PROVISIONAL]
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
					g_buzzActor.velocity.lateral = grappleVector.direction.x / 3;
					g_buzzActor.velocity.vertical = grappleVector.direction.y / 3;
					g_buzzActor.velocity.forward = grappleVector.direction.z / 3;
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
			g_buzzActor.velocity.lateral = 0;
			g_buzzActor.velocity.vertical = 0;
			g_buzzActor.velocity.forward = 0;
		}

	}
	namespace Combat
	{
		enum SegmentCollisionFlags
		{
			SEGMENT_COLLISION_ACTOR = 2,
			SEGMENT_COLLISION_CONTINUES = 4,
		};

		// FUNCTION: TOY2 0x004A5870 [PROVISIONAL]
		int32_t SweepSegmentVsActors(Vector3I* position, Vector3I* movement, int32_t damageType)
		{
			int32_t collisionFlags = 0;
			Actor::Toy2Actor* hitActor = 0;
			Actor::Toy2Actor** actorSlot = Actor::g_activeActors;
			Actor::Toy2Actor* actor = *actorSlot;
			while (actor != 0)
			{
				if ((actor->creatureRam->defenseMode & 4) != 0 && actor->damageCooldownTimer >= 0 && actor->hitpoints >= 0)
				{
					Vector3I end = {
						position->x + movement->x,
						position->y + movement->y,
						position->z + movement->z,
					};
					Actor::ActorCollisionVolume* volume = &actor->collisionVolumes[actor->primaryAnimIdx];
					int32_t backwardSine = Numerics::g_sinCosLUT[(actor->yawAngle - 0x800) & 0xFFF] >> 2;
					int32_t cosine = Numerics::g_sinCosLUT[(actor->yawAngle - 0x400) & 0xFFF] >> 2;

					int32_t centerX = ((volume->offset.x * cosine + volume->offset.z * backwardSine) >> 12) + actor->pos.x;
					int32_t centerY = volume->offset.y + actor->pos.y;
					int32_t centerZ = ((volume->offset.z * cosine - volume->offset.x * backwardSine) >> 12) + actor->pos.z;

					int32_t startDeltaX = centerX - position->x;
					int32_t startDeltaZ = centerZ - position->z;
					int32_t scaledStartX = (((startDeltaX * cosine + startDeltaZ * backwardSine) >> 12) * volume->scale.x) >> 8;
					int32_t scaledStartY = ((centerY - position->y) * volume->scale.y) >> 8;
					int32_t scaledStartZ = (((startDeltaZ * cosine - startDeltaX * backwardSine) >> 12) * volume->scale.z) >> 8;

					int32_t endDeltaX = centerX - end.x;
					int32_t endDeltaZ = centerZ - end.z;
					int32_t scaledEndX = (((endDeltaX * cosine + endDeltaZ * backwardSine) >> 12) * volume->scale.x) >> 8;
					int32_t scaledEndY = ((centerY - end.y) * volume->scale.y) >> 8;
					int32_t scaledEndZ = (((endDeltaZ * cosine - endDeltaX * backwardSine) >> 12) * volume->scale.z) >> 8;

					int32_t segmentX = (scaledStartX - scaledEndX) >> 5;
					int32_t segmentY = (scaledStartY - scaledEndY) >> 5;
					int32_t segmentZ = (scaledStartZ - scaledEndZ) >> 5;
					Vector3I segmentDirection = { segmentX, segmentY, segmentZ };
					Nu3D::Math::NormalizeToFixedPoint(&segmentDirection, &segmentDirection);

					int32_t projectedDistance =
						(segmentDirection.x * (scaledStartX >> 5) + segmentDirection.y * (scaledStartY >> 5) + segmentDirection.z * (scaledStartZ >> 5)) >> 12;
					int32_t segmentLength = (segmentDirection.x * segmentX + segmentDirection.y * segmentY + segmentDirection.z * segmentZ) >> 12;
					if (projectedDistance > 0 && projectedDistance <= segmentLength)
					{
						int32_t closestX = (scaledStartX >> 5) - ((projectedDistance * segmentDirection.x) >> 12);
						int32_t closestY = (scaledStartY >> 5) - ((projectedDistance * segmentDirection.y) >> 12);
						int32_t closestZ = (scaledStartZ >> 5) - ((projectedDistance * segmentDirection.z) >> 12);
						int32_t closestDistanceSquared = closestX * closestX + closestY * closestY + closestZ * closestZ;
						int32_t radiusSquared = volume->radius * volume->radius;
						if (closestDistanceSquared < radiusSquared)
						{
							int32_t hitDistance = projectedDistance - (int32_t)sqrt((double)(radiusSquared - closestDistanceSquared));
							int32_t localHitX = ((hitDistance * segmentDirection.x) >> 4) / volume->scale.x;
							int32_t localHitY = (hitDistance * segmentDirection.y * 2) / volume->scale.y;
							int32_t localHitZ = ((hitDistance * segmentDirection.z) >> 4) / volume->scale.z;
							movement->x = (localHitX * cosine - localHitZ * backwardSine) >> 7;
							movement->y = localHitY;
							movement->z = (localHitZ * cosine + localHitX * backwardSine) >> 7;
							collisionFlags = SEGMENT_COLLISION_ACTOR;
							hitActor = actor;
						}
					}
				}
				actor = *++actorSlot;
			}

			if (collisionFlags != 0 && damageType != 0)
			{
				if (damageType == 1)
					Actor::HandleDamage(hitActor, (int16_t)g_buzzActor.posAngles.angles.yaw, 2);
				else
					Actor::HandleDamage(hitActor, (int16_t)g_buzzActor.posAngles.angles.yaw, 3);
				if ((hitActor->creatureRam->defenseMode & 1) == 0)
					return SEGMENT_COLLISION_CONTINUES;
			}
			return collisionFlags;
		}

	}
	namespace Buzz
	{
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

	}
	// FUNCTION: TOY2 0x004A5D30 [PROVISIONAL]
	Buzz::BeamShot* SpawnBeamShot(int32_t shotType, int32_t aimYaw, int32_t aimPitch, Vector3I* origin, Vector3I* offset, int32_t range, int32_t autoAim)
	{
		Buzz::BeamShot* shot;
		int16_t oldestFadeTimer = 1000;
		for (int32_t shotIndex = 0; shotIndex < 4; shotIndex++)
		{
			if (g_beamShots[shotIndex].fadeTimer == 0)
			{
				shot = &g_beamShots[shotIndex];
				break;
			}

			if (g_beamShots[shotIndex].fadeTimer < oldestFadeTimer)
			{
				oldestFadeTimer = g_beamShots[shotIndex].fadeTimer;
				shot = &g_beamShots[shotIndex];
			}
		}

		if (autoAim != 0)
		{
			int32_t closestAngleDelta = 0x7FFFFFFF;
			Actor::Toy2Actor* autoAimTarget = 0;
			for (Actor::Toy2Actor** activeActor = Actor::g_activeActors; *activeActor != 0; activeActor++)
			{
				Actor::Toy2Actor* actor = *activeActor;
				if ((actor->actorFlags & Actor::ACTOR_FLAG_TARGETABLE) != 0 && (actor->creatureRam->defenseMode & 4) != 0 && actor->hitpoints >= 0)
				{
					int32_t deltaX = (actor->pos.x - origin->x) >> 5;
					int32_t deltaY = (actor->pos.y - origin->y) >> 5;
					int32_t deltaZ = (actor->pos.z - origin->z) >> 5;
					if (deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ < 0x1000000 && abs(deltaY) < 0x300)
					{
						int32_t angleDelta = (Nu3D::Math::CartesianToFixedAngle(deltaX, deltaZ) - aimYaw) & 0xFFF;
						if (angleDelta > 0x800)
							angleDelta -= 0xFFF;
						if (abs(angleDelta) < abs(closestAngleDelta))
						{
							closestAngleDelta = angleDelta;
							autoAimTarget = actor;
						}
					}
				}
			}

			if (autoAimTarget != 0)
			{
				Actor::ActorCollisionVolume* targetVolume = &autoAimTarget->collisionVolumes[autoAimTarget->primaryAnimIdx];
				int32_t backwardSine = Numerics::g_sinCosLUT[(autoAimTarget->yawAngle - 0x800) & 0xFFF] >> 2;
				int32_t cosine = Numerics::g_sinCosLUT[(autoAimTarget->yawAngle - 0x400) & 0xFFF] >> 2;
				int32_t targetX = ((targetVolume->offset.x * cosine + targetVolume->offset.z * backwardSine) >> 12) + autoAimTarget->pos.x - origin->x;
				int32_t targetY = targetVolume->offset.y + autoAimTarget->pos.y - origin->y;
				int32_t targetZ = ((targetVolume->offset.z * cosine - targetVolume->offset.x * backwardSine) >> 12) + autoAimTarget->pos.z - origin->z;
				targetX >>= 5;
				targetY >>= 5;
				targetZ >>= 5;

				int32_t targetYaw = Nu3D::Math::CartesianToFixedAngle(targetX, targetZ);
				if (((targetYaw - aimYaw + 0x80) & 0xFFF) < 0x100)
				{
					aimYaw = targetYaw;
					int32_t horizontalDistance = (int32_t)sqrt((double)(targetX * targetX + targetZ * targetZ));
					aimPitch = (Nu3D::Math::CartesianToFixedAngle(horizontalDistance, targetY) - 0x400) & 0xFFF;
					if (aimPitch > 0x800)
						aimPitch -= 0x1000;
					if (aimPitch > 0x40)
						aimPitch = 0x40;
					else if (aimPitch < -0x40)
						aimPitch = -0x40;
				}
				else
				{
					g_buzzActor.facingAngle = (int16_t)targetYaw;
				}
			}
		}

		int32_t pitchCosine = Numerics::g_sinCosLUT[(aimPitch + 0x400) & 0xFFF] * range;
		int32_t pitchSine = Numerics::g_sinCosLUT[(aimPitch - 0x800) & 0xFFF] * range;
		int32_t yawSine = Numerics::g_sinCosLUT[aimYaw & 0xFFF];
		int32_t yawCosine = Numerics::g_sinCosLUT[(aimYaw + 0x400) & 0xFFF];

		shot->start.x = origin->x + offset->x;
		shot->start.y = origin->y + offset->y;
		shot->start.z = origin->z + offset->z;
		shot->end.x = origin->x + ((yawSine * pitchCosine) >> 13);
		shot->end.y = origin->y + pitchSine * 2;
		shot->end.z = origin->z + ((yawCosine * pitchCosine) >> 13);
		shot->fadeTimer = 0x20;

		Vector3I collisionPosition = *origin;
		Vector3I movement = { shot->end.x - origin->x, shot->end.y - origin->y, shot->end.z - origin->z };

		if (shotType == 0)
		{
			shot->color.b = 0x80;
			shot->color.g = 0;
			shot->color.r = 0;
			shot->color.a = 0x20;
		}
		else if (shotType == 1)
		{
			shot->color.b = 0x80;
			shot->color.g = 0x80;
			shot->color.r = 0;
			shot->color.a = 0x40;
		}
		else if (shotType == 2)
		{
			shot->color.b = 0;
			shot->color.g = 0x80;
			shot->color.r = 0;
			shot->color.a = 0x20;
		}

		int32_t collisionFlags = Collision::SweepAndSlide(&collisionPosition, &movement, 0x8000, 0, 0x100);
		collisionFlags |= Combat::SweepSegmentVsActors(&collisionPosition, &movement, shotType + 1);
		if (collisionFlags != 0)
		{
			shot->end.x = collisionPosition.x + movement.x;
			shot->end.y = collisionPosition.y + movement.y;
			shot->end.z = collisionPosition.z + movement.z;

			if (shotType == 0)
			{
				if (Camera::g_scriptedCameraState == Buzz::CAMERA_STATE_VISOR)
					Nu3D::Particles::SpawnFromPreset(shot->end.x, shot->end.y, shot->end.z, 0xB, 5);
				else
					Nu3D::Particles::SpawnFromPreset(shot->end.x, shot->end.y, shot->end.z, 1, 2);
			}
			else if (shotType == 1)
			{
				Nu3D::Particles::SpawnFromPreset(shot->end.x, shot->end.y, shot->end.z, 0xA, 5);
			}
			else if (shotType == 2)
			{
				if (Camera::g_scriptedCameraState == Buzz::CAMERA_STATE_VISOR)
					Nu3D::Particles::SpawnFromPreset(shot->end.x, shot->end.y, shot->end.z, 0x4A, 5);
				else
					Nu3D::Particles::SpawnFromPreset(shot->end.x, shot->end.y, shot->end.z, 0x49, 2);
			}

			if (collisionFlags == 1)
			{
				Nu3D::Particles::SpawnFromPreset(shot->end.x, shot->end.y, shot->end.z, 2, 5);
				AudioManager::PlaySoundEffect(8, &shot->end);
			}

			for (int32_t particleCount = 5; particleCount != 0; particleCount--)
			{
				Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(shot->end.x, shot->end.y, shot->end.z, 4, 4);
				particle->lifetime = (*g_randDatBufferPtr++ & 0xF) * 2 + 0x18;
			}
		}

		Vector3I beamDirection = {
			(shot->end.x - shot->start.x) >> 2,
			(shot->end.y - shot->start.y) >> 2,
			(shot->end.z - shot->start.z) >> 2,
		};
		Nu3D::Math::NormalizeToFixedPoint(&beamDirection, &beamDirection);
		shot->velocity.x = beamDirection.x >> 1;
		shot->velocity.y = beamDirection.y >> 1;
		shot->velocity.z = beamDirection.z >> 1;

		int32_t dominantVelocity;
		int32_t beamDistance;
		if (abs(shot->velocity.y) < abs(shot->velocity.x))
		{
			dominantVelocity = shot->velocity.x;
			beamDistance = shot->end.x - shot->start.x;
		}
		else
		{
			dominantVelocity = shot->velocity.y;
			beamDistance = shot->end.y - shot->start.y;
		}
		if (abs(dominantVelocity) <= abs(shot->velocity.z))
		{
			dominantVelocity = shot->velocity.z;
			beamDistance = shot->end.z - shot->start.z;
		}
		if (dominantVelocity == 0)
			dominantVelocity = 1;
		shot->movementTimer = (int16_t)(beamDistance / dominantVelocity);

		return (collisionFlags & Combat::SEGMENT_COLLISION_CONTINUES) != 0 ? shot : 0;
	}

	namespace Buzz
	{
		// FUNCTION: TOY2 0x004A62A0 [TOOL]
		void TickGadgets()
		{
			if (g_rocketBootsTimer != 0)
			{
				if (g_environmentSurfaceY != 0 && g_buzzActor.posAngles.pos.y > g_environmentSurfaceY && g_buzzActor.velocity.vertical > -0x300)
				{
					g_buzzActor.velocity.vertical -= Renderer::g_frameDelta * 0x80;
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
}
