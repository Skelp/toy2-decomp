#include "Nu3D/Particles.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "AudioManager/AudioManager.h"
#include "Random.h"
#include "Renderer/Renderer.h"
#include "Renderer/Shadows.h"
#include "Toy2/Actor.h"
#include "Toy2/Buzz.h"
#include "Toy2/Camera.h"
#include "Toy2/Collision.h"
#include "Toy2/Lighting.h"
#include "Toy2/Toy2.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace Nu3D
{
	namespace Particles
	{
		enum ParticleUpdateFlags
		{
			PARTICLE_COLLIDES_WITH_GROUND = 0x1,
			PARTICLE_TRACKS_TARGET = 0x8,
			PARTICLE_RENDER_ACTIVE = 0x100,
			PARTICLE_SPAWNS_TRAIL = 0x200,
		};

		void DeathEffect(ParticleInstance* particle);

		// FUNCTION: TOY2 0x00410F40 [PROVISIONAL]
		void Update()
		{
			ParticleInstance* particle = g_particleInstances;
			int32_t colourMode = 0;
			int32_t particleIndex = 0;
			do
			{
				if (particle->lifetime > 0)
				{
					particle->lifetime -= (int16_t)Renderer::g_frameDelta;
					if (particle->lifetime < 0)
					{
						particle->lifetime = 0;
					}

					int32_t verticalVelocity;
					int32_t verticalAcceleration;
					if ((particle->renderFlags & PARTICLE_TRACKS_TARGET) != 0)
					{
						int32_t velocityDivisor;
						int32_t turnDivisor;
						if (particle->updateParam == 0xC4)
						{
							velocityDivisor = 8;
							int32_t remainingTime = particle->lifetime / 16;
							turnDivisor = remainingTime * remainingTime + 4;
						}
						else
						{
							velocityDivisor = 16;
							turnDivisor = 8;
						}

						if (particle->lifetime > 0)
						{
							int32_t targetX;
							int32_t targetY;
							int32_t targetZ;
							if (particle->targetActor == (Toy2::Actor::Toy2Actor*)-1)
							{
								targetX = Toy2::g_buzzActor.posAngles.pos.x - particle->pos.x;
								targetY = Toy2::g_buzzActor.posAngles.pos.y - particle->pos.y - 0x2000;
								targetZ = Toy2::g_buzzActor.posAngles.pos.z - particle->pos.z;
							}
							else
							{
								Toy2::Actor::Toy2Actor* target = particle->targetActor;
								Toy2::Actor::ActorCollisionVolume* volume = target->collisionVolumes + target->primaryAnimIdx;
								if (target->actorPhase == 0 || target->hitpoints < 0)
								{
									particle->lifetime = 1;
								}
								targetX = target->pos.x + volume->offset.x - particle->pos.x;
								targetY = target->pos.y + volume->offset.y - particle->pos.y;
								targetZ = target->pos.z + volume->offset.z - particle->pos.z;
								velocityDivisor = 6;
								if (particle->lifetime < 0x20)
								{
									turnDivisor = (particle->lifetime & 0xF) + 2;
								}
								else
								{
									turnDivisor = (particle->lifetime & 0xF) * 3 + 2;
								}
							}

							targetX >>= 5;
							targetY >>= 5;
							targetZ >>= 5;
							int32_t targetYaw = Math::CartesianToFixedAngle(targetX, targetZ);
							int32_t yawDelta = (targetYaw - particle->yawAngle) & 0xFFF;
							if (yawDelta > 0x7FF)
							{
								yawDelta -= 0x1000;
							}
							particle->yawAngle = (particle->yawAngle + Renderer::g_frameDelta * yawDelta / turnDivisor) & 0xFFF;

							if (particle->discPitchAngle != -1)
							{
								int32_t horizontalDistance = (int32_t)sqrt((double)(targetX * targetX + targetZ * targetZ));
								int32_t targetPitch = Math::CartesianToFixedAngle(horizontalDistance, targetY);
								int32_t pitchDelta = (targetPitch - 0x400 - particle->discPitchAngle) & 0xFFF;
								if (pitchDelta > 0x7FF)
								{
									pitchDelta -= 0x1000;
								}
								particle->discPitchAngle = (particle->discPitchAngle + Renderer::g_frameDelta * pitchDelta / 20) & 0xFFF;
							}
						}

						int32_t pitchSine;
						int32_t pitchCosine;
						if (particle->discPitchAngle == -1)
						{
							pitchSine = 0;
							pitchCosine = 0x4000;
						}
						else
						{
							pitchSine = Numerics::g_sinCosLUT[particle->discPitchAngle & 0xFFF];
							pitchCosine = Numerics::g_sinCosLUT[(particle->discPitchAngle + 0x400) & 0xFFF];
						}
						verticalVelocity = -pitchSine / velocityDivisor;
						int32_t yaw = particle->yawAngle + 0x400;
						particle->velX = (Numerics::g_sinCosLUT[particle->yawAngle & 0xFFF] * pitchCosine >> 14) / velocityDivisor;
						particle->velZ = (Numerics::g_sinCosLUT[yaw & 0xFFF] * pitchCosine >> 14) / velocityDivisor;
						verticalAcceleration = 0;
					}
					else
					{
						verticalVelocity = particle->velY;
						verticalAcceleration = particle->yawAngle;
					}

					particle->pos.y += Renderer::g_frameDelta * verticalVelocity;
					particle->velY += Renderer::g_frameDelta * verticalAcceleration;
					particle->pos.x += particle->velX * Renderer::g_frameDelta;
					particle->pos.z += particle->velZ * Renderer::g_frameDelta;

					if (particle->animationFrameCount > 1)
					{
						particle->animationTimer -= (uint8_t)Renderer::g_frameDelta;
						while ((int8_t)particle->animationTimer <= 0)
						{
							particle->animationTimer += particle->updateParam;
							particle->tileIndex++;
							if (particle->tileIndex >= particle->animationFrameCount)
							{
								particle->tileIndex -= particle->animationFrameCount;
							}
						}
					}

					if (particle->rotSpeed != 0)
					{
						particle->groundAlignRot = (particle->groundAlignRot + particle->rotSpeed * (int16_t)Renderer::g_frameDelta) & 0xFFF;
					}

					int16_t shadowCount = Renderer::Shadows::g_shadowCount;
					if ((particle->renderFlags & PARTICLE_COLLIDES_WITH_GROUND) != 0 && particle->lifetime > 0)
					{
						if (particle->velX != 0 || particle->velZ != 0 || particle->groundHeightY == INT_MIN)
						{
							particle->groundHeightY = Collision::GetGroundHeight(&particle->groundProbe, 0);
						}
						if (particle->groundHeightY != INT_MIN)
						{
							if (particle->groundHeightY < particle->pos.y + particle->height * 0x20)
							{
								particle->lifetime = 0;
								if (particle->pos.y + particle->yawAngle - particle->velY < particle->groundHeightY)
								{
									particle->pos.y = particle->groundHeightY - particle->height * 0x20;
									if (shadowCount < 47)
									{
										Renderer::Shadows::ShadowInstance* shadow = &Renderer::Shadows::g_shadowInstances[shadowCount];
										shadow->pos.x = particle->pos.x;
										shadow->pos.y = particle->groundHeightY;
										shadow->pos.z = particle->pos.z;
										shadow->size = particle->width;
										Renderer::Shadows::g_shadowCount++;
									}
								}
							}
							else if (shadowCount < 47)
							{
								Renderer::Shadows::ShadowInstance* shadow = &Renderer::Shadows::g_shadowInstances[shadowCount];
								shadow->pos.x = particle->pos.x;
								shadow->pos.y = particle->groundHeightY;
								shadow->pos.z = particle->pos.z;
								shadow->size = particle->width;
								Renderer::Shadows::g_shadowCount++;
							}
						}
					}

					int32_t distanceX = (Toy2::Camera::g_renderCameraTransform.pos.x - particle->pos.x) >> 8;
					int32_t distanceY = (Toy2::Camera::g_renderCameraTransform.pos.y - particle->pos.y) >> 8;
					int32_t distanceZ = (Toy2::Camera::g_renderCameraTransform.pos.z - particle->pos.z) >> 8;
					if (distanceX * distanceX + distanceY * distanceY + distanceZ * distanceZ > 0x190000)
					{
						particle->lifetime = 0;
						particle->collisionFlags = 0;
					}
					if ((particle->renderFlags & (PARTICLE_RENDER_ACTIVE | PARTICLE_SPAWNS_TRAIL)) == 0)
					{
						particle->lifetime = 0;
					}

					switch (particle->updateMode)
					{
						case 1:
							if (shadowCount < 47)
							{
								Renderer::Shadows::ShadowInstance* shadow = &Renderer::Shadows::g_shadowInstances[shadowCount];
								shadow->pos.x = particle->pos.x;
								shadow->pos.y = particle->pos.y + particle->height * 0x20;
								shadow->pos.z = particle->pos.z;
								shadow->size = particle->width;
								Renderer::Shadows::g_shadowCount++;
							}
							break;
						case 2:
							colourMode = 1;
							break;
						case 3:
							colourMode = 2;
							break;
						case 4:
							colourMode = 1;
							if (particle->lifetime > 0x10 && Toy2::g_framePulseOutputs.fourTick != 0)
								SpawnFromPreset(particle->pos.x, particle->pos.y, particle->pos.z, 3, 3);
							break;
						case 5:
							colourMode = 3;
							break;
						case 6:
							colourMode = 4;
							break;
						case 7:
							Renderer::LensFlare::RegisterLight(
								particle->pos.x, particle->pos.y, particle->pos.z, particle->colourR, particle->colourG, particle->colourB, 0x30);
							colourMode = 1;
							break;
						case 8: {
							uint32_t angle = ((uint8_t)particle->lifetime & 0x1F) * 0x80;
							particle->width = (Numerics::g_sinCosLUT[angle] >> 10) + 0x78;
							particle->height = (Numerics::g_sinCosLUT[(angle + 0x400) & 0xFFF] >> 10) + 0x78;
							if (Toy2::g_framePulseOutputs.sevenTick != 0 && particle->lifetime > 5)
							{
								ParticleInstance* spawned = SpawnFromPreset(particle->pos.x, particle->pos.y, particle->pos.z, 0xF, 2);
								spawned->rotSpeed = ((int32_t)*g_randDatBufferPtr++ - 0x80) >> 3;
							}
							break;
						}
						case 9:
							colourMode = 1;
							particle->width += (int16_t)Renderer::g_frameDelta * 2;
							particle->height += (int16_t)Renderer::g_frameDelta * 2;
							break;
						case 10:
							particle->width -= (int16_t)Renderer::g_frameDelta * 4;
							colourMode = 5;
							if (particle->width < 0x10)
								particle->width = 0x10;
							particle->height = particle->width;
							break;
						case 11:
							particle->width += (uint16_t)particle->updateParam * (int16_t)Renderer::g_frameDelta;
							colourMode = 1;
							particle->height = particle->width;
							break;
						case 12:
							colourMode = 1;
							particle->pos.x = Toy2::g_buzzActor.posAngles.pos.x;
							particle->pos.y = Toy2::g_buzzActor.posAngles.pos.y - particle->lifetime * 0x22A;
							particle->pos.z = Toy2::g_buzzActor.posAngles.pos.z;
							particle->width -= (int16_t)Renderer::g_frameDelta * 8;
							particle->height = particle->width;
							break;
						case 13:
							colourMode = 1;
							particle->pos.x = Toy2::g_buzzActor.posAngles.pos.x;
							particle->pos.y = Toy2::g_buzzActor.posAngles.pos.y - (particle->lifetime + 0x10) * 400;
							particle->pos.z = Toy2::g_buzzActor.posAngles.pos.z;
							particle->width -= (int16_t)Renderer::g_frameDelta * 5;
							particle->height = particle->width;
							break;
						case 14:
							particle->groundAlignRot = (uint16_t)(*g_randDatBufferPtr++ << 4);
							colourMode = particle->lifetime < 0x20 ? 1 : 6;
							Renderer::LensFlare::RegisterLight(
								particle->pos.x, particle->pos.y, particle->pos.z, particle->colourR, particle->colourG, particle->colourB, 0x30);
							break;
						case 15:
							particle->width -= (uint16_t)particle->updateParam * (int16_t)Renderer::g_frameDelta;
							if (particle->width < 1)
								particle->width = 1;
							particle->height = particle->width;
							break;
						case 16:
							if (particle->lifetime > 4 && Toy2::g_framePulseOutputs.fourTick != 0)
								SpawnFromPreset(particle->pos.x, particle->pos.y, particle->pos.z, 0x24, 0xF);
							break;
						case 17:
							if (particle->lifetime > 4)
							{
								for (int32_t i = 0; i < Toy2::g_framePulseOutputs.threeTick; i++)
									SpawnFromPreset(particle->pos.x, particle->pos.y, particle->pos.z, 0x27, 2);
							}
							break;
						case 18:
							if (particle->lifetime > 4 && Toy2::g_framePulseOutputs.sevenTick != 0)
							{
								int32_t offset = *g_randDatBufferPtr * 2 - 0x100;
								g_randDatBufferPtr += 3;
								ParticleInstance* spawned =
									SpawnFromPreset(particle->pos.x + offset, particle->pos.y + offset, particle->pos.z + offset, 0x2C, 2);
								spawned->rotSpeed = ((int32_t)*g_randDatBufferPtr++ - 0x80) >> 1;
							}
							break;
						case 19: {
							if ((*g_randDatBufferPtr++ & 7) == 0)
							{
								particle->velX = *g_randDatBufferPtr++ - 0x80;
								particle->velZ = *g_randDatBufferPtr++ - 0x80;
							}
							uint32_t angle = ((uint8_t)particle->lifetime & 0x1F) * 0x80;
							particle->width = (Numerics::g_sinCosLUT[angle] >> 11) + 0x28;
							particle->height = (Numerics::g_sinCosLUT[(angle + 0x400) & 0xFFF] >> 11) + 0x28;
							if (particle->pos.y < Toy2::g_environmentSurfaceY)
								particle->lifetime = 0;
							break;
						}
						case 20:
							particle->width += (uint16_t)particle->updateParam * (int16_t)Renderer::g_frameDelta;
							colourMode = 3;
							particle->height = particle->width;
							break;
						case 21: {
							particle->pos.x = Toy2::g_buzzActor.posAngles.pos.x;
							particle->pos.z = Toy2::g_buzzActor.posAngles.pos.z;
							int32_t buzzYaw = (int16_t)Toy2::g_buzzActor.posAngles.angles.yaw;
							int32_t angle;
							if (particle->updateParam == 0x65)
							{
								particle->pos.x += Numerics::g_sinCosLUT[(buzzYaw + 0x680) & 0xFFF] >> 2;
								angle = buzzYaw - 0x580;
							}
							else
							{
								particle->pos.x += Numerics::g_sinCosLUT[(buzzYaw - 0x680) & 0xFFF] >> 2;
								angle = buzzYaw - 0x280;
							}
							particle->pos.z += Numerics::g_sinCosLUT[angle & 0xFFF] >> 2;
							particle->pos.y = Toy2::g_buzzActor.posAngles.pos.y - 0x400;
							colourMode = 1;
							if (particle->lifetime > 4)
							{
								int32_t typeId = 0x2E;
								if (Toy2::g_environmentEffectType == 1 && Toy2::g_environmentSurfaceY < particle->pos.y)
									typeId = 0x2D;
								int32_t rotation = *g_randDatBufferPtr++ * 8 - 0x400;
								SpawnInstance(particle->pos.x,
									particle->pos.y,
									particle->pos.z,
									Toy2::g_buzzActor.velocity.lateral / 2,
									0,
									Toy2::g_buzzActor.velocity.forward / 2,
									-0x10,
									0,
									rotation,
									typeId);
							}
							break;
						}
						case 22:
							if (particle->width < 0x140)
							{
								particle->width += (int16_t)Renderer::g_frameDelta * 0x20;
								if (particle->width > 0x140)
									particle->width = 0x140;
							}
							particle->height = particle->width;
							break;
						case 23:
							if (shadowCount < 47)
							{
								Renderer::Shadows::ShadowInstance* shadow = &Renderer::Shadows::g_shadowInstances[shadowCount];
								shadow->pos.x = particle->pos.x;
								shadow->pos.y = particle->groundHeightY;
								shadow->pos.z = particle->pos.z;
								shadow->size = particle->width;
								Renderer::Shadows::g_shadowCount++;
							}
							if (particle->groundHeightY < particle->pos.y + particle->height * 0x20)
							{
								particle->lifetime = 0;
								particle->pos.y = particle->groundHeightY - particle->height * 0x20;
							}
							break;
						case 24:
							particle->width -= (uint16_t)particle->updateParam * (int16_t)Renderer::g_frameDelta;
							colourMode = 1;
							if (particle->width < 1)
								particle->width = 1;
							break;
						case 25:
							if (shadowCount < 47)
							{
								Renderer::Shadows::ShadowInstance* shadow = &Renderer::Shadows::g_shadowInstances[shadowCount];
								shadow->pos.x = particle->pos.x;
								shadow->pos.y = particle->groundHeightY;
								shadow->pos.z = particle->pos.z;
								shadow->size = particle->width;
								Renderer::Shadows::g_shadowCount++;
							}
							if (particle->groundHeightY < particle->pos.y + particle->height * 0x20)
							{
								particle->pos.y = particle->groundHeightY - particle->height * 0x20;
								if (particle->velY > 0)
									particle->velY = particle->velY * -6 / 8;
								if (abs(particle->velY) < 0x100)
									particle->lifetime = 0;
								AudioManager::PlaySoundEffect(0x1F, &particle->pos);
							}
							break;
						case 26:
							colourMode = 1;
							AudioManager::PlaySoundEffect(0x44, &particle->pos);
							if (particle->lifetime > 4)
							{
								int32_t offset = (*g_randDatBufferPtr - 0x80) * 0x20;
								g_randDatBufferPtr += 3;
								SpawnFromPreset(particle->pos.x + offset, particle->pos.y + offset, particle->pos.z + offset, 0x40, 2);
							}
							Renderer::LensFlare::RegisterLight(
								particle->pos.x, particle->pos.y, particle->pos.z, particle->colourG, particle->colourG, particle->colourB, 0x30);
							break;
						case 27: {
							int16_t shrink = (uint16_t)particle->updateParam * (int16_t)Renderer::g_frameDelta;
							particle->width -= shrink;
							particle->height -= shrink;
							if (particle->width < 1)
								particle->width = 1;
							if (particle->height < 1)
								particle->height = 1;
							break;
						}
						case 28:
							if (particle->updateParam != 0xC6)
							{
								particle->groundHeightY = Collision::GetGroundHeight(&particle->groundProbe, 100);
								if (particle->groundHeightY != INT_MIN && particle->groundHeightY < particle->pos.y + particle->height * 0x20)
								{
									particle->pos.y = particle->groundHeightY - particle->height * 0x20;
									if (particle->velY > 0)
										particle->velY = particle->velY * -3 / 4;
									if (abs(particle->velY) < 0x400)
										particle->lifetime = 0;
									AudioManager::PlaySoundEffect(0x68, &particle->pos);
								}
							}
							if (Toy2::g_framePulseOutputs.sevenTick != 0 && particle->lifetime > 5 && (particle->renderFlags & PARTICLE_SPAWNS_TRAIL) != 0)
							{
								ParticleInstance* spawned = SpawnFromPreset(particle->pos.x, particle->pos.y, particle->pos.z, 0xF, 2);
								spawned->rotSpeed = ((int32_t)*g_randDatBufferPtr++ - 0x80) >> 3;
							}
							break;
						case 29: {
							if (Toy2::g_framePulseOutputs.eightTick != 0)
								AudioManager::PlaySoundEffect(0x55, &particle->pos);
							Toy2::Actor::Toy2Actor* target = particle->targetActor;
							Toy2::Actor::ActorCollisionVolume* volume = target->collisionVolumes + target->primaryAnimIdx;
							int32_t dx = (target->pos.x + volume->offset.x - particle->pos.x) >> 5;
							int32_t dy = (target->pos.y + volume->offset.y - particle->pos.y) >> 5;
							int32_t dz = (target->pos.z + volume->offset.z - particle->pos.z) >> 5;
							if (dx * dx + dy * dy + dz * dz < 0x4000)
							{
								if (target->creatureRam->defenseMode == 4)
								{
									int32_t angle = Math::CartesianToFixedAngle(target->pos.x - particle->pos.x, target->pos.z - particle->pos.z);
									particle->renderFlags &= ~(PARTICLE_INTERACTS_WITH_BUZZ | PARTICLE_TRACKS_TARGET);
									particle->velY = -0x400;
									particle->yawAngle = 0x30;
									particle->lifetime = 0x32;
									particle->velX = Numerics::g_sinCosLUT[(angle + 0x800) & 0xFFF] / 16;
									particle->updateMode = 30;
									particle->velZ = Numerics::g_sinCosLUT[(angle + 0xC00) & 0xFFF] / 16;
									AudioManager::PlaySoundEffect(7, &particle->pos);
								}
								else
								{
									Toy2::Actor::HandleDamage(target, Math::CartesianToFixedAngle(dx, dz) & 0xFFF, 4);
									particle->lifetime = 0;
									particle->collisionFlags = 8;
								}
							}
						}
						case 30:
							if (particle->lifetime > 4 && Toy2::g_framePulseOutputs.threeTick != 0)
							{
								uint32_t random = *g_randDatBufferPtr;
								g_randDatBufferPtr += 4;
								SpawnInstance(particle->pos.x + random - 0x80,
									particle->pos.y + random - 0x80,
									particle->pos.z + random - 0x80,
									particle->velX / 3,
									0,
									particle->velZ / 3,
									0,
									0,
									random - 0x80,
									0x2C);
							}
							break;
						case 31:
							particle->width -= (int16_t)Renderer::g_frameDelta * 4;
							colourMode = 1;
							if (particle->width < 0x10)
								particle->width = 0x10;
							particle->height = particle->width;
							break;
						case 32: {
							uint16_t pulse = Toy2::g_framePulsePhases.thirtyTwoTick;
							if (pulse > 7)
								pulse = 15 - pulse;
							particle->width = pulse * 8 + 100;
							particle->height = particle->width;
							if (particle->lifetime > 4 && Toy2::g_framePulseOutputs.twoTickCount != 0 && (particle->renderFlags & PARTICLE_SPAWNS_TRAIL) != 0)
							{
								ParticleInstance* spawned = SpawnFromPreset(particle->pos.x, particle->pos.y, particle->pos.z, 0x4D, 2);
								spawned->rotSpeed = *g_randDatBufferPtr++ - 0x80;
							}
							break;
						}
						case 33:
							particle->width -= (uint16_t)particle->updateParam * Renderer::g_frameDelta / 2;
							if (particle->width < 1)
								particle->width = 1;
							colourMode = 1;
							particle->height = particle->width;
							break;
						case 34:
							particle->width += (uint16_t)particle->updateParam * (int16_t)Renderer::g_frameDelta;
							particle->height = particle->width;
							colourMode = 7;
							break;
						case 35: {
							particle->groundHeightY = 0;
							int16_t wobble = particle->velY >> 6;
							if ((particle->groundAlignRot & 0x400) == 0)
							{
								particle->width = wobble + 0xFA;
								particle->height = 0xFA - wobble;
							}
							else
							{
								particle->width = 0xFA - wobble;
								particle->height = wobble + 0xFA;
							}
							if (particle->groundHeightY < particle->pos.y + particle->height * 0x20)
							{
								particle->pos.y = particle->groundHeightY - particle->height * 0x20;
								if (particle->velY > 0)
									particle->velY = particle->velY * -3 / 4;
								AudioManager::PlaySoundEffect(0x71, &particle->pos);
							}
							if (Renderer::Shadows::g_shadowCount < 47)
							{
								Renderer::Shadows::ShadowInstance* shadow = &Renderer::Shadows::g_shadowInstances[Renderer::Shadows::g_shadowCount++];
								shadow->pos.x = particle->pos.x;
								shadow->pos.y = particle->groundHeightY;
								shadow->pos.z = particle->pos.z;
								shadow->size = particle->width;
							}
							break;
						}
						case 36: {
							colourMode = 1;
							int32_t yaw = (int16_t)Toy2::g_buzzActor.posAngles.angles.yaw;
							int32_t xOffset = Numerics::g_sinCosLUT[(yaw + 0x400) & 0xFFF] / 10;
							int32_t zOffset = Numerics::g_sinCosLUT[(yaw - 0x800) & 0xFFF] / 10;
							if (particle->rotSpeed < 0)
							{
								xOffset = -xOffset;
								zOffset = -zOffset;
							}
							particle->pos.x = Toy2::g_buzzActor.posAngles.pos.x + xOffset;
							particle->pos.y = Toy2::g_buzzActor.posAngles.pos.y + (0x18 - particle->lifetime) * 0x120;
							particle->pos.z = Toy2::g_buzzActor.posAngles.pos.z + zOffset;
							particle->width += (int16_t)Renderer::g_frameDelta * 4;
							particle->height = particle->width;
							break;
						}
						case 37:
							particle->width -= (uint16_t)particle->updateParam * (int16_t)Renderer::g_frameDelta;
							if (particle->width < 1)
								particle->width = 1;
							colourMode = 1;
							particle->height = particle->width / 2;
							break;
						case 38: {
							uint32_t positionX = particle->pos.x;
							if (particle->velX < 1)
							{
								if (positionX < 0xFFFA55ED)
								{
									particle->pos.x = -0x5AA13;
									particle->velX = -particle->velX;
								}
							}
							else if (positionX < 0xFFFC95AA)
							{
								if (positionX > 0xFFFBD8AC && particle->groundHeightY == 0)
								{
									if ((uint32_t)particle->pos.y < 0xFFFF5509)
										particle->groundHeightY = -0xAAF7;
									else
									{
										particle->pos.x = -0x42754;
										particle->velX = -particle->velX;
									}
								}
							}
							else
								particle->groundHeightY = 1;

							if (particle->groundHeightY < particle->pos.y + particle->height * 0x20)
							{
								particle->pos.y = particle->groundHeightY - particle->height * 0x20;
								if (particle->velY > 0)
									particle->velY = -particle->velY / 2;
								AudioManager::PlaySoundEffect(0x71, &particle->pos);
							}
							if (Renderer::Shadows::g_shadowCount < 47)
							{
								Renderer::Shadows::ShadowInstance* shadow = &Renderer::Shadows::g_shadowInstances[Renderer::Shadows::g_shadowCount++];
								shadow->pos.x = particle->pos.x;
								shadow->pos.y = particle->groundHeightY;
								shadow->pos.z = particle->pos.z;
								shadow->size = particle->width;
							}
							break;
						}
						case 39:
							AudioManager::PlaySoundEffect(0x40, &particle->pos);
							Renderer::LensFlare::RegisterLight(
								particle->pos.x, particle->pos.y, particle->pos.z, particle->colourR, particle->colourG, particle->colourB, 0x30);
							if (particle->lifetime > 4 && Toy2::g_framePulseOutputs.fourTick != 0 && (particle->renderFlags & PARTICLE_SPAWNS_TRAIL) != 0)
							{
								ParticleInstance* spawned = SpawnFromPreset(particle->pos.x, particle->pos.y, particle->pos.z, 0x5A, 2);
								spawned->rotSpeed = *g_randDatBufferPtr++ - 0x80;
							}
							colourMode = 3;
							break;
						case 40:
							particle->width -= (uint16_t)particle->updateParam * (int16_t)Renderer::g_frameDelta;
							colourMode = 3;
							particle->height = particle->width;
							break;
						case 41:
							if (particle->lifetime > 4 && Toy2::g_framePulseOutputs.fourTick != 0 && (particle->renderFlags & PARTICLE_SPAWNS_TRAIL) != 0)
								SpawnFromPreset(particle->pos.x, particle->pos.y, particle->pos.z, 0x62, 2);
							break;
						case 42: {
							if (particle->velY > 0x180)
								particle->velY = 0x180;
							if (Toy2::g_framePulseOutputs.thirtyTwoTick != 0)
							{
								particle->velX = *g_randDatBufferPtr++ * 2 - 0x100;
								particle->velZ = *g_randDatBufferPtr++ * 2 - 0x100;
							}
							int32_t value = Numerics::g_sinCosLUT[((uint8_t)particle->lifetime & 0x3F) * 0x40] / particle->updateParam;
							particle->width = (int16_t)abs(value);
							break;
						}
						case 43:
							particle->groundAlignRot = (0x7FF - (int16_t)particle->yawAngle) & 0xFFF;
							particle->groundHeightY = Collision::GetGroundHeight(&particle->groundProbe, 0);
							if (particle->groundHeightY != INT_MIN)
								particle->pos.y = particle->groundHeightY - 0x800;
							if (particle->lifetime > 4 && Toy2::g_framePulseOutputs.twoTickCount != 0 && (particle->renderFlags & PARTICLE_SPAWNS_TRAIL) != 0
								&& Toy2::g_framePulseOutputs.fourTick != 0)
							{
								ParticleInstance* spawned = SpawnFromPreset(particle->pos.x, particle->pos.y, particle->pos.z, 0x6B, 2);
								spawned->groundAlignRot = (0x7FF - (int16_t)particle->yawAngle) & 0xFFF;
							}
							break;
						case 44: {
							particle->groundHeightY = Collision::GetGroundHeight(&particle->groundProbe, 0);
							if (particle->pos.y < particle->groundHeightY - 0x1000)
								particle->lifetime = 0;
							else if (particle->lifetime > 4 && (particle->renderFlags & PARTICLE_SPAWNS_TRAIL) != 0)
							{
								if (Toy2::g_framePulseOutputs.fourTick != 0)
								{
									ParticleInstance* spawned = SpawnFromPreset(particle->pos.x, particle->pos.y, particle->pos.z, 0x2C, 4);
									spawned->velX += (particle->velX << 1) / 3;
									spawned->velZ += (particle->velZ << 1) / 3;
								}
								if (Toy2::g_framePulseOutputs.sixTick != 0)
								{
									ParticleInstance* spawned = SpawnFromPreset(particle->pos.x, particle->pos.y, particle->pos.z, 0x69, 2);
									spawned->groundAlignRot = particle->groundAlignRot;
								}
								if (Toy2::g_framePulseOutputs.eightTick != 0)
								{
									ParticleInstance* spawned = SpawnFromPreset(particle->pos.x, particle->pos.y, particle->pos.z, 0x6A, 2);
									if (Toy2::g_framePulseOutputs.sixteenTick == 0)
										spawned->groundAlignRot = (particle->groundAlignRot + *g_randDatBufferPtr++ - 0x280) & 0xFFF;
									else
										spawned->groundAlignRot = (particle->groundAlignRot + *g_randDatBufferPtr++ + 0x180) & 0xFFF;
								}
							}
							break;
						}
						case 45:
							particle->width -= (uint16_t)particle->updateParam * (int16_t)Renderer::g_frameDelta;
							colourMode = 2;
							particle->height = particle->width / 2;
							break;
						case 46:
							particle->width += (uint16_t)particle->updateParam * Renderer::g_frameDelta;
							particle->height = particle->width;
							colourMode = 1;
							Renderer::LensFlare::RegisterLight(
								particle->pos.x, particle->pos.y, particle->pos.z, particle->colourB, particle->colourB >> 1, 0, 0x30);
							break;
						case 47:
							UpdateArenaBounce(particle);
							break;
						case 48:
							UpdateDrainTrail(particle);
							break;
						case 49:
							AudioManager::PlaySoundEffect(0x40, &particle->pos);
							Link::SetPositionRawAndCommit(0x19, particle->pos.x >> 5, particle->pos.y >> 5, particle->pos.z >> 5);
							Link::SetRotationRelative8bit(0x19, 0, (particle->yawAngle + 0x400) & 0xFFF, particle->discPitchAngle);
							if (particle->lifetime > 4 && Toy2::g_framePulseOutputs.twoTickCount != 0)
							{
								int32_t rotation = *g_randDatBufferPtr++ - 0x80;
								SpawnInstance(
									particle->pos.x, particle->pos.y, particle->pos.z, particle->velX / 2, 0, particle->velZ / 2, 0, 0, rotation, 0x2E);
							}
							break;
						case 50:
							if (particle->lifetime >= 0x40)
							{
								const ParticleType* type = &g_particleTypes[particle->typeId - 1];
								int32_t scale = 0x52 - particle->lifetime;
								particle->colourR = type->colourR * scale / 2 >> 3;
								particle->colourG = type->colourG * scale / 2 >> 3;
								particle->colourB = type->colourB * scale / 2 >> 3;
							}
							colourMode = 2;
							break;
						case 51:
							if (particle->updateParam == 0x76)
								colourMode = 2;
							if (particle->lifetime > 4 && Toy2::g_framePulseOutputs.fourTick != 0)
							{
								ParticleInstance* spawned = SpawnFromPreset(particle->pos.x, particle->pos.y, particle->pos.z, particle->updateParam >> 1, 2);
								spawned->rotSpeed = *g_randDatBufferPtr++ - 0x80;
							}
							break;
						case 52:
							ReflectWallsSquareArena(particle);
							break;
						case 53: {
							int32_t value = Numerics::g_sinCosLUT[((uint16_t)particle->lifetime & 0x3F) * 0x40] / particle->updateParam;
							particle->width = (int16_t)abs(value);
							if (particle->lifetime > 4 && Toy2::g_framePulseOutputs.sixteenTick != 0)
							{
								ParticleInstance* spawned = SpawnFromPreset(particle->pos.x, particle->pos.y, particle->pos.z, 0x2E, 2);
								spawned->lifetime = 0x30;
								spawned->width = spawned->height = 0x2000 / particle->updateParam;
							}
							break;
						}
					}

					const ParticleType* type = &g_particleTypes[particle->typeId - 1];
					switch (colourMode)
					{
						case 1:
							if (particle->lifetime < 0x20)
							{
								particle->colourR = type->colourR * particle->lifetime / 2 >> 4;
								particle->colourG = type->colourG * particle->lifetime / 2 >> 4;
								particle->colourB = type->colourB * particle->lifetime / 2 >> 4;
							}
							break;
						case 2:
							if (particle->lifetime < 0x40)
							{
								particle->colourR = type->colourR * particle->lifetime / 2 >> 5;
								particle->colourG = type->colourG * particle->lifetime / 2 >> 5;
								particle->colourB = type->colourB * particle->lifetime / 2 >> 5;
							}
							break;
						case 3:
							if (particle->lifetime < 0x20)
							{
								particle->colourR = type->colourR * particle->lifetime / 2 >> 4;
								if (particle->lifetime > 0x10)
								{
									particle->colourG = type->colourG * (particle->lifetime - 0x10) / 2 >> 3;
									particle->colourB = type->colourB * (particle->lifetime - 0x10) / 2 >> 3;
								}
								else
									particle->colourG = particle->colourB = 0;
							}
							break;
						case 4:
							if (particle->lifetime < 0x100)
							{
								particle->colourR = type->colourR * particle->lifetime / 2 >> 7;
								particle->colourG = type->colourG * particle->lifetime / 2 >> 7;
								particle->colourB = type->colourB * particle->lifetime / 2 >> 7;
							}
							break;
						case 5:
							if (particle->lifetime < 0x20)
							{
								particle->colourR = particle->lifetime < 0x10 ? type->colourR * particle->lifetime / 2 >> 4 : 0;
								particle->colourB = particle->lifetime > 0xC ? type->colourB * (particle->lifetime - 0xC) / 2 >> 4 : 0;
								particle->colourG = particle->lifetime > 0x18 ? type->colourG * (particle->lifetime - 0x18) / 2 >> 3 : 0;
							}
							else
								particle->colourR = 0;
							break;
						case 6: {
							int32_t scale = (*g_randDatBufferPtr++ & 0x7F) + 0x80;
							particle->colourR = type->colourR * scale >> 8;
							particle->colourG = type->colourG * scale >> 8;
							particle->colourB = type->colourB * scale >> 8;
							break;
						}
						case 7:
							if (particle->lifetime < 0x20)
							{
								particle->colourR = type->colourR * particle->lifetime / 2 >> 4;
								particle->colourG = type->colourG * particle->lifetime / 2 >> 4;
								particle->colourB = type->colourB * particle->lifetime / 2 >> 4;
							}
							else
							{
								int32_t scale = 0x2A - particle->lifetime;
								particle->colourR = type->colourR * scale / 2 >> 2;
								particle->colourG = type->colourG * scale / 2 >> 2;
								particle->colourB = type->colourB * scale / 2 >> 2;
							}
							break;
					}

					colourMode = 0;
					if (particle->lifetime == 0)
					{
						particle->lifetime = -1;
					}
				}
				particle++;
				particleIndex++;
			} while (particleIndex < 64);

			particle = g_particleInstances;
			int32_t cleanupCount = 64;
			do
			{
				if (particle->lifetime == -1)
				{
					particle->lifetime = 0;
					particle->spriteSheet = 0;
					DeathEffect(particle);
				}
				particle++;
				cleanupCount--;
			} while (cleanupCount != 0);
		}

		// GLOBAL: TOY2 0x004EC948
		ParticlePreset g_particlePresets[29] = {
#include "ParticlePresets.inc"
		};

		// GLOBAL: TOY2 0x004EC170
		ParticleType g_particleTypes[124] = {
#include "ParticleTypes.inc"
		};

		// GLOBAL: TOY2 0x00528168
		ParticleInstance g_rejectedParticleInstance;

		// FUNCTION: TOY2 0x0040FAC0 [MATCHED]
		void Init()
		{
			memset(g_particleInstances, 0, sizeof(g_particleInstances));
			g_particleAllocationCursor = 0;
		}

		// FUNCTION: TOY2 0x0040FAE0 [PROVISIONAL]
		ParticleInstance* SpawnInstance(int32_t x,
			int32_t y,
			int32_t z,
			int32_t velocityX,
			int32_t velocityY,
			int32_t velocityZ,
			int32_t yawAngle,
			int32_t groundAlignRotation,
			int32_t rotationSpeed,
			int32_t typeId)
		{
			int32_t maximumDistanceSquared;
			if (typeId == 0x37 || typeId == 0x43 || typeId == 0x50)
			{
				maximumDistanceSquared = 0x190000;
			}
			else
			{
				maximumDistanceSquared = 640000;
			}

			int32_t distanceX = (Toy2::Camera::g_renderCameraTransform.pos.x - x) >> 8;
			int32_t distanceY = (Toy2::Camera::g_renderCameraTransform.pos.y - y) >> 8;
			int32_t distanceZ = (Toy2::Camera::g_renderCameraTransform.pos.z - z) >> 8;
			if (distanceX * distanceX + distanceY * distanceY + distanceZ * distanceZ >= maximumDistanceSquared)
			{
				return &g_rejectedParticleInstance;
			}

			int32_t oldestParticleIndex = -1;
			int32_t shortestLifetime = 9999;
			int32_t checkedCount = 0;
			g_particleAllocationCursor++;
			if (g_particleAllocationCursor >= 64)
			{
				g_particleAllocationCursor = 0;
			}

			while ((g_particleInstances[g_particleAllocationCursor].renderFlags & Renderer::RENDER_TEXTURE_WRAP_UV) != 0
				&& g_particleInstances[g_particleAllocationCursor].lifetime != 0)
			{
				if (g_particleInstances[g_particleAllocationCursor].lifetime < shortestLifetime)
				{
					shortestLifetime = g_particleInstances[g_particleAllocationCursor].lifetime;
					oldestParticleIndex = g_particleAllocationCursor;
				}

				checkedCount++;
				if (checkedCount >= 64)
				{
					g_particleAllocationCursor = oldestParticleIndex;
					break;
				}

				g_particleAllocationCursor++;
				if (g_particleAllocationCursor >= 64)
				{
					g_particleAllocationCursor = 0;
				}
			}

			ParticleInstance* particle = &g_particleInstances[g_particleAllocationCursor];
			const ParticleType* particleType = &g_particleTypes[typeId - 1];
			particle->pos.z = z;
			particle->pos.x = x;
			particle->velX = velocityX / 2;
			particle->pos.y = y;
			particle->groundHeightY = INT_MIN;
			particle->velY = velocityY / 2;
			particle->velZ = velocityZ / 2;
			particle->yawAngle = yawAngle / 4;
			particle->lifetime = particleType->lifetime * 2;
			particle->width = particleType->width;
			particle->height = particleType->height;

			if (particleType->animationRate < 0)
			{
				particle->updateParam = (particleType->animationRate * 127 + (*g_randDatBufferPtr & 3)) * 2;
				g_randDatBufferPtr++;
				particle->lifetime = particle->animationFrameCount * particle->updateParam * 2;
			}
			else
			{
				particle->updateParam = particleType->animationRate * 2;
			}

			particle->animationTimer = particle->updateParam;
			particle->spriteSheet = particleType->spriteSheet;
			particle->typeId = typeId;
			particle->collisionFlags = particleType->collisionFlags;
			particle->updateMode = particleType->updateMode;
			particle->animationFrameCount = particleType->animationFrameCount;
			particle->tileIndex = 0;
			particle->groundAlignRot = groundAlignRotation;
			particle->renderFlags = particleType->renderFlags | Renderer::RENDER_BILINEAR_FILTER;
			particle->rotSpeed = rotationSpeed / 2;
			particle->colourR = particleType->colourR;
			particle->colourG = particleType->colourG;
			particle->colourB = particleType->colourB;
			SetDefaultAlpha(particle);
			return particle;
		}

		// FUNCTION: TOY2 0x0040FDF0 [PROVISIONAL]
		ParticleInstance* SpawnFromPreset(int32_t x, int32_t y, int32_t z, int32_t typeId, int32_t presetIndex)
		{
			const ParticlePreset* preset = &g_particlePresets[presetIndex];
			int32_t velocityX = preset->velocityXMask;
			switch ((preset->randomModes >> 12) & 0xF)
			{
				case 1:
					velocityX = ((*g_randDatBufferPtr++ & velocityX) - (velocityX >> 1)) << 3;
					break;
				case 2:
					velocityX = (*g_randDatBufferPtr++ & velocityX) << 3;
					break;
				case 3:
					velocityX = -(*g_randDatBufferPtr++ & velocityX) << 3;
					break;
				case 4:
					velocityX = ((*g_randDatBufferPtr++ & velocityX) + ((velocityX >> 4) & 0xFF0)) * 8;
					break;
				case 5:
					velocityX = ((*g_randDatBufferPtr++ & velocityX) - ((velocityX >> 4) & 0xFF0)) << 3;
					break;
				case 6:
					velocityX = ((*g_randDatBufferPtr++ & velocityX) - (velocityX >> 1)) << 4;
					break;
			}

			int32_t velocityY = preset->velocityYMask;
			switch ((preset->randomModes >> 8) & 0xF)
			{
				case 1:
					velocityY = ((*g_randDatBufferPtr++ & velocityY) - (velocityY >> 1)) << 3;
					break;
				case 2:
					velocityY = (*g_randDatBufferPtr++ & velocityY) << 3;
					break;
				case 3:
					velocityY = -(*g_randDatBufferPtr++ & velocityY) << 3;
					break;
				case 4:
					velocityY = ((*g_randDatBufferPtr++ & velocityY) + ((velocityY >> 4) & 0xFF0)) * 8;
					break;
				case 5:
					velocityY = ((*g_randDatBufferPtr++ & velocityY) - ((velocityY >> 4) & 0xFF0)) << 3;
					break;
				case 6:
					velocityY = ((*g_randDatBufferPtr++ & velocityY) - (velocityY >> 1)) << 4;
					break;
			}

			int32_t velocityZ = preset->velocityZMask;
			switch ((preset->randomModes >> 4) & 0xF)
			{
				case 1:
					velocityZ = ((*g_randDatBufferPtr++ & velocityZ) - (velocityZ >> 1)) << 3;
					break;
				case 2:
					velocityZ = (*g_randDatBufferPtr++ & velocityZ) << 3;
					break;
				case 3:
					velocityZ = -(*g_randDatBufferPtr++ & velocityZ) << 3;
					break;
				case 4:
					velocityZ = ((*g_randDatBufferPtr++ & velocityZ) + ((velocityZ >> 4) & 0xFF0)) * 8;
					break;
				case 5:
					velocityZ = ((*g_randDatBufferPtr++ & velocityZ) - ((velocityZ >> 4) & 0xFF0)) << 3;
					break;
				case 6:
					velocityZ = ((*g_randDatBufferPtr++ & velocityZ) - (velocityZ >> 1)) << 4;
					break;
			}

			int32_t yawAngle = preset->yawMask;
			switch (preset->randomModes & 0xF)
			{
				case 1:
					yawAngle = ((*g_randDatBufferPtr++ & yawAngle) - (yawAngle >> 1)) << 3;
					break;
				case 2:
					yawAngle = (*g_randDatBufferPtr++ & yawAngle) << 3;
					break;
				case 3:
					yawAngle = -(*g_randDatBufferPtr++ & yawAngle) << 3;
					break;
				case 4:
					yawAngle = ((*g_randDatBufferPtr++ & yawAngle) + ((yawAngle >> 4) & 0xFF0)) * 8;
					break;
				case 5:
					yawAngle = ((*g_randDatBufferPtr++ & yawAngle) - ((yawAngle >> 4) & 0xFF0)) << 3;
					break;
				case 6:
					yawAngle = ((*g_randDatBufferPtr++ & yawAngle) - (yawAngle >> 1)) << 4;
					break;
			}

			return SpawnInstance(x, y, z, velocityX, velocityY, velocityZ, yawAngle, preset->groundAlignRotation, preset->rotationSpeed, typeId);
		}

		// FUNCTION: TOY2 0x00410850 [MATCHED]
		void SpawnBreakBurst(ParticleInstance* source, uint8_t flags)
		{
			int32_t spawnY = source->height * 0x20 + source->pos.y;
			int32_t burstType;
			int32_t finalType;
			if ((flags & 1) == 0)
			{
				finalType = 0xE;
				burstType = 0xD;
				if (Toy2::g_levelFileIndex == 1)
				{
					AudioManager::PlaySoundEffect(0xC, &source->pos);
				}
				else
				{
					AudioManager::PlaySoundEffect(0x64, &source->pos);
				}
			}
			else
			{
				finalType = 0x38;
				burstType = 0x1E;
				AudioManager::PlaySoundEffect(0x4A, &source->pos);
			}

			int32_t particleCount = 5;
			do
			{
				ParticleInstance* particle = SpawnFromPreset(source->pos.x, spawnY, source->pos.z, burstType, 9);
				particle->updateParam = ((*g_randDatBufferPtr & 3) + 3) * 2;
				g_randDatBufferPtr++;
				particleCount--;
				particle->lifetime = (uint16_t)particle->updateParam * 5;
			} while (particleCount != 0);

			if ((flags & 2) == 0)
			{
				SpawnFromPreset(source->pos.x, spawnY, source->pos.z, finalType, 2);
			}
		}

		// FUNCTION: TOY2 0x00410B80 [PROVISIONAL]
		void DeathEffect(ParticleInstance* particle)
		{
			int8_t effectType = particle->collisionFlags;
			if (effectType < 0)
			{
				SpawnFromPreset(particle->pos.x, particle->pos.y, particle->pos.z, -effectType, 2);
				return;
			}

			switch (effectType)
			{
				case 1:
					SpawnBreakBurst(particle, 0);
					return;
				case 2:
					SpawnBreakBurst(particle, 1);
					return;
				case 3: {
					int32_t spawnY = particle->height * 0x20 + particle->pos.y;
					int32_t particleCount = 5;
					do
					{
						ParticleInstance* spawned = SpawnFromPreset(particle->pos.x, spawnY, particle->pos.z, 0x25, 0x11);
						spawned->lifetime = (*g_randDatBufferPtr & 7) * 2 + 0x20;
						g_randDatBufferPtr++;
						particleCount--;
					} while (particleCount != 0);

					SpawnFromPreset(particle->pos.x, spawnY, particle->pos.z, 0x28, 2);
					Toy2::Lighting::SpawnLight(particle->pos.x, spawnY, particle->pos.z, 0xA00000, 0x18, (int32_t)particle);
					AudioManager::PlaySoundEffect(0xB, &particle->pos);
					return;
				}
				case 4:
					SpawnBreakBurst(particle, 2);
					return;
				case 5: {
					int32_t sourceZ = particle->pos.z;
					int32_t sourceY = particle->pos.y;
					int32_t sourceX = particle->pos.x;
					int32_t particleCount = 5;
					do
					{
						uint8_t random = *g_randDatBufferPtr;
						g_randDatBufferPtr += 3;
						int32_t offset = ((random & 0x3F) - 0x20) * 0x3C;
						ParticleInstance* spawned = SpawnFromPreset(sourceX + offset, sourceY + offset, sourceZ + offset, 0x29, 0xF);
						spawned->rotSpeed = *g_randDatBufferPtr++ - 0x80;
						spawned->lifetime = (*g_randDatBufferPtr++ & 0xF) * 2 + 0x18;
						particleCount--;
					} while (particleCount != 0);

					Toy2::Lighting::SpawnLight(sourceX, sourceY, sourceZ, 0x604000, 0x10, sourceX);
					return;
				}
				case 6: {
					int32_t particleCount = 5;
					do
					{
						ParticleInstance* spawned = SpawnFromPreset(particle->pos.x, particle->pos.y, particle->pos.z, 0x3E, 9);
						spawned->updateParam = ((*g_randDatBufferPtr & 3) + 3) * 2;
						g_randDatBufferPtr++;
						particleCount--;
						spawned->lifetime = (uint16_t)spawned->updateParam * 5;
					} while (particleCount != 0);
					return;
				}
				case 7: {
					int32_t spawnY = particle->height * 0x20 + particle->pos.y;
					int32_t particleCount = 3;
					do
					{
						ParticleInstance* spawned = SpawnFromPreset(particle->pos.x, spawnY, particle->pos.z, 0x25, 0x11);
						spawned->lifetime = (*g_randDatBufferPtr & 7) * 2 + 0x20;
						g_randDatBufferPtr++;
						particleCount--;
					} while (particleCount != 0);

					AudioManager::PlaySoundEffect(0xB, &particle->pos);
					return;
				}
				case 8: {
					int32_t spawnY = particle->height * 0x20 + particle->pos.y;
					int32_t particleCount = 5;
					do
					{
						ParticleInstance* spawned = SpawnFromPreset(particle->pos.x, spawnY, particle->pos.z, 0x25, 0x11);
						spawned->lifetime = (*g_randDatBufferPtr & 7) * 2 + 0x20;
						g_randDatBufferPtr++;
						particleCount--;
					} while (particleCount != 0);

					Toy2::Lighting::SpawnLight(particle->pos.x, spawnY, particle->pos.z, 0xA00000, 0x18, (int32_t)particle);
					AudioManager::PlaySoundEffect(0xB, &particle->pos);
					return;
				}
				case 9: {
					int32_t particleCount = 3;
					do
					{
						ParticleInstance* spawned = SpawnFromPreset(particle->pos.x, particle->pos.y, particle->pos.z, 0x11, 4);
						spawned->width = 0xFA;
						spawned->height = 0xFA;
						spawned->rotSpeed = *g_randDatBufferPtr++ - 0x40;
						spawned->lifetime = (*g_randDatBufferPtr++ & 7) * 2 + 0x20;
						particleCount--;
					} while (particleCount != 0);

					AudioManager::PlaySoundEffect(0xB, &particle->pos);
					return;
				}
				case 10:
					SpawnFromPreset(particle->pos.x, particle->height * 0x20 + particle->pos.y, particle->pos.z, 0x78, 2);
					return;
				case 11: {
					ParticleInstance* spawned = SpawnFromPreset(particle->pos.x, particle->pos.y, particle->pos.z, 0x75, 0x1C);
					spawned->rotSpeed = *g_randDatBufferPtr++ - 0x80;
					return;
				}
			}
		}

		// GLOBAL: TOY2 0x00529E58;
		ParticleInstance g_particleInstances[64];

		// GLOBAL: TOY2 0x0052AD90
		int32_t g_particleAllocationCursor;

		// FUNCTION: TOY2 0x00446FA0 [MATCHED]
		void SetDefaultAlpha(ParticleInstance* particle)
		{
			particle->colourA = 0x2C;
			if ((particle->renderFlags & PARTICLE_RENDER_ALPHA_MODE_MASK) != PARTICLE_RENDER_ALPHA_MODE_MASK)
			{
				particle->colourA = 0x2E;
			}
		}

	}
}
