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

// The contact effects of Buzz: retail holds 0x004A2D80 as one object.
namespace Toy2
{
	namespace Buzz
	{
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
}
