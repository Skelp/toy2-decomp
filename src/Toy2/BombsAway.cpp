#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Actor.h"
#include "Toy2/BuzzLightPreset.h"
#include "Toy2/Camera.h"
#include "Toy2/Collectables.h"
#include "Toy2/Levels.h"
#include "Toy2/Weather.h"
#include "Toy2/Lighting.h"
#include "Toy2/Buzz.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Nu3D/Camera.h"
#include "Renderer/Renderer.h"
#include "AudioManager/AudioManager.h"
#include "SaveManager.h"
#include "Random.h"
#include "Numerics.h"

#include <limits.h>

namespace Toy2
{
	extern int32_t g_hudActorAnimationFrame;
	namespace BombsAway
	{
		enum EncounterState
		{
			ENCOUNTER_WAITING = 0,
			ENCOUNTER_INTRO = 1,
			ENCOUNTER_ACTIVE = 999,
			ENCOUNTER_DEFEAT_CUTSCENE = 1000,
			ENCOUNTER_BOMB_FALLING = 1001,
			ENCOUNTER_TOKEN_DELAY = 1002,
			ENCOUNTER_TOKEN_TRIGGER = 1030,
		};

		// GLOBAL: TOY2 0x004DFAA8
		uint16_t g_bossMovementScript[] = {
			0x0016, 0xFFC0, 0xFFC0, 0x000D, 0x0001, 0x0001, 0x0004, 0xFFFF, 0x0001, 0x0016,
			0xFFC0, 0xFFC0, 0x000D, 0x0001, 0x0001, 0x0001, 0x0001, 0x0004, 0x0010, 0x0640,
			0x0008, 0x0025, 0x0016, 0xFFE8, 0xFFE8, 0x001F, 0x0006, 0x0001, 0x000F, 0x0004,
			0x000D, 0x0003, 0x0004, 0x0001, 0x0021, 0x0004, 0x000D, 0x0003, 0x0005, 0x0018,
			0xF800, 0x0017, 0x00D3, 0x0019, 0x0002, 0x0007, 0x0017, 0x003B, 0x0021, 0x0003,
			0x000D, 0x0003, 0x0006, 0x0001, 0x0026, 0x0004, 0xFFFF, 0x002F, 0x0004, 0x0004,
			0x0004, 0x0016, 0xFFE0, 0xFFE0, 0x000D, 0x0007, 0x0001, 0x0021, 0x0002, 0x0001,
			0x0028, 0x0020, 0x0021, 0x0001, 0x0001, 0x0028, 0x0020, 0xFFFF, 0x0044, 0x0016,
			0xFFED, 0xFFED, 0x001E, 0x0004, 0x000D, 0x0005, 0x0001, 0x0001, 0x0096, 0x0004,
			0x001E, 0x0007, 0xFFFF, 0x0053, 0x0016, 0xFFC0, 0xFFC0, 0x001E, 0x0004, 0x000D,
			0x0001, 0x0001, 0x0001, 0x00B4, 0x0004, 0xFFFF, 0x0003,
		};

		// GLOBAL: TOY2 0x004E02FC
		uint16_t* g_bossMovementData = g_bossMovementScript;

		// GLOBAL: TOY2 0x0052F6F0
		int32_t g_encounterProgress;
		// GLOBAL: TOY2 0x0052F6F4
		int32_t g_bombPositionY;
		// GLOBAL: TOY2 0x0052F6F8
		int32_t g_bombRotationAngle;
		// GLOBAL: TOY2 0x0052F6FC
		int32_t g_bombVerticalVelocity;
		// GLOBAL: TOY2 0x0052F700
		int32_t g_thunderSoundTimer;
		// GLOBAL: TOY2 0x0052F704
		int32_t g_bossScale;
		// GLOBAL: TOY2 0x0052F708
		int32_t g_lightningFlashTimer;
		// GLOBAL: TOY2 0x0052F70C
		int32_t g_previousCutsceneDuration;
		// GLOBAL: TOY2 0x0052F710
		int32_t g_damageTintTimer;
		// GLOBAL: TOY2 0x0052F714
		int32_t g_unusedState;
		// GLOBAL: TOY2 0x0052F718
		int32_t g_introSoundTimer;
		// GLOBAL: TOY2 0x0052F71C
		int32_t g_attackSoundTimer;
		// GLOBAL: TOY2 0x0052F720
		int32_t g_hudWrapY;
		// GLOBAL: TOY2 0x0052F724
		int32_t g_hudWrapX;
		// GLOBAL: TOY2 0x0052F72C
		int32_t g_savedPickupVerticalPosition;
		// GLOBAL: TOY2 0x0052F730
		int32_t g_previousBossPhase;
		// GLOBAL: TOY2 0x0052F738
		Vector3I g_thunderSoundPosition;
		// GLOBAL: TOY2 0x0052F748
		int32_t g_tintAngleRed;
		// GLOBAL: TOY2 0x0052F74C
		int32_t g_tintAngleBlue;
		// GLOBAL: TOY2 0x0052F750
		int32_t g_tintAngleGreen;
		// GLOBAL: TOY2 0x0052F754
		int32_t g_encounterState;
		// GLOBAL: TOY2 0x0052F758
		int32_t g_phaseCutsceneDuration;
		// GLOBAL: TOY2 0x0052F75C
		int32_t g_targetBossScale;

		// FUNCTION: TOY2 0x0041A8A0 [PROVISIONAL]
		void Init()
		{
			MoveableObject::InitTable(0);
			Collectables::Init(0, 0);
			Weather::Init();

			Actor::Toy2Actor* boss = &Actor::g_creatureActors[0];
			g_targetBossScale = 0x1000;
			g_encounterProgress = 0x1000;
			g_previousBossPhase = boss->actorPhase;
			g_phaseCutsceneDuration = 180;
			g_introSoundTimer = 180;
			g_attackSoundTimer = 180;
			g_previousCutsceneDuration = Camera::g_cutsceneDuration;

			boss->actorFlags |= Actor::ACTOR_FLAG_BOSS;
			boss->yawAngle = 0x800;
			boss->targetYaw = 0x800;
			boss->pos.z += 0x10000;

			g_bossScale = 1;
			g_encounterState = 0;
			g_unusedState = 0;
			g_bombRotationAngle = 0;
			g_bombPositionY = 0;
			g_bombVerticalVelocity = 0;
			g_hudWrapX = 0;
			g_hudWrapY = 0;
			g_damageTintTimer = 0;
			g_tintAngleRed = 0;
			g_tintAngleGreen = 0;
			g_tintAngleBlue = 0;
			g_lightningFlashTimer = 200;
			g_thunderSoundTimer = 20000;

			boss->previousActorPhase = 0;
			boss->actorTint.r = 0x2000;
			boss->actorTint.g = 0x2000;
			boss->actorTint.b = 0x2000;
			boss->actorAlpha = 1000;

			Nu3D::Link::SetScaleFromFixedOffsets(0x32, 0, 0, 0);
			g_savedPickupVerticalPosition = Levels::g_recordData[63]->data[0].y;
			Levels::g_recordData[63]->data[0].y = INT_MIN;

			g_bombPositionY = boss->pos.y - 11000;
			Nu3D::Link::SetPositionRawAndCommit(0, boss->pos.x >> 5, g_bombPositionY, boss->pos.z >> 5);
			Nu3D::Link::SetScaleFromFixedOffsets(0, 5000, 5000, 5000);
		}

		// FUNCTION: TOY2 0x0041AA10 [PROVISIONAL]
		void Interactions()
		{
			Actor::Toy2Actor* boss = &Actor::g_creatureActors[0];
			bool bossVisible = false;

			AudioManager::PlaySoundEffect(0x6F, reinterpret_cast<const Vector3I*>(1));
			AudioManager::PlaySoundEffect(0x67, 0);
			if (g_introSoundTimer > 0)
			{
				g_introSoundTimer -= Renderer::g_frameDelta;
				if (g_introSoundTimer < 0)
					AudioManager::Preset::PlayOneShotSound(0xD2, &g_buzzActor);
			}

			g_tintAngleRed = (g_tintAngleRed + Renderer::g_frameDelta * 31) & 0xFFF;
			g_tintAngleGreen &= 0xFFF;
			g_tintAngleBlue = (g_tintAngleBlue + Renderer::g_frameDelta * 63) & 0xFFF;

			if (g_encounterState == ENCOUNTER_WAITING && Nu3D::Math::IsWithinDistanceXZ(&g_buzzActor.posAngles.pos, &boss->pos, 380) != 0)
			{
				boss->pos.y -= 11000;
				g_encounterState = ENCOUNTER_INTRO;
				Camera::BeginScriptedCutsceneAtPoint(&boss->pos, 300, 0x10);
				boss->pos.y += 11000;
				AudioManager::PlayMusicLooping(g_levelIndex);
			}

			if (g_encounterState == ENCOUNTER_INTRO)
			{
				if (Camera::g_cutsceneDuration < 240)
				{
					if (g_previousCutsceneDuration >= 240)
						AudioManager::PlaySoundEffect(0x46, &boss->pos);
					bossVisible = true;
				}
				if (Camera::g_cutsceneDuration < 150)
				{
					if (g_previousCutsceneDuration >= 150)
						AudioManager::PlaySoundEffect(-4, &boss->pos);
					g_bossScale -= (g_bossScale - g_targetBossScale) * Renderer::g_frameDelta / 32;
				}
				if (Camera::g_cutsceneDuration == 0)
				{
					boss->movementData = g_bossMovementData + 9;
					g_encounterState = ENCOUNTER_ACTIVE;
					boss->movementCommandTimer = 0;
					Nu3D::Link::SetRotationRelative8bit(0, 0, 0, 0);
				}
			}

			if (g_encounterState == ENCOUNTER_ACTIVE)
			{
				g_attackSoundTimer -= Renderer::g_frameDelta;
				if (g_attackSoundTimer < 1)
				{
					g_attackSoundTimer = *g_randDatBufferPtr++ + 0x708;
					AudioManager::Preset::PlayOneShotSound(0xD1, &g_buzzActor);
				}
				if (Nu3D::Math::IsWithinDistance(&g_buzzActor.posAngles.pos, &boss->pos, 50) != 0)
				{
					uint32_t damageAngle =
						Nu3D::Math::CartesianToFixedAngle(g_buzzActor.posAngles.pos.x - boss->pos.x, g_buzzActor.posAngles.pos.z - boss->pos.z);
					Buzz::HandleDamage(damageAngle & 0xFFF, 3);
				}

				if (g_previousBossPhase != boss->actorPhase)
				{
					g_damageTintTimer = 4;
					g_targetBossScale -= 0x300;
					boss->actorPhase = (int16_t)g_previousBossPhase;
					if (g_targetBossScale < 0x200)
					{
						g_targetBossScale = 0x200;
						g_encounterProgress += 0x800;
						for (int32_t particleIndex = 0; particleIndex < 64; particleIndex++)
						{
							Nu3D::Particles::ParticleInstance* particle = &Nu3D::Particles::g_particleInstances[particleIndex];
							if (particle->typeId == 0x3F)
								particle->lifetime = 1;
						}

						if (g_encounterProgress >= 0x3000 && g_savedPickupVerticalPosition != 0)
						{
							Nu3D::Link::SetScaleFromFixedOffsets(0x32, 0x1000, 0x1000, 0x1000);
							Levels::g_recordData[63]->data[0].y = g_savedPickupVerticalPosition;
							g_savedPickupVerticalPosition = 0;
						}
						if (g_encounterProgress >= 0x3800)
						{
							boss->movementData = g_bossMovementData + 94;
							boss->pos.y -= 11000;
							g_buzzActor.posAngles.pos.x += 10000;
							g_buzzActor.posAngles.pos.z += 10000;
							g_encounterProgress = 0x3800;
							g_bossScale = 1;
							g_targetBossScale = 1;
							boss->movementCommandTimer = 0;
							g_encounterState = ENCOUNTER_DEFEAT_CUTSCENE;
							Camera::BeginScriptedCutsceneAtPoint(&boss->pos, 420, 0x10);
							boss->actorFlags &= ~Actor::ACTOR_FLAG_COLLIDABLE;
							g_buzzActor.posAngles.pos.z -= 10000;
							g_buzzActor.posAngles.pos.x -= 10000;
							g_bombPositionY = boss->pos.y - 1000;
							boss->pos.y += 11000;
							SaveManager::g_save0Data.tokens[g_levelFileIndex] = 0x80;
						}
						else
						{
							boss->creatureRam->defenseMode = 4;
							boss->pos.y -= 14000;
							boss->movementData = g_bossMovementData + 79;
							boss->movementCommandTimer = 0;
							Camera::BeginScriptedCutsceneAtPoint(&boss->pos, g_phaseCutsceneDuration, 0x10);
							boss->pos.y += 14000;
							boss->velX = 0;
							boss->gravityVel = 0;
							boss->velForward = 0;
							Actor::SetAnimation(boss, 1, 0x10);
							g_phaseCutsceneDuration += 40;
						}
					}
				}

				if (boss->creatureRam->defenseMode == 4)
					Camera::g_cutsceneFocusPosition.y = boss->pos.y - (g_bossScale * 2 + 10000);

				if (g_targetBossScale < g_encounterProgress)
				{
					if (boss->creatureRam->defenseMode == 4)
					{
						if (Camera::g_cutsceneDuration < 120)
						{
							if (g_previousCutsceneDuration >= 120)
								AudioManager::PlaySoundEffect(-4, &boss->pos);
							g_targetBossScale -= (g_targetBossScale - g_encounterProgress) * Renderer::g_frameDelta / 32;
						}
						else
						{
							boss->movementCommandTimer = 5;
						}
					}
					else
					{
						g_targetBossScale += Renderer::g_frameDelta << 5;
					}
					if (g_targetBossScale >= g_encounterProgress)
						g_targetBossScale = g_encounterProgress;
				}

				g_bossScale -= (g_bossScale - g_targetBossScale) * Renderer::g_frameDelta / 8;
				if (boss->previousActorPhase != 0)
				{
					if (boss->previousActorPhase == 1)
					{
						Nu3D::Particles::ParticleInstance* particle =
							Nu3D::Particles::SpawnInstance(boss->pos.x, boss->pos.y - 10000, boss->pos.z, 0, -2, 0, boss->yawAngle << 2, 0, 0x80, 0x3F);
						particle->discPitchAngle = 0;
						AudioManager::PlaySoundEffect(0xD, &boss->pos);
					}
					if (boss->previousActorPhase == 3)
						Camera::g_shakeTimer = 40;
					boss->previousActorPhase = 0;
				}
				bossVisible = true;
			}

			if (g_encounterState == ENCOUNTER_DEFEAT_CUTSCENE)
			{
				Camera::g_cutsceneFocusPosition.x = boss->pos.x;
				Camera::g_cutsceneFocusPosition.y = boss->pos.y - 11000;
				Camera::g_cutsceneFocusPosition.z = boss->pos.z;
				bossVisible = true;
				if (Camera::g_cutsceneDuration < 180)
				{
					boss->pos.y += 9000;
					g_encounterState = ENCOUNTER_BOMB_FALLING;
					g_bombVerticalVelocity = -0xC00;
					Actor::Kill(boss, Actor::KILL_EFFECTS);
					boss->pos.y -= 9000;
				}
			}

			if (g_encounterState <= ENCOUNTER_DEFEAT_CUTSCENE)
			{
				g_bombRotationAngle = (g_bombRotationAngle + 0x80) & 0x1FFF;
				if (g_bossScale < 0x400)
					g_bombRotationAngle = 0;
				Nu3D::Link::SetRotationRelative8bit(
					0, Numerics::g_sinCosLUT[g_bombRotationAngle & 0xFFF] >> 8, boss->yawAngle, Numerics::g_sinCosLUT[(g_bombRotationAngle >> 1) & 0xFFF] >> 8);
				Vector4I bombPosition;
				bombPosition.x = 0;
				bombPosition.y = -700;
				bombPosition.z = 0;
				Actor::ResolveBoneAttachmentPos(&bombPosition, boss, 10);
				Nu3D::Link::SetPositionRawAndCommit(0, bombPosition.x >> 5, (bombPosition.y >> 5) - 0x20, bombPosition.z >> 5);
			}
			else
			{
				int32_t groundY = boss->pos.y - 2000;
				g_bombVerticalVelocity += Renderer::g_frameDelta * 0x60;
				g_bombPositionY += g_bombVerticalVelocity;
				Camera::g_cutsceneFocusPosition.y += 0x80;
				if (g_bombPositionY > groundY)
				{
					g_bombVerticalVelocity = -(g_bombVerticalVelocity >> 1);
					g_bombPositionY = groundY;
					if (abs(g_bombVerticalVelocity) > 0x80)
						AudioManager::PlaySoundEffect(0x45, &boss->pos);
				}
				Nu3D::Link::SetPositionRawAndCommit(0, boss->pos.x >> 5, g_bombPositionY >> 5, boss->pos.z >> 5);
			}

			if (g_encounterState == ENCOUNTER_BOMB_FALLING)
			{
				if (Camera::g_cutsceneDuration == 0)
					g_encounterState = ENCOUNTER_TOKEN_DELAY;
			}
			if (g_encounterState >= ENCOUNTER_TOKEN_DELAY && Collectables::g_tokenCollectionState == 0)
			{
				g_encounterState += Renderer::g_frameDelta;
				if (g_encounterState > ENCOUNTER_TOKEN_TRIGGER)
					Collectables::g_tokenCollectionState = 1;
			}

			if (g_framePulseOutputs.twoTickCount != 0 && bossVisible)
			{
				Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(boss->pos.x, boss->pos.y - 10000, boss->pos.z, 0x41, 0x14);
				particle->rotSpeed = (int16_t)((*g_randDatBufferPtr++ - 0x80) >> 3);
			}

			if (g_encounterState < ENCOUNTER_BOMB_FALLING)
			{
				boss->secondaryAnimIdx = boss->primaryAnimIdx - 1;
				boss->secondaryAnimationFramePosition = boss->animationFramePosition;
				boss->scalePivotHeight = -301;
				int32_t collisionOffset = -12000 - g_bossScale;
				if (collisionOffset == 0)
					collisionOffset = 1;
				boss->scaleX = (int16_t)g_bossScale;
				boss->scaleY = boss->scaleX;
				boss->scaleZ = boss->scaleX;
				int32_t collisionRadius = g_bossScale / 16;
				if (collisionRadius == 0)
					collisionRadius = 1;
				for (int32_t volumeIndex = 1; volumeIndex < 8; volumeIndex += 2)
				{
					boss->collisionVolumes[volumeIndex].offset.y = (int16_t)collisionOffset;
					boss->collisionVolumes[volumeIndex].radius = (int16_t)collisionRadius;
				}
			}

			g_hudActorAnimationFrame = ((0x3800 - g_encounterProgress) / 0x800) * 54 / 5;
			if (g_encounterState >= ENCOUNTER_ACTIVE)
				HUD::g_slideTimers[HUD::SLIDE_BOSS_STATUS] = 90;
			g_previousCutsceneDuration = Camera::g_cutsceneDuration;
			Renderer::BlitTextureByIndexOffset(0x18, 0, 0x40, 0x40, 0x40, g_hudWrapX, g_hudWrapY, 0, 0x40);
			g_hudWrapY = (g_hudWrapY - Renderer::g_frameDelta) & 0x3F;

			if (g_encounterState >= ENCOUNTER_ACTIVE)
			{
				Camera::g_actorCameraTarget.x = boss->pos.x;
				Camera::g_actorCameraTarget.y = boss->pos.y - 10000;
				Camera::g_actorCameraTarget.z = boss->pos.z;
			}

			Vector3I precipitationDirection = { 0x348D, -0x336DB, -0x30D5 };
			Renderer::LensFlare::RegisterLight(0x3889, -0x36099, -0x1FBC, 0x80, 0x80, 0x80, 0x80);
			if (g_damageTintTimer == 0)
			{
				boss->useTint = 0xF0000;
				boss->actorTint.r = Numerics::g_sinCosLUT[g_tintAngleRed] >> 4;
				boss->actorTint.g = Numerics::g_sinCosLUT[g_tintAngleGreen] >> 4;
				boss->actorTint.b = Numerics::g_sinCosLUT[g_tintAngleBlue] >> 4;
			}
			else
			{
				g_damageTintTimer -= Renderer::g_frameDelta;
				if (g_damageTintTimer < 0)
					g_damageTintTimer = 0;
				boss->useTint = 1;
				boss->actorTint.r = 0x2000;
				boss->actorTint.g = 0x2000;
				boss->actorTint.b = 0x2000;
			}

			Lighting::DynamicLight* particleLight = &Lighting::g_lightingState.dynamicLights[1];
			particleLight->lifetime = 0;
			for (int32_t particleIndex = 0; particleIndex < 64; particleIndex++)
			{
				Nu3D::Particles::ParticleInstance* particle = &Nu3D::Particles::g_particleInstances[particleIndex];
				if (particle->typeId == 0x3F && particle->lifetime > 0)
				{
					particleLight->colour.r = 0;
					particleLight->colour.g = 0xFF;
					particleLight->colour.b = 0;
					particleLight->lifetime = 1;
					particleLight->sourceId = -3;
					particleLight->position = particle->pos;
					break;
				}
			}

			precipitationDirection.x = (precipitationDirection.x - g_buzzActor.posAngles.pos.x) >> 7;
			precipitationDirection.y = (precipitationDirection.y - g_buzzActor.posAngles.pos.y) >> 7;
			precipitationDirection.z = (-0x30D5 - g_buzzActor.posAngles.pos.z) >> 7;
			Nu3D::Math::NormalizeToFixedPoint(&precipitationDirection, &precipitationDirection);
			Lighting::BuzzLightPreset* lightPreset = &Lighting::g_buzzLightPresets[g_levelFileIndex - 1];
			lightPreset->positionOffset.x = precipitationDirection.x >> 2;
			lightPreset->positionOffset.y = precipitationDirection.y >> 2;
			lightPreset->positionOffset.z = precipitationDirection.z >> 2;
			lightPreset->colour = 0xC0C000;

			Weather::StepPrecipitation(0x100, 0x1000, 0x32);
			if (g_framePulseOutputs.eightTick != 0)
			{
				int32_t randomOffset = (*g_randDatBufferPtr - 0x80) << 8;
				g_randDatBufferPtr += 2;
				Nu3D::Particles::SpawnFromPreset(
					Camera::g_renderCameraTransform.pos.x + Numerics::g_sinCosLUT[Camera::g_renderCameraTransform.rotation.euler.angles.yaw] * 3 + randomOffset,
					0,
					Camera::g_renderCameraTransform.pos.z
						+ Numerics::g_sinCosLUT[(Camera::g_renderCameraTransform.rotation.euler.angles.yaw + 0x400) & 0xFFF] * 3 + randomOffset,
					0x1B,
					2);
			}

			int32_t flashTimer = g_lightningFlashTimer - Renderer::g_frameDelta;
			if (flashTimer < 0x20)
			{
				if (g_lightningFlashTimer >= 0x20)
				{
					g_thunderSoundTimer = *g_randDatBufferPtr;
					uint32_t angle = (Camera::g_renderCameraTransform.rotation.euler.angles.yaw - 0x80 + g_randDatBufferPtr[1]) & 0xFFF;
					g_randDatBufferPtr += 2;
					g_thunderSoundPosition.x = (Numerics::g_sinCosLUT[angle] * g_thunderSoundTimer >> 5) + Camera::g_renderCameraTransform.pos.x;
					g_thunderSoundPosition.y = Camera::g_renderCameraTransform.pos.y;
					g_thunderSoundPosition.z =
						(Numerics::g_sinCosLUT[(angle + 0x400) & 0xFFF] * g_thunderSoundTimer >> 5) + Camera::g_renderCameraTransform.pos.z;
				}
				if (Nu3D::Camera::g_targetTintFadeSpeed == 0)
				{
					if (flashTimer < 0)
						flashTimer = *g_randDatBufferPtr++ * 2 + 0x20;
					else if (Nu3D::Camera::g_cameraTintBlue > 0x40)
					{
						Nu3D::Camera::g_cameraTintBlue = (int16_t)flashTimer * 3 + 0x80;
						Nu3D::Camera::g_cameraTintRed = Nu3D::Camera::g_cameraTintBlue;
						Nu3D::Camera::g_cameraTintGreen = Nu3D::Camera::g_cameraTintBlue;
					}
				}
				else
				{
					Nu3D::Camera::g_cameraTintRed = 0x80;
					Nu3D::Camera::g_cameraTintGreen = 0x80;
					Nu3D::Camera::g_cameraTintBlue = 0x80;
					g_lightningFlashTimer = 10000;
					flashTimer = g_lightningFlashTimer;
				}
			}
			g_lightningFlashTimer = flashTimer;
			g_thunderSoundTimer -= Renderer::g_frameDelta;
			if (g_thunderSoundTimer < 1)
			{
				g_thunderSoundTimer = 20000;
				AudioManager::PlaySoundEffect(0x70, &g_thunderSoundPosition);
			}
		}
	}
}
