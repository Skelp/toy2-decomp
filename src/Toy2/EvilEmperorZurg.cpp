#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Actor.h"
#include "Toy2/Buzz.h"
#include "Toy2/Camera.h"
#include "Toy2/Collectables.h"
#include "Toy2/Levels.h"
#include "Toy2/Lighting.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Random.h"
#include "Renderer/Renderer.h"
#include "Renderer/Shadows.h"
#include "SaveManager.h"

#include <limits.h>
#include <stdlib.h>

namespace Toy2
{
	extern int32_t g_hudActorAnimationFrame;

	namespace EvilEmperorZurg
	{
		enum EncounterState
		{
			ENCOUNTER_WAITING = 0,
			ENCOUNTER_INTRO = 1,
			ENCOUNTER_ACTIVE = 2,
			ENCOUNTER_DEFEATED = 3,
			ENCOUNTER_REWARD = 4,
		};

		// GLOBAL: TOY2 0x0052FE00
		int32_t g_previousPhase;

		// GLOBAL: TOY2 0x0052FE04
		int32_t g_damageFlashTimer;

		// GLOBAL: TOY2 0x0052FE08
		int32_t g_voiceTimer;

		// GLOBAL: TOY2 0x0052FE20
		int32_t g_introSoundTimer;

		// GLOBAL: TOY2 0x0052FE24
		int32_t g_encounterState;

		// GLOBAL: TOY2 0x0052FE28
		int32_t g_damageFlashToggle;

		// GLOBAL: TOY2 0x0052FE2C
		int32_t g_flightVelocityY;

		// GLOBAL: TOY2 0x0052FE30
		int32_t g_attackTimer;

		// GLOBAL: TOY2 0x0052FE34
		int32_t g_attackVariant;

		// FUNCTION: TOY2 0x0042B2D0 [MATCHED]
		void BuzzRespawn()
		{
			g_buzzActor.respawnPos.x = -0x1DA7C;
			g_buzzActor.respawnPos.y = -0x12BD0;
			g_buzzActor.respawnPos.z = 0xF699;
			g_buzzActor.respawnYawAngle = 0x400;
		}

		// FUNCTION: TOY2 0x0042B300 [MATCHED]
		void Init()
		{
			Toy2::MoveableObject::InitTable(0);
			Collectables::Init(0, 0);

			Actor::Toy2Actor* zurg = &Actor::g_creatureActors[0];
			g_previousPhase = zurg->actorPhase;
			zurg->actorFlags |= Actor::ACTOR_FLAG_BOSS;
			zurg->pos.y -= 0x28000;
			zurg->motionTargetPos.y = zurg->pos.y;
			g_voiceTimer = 300;
			int32_t startZ = zurg->pos.z + 0x10000;
			g_introSoundTimer = 0x78;
			g_encounterState = ENCOUNTER_WAITING;
			g_damageFlashToggle = 0;
			g_damageFlashTimer = 0;
			g_attackTimer = 0x104;
			g_attackVariant = 0;
			zurg->yawAngle = 0xC00;
			zurg->pos.z = startZ;
			zurg->creatureRam->rotSpeed = 0;
		}

		// FUNCTION: TOY2 0x0042B3A0 [PROVISIONAL]
		void Interactions()
		{
			Actor::Toy2Actor* zurg = &Actor::g_creatureActors[0];
			AudioManager::PlaySoundEffect(0x67, 0);

			int32_t attackFrame = 0;
			Levels::RecordData* flareRecords = Levels::g_recordData[0];
			int32_t nearestDistanceSquared = INT_MAX;
			int32_t nearestFlareIndex;
			for (int32_t flareIndex = 0; flareIndex < flareRecords->recordCount; flareIndex++)
			{
				Vector3I* flare = &flareRecords->data[flareIndex];
				int32_t cameraOffsetY = (Camera::g_renderCameraTransform.pos.y - flare->y * 0x20) >> 8;
				int32_t cameraOffsetZ = (Camera::g_renderCameraTransform.pos.z - flare->z * 0x20) >> 8;
				int32_t cameraOffsetX = (Camera::g_renderCameraTransform.pos.x - flare->x * 0x20) >> 8;
				if (cameraOffsetZ * cameraOffsetZ + cameraOffsetY * cameraOffsetY + cameraOffsetX * cameraOffsetX < 1000000)
				{
					Renderer::LensFlare::RegisterLight(flare->x * 0x20, flare->y * 0x20, flare->z * 0x20, 0x80, 0x80, 0x80, 0x40);
					int32_t buzzOffsetZ = (g_buzzActor.posAngles.pos.z - flare->z * 0x20) >> 8;
					int32_t buzzOffsetY = (g_buzzActor.posAngles.pos.y - flare->y * 0x20 - 0x2000) >> 8;
					int32_t buzzOffsetX = (g_buzzActor.posAngles.pos.x - flare->x * 0x20) >> 8;
					int32_t distanceSquared = buzzOffsetX * buzzOffsetX + buzzOffsetZ * buzzOffsetZ + buzzOffsetY * buzzOffsetY;
					if (distanceSquared < nearestDistanceSquared)
					{
						nearestDistanceSquared = distanceSquared;
						nearestFlareIndex = flareIndex;
					}
				}
			}

			if (nearestDistanceSquared < 0x10000)
			{
				Vector3I* nearestFlare = &flareRecords->data[nearestFlareIndex];
				Lighting::DynamicLight* light = &Lighting::g_lightingState.dynamicLights[1];
				light->position.x = nearestFlare->x << 5;
				light->position.y = nearestFlare->y << 5;
				light->position.z = nearestFlare->z << 5;
				light->colour.r = 0xFF;
				light->colour.g = 0xFF;
				light->colour.b = 0xFF;
				light->lifetime = 1;
				light->sourceId = reinterpret_cast<int32_t>(nearestFlare);
			}
			else
			{
				Lighting::g_lightingState.dynamicLights[1].lifetime = 0;
			}

			g_damageFlashToggle = (g_damageFlashToggle - 1) & 1;
			if (g_encounterState > ENCOUNTER_INTRO)
			{
				g_damageFlashTimer -= Renderer::g_frameDelta;
				if (g_damageFlashTimer < 0)
				{
					g_damageFlashTimer = 0;
					zurg->creatureRam->defenseMode = 6;
					zurg->useTint = 0;
				}
				else if (g_damageFlashToggle == 0)
				{
					zurg->useTint = 0;
				}
				else
				{
					zurg->useTint = 1;
					zurg->actorTint.r = 0x2000;
					zurg->actorTint.g = 0x2000;
					zurg->actorTint.b = 0x2000;
				}
			}

			if (g_encounterState == ENCOUNTER_WAITING && g_buzzActor.posAngles.pos.x >= -0x18B2A)
			{
				AudioManager::PlayMusicLooping(g_levelIndex);
				g_encounterState = ENCOUNTER_INTRO;
				Camera::BeginScriptedCutsceneAtPoint(&zurg->pos, 300, 0x10);
				Camera::g_cutsceneCameraPosition.x = zurg->pos.x - 0x3000;
				Camera::g_cutsceneFocusPosition.y = zurg->pos.y - 0x4000;
				Camera::g_cutsceneCameraPosition.y = zurg->pos.y - 0x5000;
				Camera::g_cutsceneCameraPosition.z = zurg->pos.z;
				AudioManager::PlaySoundEffect(0xD8, reinterpret_cast<const Vector3I*>(1));
			}

			if (g_encounterState == ENCOUNTER_INTRO)
			{
				if (g_introSoundTimer > 0)
				{
					g_introSoundTimer -= Renderer::g_frameDelta;
					if (g_introSoundTimer < 1)
						AudioManager::Preset::PlayOneShotSound(0xB9, zurg);
				}
				if (g_buzzActor.posAngles.pos.y > -70000)
				{
					g_buzzActor.posAngles.pos.x = -0x1DA7C;
					g_buzzActor.posAngles.pos.y = -0x12BD0;
					g_buzzActor.posAngles.pos.z = 0xF699;
				}
				if (Camera::g_cutsceneDuration == 0)
				{
					zurg->actorFlags |= Actor::ACTOR_FLAG_TRACKS_TARGET | Actor::ACTOR_FLAG_TARGETS_BUZZ;
					g_encounterState = ENCOUNTER_ACTIVE;
					zurg->creatureRam->rotSpeed = 10;
					zurg->creatureRam->defenseMode = 6;
				}
				else
				{
					zurg->pos.y += Renderer::g_frameDelta * 0x200;
					Camera::g_cutsceneCameraPosition.y += Renderer::g_frameDelta * 0x220;
					Camera::g_cutsceneCameraPosition.x -= Renderer::g_frameDelta * 0x20;
					if (Camera::g_cutsceneDuration < 0xB4)
						Camera::g_cutsceneCameraPosition.x -= Renderer::g_frameDelta * 0x150;
					Camera::g_cutsceneFocusPosition.y = zurg->pos.y - 0x4000;
					zurg->motionTargetPos.y = zurg->pos.y;
				}
			}

			if (g_encounterState == ENCOUNTER_ACTIVE)
			{
				g_voiceTimer -= Renderer::g_frameDelta;
				if (g_voiceTimer < 0)
				{
					uint8_t* soundChoice = g_randDatBufferPtr + 1;
					g_voiceTimer = *g_randDatBufferPtr * 2 + 400;
					g_randDatBufferPtr += 2;
					AudioManager::Preset::PlayOneShotSound((*soundChoice & 3) + 0xBA, zurg);
				}
				Camera::g_actorCameraTarget = zurg->pos;
				HUD::g_slideTimers[HUD::SLIDE_BOSS_STATUS] = 90;
				if (zurg->actorPhase != g_previousPhase)
				{
					if ((*g_randDatBufferPtr++ & 3) == 0)
						AudioManager::Preset::PlayOneShotSound(0xD9, &g_buzzActor);
					g_previousPhase = zurg->actorPhase;
					g_damageFlashTimer = 0x3C;
					zurg->creatureRam->defenseMode = 4;
					if (zurg->actorPhase < 10)
					{
						g_encounterState = ENCOUNTER_DEFEATED;
						Camera::BeginScriptedCutsceneAtPoint(&zurg->pos, 300, 0x10);
						Camera::g_cutsceneFocusPosition.y = zurg->pos.y - 0x4000;
						Camera::g_cutsceneCameraPosition.x = Numerics::g_sinCosLUT[(uint16_t)zurg->yawAngle] + zurg->pos.x;
						Camera::g_cutsceneCameraPosition.z = Numerics::g_sinCosLUT[(zurg->yawAngle + 0x400) & 0xFFF] + zurg->pos.z;
						Camera::g_cutsceneCameraPosition.y = Camera::g_cutsceneFocusPosition.y;
						Actor::SetAnimation(zurg, 2, 0x19);
						zurg->actorFlags &= ~(Actor::ACTOR_FLAG_TRACKS_TARGET | Actor::ACTOR_FLAG_TARGETS_BUZZ);
						g_attackTimer = 0x1E;
						zurg->reservedArea[1] = 0xC0;
						zurg->reservedArea[2] = 0xC0;
						zurg->creatureRam->latSpeedNoTarget = 0x20;
						zurg->creatureRam->latSpeedTarget = 0x20;
						SaveManager::g_save0Data.tokens[g_levelFileIndex] = 0x80;
						AudioManager::Preset::PlayOneShotSound(0xC0, zurg);
					}
					else
					{
						AudioManager::Preset::PlayOneShotSound((*g_randDatBufferPtr++ & 1) + 0xBE, zurg);
						g_attackTimer = 0;
						g_attackVariant = 1;
						Actor::SetAnimation(zurg, 1, 2);
						zurg->reservedArea[1] = 0xE0;
						zurg->reservedArea[2] = 0xE0;
					}
				}

				int32_t previousAttackTimer = g_attackTimer;
				int32_t nextAttackTimer = g_attackTimer - Renderer::g_frameDelta;
				if (nextAttackTimer < 0)
				{
					g_attackTimer = 0x12E;
					int32_t movementAngle = zurg->yawAngle + 0x200;
					if (*g_randDatBufferPtr++ < 0x80)
						movementAngle = zurg->yawAngle - 0x200;
					zurg->velX = Numerics::g_sinCosLUT[movementAngle] >> 3;
					zurg->velForward = Numerics::g_sinCosLUT[(movementAngle + 0x400) & 0xFFF] >> 3;
				}
				else
				{
					bool passedAnimationFrame = g_attackTimer > 200;
					g_attackTimer = nextAttackTimer;
					if (passedAnimationFrame && nextAttackTimer < 201)
					{
						Actor::SetAnimation(zurg, 0, 0x18);
						zurg->reservedArea[1] = 0xC0;
						zurg->reservedArea[2] = 0xC0;
					}
					if (previousAttackTimer > 0x92 && g_attackTimer < 0x93)
						attackFrame = 1;
					if (previousAttackTimer > 0x8A && g_attackTimer < 0x8B)
						attackFrame = 2;
					if (previousAttackTimer >= 0x7F && g_attackTimer < 0x7F)
						attackFrame = 1;
					if (attackFrame != 0)
					{
						Vector4I particlePosition;
						particlePosition.x = -0xFA;
						particlePosition.y = -0xFA;
						particlePosition.z = 0;
						Actor::ResolveBoneAttachmentPos(&particlePosition, zurg, 1);
						int32_t particleType;
						int32_t particleYaw;
						int32_t velocityX;
						int32_t velocityY;
						int32_t velocityZ;
						if (g_attackVariant == 0 && (zurg->actorPhase >= 20 || attackFrame != 2))
						{
							particleType = 0x6C;
							particleYaw = 0x100;
							velocityZ = Numerics::g_sinCosLUT[zurg->yawAngle + 0x380] >> 2;
							velocityY = 0x400;
							velocityX = Numerics::g_sinCosLUT[(zurg->yawAngle - 0x80) & 0xFFF] >> 2;
						}
						else
						{
							particleType = 0x6D;
							particleYaw = zurg->yawAngle << 2;
							velocityZ = 0;
							velocityY = -2;
							velocityX = 0;
						}
						Nu3D::Particles::SpawnInstance(
							particlePosition.x, particlePosition.y, particlePosition.z, velocityX, velocityY, velocityZ, particleYaw, 0, 0, particleType);
						AudioManager::PlaySoundEffect(0x9D, &zurg->pos);
					}
					if (previousAttackTimer > 0x68 && g_attackTimer < 0x69)
					{
						g_attackVariant = 0;
						Actor::SetAnimation(zurg, 1, 2);
						zurg->reservedArea[1] = 0xE0;
						zurg->reservedArea[2] = 0xE0;
					}
				}
			}

			if (g_encounterState == ENCOUNTER_DEFEATED)
			{
				if (g_buzzActor.posAngles.pos.y > -70000)
				{
					g_buzzActor.posAngles.pos.x = -0x1DA7C;
					g_buzzActor.posAngles.pos.y = -0x12BD0;
					g_buzzActor.posAngles.pos.z = 0xF699;
				}
				zurg->targetYaw = Nu3D::Math::CartesianToFixedAngle((-0xBD7C - zurg->pos.x) >> 5, (0x99 - zurg->pos.z) >> 5) & 0xFFF;
				if (Actor::IsInsideBounds(&zurg->pos, -0x256D6, 0xE0AA, -0x1B3E9, 0x1B297) == 0)
				{
					zurg->creatureRam->rotSpeed = 0;
					zurg->yawAngle = (zurg->yawAngle + (Renderer::g_frameDelta << 4)) & 0xFFF;
					if (zurg->pos.y < 170000)
					{
						zurg->pos.y += g_flightVelocityY * Renderer::g_frameDelta;
						if (zurg->pos.y >= 170000)
							AudioManager::PlaySoundEffect(0x9F, 0);
						g_flightVelocityY += 0x40;
						zurg->motionTargetPos.y = zurg->pos.y;
					}
					if (zurg->pos.y > 70000)
					{
						AudioManager::PlaySoundEffect(0x9E, reinterpret_cast<const Vector3I*>(1));
						if (zurg->pos.y > 70000 && g_framePulseOutputs.sixteenTick != 0 && Camera::g_cutsceneDuration > 0x32)
						{
							uint8_t randomAngle = *g_randDatBufferPtr++;
							Nu3D::Particles::SpawnInstance(zurg->pos.x, 80000, zurg->pos.z, 0, -0x2400, 0, 0, randomAngle << 4, 0x80, 0x70);
						}
					}
				}
				else
				{
					zurg->velX = -Numerics::g_sinCosLUT[zurg->targetYaw] >> 4;
					zurg->velForward = -Numerics::g_sinCosLUT[(zurg->targetYaw + 0x400) & 0xFFF] >> 4;
				}
				Camera::g_cutsceneFocusPosition.y = zurg->pos.y - 0x4000;
				Camera::g_cutsceneFocusPosition.x = zurg->pos.x;
				Camera::g_cutsceneFocusPosition.z = zurg->pos.z;
				int32_t cameraDistanceDivisor = (g_flightVelocityY + (g_flightVelocityY >> 31 & 0x3FU)) >> 6;
				if (cameraDistanceDivisor == 0)
					cameraDistanceDivisor = 1;
				else if (cameraDistanceDivisor > 8)
					cameraDistanceDivisor = 8;
				Camera::g_cutsceneCameraPosition.y = -0x188E0;
				Camera::g_cutsceneCameraPosition.x = Numerics::g_sinCosLUT[(uint16_t)zurg->yawAngle] / cameraDistanceDivisor + zurg->pos.x;
				Camera::g_cutsceneCameraPosition.z = Numerics::g_sinCosLUT[(zurg->yawAngle + 0x400) & 0xFFF] / cameraDistanceDivisor + zurg->pos.z;
				if (Camera::g_cutsceneDuration == 0)
				{
					Collectables::g_tokenCollectionState = 1;
					g_encounterState = ENCOUNTER_REWARD;
				}
			}

			g_hudActorAnimationFrame = (zurg->actorPhase - 9) * 0x36 / 0x14;
			int32_t drainOffsetX = (-0xBD7C - zurg->pos.x) >> 5;
			int32_t drainOffsetZ = (0x99 - zurg->pos.z) >> 5;
			if (drainOffsetZ * drainOffsetZ + drainOffsetX * drainOffsetX < 0x190000)
			{
				uint32_t drainAngle = Nu3D::Math::CartesianToFixedAngle(drainOffsetX, drainOffsetZ);
				zurg->pos.x = -0xBD7C - (Numerics::g_sinCosLUT[drainAngle & 0xFFF] * 0x500 >> 9);
				zurg->pos.z = 0x99 - (Numerics::g_sinCosLUT[(drainAngle + 0x400) & 0xFFF] * 0x500 >> 9);
			}
		}
	}
}

namespace Nu3D
{
	namespace Particles
	{
		// FUNCTION: TOY2 0x0042B090 [PROVISIONAL]
		void UpdateArenaBounce(ParticleInstance* particle)
		{
			const int32_t drainCenterX = -0xBD7C;
			const int32_t drainCenterZ = 0x99;
			int32_t bounced = 0;

			if (particle->lifetime > 4 && Toy2::g_framePulseOutputs.fourTick != 0)
			{
				SpawnFromPreset(particle->pos.x - particle->velX * Renderer::g_frameDelta,
					particle->pos.y,
					particle->pos.z - particle->velZ * Renderer::g_frameDelta,
					0x6E,
					2);
			}

			if (Toy2::Actor::IsInsideBounds(&particle->pos, -0x256D6, 0xE0AA, -0x1B3E9, 0x1B297))
			{
				const int32_t arenaFloorY = -0x12BD3;
				if (particle->groundHeightY == INT_MIN)
				{
					particle->groundHeightY = arenaFloorY;
				}

				if (particle->pos.y > arenaFloorY)
				{
					if (particle->groundHeightY == arenaFloorY)
					{
						particle->pos.y = arenaFloorY;
						bounced = 1;
						particle->velY = -abs((particle->velY * 7) >> 3);
					}
					else
					{
						particle->velX = -particle->velX;
						particle->velZ = -particle->velZ;
						bounced = 1;
					}
				}
				else
				{
					particle->groundHeightY = arenaFloorY;
					if (Renderer::Shadows::g_shadowCount < 47)
					{
						int16_t shadowIndex = Renderer::Shadows::g_shadowCount;
						Renderer::Shadows::g_shadowInstances[shadowIndex].pos.x = particle->pos.x;
						Renderer::Shadows::g_shadowInstances[shadowIndex].pos.y = particle->groundHeightY;
						Renderer::Shadows::g_shadowInstances[shadowIndex].pos.z = particle->pos.z;
						Renderer::Shadows::g_shadowInstances[shadowIndex].size = particle->width;
						Renderer::Shadows::g_shadowCount = shadowIndex + 1;
					}
				}
			}
			else
			{
				particle->groundHeightY = 400000;
			}

			int32_t posX = particle->pos.x;
			if (posX < -0x29B56)
			{
				bounced = 1;
				particle->velX = abs(particle->velX);
			}
			if (posX > 0x2A62A)
			{
				bounced = 1;
				particle->velX = -abs(particle->velX);
			}

			int32_t posZ = particle->pos.z;
			if (posZ < -0x230E9)
			{
				bounced = 1;
				particle->velZ = abs(particle->velZ);
			}
			if (posZ > 0x22B97)
			{
				bounced = 1;
				particle->velZ = -abs(particle->velZ);
			}

			int32_t deltaX = (drainCenterX - posX) >> 5;
			int32_t deltaZ = (drainCenterZ - posZ) >> 5;
			if (deltaZ * deltaZ + deltaX * deltaX < 0x100000)
			{
				particle->lifetime = 0;
			}

			if (bounced)
			{
				AudioManager::PlaySoundEffect(0x43, &particle->pos);
			}
		}

		// FUNCTION: TOY2 0x0042B250 [MATCHED]
		void UpdateDrainTrail(ParticleInstance* particle)
		{
			const int32_t drainCenterX = -0xBD7C;
			const int32_t drainCenterZ = 0x99;
			const int32_t triggerRadius = 0x8000;
			int32_t deltaX = (drainCenterX - particle->pos.x) >> 5;
			int32_t deltaZ = (drainCenterZ - particle->pos.z) >> 5;

			if (deltaZ * deltaZ + deltaX * deltaX < (triggerRadius >> 5) * (triggerRadius >> 5))
			{
				particle->lifetime = 0;
			}

			if (particle->lifetime > 4 && Toy2::g_framePulseOutputs.fourTick != 0)
			{
				SpawnFromPreset(particle->pos.x - particle->velX * Renderer::g_frameDelta,
					particle->pos.y,
					particle->pos.z - particle->velZ * Renderer::g_frameDelta,
					0x6F,
					2);
			}
		}
	}
}
