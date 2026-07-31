#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Actor.h"
#include "Toy2/Camera.h"
#include "Toy2/Collectables.h"
#include "Toy2/Collision.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Random.h"
#include "SaveManager.h"

#include <limits.h>

namespace Toy2
{
	extern int32_t g_hudActorAnimationFrame;

	namespace Camera
	{
		extern Vector3I g_cutsceneFocusPosition;
		extern Vector3I g_cutsceneCameraPosition;
	}

	namespace SlimeTime
	{
		enum EncounterState
		{
			ENCOUNTER_WAITING = 0,
			ENCOUNTER_INTRO = 1,
			ENCOUNTER_ACTIVE = 2,
			ENCOUNTER_DEFEATED = 3,
			ENCOUNTER_REWARD = 4,
		};

		union EffectPosition
		{
			Vector4I vector;
			PosAndAngles groundProbe;
		};

		// GLOBAL: TOY2 0x0052F98C
		int32_t g_previousPhase;
		// GLOBAL: TOY2 0x0052F990
		int32_t g_phaseCutsceneTimer;
		// GLOBAL: TOY2 0x0052F994
		int32_t g_bobAngle;
		// GLOBAL: TOY2 0x0052F998
		int32_t g_attackSoundTimer;
		// GLOBAL: TOY2 0x0052F99C
		int32_t g_actorOutsideArena;
		// GLOBAL: TOY2 0x0052F9A0
		int32_t g_movementCycleTimer;
		// GLOBAL: TOY2 0x0052F9A4
		EncounterState g_encounterState;
		// GLOBAL: TOY2 0x0052F9A8
		int32_t g_tintFlashToggle;
		// GLOBAL: TOY2 0x0052F9AC
		int32_t g_unusedStateLimit;
		// GLOBAL: TOY2 0x0052F9B0
		int32_t g_nearbySoundCooldown;
		// GLOBAL: TOY2 0x0052F9B4
		int32_t g_previousCameraYaw;
		// GLOBAL: TOY2 0x0052F9B8
		int32_t g_horizontalMovementDirection;

		// FUNCTION: TOY2 0x0041FFB0 [MATCHED]
		void Init()
		{
			MoveableObject::InitTable(0);
			Collectables::Init(0, 0);

			Actor::Toy2Actor* boss = &Actor::g_creatureActors[0];
			g_previousPhase = boss->actorPhase;
			boss->actorFlags |= Actor::ACTOR_FLAG_BOSS;
			boss->pos.x += 0x80000;
			g_encounterState = ENCOUNTER_WAITING;
			g_movementCycleTimer = 300;
			g_horizontalMovementDirection = 0x40000;
			g_actorOutsideArena = 0;
			g_attackSoundTimer = 150;
			g_bobAngle = 0;
			g_unusedStateLimit = 0x7FFFFFFF;
			g_previousCameraYaw = 0;
			g_nearbySoundCooldown = 0;
			g_tintFlashToggle = 0;
			g_phaseCutsceneTimer = 0;

			boss->yawAngle = 0xC00;
			boss->creatureRam->boundHalfX = 0x1000;
			boss->creatureRam->boundHalfZ = 0x1000;
		}

		// FUNCTION: TOY2 0x00420060 [PROVISIONAL]
		void Interactions()
		{
			Actor::Toy2Actor* boss = &Actor::g_creatureActors[0];
			EffectPosition particlePosition;
			AudioManager::PlaySoundEffect(0x67, 0);
			g_tintFlashToggle = (g_tintFlashToggle - 1) & 1;

			if (boss->actorPhase != 0 && boss->actorPhase != g_previousPhase)
			{
				g_previousPhase = boss->actorPhase;
				boss->creatureRam->defenseMode = 4;
				g_movementCycleTimer = 299;
				if (boss->actorPhase <= 10)
				{
					g_phaseCutsceneTimer = 300;
					Camera::BeginScriptedCutsceneAtPoint(&boss->pos, 300, 0x10);
					boss->motionTargetPos.z = boss->pos.z;
					Camera::g_cutsceneCameraPosition.z = boss->pos.z;
					Camera::g_cutsceneCameraPosition.y = boss->pos.y - 0x5000;
					boss->motionTargetPos.x = boss->pos.x;
					Camera::g_cutsceneCameraPosition.x = boss->pos.x;
					g_encounterState = ENCOUNTER_DEFEATED;
					SaveManager::g_save0Data.tokens[g_levelFileIndex] = 0x80;
				}
				else
				{
					g_phaseCutsceneTimer = 120;
					Camera::BeginScriptedCutsceneAtPoint(&boss->pos, 120, 0x10);
				}
			}

			g_phaseCutsceneTimer -= Renderer::g_frameDelta;
			if (g_phaseCutsceneTimer < 0)
			{
				g_phaseCutsceneTimer = 0;
				boss->creatureRam->defenseMode = 7;
				boss->useTint = 0;
			}
			else if (boss->creatureId != 0)
			{
				Camera::g_cutsceneFocusPosition = boss->pos;
				if (g_phaseCutsceneTimer < 90)
					AudioManager::PlaySoundEffect(0x66, &Camera::g_cutsceneFocusPosition);

				if (g_tintFlashToggle != 0)
				{
					boss->useTint = 1;
					boss->actorTint.r = 0x2000;
					boss->actorTint.g = 0x2000;
					boss->actorTint.b = 0x2000;
				}
				else
				{
					boss->useTint = 0;
				}
			}

			if (Actor::IsInsideBounds(&boss->pos, -0x390AF, 0x390AF, -0x21F9E, 0x21F9E) == 0)
			{
				g_actorOutsideArena = 1;
				if (g_bobAngle < 0x800)
				{
					g_bobAngle += Renderer::g_frameDelta * 0x80;
					if (g_bobAngle > 0x7FF)
						g_bobAngle = 0x800;
				}
			}
			else
			{
				g_actorOutsideArena = 0;
				if (g_bobAngle > 0)
				{
					g_bobAngle -= Renderer::g_frameDelta * 0x80;
					if (g_bobAngle < 1)
						g_bobAngle = 0;
				}
			}

			if (g_buzzActor.posAngles.pos.x < 0x31F9E && g_encounterState == ENCOUNTER_WAITING)
			{
				AudioManager::PlayMusicLooping(g_levelIndex);
				g_encounterState = ENCOUNTER_INTRO;
				Camera::BeginScriptedCutsceneAtPoint(&boss->pos, 300, 0x10);
				Camera::g_cutsceneCameraPosition.x = boss->pos.x - 0x48000;
				Camera::g_cutsceneCameraPosition.y = boss->pos.y - 0x8000;
				Camera::g_cutsceneCameraPosition.z = boss->pos.z;
			}

			if (g_encounterState > ENCOUNTER_WAITING && g_encounterState < ENCOUNTER_DEFEATED)
			{
				if (g_phaseCutsceneTimer == 0)
					AudioManager::PlaySoundEffect(0x61, &boss->pos);

				if (g_nearbySoundCooldown == 0 && Nu3D::Math::IsWithinDistance(&g_buzzActor.posAngles.pos, &boss->pos, 400) != 0)
				{
					int32_t cameraYawDelta = (g_previousCameraYaw - Camera::g_renderCameraTransform.rotation.euler.angles.yaw) & 0xFFF;
					if (cameraYawDelta > 0x800)
						cameraYawDelta -= 0x1000;

					if (abs(cameraYawDelta) > 0x40)
					{
						AudioManager::PlaySoundEffect((*g_randDatBufferPtr++ & 1) + 0x62, &boss->pos);
						g_nearbySoundCooldown = 100;
					}
				}
				else
				{
					g_nearbySoundCooldown -= Renderer::g_frameDelta;
					if (g_nearbySoundCooldown < 1)
						g_nearbySoundCooldown = 0;
				}
				g_previousCameraYaw = Camera::g_renderCameraTransform.rotation.euler.angles.yaw;
			}

			if (g_encounterState == ENCOUNTER_INTRO)
			{
				if (Camera::g_cutsceneDuration < 30)
					boss->actorFlags |= Actor::ACTOR_FLAG_TRACKS_TARGET;

				int32_t cutsceneProgress = Camera::g_cutsceneDuration - 30;
				if (cutsceneProgress < 0)
					cutsceneProgress = 0;
				boss->rollAngle = ((uint16_t)Numerics::g_sinCosLUT[(cutsceneProgress * 20 + 0x400) & 0xFFF] >> 3) - 0x800;
				Camera::g_cutsceneFocusPosition.x = boss->pos.x - Renderer::g_frameDelta * 0x800;
				if (Camera::g_cutsceneDuration == 0)
					g_encounterState = ENCOUNTER_ACTIVE;
				Camera::g_cutsceneCameraPosition.y += Renderer::g_frameDelta * 0x130;
				boss->pos.x = Camera::g_cutsceneFocusPosition.x;
				if (Camera::g_cutsceneDuration < 180)
					Camera::g_cutsceneCameraPosition.x -= Renderer::g_frameDelta * 0x200;
				else
					Camera::g_cutsceneCameraPosition.z -= Renderer::g_frameDelta * 0x80;
			}

			if (g_encounterState == ENCOUNTER_ACTIVE)
			{
				g_movementCycleTimer -= Renderer::g_frameDelta;
				if (g_movementCycleTimer <= 0)
				{
					g_movementCycleTimer = 600;
					g_horizontalMovementDirection = -g_horizontalMovementDirection;
				}

				Camera::g_actorCameraTarget = boss->pos;
				if (g_framePulseOutputs.sixtyFourTick != 0 && g_phaseCutsceneTimer == 0
					&& Nu3D::Math::IsWithinDistance(&g_buzzActor.posAngles.pos, &boss->pos, 400) != 0)
				{
					Nu3D::Particles::SpawnInstance(boss->pos.x, boss->pos.y, boss->pos.z, boss->velX * 2, 0, boss->velForward * 2, 0x80, 0, 0, 0xC);
				}

				if (g_movementCycleTimer > 300)
				{
					boss->motionTargetPos.y = Numerics::g_sinCosLUT[g_bobAngle + 0x400] * 3 - 0x10B13;
					boss->motionTargetPos.x = g_buzzActor.posAngles.pos.x;
					boss->motionTargetPos.z = g_buzzActor.posAngles.pos.z;
					if (g_movementCycleTimer < 550)
					{
						AudioManager::PlaySoundEffect(0x65, &boss->motionTargetPos);
						g_attackSoundTimer -= Renderer::g_frameDelta;
						if (g_attackSoundTimer < 0)
						{
							g_attackSoundTimer = 400;
							AudioManager::Preset::PlayOneShotSound(0xD0, &g_buzzActor);
						}
					}

					if (g_framePulseOutputs.sixTick != 0 && g_movementCycleTimer < 550)
					{
						int32_t bossYaw = boss->yawAngle;
						particlePosition.vector.x = boss->pos.x + Numerics::g_sinCosLUT[(bossYaw - 0x80) & 0xFFF] * 4;
						particlePosition.vector.y = boss->pos.y;
						particlePosition.vector.z = boss->pos.z + Numerics::g_sinCosLUT[(bossYaw + 0x380) & 0xFFF] * 4;
						particlePosition.vector.y = Nu3D::Collision::GetGroundHeight(&particlePosition.groundProbe, 0);
						if (particlePosition.vector.y != INT_MIN)
						{
							Nu3D::Particles::SpawnFromPreset(particlePosition.vector.x, particlePosition.vector.y, particlePosition.vector.z, 0x7B, 2);
							Nu3D::Particles::ParticleInstance* particle =
								Nu3D::Particles::SpawnFromPreset(particlePosition.vector.x, particlePosition.vector.y, particlePosition.vector.z, 0x7C, 1);
							particle->rotSpeed = *g_randDatBufferPtr++;
						}

						particlePosition.vector.x = boss->pos.x + Numerics::g_sinCosLUT[(bossYaw + 0x80) & 0xFFF] * 4;
						particlePosition.vector.y = boss->pos.y;
						particlePosition.vector.z = boss->pos.z + Numerics::g_sinCosLUT[(bossYaw + 0x480) & 0xFFF] * 4;
						particlePosition.vector.y = Nu3D::Collision::GetGroundHeight(&particlePosition.groundProbe, 0);
						if (particlePosition.vector.y != INT_MIN)
						{
							Nu3D::Particles::SpawnFromPreset(particlePosition.vector.x, particlePosition.vector.y, particlePosition.vector.z, 0x7B, 2);
							Nu3D::Particles::ParticleInstance* particle =
								Nu3D::Particles::SpawnFromPreset(particlePosition.vector.x, particlePosition.vector.y, particlePosition.vector.z, 0x7C, 1);
							particle->rotSpeed = *g_randDatBufferPtr++;
						}
					}
				}
				else
				{
					boss->motionTargetPos.x = g_horizontalMovementDirection;
					boss->motionTargetPos.z = 0;
					boss->motionTargetPos.y = Numerics::g_sinCosLUT[g_bobAngle + 0x400] * 3 + 0x4000 + boss->boundary.y;
				}
				HUD::g_slideTimers[HUD::SLIDE_BOSS_STATUS] = 90;
			}

			if (g_encounterState == ENCOUNTER_DEFEATED)
			{
				boss->motionTargetPos.y -= Renderer::g_frameDelta * 0x280;
				boss->pos.y -= Renderer::g_frameDelta * 0x280;
				Camera::g_cutsceneCameraPosition.y -= Renderer::g_frameDelta * 0x80;
				if (boss->actorPhase <= 10 && Camera::g_cutsceneDuration < 120)
					Actor::Kill(boss, Actor::KILL_EFFECTS);
				if (Camera::g_cutsceneDuration == 0)
				{
					Collectables::g_tokenCollectionState = 1;
					g_encounterState = ENCOUNTER_REWARD;
				}
			}

			if ((boss->actorFlags & Actor::ACTOR_FLAG_TRACKS_TARGET) != 0)
			{
				if (g_phaseCutsceneTimer != 0)
				{
					boss->rollAngle = (boss->rollAngle + (Renderer::g_frameDelta << 6)) & 0xFFF;
					if (g_encounterState == ENCOUNTER_DEFEATED)
						boss->pitchAngle = (boss->pitchAngle + Renderer::g_frameDelta * 0x24) & 0xFFF;
				}
				else
				{
					int32_t yawDelta = (boss->yawAngle - boss->targetYaw) & 0xFFF;
					if (yawDelta > 0x800)
						yawDelta -= 0x1000;
					boss->rollAngle -= (boss->rollAngle - yawDelta / 2) >> 5;
				}
			}

			if ((boss->actorFlags & Actor::ACTOR_FLAG_TARGETABLE) != 0)
			{
				if (g_framePulseOutputs.twoTickCount != 0)
				{
					particlePosition.vector.x = 500;
					particlePosition.vector.y = -400;
					particlePosition.vector.z = 0;
					Actor::ResolveBoneAttachmentPos(&particlePosition.vector, boss, 0);
					Nu3D::Particles::ParticleInstance* particle =
						Nu3D::Particles::SpawnFromPreset(particlePosition.vector.x, particlePosition.vector.y, particlePosition.vector.z, 0x11, 2);
					particle->rotSpeed = (*g_randDatBufferPtr++ & 0xFF1F) - 0x10;

					particlePosition.vector.x = -500;
					particlePosition.vector.y = -400;
					particlePosition.vector.z = 0;
					Actor::ResolveBoneAttachmentPos(&particlePosition.vector, boss, 0);
					particle = Nu3D::Particles::SpawnFromPreset(particlePosition.vector.x, particlePosition.vector.y, particlePosition.vector.z, 0x11, 2);
					particle->rotSpeed = (*g_randDatBufferPtr++ & 0xFF1F) - 0x10;
				}

				if (boss->actorPhase <= 10 && g_framePulseOutputs.sevenTick != 0)
				{
					particlePosition.vector.x = 0;
					particlePosition.vector.y = 0;
					particlePosition.vector.z = -200;
					Actor::ResolveBoneAttachmentPos(&particlePosition.vector, boss, 0);
					Nu3D::Particles::ParticleInstance* particle =
						Nu3D::Particles::SpawnFromPreset(particlePosition.vector.x, particlePosition.vector.y, particlePosition.vector.z, 0x23, 0xE);
					particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
				}
			}

			g_hudActorAnimationFrame = ((boss->actorPhase - 10) * 27) / 5;
		}
	}
}
