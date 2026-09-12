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
}

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

		enum SurfaceEffectStateMask
		{
			SURFACE_EFFECT_TYPE_MASK = 0xFFFF,
		};

		// FUNCTION: TOY2 0x004100F0 [PROVISIONAL]
		void CheckParticleCollisions()
		{
			const int32_t buzzX = g_buzzActor.posAngles.pos.x;
			const int32_t buzzY = g_buzzActor.posAngles.pos.y - 0x1CC0;
			const int32_t buzzZ = g_buzzActor.posAngles.pos.z;
			int32_t collisionResponse = 0;
			int32_t replacementCollisionFlags = 0;
			Nu3D::Particles::ParticleInstance* collisionParticle;

			Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::g_particleInstances;
			for (int32_t remainingParticleCount = 64; remainingParticleCount != 0; remainingParticleCount--, particle++)
			{
				if (particle->lifetime <= 0 || (particle->renderFlags & Nu3D::Particles::PARTICLE_INTERACTS_WITH_BUZZ) == 0)
				{
					continue;
				}

				int32_t deltaX = (buzzX - particle->pos.x) * 0x100 >> 13;
				int32_t deltaY = (buzzY - particle->pos.y) * 0x80 >> 13;
				int32_t deltaZ = (buzzZ - particle->pos.z) * 0x100 >> 13;
				if (deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ >= (particle->width + 100) * (particle->width + 100))
				{
					continue;
				}

				int32_t soundEffect = 0;
				switch (particle->spriteSheet)
				{
					case 7:
						if (g_buzzActor.specialAirState == 0)
						{
							break;
						}
					case 0:
					case 1:
					case 0x13:
						collisionResponse = 1;
						replacementCollisionFlags = 0;
						soundEffect = 0;
						collisionParticle = particle;
						break;

					case 4:
					case 0x33:
					case 0x34:
						collisionResponse = 2;
						replacementCollisionFlags = particle->collisionFlags;
						soundEffect = 0;
						collisionParticle = particle;
						break;

					case 0x10:
						soundEffect = -1;
						replacementCollisionFlags = 5;
						HUD::g_slideTimers[HUD::SLIDE_COINS] = 180;
						if (g_buzzActor.coinsCollected < 99)
						{
							g_buzzActor.coinsCollected++;
						}
						if (g_buzzActor.coinsCollected == 50)
						{
							AudioManager::PlaySoundEffect(0x4F, 0);
						}
						break;

					case 0x15:
						replacementCollisionFlags = 4;
						soundEffect = 0;
						collisionResponse = 1;
						collisionParticle = particle;
						break;

					case 0x16:
						if (g_buzzActor.collisionFlags != 0)
						{
							g_pendingFootingType = (particle->updateParam >> 1) - 0x5A;
						}
						break;

					case 5:
						collisionResponse = 2;
						replacementCollisionFlags = particle->collisionFlags;
						soundEffect = 0;
						collisionParticle = particle;
						break;

					case 0x35:
						if (g_buzzActor.specialAirState == 0)
						{
							break;
						}
					case 0x36:
						collisionResponse = 1;
						replacementCollisionFlags = particle->collisionFlags;
						soundEffect = 0;
						collisionParticle = particle;
						break;

					case 0x37:
						collisionResponse = 2;
						replacementCollisionFlags = 0;
						soundEffect = 0;
						collisionParticle = particle;
						break;
				}

				if ((g_spinCooldownTimer > 20 || g_spinHoverTimer <= -120) && collisionResponse == 2)
				{
					int32_t direction =
						Nu3D::Math::CartesianToFixedAngle(g_buzzActor.posAngles.pos.x - particle->pos.x, g_buzzActor.posAngles.pos.z - particle->pos.z);
					particle->renderFlags &= ~0xA;
					direction += 0x800;
					particle->velY = -0x400;
					particle->yawAngle = 0x30;
					particle->lifetime = 50;
					particle->velX = Numerics::g_sinCosLUT[direction & 0xFFF] / 16;
					collisionResponse = 0;
					particle->velZ = Numerics::g_sinCosLUT[(direction + 0x400) & 0xFFF] / 16;
					AudioManager::PlaySoundEffect(7, &particle->pos);
				}
				else
				{
					if (replacementCollisionFlags > 0)
					{
						particle->collisionFlags = replacementCollisionFlags;
						particle->lifetime = 1;
					}
					if (replacementCollisionFlags < 0)
					{
						particle->lifetime = 1;
					}
					if (soundEffect != 0)
					{
						AudioManager::PlaySoundEffect(soundEffect, &particle->pos);
					}
				}
			}

			if (collisionResponse != 0)
			{
				int32_t direction = Nu3D::Math::CartesianToFixedAngle(
					g_buzzActor.posAngles.pos.x - collisionParticle->pos.x, g_buzzActor.posAngles.pos.z - collisionParticle->pos.z);
				HandleDamage(direction, 3);
			}
			if (g_pendingFootingType != -1)
			{
				g_footingType = g_pendingFootingType;
			}
		}

		// FUNCTION: TOY2 0x004A2D80 [PROVISIONAL]
		void UpdateContactEffects()
		{
			bool aboveEnvironmentSurface = g_environmentEffectType != 0 && g_buzzActor.posAngles.pos.y > g_environmentSurfaceY;

			if (g_gunFireTimer != 0 && (g_aimTargetIndex < 1000 || Camera::g_scriptedCameraState == 0) && g_discLauncherAmmo == 0)
			{
				if (g_laserAimParticle == (Nu3D::Particles::ParticleInstance*)-1)
				{
					g_laserAimParticle = Nu3D::Particles::SpawnFromPreset(g_aimTargetPosition.x, g_aimTargetPosition.y, g_aimTargetPosition.z, 9, 5);
				}

				g_laserAimParticle->lifetime = 10000;
				g_laserAimParticle->pos.x = g_aimTargetPosition.x;
				g_laserAimParticle->pos.y = g_aimTargetPosition.y;
				g_laserAimParticle->pos.z = g_aimTargetPosition.z;
				if (g_poweredLaserCharge == 0)
				{
					if (g_gunFireTimer < 16)
						g_laserAimParticle->colourR = (uint8_t)(g_gunFireTimer << 3);
					if (g_gunFireTimer > 52)
						g_laserAimParticle->colourR = (uint8_t)(g_gunFireTimer * -8);
					if (g_gunChargeTimer > 0)
						g_laserAimParticle->colourG = (uint8_t)(g_gunChargeTimer << 1);
					else
						g_laserAimParticle->colourG = 0;
				}
				else
				{
					if (g_gunFireTimer < 16)
						g_laserAimParticle->colourG = (uint8_t)(g_gunFireTimer << 3);
					if (g_gunFireTimer > 52)
						g_laserAimParticle->colourG = (uint8_t)(g_gunFireTimer * -8);
					g_laserAimParticle->colourR = 0;
				}
			}
			else if (g_laserAimParticle != (Nu3D::Particles::ParticleInstance*)-1)
			{
				g_laserAimParticle->lifetime = 0;
				g_laserAimParticle->spriteSheet = 0;
				g_laserAimParticle = (Nu3D::Particles::ParticleInstance*)-1;
			}

			bool rightFootContact = (g_animationEventFlags & ANIMATION_EVENT_RIGHT_FOOT) != 0;
			bool leftFootContact = (g_animationEventFlags & ANIMATION_EVENT_LEFT_FOOT) != 0;
			int32_t footOffsetX;
			int32_t footOffsetZ;
			if (leftFootContact)
			{
				footOffsetX = Numerics::g_sinCosLUT[(g_buzzActor.posAngles.angles.yaw + 0x400) & 0xFFF] >> 4;
				footOffsetZ = Numerics::g_sinCosLUT[(g_buzzActor.posAngles.angles.yaw - 0x800) & 0xFFF] >> 4;
			}
			if (rightFootContact)
			{
				footOffsetX = Numerics::g_sinCosLUT[(g_buzzActor.posAngles.angles.yaw - 0x400) & 0xFFF] >> 4;
				footOffsetZ = Numerics::g_sinCosLUT[g_buzzActor.posAngles.angles.yaw & 0xFFF] >> 4;
			}

			if (! aboveEnvironmentSurface)
			{
				int32_t spawnBurst = 0;
				int32_t pulseCount = g_framePulseOutputs.sixTick;
				if (g_animationEventFlags == ANIMATION_EVENT_RIGHT_FOOT || g_animationEventFlags == ANIMATION_EVENT_LEFT_FOOT)
				{
					spawnBurst = 1;
					pulseCount = 1;
				}
				else
				{
					footOffsetX = *g_randDatBufferPtr++ * 4 - 0x200;
					footOffsetZ = *g_randDatBufferPtr++ * 4 - 0x200;
				}
				if ((g_animationEventFlags & ANIMATION_EVENT_MOVEMENT) != 0)
					pulseCount = g_framePulseOutputs.fourTick;
				if ((g_animationEventFlags & ANIMATION_EVENT_IDLE) != 0)
					pulseCount = g_framePulseOutputs.sixteenTick;
				if ((g_animationEventFlags & ANIMATION_EVENT_FORCE_EFFECT) != 0)
					spawnBurst = 1;

				if ((pulseCount != 0 || spawnBurst != 0) && g_buzzActor.specialAirState != 0)
				{
					int32_t particleX = g_buzzActor.posAngles.pos.x + footOffsetX;
					int32_t particleZ = g_buzzActor.posAngles.pos.z + footOffsetZ;
					switch (g_footingType)
					{
						case 0:
							g_surfaceEffectState = 0xB40001;
							if (spawnBurst != 0)
							{
								for (int32_t count = 4; count != 0; count--)
									Nu3D::Particles::SpawnFromPreset(particleX, g_buzzActor.posAngles.pos.y, particleZ, 0x1E, 4);
							}
							else
							{
								Nu3D::Particles::SpawnFromPreset(particleX, g_buzzActor.posAngles.pos.y, particleZ, 0x1E, 4);
							}
							break;

						case 4:
							g_surfaceEffectState = 0xB40002;
							Nu3D::Particles::SpawnFromPreset(particleX, g_buzzActor.posAngles.pos.y, particleZ, spawnBurst + 0x1B, 2);
							break;

						case 5:
							g_surfaceEffectState = 0xB40003;
							if (spawnBurst != 0)
							{
								for (int32_t count = 4; count != 0; count--)
									Nu3D::Particles::SpawnFromPreset(particleX, g_buzzActor.posAngles.pos.y, particleZ, 0x1D, 4);
							}
							else if ((g_animationEventFlags & ANIMATION_EVENT_MOVEMENT) != 0)
							{
								Nu3D::Particles::SpawnFromPreset(particleX, g_buzzActor.posAngles.pos.y, particleZ, 0x1D, 4);
							}
							break;

						case -1:
						case 13:
							if (g_surfaceEffectState > 0 && g_animationEventFlags > 0 && g_animationEventFlags < 4)
							{
								Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(
									particleX, g_buzzActor.posAngles.pos.y, particleZ, (g_surfaceEffectState & SURFACE_EFFECT_TYPE_MASK) + 0x1E, 2);
								particle->groundAlignRot = (-g_buzzActor.posAngles.angles.yaw - 0x400) & 0xFFF;
							}
							if ((g_animationEventFlags & ANIMATION_EVENT_MOVEMENT) != 0)
							{
								for (int32_t count = g_framePulseOutputs.twoTickCount; count != 0; count--)
									Nu3D::Particles::SpawnFromPreset(
										g_buzzActor.posAngles.pos.x, g_buzzActor.posAngles.pos.y, g_buzzActor.posAngles.pos.z, 2, 2);
							}
							break;
					}
				}
			}

			if (g_surfaceEffectState > 0)
			{
				g_surfaceEffectState -= Renderer::g_frameDelta * 0x10000;
				if (g_surfaceEffectState < 0)
					g_surfaceEffectState = 0;
			}

			if (g_framePulseOutputs.fourTick != 0 && g_environmentEffectType == 1 && g_environmentSurfaceY < Camera::g_renderCameraTransform.pos.y)
			{
				int32_t offset = (*g_randDatBufferPtr - 0x80) * 0x100;
				g_randDatBufferPtr += 2;
				Nu3D::Particles::SpawnFromPreset(g_buzzActor.posAngles.pos.x + offset, g_environmentSurfaceY, g_buzzActor.posAngles.pos.z + offset, 0x39, 2);
			}

			if (g_environmentEffectType == 1 && Nu3D::Camera::g_targetTintFadeSpeed == 0)
			{
				int32_t tintBlend = ((g_environmentSurfaceY - Camera::g_renderCameraTransform.pos.y) >> 4) + 0x80;
				if (tintBlend < 0)
					tintBlend = 0;
				if (tintBlend < 0x80)
					AudioManager::PlaySoundEffect(0x5F, 0);

				if (tintBlend < 0x100)
				{
					if (tintBlend == 0)
						Nu3D::Camera::EnableCameraSkew();
					Nu3D::Camera::g_cameraTintRed = g_environmentTintRed;
					Nu3D::Camera::g_cameraTintGreen = g_environmentTintGreen;
					Nu3D::Camera::g_cameraTintBlue = g_environmentTintBlue;
					Nu3D::Camera::g_tintBlend = (int16_t)tintBlend;
				}
				else
				{
					Nu3D::Camera::g_tintBlend = -1;
					Nu3D::Camera::g_cameraTintRed = 0x80;
					Nu3D::Camera::g_cameraTintGreen = 0x80;
					Nu3D::Camera::g_cameraTintBlue = 0x80;
				}
			}
			else if (Nu3D::Camera::g_tintBlend != -1)
			{
				Nu3D::Camera::g_tintBlend = -1;
				Nu3D::Camera::g_cameraTintRed = 0x80;
				Nu3D::Camera::g_cameraTintGreen = 0x80;
				Nu3D::Camera::g_cameraTintBlue = 0x80;
			}

			if (g_environmentEffectType > 0 && g_environmentEffectType < 3)
			{
				int32_t stationaryPulseIndex;
				int32_t movingPulseIndex;
				int32_t ambientParticleType;
				if (g_environmentEffectType == 1)
				{
					stationaryPulseIndex = 9;
					movingPulseIndex = 4;
					ambientParticleType = 0x1B;
				}
				else
				{
					stationaryPulseIndex = 4;
					movingPulseIndex = 9;
					ambientParticleType = 0x33;
				}

				if (g_buzzActor.posAngles.pos.y > g_environmentSurfaceY)
				{
					if (g_previousBuzzEnvironmentY <= g_environmentSurfaceY)
					{
						int32_t splashPreset = stationaryPulseIndex;
						int32_t splashCount = 5;
						if (g_airborneTimer == 0x50)
						{
							splashPreset = 0xE;
							splashCount = 0x14;
						}
						for (; splashCount != 0; splashCount--)
						{
							int32_t particleType = g_environmentEffectType == 1 ? 0xD : 0x35;
							Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(
								g_buzzActor.posAngles.pos.x, g_environmentSurfaceY, g_buzzActor.posAngles.pos.z, particleType, splashPreset);
							particle->updateParam = ((*g_randDatBufferPtr++ & 1) + (splashPreset == 0xE ? 4 : 3)) * 2;
							particle->lifetime = particle->updateParam * 5;
							particle->groundAlignRot = *g_randDatBufferPtr++ << 4;
						}
						AudioManager::PlaySoundEffect(g_environmentEffectType == 1 ? 0x3A : 0x4E, &g_buzzActor.posAngles.pos);
					}
					g_surfaceEffectState = 0;
				}
				else if (g_previousBuzzEnvironmentY > g_environmentSurfaceY)
				{
					g_surfaceEffectState = g_environmentEffectType == 1 ? 0xB40002 : 0xB40018;
				}

				int32_t ambientPulseIndex = stationaryPulseIndex;
				if (g_buzzActor.forwardSpeed != 0 || g_buzzActor.lateralSpeed != 0)
					ambientPulseIndex = movingPulseIndex;
				if (g_buzzActor.posAngles.pos.y > g_environmentSurfaceY && g_buzzActor.posAngles.pos.y < g_environmentSurfaceY + 0x3000
					&& ((uint8_t*)&g_framePulseOutputs)[ambientPulseIndex] != 0)
				{
					int32_t particleType = ambientParticleType + (*g_randDatBufferPtr++ & 1);
					Nu3D::Particles::SpawnFromPreset(g_buzzActor.posAngles.pos.x, g_environmentSurfaceY, g_buzzActor.posAngles.pos.z, particleType, 2);
				}

				int32_t effectPulseIndex = 7;
				if (g_spinCooldownTimer != 0 || g_spinHoverTimer < -120)
					effectPulseIndex = 2;
				if ((g_buzzActor.actorFlags & ACTOR_FLAG_BLOCK_EDGE_IDLE) != 0 && (*g_randDatBufferPtr++ & 3) == 0)
				{
					for (int32_t count = ((uint8_t*)&g_framePulseOutputs)[effectPulseIndex]; count != 0; count--)
					{
						if (g_environmentEffectType == 1)
						{
							uint8_t random = *g_randDatBufferPtr;
							g_randDatBufferPtr += 3;
							int32_t horizontalOffset = (random - 0x80) * 0x10;
							Nu3D::Particles::SpawnFromPreset(g_buzzActor.posAngles.pos.x + horizontalOffset,
								g_buzzActor.posAngles.pos.y + (random - 0x280) * 0x10,
								g_buzzActor.posAngles.pos.z + horizontalOffset,
								0x2D,
								0x12);
						}
						if (g_environmentEffectType == 2)
						{
							int32_t horizontalOffset = (*g_randDatBufferPtr - 0x80) * 0x10;
							g_randDatBufferPtr += 2;
							Nu3D::Particles::SpawnFromPreset(
								g_buzzActor.posAngles.pos.x + horizontalOffset, g_environmentSurfaceY, g_buzzActor.posAngles.pos.z + horizontalOffset, 0x35, 4);
						}
					}
				}
			}

			g_previousBuzzEnvironmentY = g_buzzActor.posAngles.pos.y;
			if (g_buzzActor.specialAirState == 0 || (! leftFootContact && ! rightFootContact))
				return;

			int32_t soundEffect = 0;
			if (g_footingType != -1)
			{
				switch (g_surfaceEffectState & SURFACE_EFFECT_TYPE_MASK)
				{
					case 1:
						soundEffect = 1;
						break;
					case 2:
						soundEffect = 3;
						break;
					case 3:
						soundEffect = 4;
						break;
					case 0x18:
						soundEffect = 5;
						break;
				}
			}
			Vector3I soundPosition = {
				g_buzzActor.posAngles.pos.x + footOffsetX * 8, g_buzzActor.posAngles.pos.y, g_buzzActor.posAngles.pos.z + footOffsetZ * 8
			};
			AudioManager::PlaySoundEffect(soundEffect, &soundPosition);
		}
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
		struct StartPosition
		{
			Vector3I position;
			int16_t yawAngle;
			int16_t reserved;
		};

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

		STATIC_ASSERT(sizeof(StartPosition) == 0x10);

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

	namespace Buzz
	{
		struct AnimationStateDefinition
		{
			uint8_t* eventTrack;
			int32_t primaryAnimationIndex;
			int32_t secondaryAnimationIndex;
			int32_t frameAdvanceRate;
			int32_t unusedFlags;
		};

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
	{
		// FUNCTION: TOY2 0x00401000 [PROVISIONAL]
		int32_t StepEventTrack(const uint8_t* eventTrack)
		{
			int32_t eventPosition = g_buzzActor.animationEventPosition;
			const uint8_t* event = eventTrack + (eventPosition >> 16);
			if (event[1] == 0xFF && event[2] == 1)
			{
				eventPosition &= 0xFFFF0000;
				g_buzzActor.animationEventPosition = eventPosition;
			}

			int32_t eventCode = eventTrack[eventPosition >> 16];
			while (eventCode >= 0x80)
			{
				switch (eventCode)
				{
					case 0xFF:
						eventPosition -= ((int32_t)event[1] + 1) << 16;
						g_buzzActor.animationEventPosition = eventPosition;
						break;

					case 0x81:
						if (g_buzzActor.specialAirState != 0)
							g_animationEventFlags |= Buzz::ANIMATION_EVENT_RIGHT_FOOT;
						break;

					case 0x82:
						if (g_buzzActor.specialAirState != 0)
							g_animationEventFlags |= Buzz::ANIMATION_EVENT_LEFT_FOOT;
						break;

					case 0x83:
						AudioManager::PlaySoundEffect(0x30, &g_buzzActor.posAngles.pos);
						break;

					case 0x84:
						AudioManager::PlaySoundEffect(0x10, &g_buzzActor.posAngles.pos);
						break;

					case 0x85:
						AudioManager::PlaySoundEffect(0x17, &g_buzzActor.posAngles.pos);
						break;

					case 0x86:
						AudioManager::PlaySoundEffect(0x43, &g_buzzActor.posAngles.pos);
						break;

					case 0xFE:
						g_buzzActor.animationState = 0;
						g_buzzActor.previousAnimationState = -1;
						g_idleAnimationState = 0;
						return -1;
				}

				eventPosition = g_buzzActor.animationEventPosition + 0x10000;
				g_buzzActor.animationEventPosition = eventPosition;
				event = eventTrack + (eventPosition >> 16);
				eventCode = *event;
			}

			return eventCode;
		}
	}

	namespace Buzz
	{
		// FUNCTION: TOY2 0x004011D0 [PROVISIONAL]
		void UpdateAnimationState()
		{
			for (;;)
			{
				g_idleVoiceCooldown -= Renderer::g_frameDelta;
				if (g_idleVoiceCooldown <= 0)
					g_idleVoiceCooldown = 0;

				if (g_buzzActor.specialAirState != 0)
				{
					if (g_buzzActor.animationState == ANIMATION_STATE_LANDING)
						g_buzzActor.animationState = ANIMATION_STATE_STANDING;

					if (g_buzzActor.forwardSpeed < 0x200 && abs(g_buzzActor.lateralSpeed) < 0x200
						&& (((uint8_t)InputManager::g_directionInputState & (INPUT_UP | INPUT_RIGHT | INPUT_DOWN | INPUT_LEFT)) == 0
							|| g_idleAnimationTimer == 30))
					{
						if (g_buzzActor.animationState == ANIMATION_STATE_STANDING)
						{
							g_idleAnimationState = 0;
						}
						else
						{
							if (g_idleAnimationState == 0)
							{
								g_idleAnimationState = 1;
								if (((g_buzzActor.actorFlags & ACTOR_FLAG_BLOCK_EDGE_IDLE) == 0 || g_environmentEffectType < 2)
									&& g_buzzActor.floorYPos - g_buzzActor.posAngles.pos.y > 0x1000 && g_buzzActor.floorYPos != 0x80000000)
								{
									PosAndAngles groundProbe;
									groundProbe.pos.x =
										g_buzzActor.posAngles.pos.x + (Numerics::g_sinCosLUT[(int16_t)g_buzzActor.posAngles.angles.yaw & 0xFFF] >> 2);
									groundProbe.pos.y = g_buzzActor.posAngles.pos.y;
									groundProbe.pos.z =
										g_buzzActor.posAngles.pos.z + (Numerics::g_sinCosLUT[((int16_t)g_buzzActor.posAngles.angles.yaw + 0x400) & 0xFFF] >> 2);
									int32_t groundHeight = Nu3D::Collision::GetGroundHeight(&groundProbe, 0);
									if (groundHeight - g_buzzActor.posAngles.pos.y > 0x1000 && groundHeight != 0x80000000)
									{
										g_idleAnimationState = 10;
									}
									else
									{
										groundProbe.pos.x = g_buzzActor.posAngles.pos.x
											+ (Numerics::g_sinCosLUT[((int16_t)g_buzzActor.posAngles.angles.yaw - 0x800) & 0xFFF] >> 2);
										groundProbe.pos.z = g_buzzActor.posAngles.pos.z
											+ (Numerics::g_sinCosLUT[((int16_t)g_buzzActor.posAngles.angles.yaw - 0x400) & 0xFFF] >> 2);
										groundHeight = Nu3D::Collision::GetGroundHeight(&groundProbe, 0);
										if (groundHeight - g_buzzActor.posAngles.pos.y > 0x1000 && groundHeight != 0x80000000)
											g_idleAnimationState = 11;
									}
								}
							}

							if (g_idleAnimationState == 1 && (g_buzzActor.animationEventPosition & 0xFFFF0000) == 0xE0000)
							{
								uint8_t random = *g_randDatBufferPtr++;
								if ((random & 3) == 0)
								{
									g_idleAnimationState = (*g_randDatBufferPtr++ & 1) + 28;
									if (g_idleVoiceCooldown == 0)
									{
										g_idleVoiceCooldown = 1800;
										if (g_gunFireTimer <= 0 && g_spinHoverTimer == 0 && g_spinCooldownTimer == 0
											&& (g_gameplayStateFlags & GAMEPLAY_STATE_CUTSCENE_ACTIVE) == 0)
										{
											g_idleVoicePreset = (g_idleVoicePreset != 207) + 206;
											AudioManager::Preset::PlayOneShotSound(g_idleVoicePreset, &g_buzzActor);
										}
									}
								}
							}

							if (g_idleAnimationState >= 28 && (g_gunFireTimer > 0 || g_spinHoverTimer != 0 || g_spinCooldownTimer != 0))
								g_idleAnimationState = 1;

							g_buzzActor.animationState = (int16_t)g_idleAnimationState;
							if ((g_gameplayStateFlags & GAMEPLAY_STATE_CUTSCENE_ACTIVE) != 0)
							{
								g_idleAnimationState = 1;
								g_buzzActor.animationState = 1;
							}
							if (g_turnRecoveryTimer != 0)
								g_buzzActor.animationState = 18;
						}
					}
					else
					{
						g_idleAnimationState = 0;
						g_buzzActor.animationState = 0;
						if (g_turnRecoveryTimer != 0)
							g_buzzActor.animationState = 18;
					}
				}
				else
				{
					g_idleAnimationState = 0;
					if (g_buzzActor.animationState == ANIMATION_STATE_FORWARD_EDGE || g_buzzActor.animationState == ANIMATION_STATE_REAR_EDGE)
						g_buzzActor.animationState = 0;
					else if (g_buzzActor.animationState == ANIMATION_STATE_AIRBORNE && g_buzzActor.velocity.vertical > 0)
						g_buzzActor.animationState = ANIMATION_STATE_LANDING;
					else if (g_buzzActor.animationState == ANIMATION_STATE_DAMAGE_REACTION)
						g_buzzActor.animationState = ANIMATION_STATE_STANDING;
					if (g_airborneTimer == 0x50)
						g_buzzActor.animationState = 12;
				}

				if (g_swingTimer != 0 && g_buzzActor.animationState != ANIMATION_STATE_SWINGING)
					g_buzzActor.animationState = SWING_START_ANIMATION_STATE;
				if (g_forcedFacingActive != 0)
					g_buzzActor.animationState = 25;
				if (g_ziplineState == ZIPLINE_RIDING)
					g_buzzActor.animationState = 17;
				if (g_grappleState == GRAPPLE_PULLING && g_grappleElapsedTime > 9)
					g_buzzActor.animationState = 30;
				if (g_gravityBootsTimer != 0)
					g_buzzActor.animationState = 31;
				if (g_spinCancelRequested != 0)
					g_buzzActor.animationState = 19;
				if (g_slipperySurfaceState != 0 && (g_buzzActor.specialAirState != 0 || g_buzzActor.animationState == 0))
					g_buzzActor.animationState = 32;
				if (g_spinHoverTimer < 0)
					g_buzzActor.animationState = g_spinHoverTimer > -120 ? 20 : 19;

				if (g_poleClimbState > 0)
				{
					g_buzzActor.animationState = 15;
					if ((g_poleClimbState & POLE_CLIMB_ASCENDING) != 0)
						g_buzzActor.animationState = 14;
					if ((g_poleClimbState & POLE_CLIMB_SLIDING) != 0)
					{
						g_buzzActor.animationState = 16;
						AudioManager::PlaySoundEffect(0x23, &g_buzzActor.posAngles.pos);
					}
				}
				if (g_ledgeClimbTimer != 0)
					g_buzzActor.animationState = 9;
				if (g_groundSlamTimer > 0)
					g_buzzActor.animationState = 23;
				if (g_groundSlamTimer < -14)
					g_buzzActor.animationState = 24;
				if (g_airborneTimer < 0)
					g_buzzActor.animationState = 13;
				if (g_rocketBootsTimer != 0)
					g_buzzActor.animationState = 10;
				if (g_buzzActor.stunTimer > 0)
				{
					if (g_buzzActor.stunTimer > 67)
						g_buzzActor.animationState = 5;
					else
						g_buzzActor.actorFlags |= ACTOR_FLAG_STUNNED;
				}
				if (g_levelTransition == 2)
					g_buzzActor.animationState = 7;
				else if (g_levelTransition == 1)
					g_buzzActor.animationState = 27;

				int32_t animationState = g_buzzActor.animationState;
				AnimationStateDefinition* definition = &g_animationStateDefinitions[animationState];
				const uint8_t* eventTrack = definition->eventTrack;
				int32_t eventSample;
				if (animationState != g_buzzActor.previousAnimationState)
				{
					g_buzzActor.animationEventPosition = 0;
					g_buzzActor.previousAnimationState = (int16_t)animationState;
					eventSample = *eventTrack;
				}
				else
				{
					int32_t frameAdvance = definition->frameAdvanceRate * Renderer::g_frameDelta;
					if (frameAdvance < 0)
					{
						int32_t movementSpeedSquared =
							g_buzzActor.lateralSpeed * g_buzzActor.lateralSpeed + g_buzzActor.forwardSpeed * g_buzzActor.forwardSpeed;
						frameAdvance = -(int32_t)sqrt((double)movementSpeedSquared) * frameAdvance;
						if (frameAdvance < 0)
							frameAdvance = 0;
					}

					while (frameAdvance > 0xFFFF)
					{
						g_buzzActor.animationEventPosition += 0x10000;
						frameAdvance -= 0x10000;
						if (Animation::StepEventTrack(eventTrack) == -1)
							break;
					}
					if (g_buzzActor.previousAnimationState == -1)
						continue;

					g_buzzActor.animationEventPosition += frameAdvance;
					eventSample = Animation::StepEventTrack(eventTrack);
					if (eventSample == -1)
						continue;
				}

				int32_t eventFramePosition = (g_buzzActor.animationEventPosition & 0xFFFF) + (eventSample << 16);
				int32_t primaryAnimationIndex = definition->primaryAnimationIndex;
				int32_t secondaryAnimationIndex = definition->secondaryAnimationIndex;
				int32_t primaryFramePosition = eventFramePosition;

				if (g_spinCooldownTimer > 0)
				{
					if (primaryAnimationIndex == secondaryAnimationIndex)
						g_spinCooldownTimer = 0;
					else
					{
						primaryAnimationIndex = 9;
						primaryFramePosition = (48 - g_spinCooldownTimer) << 15;
					}
				}
				if (g_gunFireTimer > 0)
				{
					if (primaryAnimationIndex == secondaryAnimationIndex && Camera::g_scriptedCameraState != CAMERA_STATE_VISOR)
						g_gunFireTimer = 0;
					else
					{
						primaryAnimationIndex = 26;
						primaryFramePosition = ((g_gunFireTimer & 1) + g_animationEventData[0x254 + g_gunFireTimer / 2] * 2) << 15;
					}
				}

				g_buzzActor.primaryAnimIdx = (int16_t)primaryAnimationIndex;
				g_buzzActor.animationFramePosition = primaryFramePosition;
				g_buzzActor.secondaryAnimIdx = (int16_t)secondaryAnimationIndex;
				g_buzzActor.secondaryAnimationFramePosition = eventFramePosition;
				g_buzzActor.baseAnimationFramePosition = eventFramePosition;
				if (primaryAnimationIndex == secondaryAnimationIndex)
					g_buzzActor.secondaryAnimationFramePosition = primaryFramePosition;

				if (primaryAnimationIndex != secondaryAnimationIndex)
				{
					Animation::EvaluateClip(CharacterLoader::g_characterAnimationData[0]->clips[secondaryAnimationIndex].pointer,
						g_buzzActor.secondaryAnimationFramePosition,
						0,
						0);
				}
				Animation::g_applyBuzzBoneOffset = 1;
				Animation::EvaluateClip(CharacterLoader::g_characterAnimationData[0]->clips[primaryAnimationIndex].pointer, primaryFramePosition, 0, 1);
				Animation::g_applyBuzzBoneOffset = 0;

				if (Camera::g_scriptedCameraState != 0)
				{
					g_aimTargetPosition.x = 0x5000;
					g_aimTargetPosition.y = 0x1500;
					g_aimTargetPosition.z = -0x3800;
					Nu3D::Link::TransformVectorInt3x3(0x2F, &g_aimTargetPosition);
					g_aimTargetPosition.x += g_buzzActor.posAngles.pos.x;
					g_aimTargetPosition.y += g_buzzActor.posAngles.pos.y - 0x3000;
					g_aimTargetPosition.z += g_buzzActor.posAngles.pos.z;
				}
				else
				{
					g_aimTargetPosition.x = -0x85;
					g_aimTargetPosition.y = -0xD3;
					g_aimTargetPosition.z = -6;
					Actor::ResolveBoneAttachmentPos(&g_aimTargetPosition, &g_buzzActor.boneAttachmentActor, 15);
				}

				int32_t lateralSpeed = abs(g_buzzActor.lateralSpeed);
				if (((g_buzzActor.forwardSpeed < lateralSpeed && lateralSpeed > 0x100) || g_buzzActor.forwardSpeed < -0x100) && g_buzzActor.specialAirState != 0
					&& (g_buzzActor.actorFlags & ACTOR_FLAG_LOCK_FACING) == 0 && g_airborneTimer == 0)
				{
					g_animationEventFlags |= ANIMATION_EVENT_MOVEMENT;
				}
				if (g_buzzActor.forwardSpeed == 0 && g_buzzActor.lateralSpeed == 0)
					g_animationEventFlags |= ANIMATION_EVENT_IDLE;
				return;
			}
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

		// FUNCTION: TOY2 0x00484380 [PROVISIONAL]
		void ResolveCollisions(Toy2BuzzActor* buzz, MovementVelocity* movement, uint8_t* contactState, int32_t queryIndex, int32_t collisionPass)
		{
			Collision::SurfaceCollisionResult& result = reinterpret_cast<Collision::SurfaceCollisionResult*>(Collision::g_collisionQueryResults)[queryIndex];
			buzz->posAngles.pos.y -= result.collisionDistance + 0xC0;
			result.platformIndex = -1;
			result.movement.x = 0;
			result.movement.y = 0;
			result.movement.z = 0;
			Collision::g_collisionEdgeVertexCount = 0;
			Collision::g_collisionTriangleCount = 0;
			Collision::g_rotatedCollisionTriangleCount = 0;
			Collision::g_collisionPassFlags = 0;

			Vector3I16 requestedMovement;
			requestedMovement.x = (int16_t)movement->lateral;
			if (result.contactState == 1)
			{
				requestedMovement.y = (int16_t)movement->vertical;
			}
			else
			{
				requestedMovement.x += result.movement.x;
				requestedMovement.y = (int16_t)movement->vertical + result.movement.y;
			}
			requestedMovement.z = (int16_t)movement->forward;
			if (collisionPass != 0)
			{
				requestedMovement.x /= 2;
				requestedMovement.y /= 2;
				requestedMovement.z /= 2;
			}

			Platform::g_contactPlatformCount = 0;
			result.collisionDistance = 0;
			int32_t movementLength = (int32_t)sqrt(
				(double)(requestedMovement.x * requestedMovement.x + requestedMovement.y * requestedMovement.y + requestedMovement.z * requestedMovement.z));

			if (contactState[1] == 1)
			{
				int32_t meshIndex = result.contactFlags & Collision::COLLISION_CONTACT_MESH_INDEX_MASK;
				Collision::CollisionMeshInstance& mesh = Collision::g_collisionMeshInstances[meshIndex];
				const Vector3I16* normal =
					(result.contactFlags & Collision::COLLISION_CONTACT_SECONDARY_FACE) == 0 ? &result.face->normal : &result.face->secondaryNormal;
				Vector3I16 rotatedNormal;
				bool rotated = mesh.typeFlags == Collision::COLLISION_MESH_MOVING
					&& (Platform::g_platformStates[mesh.platformIdx].flags & Platform::PLATFORM_FLAG_ROTATED) != 0;
				if (rotated)
				{
					Platform::PlatformState& platform = Platform::g_platformStates[mesh.platformIdx];
					Vector3I16 angles = { (int16_t)(platform.rotationAnglesFixed.x >> 2),
						(int16_t)(platform.rotationAnglesFixed.y >> 2),
						(int16_t)(platform.rotationAnglesFixed.z >> 2) };
					Nu3D::Math::SetRotationXYZ(&angles, &Animation::g_keyframeRotation.matrix);
					int32_t value = Animation::g_keyframeRotation.matrix.m00 * normal->x + Animation::g_keyframeRotation.matrix.m01 * normal->y
						+ Animation::g_keyframeRotation.matrix.m02 * normal->z;
					rotatedNormal.x = (int16_t)((value + ((value >> 31) & 0xFFF)) >> 12);
					value = Animation::g_keyframeRotation.matrix.m10 * normal->x + Animation::g_keyframeRotation.matrix.m11 * normal->y
						+ Animation::g_keyframeRotation.matrix.m12 * normal->z;
					rotatedNormal.y = (int16_t)((value + ((value >> 31) & 0xFFF)) >> 12);
					value = Animation::g_keyframeRotation.matrix.m20 * normal->x + Animation::g_keyframeRotation.matrix.m21 * normal->y
						+ Animation::g_keyframeRotation.matrix.m22 * normal->z;
					rotatedNormal.z = (int16_t)((value + ((value >> 31) & 0xFFF)) >> 12);
					normal = &rotatedNormal;
				}

				if (normal->y >= -11999)
				{
					int32_t divisor;
					if (normal->y < -0x1000)
					{
						int32_t value = (movementLength + 0xC80) * normal->y;
						divisor = (value + ((value >> 31) & (rotated ? 0x7F : 0x1F))) >> (rotated ? 7 : 5);
						movement->lateral += normal->x * -0x1000 / divisor;
						movement->forward += normal->z * -0x1000 / divisor;
					}
					else
					{
						int32_t value = (movementLength + 0xC80) * 0x1000;
						divisor = (value + ((value >> 31) & (rotated ? 0x7F : 0x1F))) >> (rotated ? 7 : 5);
						movement->lateral += ((int32_t)normal->x << 12) / divisor;
						movement->forward += ((int32_t)normal->z << 12) / divisor;
					}
				}
			}

			int32_t value = result.collisionDistance * 8000;
			int32_t collisionRadius = movementLength + 0x1880 + ((value + ((value >> 31) & 0xFFF)) >> 12);
			if (queryIndex == 0)
				Platform::UpdateBuzzPlatformMotion(0, collisionPass);

			int16_t* candidateMeshes = reinterpret_cast<int16_t*>(Collision::g_mathScratch);
			int32_t candidateCount = 0;
			int32_t queryMaxX = buzz->posAngles.pos.x + collisionRadius;
			int32_t queryMaxY = buzz->posAngles.pos.y + collisionRadius;
			int32_t queryMaxZ = buzz->posAngles.pos.z + collisionRadius;
			int32_t queryDiameter = collisionRadius * 2;
			int32_t faceTolerance = (collisionRadius + 15 + (((collisionRadius + 15) >> 31) & 15)) >> 4;

			for (int32_t cellIndex = 0; cellIndex < Collision::g_activeCollisionGridCellCount; cellIndex++)
			{
				Collision::CollisionGridCell& cell = Collision::g_collisionGrid[cellIndex];
				if ((uint32_t)(queryMaxX - cell.boundsMinX) < (uint32_t)(cell.boundsExtentX + queryDiameter)
					&& (uint32_t)(queryMaxZ - cell.boundsMinZ) < (uint32_t)(cell.boundsExtentZ + queryDiameter))
				{
					for (int32_t index = 0; index < cell.meshCount; index++)
						candidateMeshes[candidateCount++] = Collision::g_collisionGridMeshIndices[cell.meshListStart + index];
				}
			}

			for (int32_t candidateIndex = 0; candidateIndex < candidateCount; candidateIndex++)
			{
				int32_t meshIndex = candidateMeshes[candidateIndex];
				Collision::CollisionMeshInstance& mesh = Collision::g_collisionMeshInstances[meshIndex];
				if ((uint32_t)(queryMaxX - mesh.boundsMin.x) >= (uint32_t)(mesh.boundsExt.x + queryDiameter)
					|| (uint32_t)(queryMaxY - mesh.boundsMin.y) >= (uint32_t)(mesh.boundsExt.y + queryDiameter)
					|| (uint32_t)(queryMaxZ - mesh.boundsMin.z) >= (uint32_t)(mesh.boundsExt.z + queryDiameter) || mesh.typeFlags == 0
					|| (mesh.unk & 0x100) != 0)
					continue;

				int32_t localX = queryMaxX - mesh.origin.x;
				int32_t localY = queryMaxY - mesh.origin.y;
				int32_t localZ = queryMaxZ - mesh.origin.z;
				localX = (localX + ((localX >> 31) & 31)) >> 5;
				localZ = (localZ + ((localZ >> 31) & 31)) >> 5;
				Collision::CollisionTreeGroup* group = reinterpret_cast<Collision::CollisionTreeGroup*>(mesh.collisionTree);
				while (group->marker >= 0)
				{
					int32_t faceCount = group->faceCount;
					Collision::PackedCollisionFace* face = reinterpret_cast<Collision::PackedCollisionFace*>(group + 1);
					if ((uint32_t)(localX - group->boundsMinX) < (uint32_t)(group->boundsExtentX + faceTolerance)
						&& (uint32_t)(localZ - group->boundsMinZ) < (uint32_t)(group->boundsExtentZ + faceTolerance))
					{
						for (int32_t faceIndex = 0; faceIndex < faceCount; faceIndex++, face++)
						{
							int32_t scaledY = (localY + ((localY >> 31) & 31)) >> 5;
							if ((uint32_t)(localX - face->boundsMinX) < (uint32_t)(face->boundsExtentX + faceTolerance)
								&& (uint32_t)(localZ - face->boundsMinZBlock * 64 - face->vertex0.z) < (uint32_t)(face->boundsExtentZBlock * 64 + faceTolerance)
								&& (uint32_t)(scaledY - face->boundsMinYBlock * 64 - face->vertex0.y)
									< (uint32_t)(face->boundsExtentYBlock * 64 + faceTolerance)
								&& Collision::g_collisionTriangleCount < 240)
							{
								int32_t triangleIndex = Collision::g_collisionTriangleCount++;
								Collision::g_collisionTriangles[triangleIndex] = face;
								Collision::g_collisionTriangleMeshIndices[triangleIndex] = (int16_t)meshIndex;
							}
						}
					}
					else
						face += faceCount;
					group = reinterpret_cast<Collision::CollisionTreeGroup*>(face);
				}
			}

			int32_t staticCandidateCount = candidateCount;
			if (queryIndex == 0)
			{
				Collision::CollisionGridCell& movingCell = Collision::g_collisionGrid[256];
				for (int32_t index = 0; index < movingCell.meshCount; index++)
					candidateMeshes[candidateCount++] = Collision::g_collisionGridMeshIndices[movingCell.meshListStart + index];

				for (int32_t candidateIndex = staticCandidateCount; candidateIndex < candidateCount; candidateIndex++)
				{
					int32_t meshIndex = candidateMeshes[candidateIndex];
					Collision::CollisionMeshInstance& mesh = Collision::g_collisionMeshInstances[meshIndex];
					Platform::PlatformState& platform = Platform::g_platformStates[mesh.platformIdx];
					int32_t platformRadius = collisionRadius + 0x400 + abs(platform.velocity.x) + abs(platform.velocity.y) + abs(platform.velocity.z);
					queryMaxX = buzz->posAngles.pos.x + platformRadius;
					queryMaxY = buzz->posAngles.pos.y;
					queryMaxZ = buzz->posAngles.pos.z + platformRadius;
					queryDiameter = platformRadius * 2;
					faceTolerance = (platformRadius + 15 + (((platformRadius + 15) >> 31) & 15)) >> 4;

					int32_t transformedX = queryMaxX;
					int32_t transformedY = queryMaxY;
					int32_t transformedZ = queryMaxZ;
					if ((platform.flags & Platform::PLATFORM_FLAG_ROTATED) != 0)
					{
						Vector3I16 angles = { (int16_t)(platform.rotationAnglesFixed.x >> 2),
							(int16_t)(platform.rotationAnglesFixed.y >> 2),
							(int16_t)(platform.rotationAnglesFixed.z >> 2) };
						Nu3D::Math::SetRotationXYZ(&angles, &Animation::g_keyframeRotation.matrix);
						int32_t deltaX = (buzz->posAngles.pos.x - mesh.origin.x) >> 5;
						int32_t deltaY = (buzz->posAngles.pos.y - mesh.origin.y) >> 5;
						int32_t deltaZ = (buzz->posAngles.pos.z - mesh.origin.z) >> 5;
						int32_t transformed = Animation::g_keyframeRotation.matrix.m00 * deltaX + Animation::g_keyframeRotation.matrix.m10 * deltaY
							+ Animation::g_keyframeRotation.matrix.m20 * deltaZ;
						transformedX = ((transformed + ((transformed >> 31) & 0x7F)) >> 7) + mesh.origin.x + platformRadius;
						transformed = Animation::g_keyframeRotation.matrix.m01 * deltaX + Animation::g_keyframeRotation.matrix.m11 * deltaY
							+ Animation::g_keyframeRotation.matrix.m21 * deltaZ;
						transformedY = ((transformed + ((transformed >> 31) & 0x7F)) >> 7) + mesh.origin.y;
						transformed = Animation::g_keyframeRotation.matrix.m02 * deltaX + Animation::g_keyframeRotation.matrix.m12 * deltaY
							+ Animation::g_keyframeRotation.matrix.m22 * deltaZ;
						transformedZ = ((transformed + ((transformed >> 31) & 0x7F)) >> 7) + mesh.origin.z + platformRadius;
					}
					transformedY += platformRadius;

					if ((uint32_t)(transformedX - mesh.boundsMin.x) < (uint32_t)(mesh.boundsExt.x + queryDiameter)
						&& (uint32_t)(transformedZ - mesh.boundsMin.z) < (uint32_t)(mesh.boundsExt.z + queryDiameter))
					{
						Platform::g_contactPlatformIndices[Platform::g_contactPlatformCount++] = (int16_t)mesh.platformIdx;
						if (mesh.typeFlags != 0 && (mesh.unk & 0x100) == 0)
						{
							int32_t localX = transformedX - mesh.origin.x;
							int32_t localY = transformedY - mesh.origin.y;
							int32_t localZ = transformedZ - mesh.origin.z;
							localX = (localX + ((localX >> 31) & 31)) >> 5;
							localZ = (localZ + ((localZ >> 31) & 31)) >> 5;
							Collision::CollisionTreeGroup* group = reinterpret_cast<Collision::CollisionTreeGroup*>(mesh.collisionTree);
							while (group->marker >= 0)
							{
								int32_t faceCount = group->faceCount;
								Collision::PackedCollisionFace* face = reinterpret_cast<Collision::PackedCollisionFace*>(group + 1);
								if ((uint32_t)(localX - group->boundsMinX) < (uint32_t)(group->boundsExtentX + faceTolerance)
									&& (uint32_t)(localZ - group->boundsMinZ) < (uint32_t)(group->boundsExtentZ + faceTolerance))
								{
									for (int32_t faceIndex = 0; faceIndex < faceCount; faceIndex++, face++)
									{
										int32_t scaledY = (localY + ((localY >> 31) & 31)) >> 5;
										if ((uint32_t)(localX - face->boundsMinX) < (uint32_t)(face->boundsExtentX + faceTolerance)
											&& (uint32_t)(localZ - face->boundsMinZBlock * 64 - face->vertex0.z)
												< (uint32_t)(face->boundsExtentZBlock * 64 + faceTolerance)
											&& (uint32_t)(scaledY - face->boundsMinYBlock * 64 - face->vertex0.y)
												< (uint32_t)(face->boundsExtentYBlock * 64 + faceTolerance))
										{
											if (Collision::g_collisionTriangleCount < 240)
											{
												int32_t triangleIndex = Collision::g_collisionTriangleCount++;
												Collision::g_collisionTriangles[triangleIndex] = face;
												Collision::g_collisionTriangleMeshIndices[triangleIndex] = (int16_t)meshIndex;
											}
											if ((platform.flags & Platform::PLATFORM_FLAG_ROTATED) != 0)
											{
												int32_t rotatedIndex = Collision::g_rotatedCollisionTriangleCount++;
												Collision::g_rotatedCollisionTriangles[rotatedIndex] = face;
												Collision::g_rotatedCollisionTriangleMeshIndices[rotatedIndex] = (int16_t)meshIndex;
											}
										}
									}
								}
								else
									face += faceCount;
								group = reinterpret_cast<Collision::CollisionTreeGroup*>(face);
							}
						}
					}
					else
					{
						mesh.origin.x += platform.remainingTranslation.x;
						mesh.origin.y += platform.remainingTranslation.y;
						mesh.origin.z += platform.remainingTranslation.z;
						mesh.boundsMin.x += platform.remainingTranslation.x;
						mesh.boundsMin.z += platform.remainingTranslation.z;
						platform.remainingTranslation.x = 0;
						platform.remainingTranslation.y = 0;
						platform.remainingTranslation.z = 0;
						platform.rotationAnglesFixed.x += platform.remainingRotation.x;
						platform.rotationAnglesFixed.y += platform.remainingRotation.y;
						platform.rotationAnglesFixed.z += platform.remainingRotation.z;
						platform.remainingRotation.x = 0;
						platform.remainingRotation.y = 0;
						platform.remainingRotation.z = 0;
					}
				}
			}

			int32_t minimumX = (queryMaxX - queryDiameter + (((queryMaxX - queryDiameter) >> 31) & 31)) >> 5;
			int32_t minimumZ = (queryMaxZ - queryDiameter + (((queryMaxZ - queryDiameter) >> 31) & 31)) >> 5;
			int32_t maximumX = (queryMaxX + ((queryMaxX >> 31) & 31)) >> 5;
			int32_t maximumZ = (queryMaxZ + ((queryMaxZ >> 31) & 31)) >> 5;
			uint8_t* terrainHead = Terrain::g_terrainRelocationHeads[1];
			Terrain::TerrainEdgeChain* terrainChain =
				terrainHead == 0 ? 0 : reinterpret_cast<Terrain::TerrainEdgeChain*>(terrainHead - offsetof(Terrain::TerrainEdgeChain, edgeCount));
			while (terrainChain != 0)
			{
				int32_t edgeIndex = 0;
				int32_t edgeCount = terrainChain->edgeCount;
				while (edgeIndex < edgeCount)
				{
					Vector3I* vertices = &terrainChain->vertices[edgeIndex];
					int32_t blockEnd = 0;
					if (vertices[0].y != (int32_t)0x80000000 && (uint32_t)(maximumX - vertices[0].y) < (uint32_t)(vertices[1].y + faceTolerance)
						&& (uint32_t)(maximumZ - vertices[2].y) < (uint32_t)(vertices[3].y + faceTolerance))
						blockEnd = edgeIndex + 16;

					if (edgeIndex < blockEnd)
					{
						for (int32_t index = edgeIndex; index < blockEnd; index++, vertices++)
						{
							Vector3I* start = vertices;
							Vector3I* end = vertices + 1;
							if (((minimumX <= start->x && end->x <= maximumX) || (minimumX <= end->x && start->x <= maximumX))
								&& ((minimumZ <= start->z && end->z <= maximumZ) || (minimumZ <= end->z && start->z <= maximumZ))
								&& Collision::g_collisionEdgeVertexCount < 32)
							{
								int32_t outputIndex = Collision::g_collisionEdgeVertexCount;
								Collision::g_collisionEdgeVertices[outputIndex] = *start;
								Collision::g_collisionEdgeVertices[outputIndex + 1] = *end;
								Collision::g_collisionEdgeVertexCount += 2;
							}
						}
					}
					edgeIndex += 16;
				}
				terrainChain = terrainChain->next;
			}

			contactState[0] = 0;
			int32_t triangleCount = Collision::g_collisionTriangleCount;
			result.contactState = 0;
			result.surfaceType = 0xFF;
			bool hadGroundResponse = false;
			if (triangleCount < 1 && Collision::g_collisionEdgeVertexCount < 1)
			{
				Platform::AdvancePartialMotion(100, 0, 0);
				Platform::AdvancePartialMotion(100, 0, 1);
				contactState[1] = 0;
				buzz->posAngles.pos.x += collisionPass == 0 ? movement->lateral : movement->lateral / 2;
				buzz->posAngles.pos.y += collisionPass == 0 ? movement->vertical : movement->vertical / 2;
				buzz->posAngles.pos.z += collisionPass == 0 ? movement->forward : movement->forward / 2;
				result.movement.x = 0;
				result.movement.y = 0;
				result.movement.z = 0;
			}
			else
			{
				Vector3I position = buzz->posAngles.pos;
				Vector3I velocity = { movement->lateral, movement->vertical, movement->forward };
				Collision::g_hadGroundResponse = 0;
				Collision::CollisionStepMotion stepMotion;
				stepMotion.movement.x = (int16_t)movement->lateral;
				stepMotion.movement.y = (int16_t)movement->vertical;
				stepMotion.movement.z = (int16_t)movement->forward;
				if (queryIndex == 0)
					stepMotion.platformMovement = result.movement;
				else
					stepMotion.platformMovement.x = stepMotion.platformMovement.y = stepMotion.platformMovement.z = 0;
				if (collisionPass != 0)
				{
					stepMotion.movement.x /= 2;
					stepMotion.movement.y /= 2;
					stepMotion.movement.z /= 2;
					stepMotion.platformMovement.x /= 2;
					stepMotion.platformMovement.y /= 2;
					stepMotion.platformMovement.z /= 2;
				}

				int16_t remainingSteps = (Collision::g_collisionTriangleCount < 11) + 3;
				int32_t stepResult;
				do
				{
					int32_t footingResult = 0;
					if (contactState[1] == 1)
					{
						footingResult = Collision::ResolvePlatformFooting(&stepMotion, &position, &velocity, &result);
						if (footingResult == 1)
							contactState[0] = 1;
					}
					stepResult = Collision::ResolveSubstep(&stepMotion,
						&position,
						&velocity,
						&result,
						Collision::g_collisionTriangleMeshIndices,
						Collision::g_collisionTriangles,
						Collision::g_collisionTriangleCount,
						1);
					if (stepResult == 2)
					{
						contactState[1] = 0;
						hadGroundResponse = true;
					}
					else if (footingResult == 1 || stepResult == 1)
					{
						contactState[0] = 1;
						contactState[1] = 1;
					}
					else
						contactState[1] = 0;
					remainingSteps--;
				} while (remainingSteps > 0 && stepResult > 0);

				buzz->posAngles.pos = position;
				movement->lateral = velocity.x;
				movement->vertical = velocity.y;
				movement->forward = velocity.z;
				if (queryIndex == 0)
				{
					value = stepMotion.platformMovement.x * 3;
					result.movement.x = (int16_t)((value + ((value >> 31) & 3)) >> 2);
					value = stepMotion.platformMovement.y * 3;
					result.movement.y = (int16_t)((value + ((value >> 31) & 3)) >> 2);
					value = stepMotion.platformMovement.z * 3;
					result.movement.z = (int16_t)((value + ((value >> 31) & 3)) >> 2);
				}
			}

			buzz->posAngles.pos.y += result.collisionDistance + 0xC0;
			if (buzz->posAngles.pos.x == g_buzzActor.motionTargetPos.x && buzz->posAngles.pos.y == g_buzzActor.motionTargetPos.y
				&& buzz->posAngles.pos.z == g_buzzActor.motionTargetPos.z && hadGroundResponse)
				result.contactTimer += Renderer::g_frameDelta;
			else
				result.contactTimer = 0;
			if (result.contactTimer > 20)
			{
				result.contactTimer = 20;
				contactState[0] = 1;
			}
			result.surfaceVelocity.x = 0;
			result.surfaceVelocity.y = 0;
			result.surfaceVelocity.z = 0;
		}

		// FUNCTION: TOY2 0x004855F0 [MATCHED]
		void HandleCollisions(Toy2BuzzActor* buzz, MovementVelocity* movement, uint8_t* contactState, int32_t queryIndex)
		{
			if (movement->lateral * movement->lateral + movement->vertical * movement->vertical + movement->forward * movement->forward > 0x400000)
			{
				ResolveCollisions(buzz, movement, contactState, queryIndex, 1);
				uint8_t firstPassContacts = contactState[0] | contactState[1];
				ResolveCollisions(buzz, movement, contactState, queryIndex, 2);
				contactState[1] |= firstPassContacts;
			}
			else
			{
				ResolveCollisions(buzz, movement, contactState, queryIndex, 0);
			}
		}

		// FUNCTION: TOY2 0x004071E0 [PROVISIONAL]
		void HandleDamage(uint32_t direction, uint32_t damageFlags)
		{
			if (g_buzzActor.health < 0 || (g_gameplayStateFlags & 1) != 0)
			{
				return;
			}

			if (Camera::g_scriptedCameraState != 0)
			{
				Camera::g_scriptedCameraState = 5;
			}

			if ((damageFlags & 1) != 0)
			{
				g_buzzActor.velocity.lateral = Numerics::g_sinCosLUT[direction & 0xFFF] / 16;
				g_buzzActor.velocity.forward = Numerics::g_sinCosLUT[(direction + 0x400) & 0xFFF] / 16;
			}

			if ((damageFlags & DAMAGE_NORMAL) != 0 && g_buzzActor.stunTimer <= 0)
			{
				g_buzzActor.actorFlags |= ACTOR_FLAG_DAMAGE_REACTION;
				if (g_buzzActor.cosmicShieldTimer == 0)
				{
					if ((int16_t)g_levelTransition != 1 && (int16_t)g_levelTransition != 5 && g_ledgeClimbTimer == 0)
					{
						goto damageTypeDispatch;
					}
				}
				else if (g_buzzActor.cosmicShieldTimer < 0)
				{
					if (g_buzzActor.stunTimer != 0)
					{
						damageFlags &= ~DAMAGE_NORMAL;
						goto damageTypeDispatch;
					}
					g_buzzActor.cosmicShieldTimer++;
				}

				g_buzzActor.stunTimer = -60;
				damageFlags &= ~DAMAGE_NORMAL;
			}

		damageTypeDispatch:
			if ((damageFlags & DAMAGE_FORCE_DEATH) != 0)
			{
				if ((int16_t)g_levelTransition != 2)
				{
					if (Camera::g_scriptedCameraState != 0)
					{
						Camera::g_scriptedCameraState = 5;
					}
					AudioManager::Preset::PlayOneShotSound2(0x15, &g_buzzActor);
					if (AudioManager::g_maxLeftVolume < 0xE)
					{
						AudioManager::g_maxLeftVolume = 0xE;
					}
					g_buzzActor.actorFlags |= ACTOR_FLAG_DEATH_TRANSITION | ACTOR_FLAG_LOCK_FACING;
					g_levelTransition = 2;
					g_levelTransitionTimer = 0x2E;
					g_buzzActor.stunTimer = -90;
					HUD::g_slideTimers[0] = 180;
				}
				return;
			}

			if ((damageFlags & DAMAGE_NORMAL) == 0)
			{
				return;
			}

			g_buzzActor.health--;
			g_jumpHeightControlActive = 0;
			g_buzzActor.airborneMode = 5;
			g_buzzActor.velocity.vertical = -0x200;
			g_buzzActor.collisionFlags = 0;
			g_buzzActor.specialAirState = 0;
			ResetBuzzState();
			g_damageRegistered = 1;
			HUD::g_slideTimers[1] = 180;

			if (g_buzzActor.health < 0)
			{
				int16_t deathTransition = 2;
				if (g_levelTransition != deathTransition)
				{
					if (Camera::g_scriptedCameraState != 0)
					{
						Camera::g_scriptedCameraState = 5;
					}
					AudioManager::Preset::PlayOneShotSound2(0x15, &g_buzzActor);
					if (AudioManager::g_maxLeftVolume < 0xE)
					{
						AudioManager::g_maxLeftVolume = 0xE;
					}
					g_buzzActor.actorFlags |= ACTOR_FLAG_LOCK_FACING;
					HUD::g_slideTimers[0] = 180;
					g_levelTransition = deathTransition;
					g_levelTransitionTimer = 120;
					g_buzzActor.stunTimer = -90;
				}
				return;
			}

			g_buzzActor.stunTimer = 90;
			if (AudioManager::IsActorSoundPlaying(&g_buzzActor) == 0)
			{
				AudioManager::Preset::PlayOneShotSound2(0x1A, &g_buzzActor);
			}
			if (AudioManager::g_maxLeftVolume < 0xE)
			{
				AudioManager::g_maxLeftVolume = 0xE;
			}
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