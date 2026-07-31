#include "Toy2/Buzz.h"
#include "Toy2/Animation.h"
#include "Toy2/Actor.h"
#include "Toy2/Camera.h"
#include "Toy2/Collision.h"
#include "Toy2/Collectables.h"
#include "Toy2/Levels.h"
#include "Toy2/LevelLogic.h"
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
	// GLOBAL: TOY2 0x005281A4
	int32_t g_pendingFootingType;

	// GLOBAL: TOY2 0x0052B81C
	int32_t g_footingType;

	// GLOBAL: TOY2 0x0053C650
	int32_t g_spinCooldownTimer;

	// GLOBAL: TOY2 0x0053C654
	int32_t g_swingFacingAngle;

	// GLOBAL: TOY2 0x0053C83C
	int32_t g_spinHoverTimer;

	extern uint8_t g_environmentTintRed;
	extern uint8_t g_environmentTintGreen;
	extern uint8_t g_environmentTintBlue;
	extern Nu3D::Particles::ParticleInstance* g_laserAimParticle;
	extern Vector4I g_aimTargetPosition;
	extern int32_t g_aimTargetIndex;
	extern int32_t g_gunFireTimer;
	extern int32_t g_gunChargeTimer;
	extern int32_t g_airborneTimer;
	namespace Camera
	{
		extern int32_t g_forwardInputDisabled;
	}
	extern uint32_t g_animationEventFlags;
	extern int32_t g_surfaceEffectState;

	namespace Buzz
	{
		enum AnimationEventFlag
		{
			ANIMATION_EVENT_RIGHT_FOOT = 0x1,
			ANIMATION_EVENT_LEFT_FOOT = 0x2,
			ANIMATION_EVENT_MOVEMENT = 0x4,
			ANIMATION_EVENT_IDLE = 0x8,
			ANIMATION_EVENT_FORCE_EFFECT = 0x10,
		};

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

		// STUB: TOY2 0x00436220
		void HandleGameplay(Toy2BuzzActor* buzz) {}

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
					Nu3D::Camera::g_cameraTintBlue = g_environmentTintBlue;
					Nu3D::Camera::g_cameraTintGreen = g_environmentTintGreen;
					Nu3D::Camera::g_cameraTintRed = g_environmentTintRed;
					Nu3D::Camera::g_tintBlend = (int16_t)tintBlend;
				}
				else
				{
					Nu3D::Camera::g_tintBlend = -1;
					Nu3D::Camera::g_cameraTintBlue = 0x80;
					Nu3D::Camera::g_cameraTintGreen = 0x80;
					Nu3D::Camera::g_cameraTintRed = 0x80;
				}
			}
			else if (Nu3D::Camera::g_tintBlend != -1)
			{
				Nu3D::Camera::g_tintBlend = -1;
				Nu3D::Camera::g_cameraTintBlue = 0x80;
				Nu3D::Camera::g_cameraTintGreen = 0x80;
				Nu3D::Camera::g_cameraTintRed = 0x80;
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

	// GLOBAL: TOY2 0x0050A540
	int32_t g_aimTargetLocked;

	// GLOBAL: TOY2 0x0053C5D4
	int32_t g_turnRecoveryTimer;

	// GLOBAL: TOY2 0x0053C5D8
	int32_t g_ziplineEndProgress;

	// GLOBAL: TOY2 0x0053C5E0
	int32_t g_rocketBootsTimer;

	// GLOBAL: TOY2 0x0053C5E8
	int32_t g_environmentEffectType;

	// GLOBAL: TOY2 0x0053C5F0
	Vector3I g_swingAnchorPosition;

	// GLOBAL: TOY2 0x0053C604
	int32_t g_ziplineTargetYOrSpeed;

	// GLOBAL: TOY2 0x0053C614
	int32_t g_ziplineProgress;

	// GLOBAL: TOY2 0x0053C628
	int32_t g_environmentSurfaceY;

	// GLOBAL: TOY2 0x0053C638
	Vector3I g_ziplineDirection;

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

	// GLOBAL: TOY2 0x00559E80
	int32_t g_ledgeClimbPlatformIndex;

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
					g_buzzActor.animationState = 26;
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
		const uint32_t GROUND_SLAM_BLOCKING_ACTIONS = 0xFFF7F;
		const uint32_t GUN_IDLE_BLOCKING_ACTIONS = 0xFBE3C;
		const uint32_t GUN_START_BLOCKING_ACTIONS = 0xFBEFE;
		const uint32_t GUN_REPEAT_BLOCKING_ACTIONS = 0xFBE7E;
		const uint32_t ACTION_STATE_GUN_FIRE = 0x80;
		const int32_t SURFACE_DAMAGE_GROUP = 0;
		const int32_t SURFACE_DEATH_GROUP = 4;
		const int32_t SLIPPERY_SURFACE_QUALITY = 13;
		const uint32_t ACTION_STATE_GROUND_SLAM = 0x40;
		const uint32_t CLEAR_ACTION_STATE_GUN_FIRE = 0xFF7F;
		const uint32_t ACTION_STATE_SPIN_HOVER = 0x2;
		const uint32_t SPIN_START_BLOCKING_ACTIONS = 0xFFF7F;
		const uint32_t SPIN_CHARGE_BLOCKING_ACTIONS = 0xFFFFE;
		const uint32_t SPIN_HOVER_BLOCKING_ACTIONS = 0xFFF7E;
		const uint32_t LEDGE_CLIMB_BLOCKING_ACTIONS = 0xFFF7F;
		const uint32_t POLE_CLIMB_BLOCKING_ACTIONS = 0xFFF6E;
		const uint32_t SWING_BLOCKING_ACTIONS = 0xFFB7E;
		const uint32_t ACTION_STATE_SWING = 0x400;
		const int16_t SWING_START_ANIMATION_STATE = 26;
		const uint32_t TURN_RECOVERY_ACTION_MASK = 0xFF481;
		const int16_t GROUND_SLAM_ANIMATION_STATE = 8;
		const int32_t FACING_SNAP_THRESHOLD = 0x5DC;
		const int32_t JUMP_HEIGHT_TARGET = 0x6A80;
		const int32_t MIN_CONTROLLED_JUMP_VELOCITY = -0x74C;
		const int32_t MAX_CONTROLLED_JUMP_VELOCITY = -0x22A;

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

		// STUB: TOY2 0x00484380
		void ResolveCollisions(Toy2BuzzActor* buzz, MovementVelocity* movement, uint8_t* contactState, int32_t queryIndex, int32_t collisionPass) {}

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

		struct SwingRecord
		{
			Vector3I start;
			Vector3I end;
		};
		STATIC_ASSERT(sizeof(SwingRecord) == 0x18);

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

		struct PoleRecord
		{
			Vector3I position;
			int32_t type;
			int32_t height;
			int32_t reserved;
		};
		STATIC_ASSERT(sizeof(PoleRecord) == 0x18);

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

		struct ZiplineRecord
		{
			Vector3I start;
			Vector3I end;
		};
		STATIC_ASSERT(sizeof(ZiplineRecord) == 0x18);

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

		// FUNCTION: TOY2 0x004A4B90 [MATCHED]
		void ResetGravityBoots()
		{
			if (g_gravityBootsTimer != 0)
			{
				g_gravityBootsTimer = 0;
			}
		}

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
}
