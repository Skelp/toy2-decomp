#include "Nu3D/Particles.h"
#include "Nu3D/Camera.h"
#include "AudioManager/AudioManager.h"
#include "Random.h"
#include "Renderer/Renderer.h"
#include "Renderer/Shadows.h"
#include "Toy2/Actor.h"
#include "Toy2/Camera.h"
#include "Toy2/Lighting.h"
#include "Toy2/Toy2.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

namespace Nu3D
{
	namespace Particles
	{
		// STUB: TOY2 0x00410F40
		void Update() {}

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

			int32_t distanceZ = (Toy2::Camera::g_renderCameraTransform.pos.z - z) >> 8;
			int32_t distanceY = (Toy2::Camera::g_renderCameraTransform.pos.y - y) >> 8;
			int32_t distanceX = (Toy2::Camera::g_renderCameraTransform.pos.x - x) >> 8;
			if (distanceZ * distanceZ + distanceY * distanceY + distanceX * distanceX >= maximumDistanceSquared)
			{
				return &g_rejectedParticleInstance;
			}

			g_particleAllocationCursor++;
			if (g_particleAllocationCursor >= 64)
			{
				g_particleAllocationCursor = 0;
			}

			int32_t oldestParticleIndex = -1;
			int32_t shortestLifetime = 9999;
			int32_t checkedCount = 0;
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
			particle->pos.y = y;
			particle->velX = velocityX / 2;
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
					velocityX = ((*g_randDatBufferPtr++ & velocityX) + ((velocityX >> 4) & 0xFF0)) << 3;
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
					velocityY = ((*g_randDatBufferPtr++ & velocityY) + ((velocityY >> 4) & 0xFF0)) << 3;
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
					velocityZ = ((*g_randDatBufferPtr++ & velocityZ) + ((velocityZ >> 4) & 0xFF0)) << 3;
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
					yawAngle = ((*g_randDatBufferPtr++ & yawAngle) + ((yawAngle >> 4) & 0xFF0)) << 3;
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
