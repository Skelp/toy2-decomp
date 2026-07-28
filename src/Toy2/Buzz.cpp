#include "Toy2/Buzz.h"
#include "Toy2/Camera.h"
#include "Toy2/Toy2.h"
#include "AudioManager/AudioManager.h"
#include "InputManager.h"
#include "Nu3D/Link.h"
#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
#include <string.h>

namespace Toy2
{
	// GLOBAL: TOY2 0x0053C5E0
	int32_t g_rocketBootsTimer;

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

	// STUB: TOY2 0x00433ED0
	void ResetBuzzState() {}

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

		// STUB: TOY2 0x00414110
		void Respawn() {}

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
