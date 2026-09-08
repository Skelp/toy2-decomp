#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Actor.h"
#include "Toy2/Buzz.h"
#include "Toy2/Camera.h"
#include "Toy2/Collectables.h"
#include "Toy2/Collision.h"
#include "Toy2/Lighting.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Random.h"
#include "SaveManager.h"
#include "Numerics.h"

namespace Toy2
{
	extern int32_t g_hudActorAnimationFrame;

	namespace Game
	{
		void InitActor(Actor::Toy2Actor* actor, int32_t fullInit);
	}

	namespace BarnEncounter
	{
		union AttachmentPosition
		{
			Vector4I vector;
			Vector3I position;
			PosAndAngles groundProbe;
		};

		enum EncounterState
		{
			ENCOUNTER_WAITING = 0,
			ENCOUNTER_INTRO = 1,
			ENCOUNTER_WAVES = 2,
			ENCOUNTER_FINAL_PHASE = 3,
			ENCOUNTER_DEFEATED = 4,
			ENCOUNTER_REWARD = 5,
		};

		// GLOBAL: TOY2 0x004F2F5C
		int32_t g_minionActorPairs[] = {
			8,
			8,
			7,
			7,
			8,
			9,
			10,
			7,
			11,
			8,
			11,
			7,
		};

		// GLOBAL: TOY2 0x0052FBB8
		int32_t g_previousBossPhase;
		// GLOBAL: TOY2 0x0052FBBC
		int32_t g_cameraTargetActorIndex;
		// GLOBAL: TOY2 0x0052FBC0
		int32_t g_phaseTimer;
		// GLOBAL: TOY2 0x0052FBC4
		int32_t g_secondMinionActorIndex;
		// GLOBAL: TOY2 0x0052FBC8
		int32_t g_firstMinionActorIndex;
		// GLOBAL: TOY2 0x0052FBCC
		int32_t g_orbitHeight;
		// GLOBAL: TOY2 0x0052FBD0
		int32_t g_cameraTargetUpdateTimer;
		// GLOBAL: TOY2 0x0052FBD4
		int32_t g_waveActorIndex;
		// GLOBAL: TOY2 0x0052FBE8
		int32_t g_tintFlashToggle;
		// GLOBAL: TOY2 0x0052FBEC
		int32_t g_orbitPosition;
		// GLOBAL: TOY2 0x0052FBF0
		int32_t g_waitingForMinions;
		// GLOBAL: TOY2 0x0052FBF4
		int32_t g_nextMinionPairIndex;
		// GLOBAL: TOY2 0x0052FBF8
		int32_t g_introSoundTimer;
		// GLOBAL: TOY2 0x0052FBFC
		int32_t g_damageSoundCooldown;
		// GLOBAL: TOY2 0x0052FC00
		int32_t g_laserCooldown;
		// GLOBAL: TOY2 0x0052FC04
		int32_t g_encounterState;
		// GLOBAL: TOY2 0x0052FC08
		int32_t g_heightBobAngle;
		// GLOBAL: TOY2 0x0052FC0C
		int32_t g_orbitAngle;

		// FUNCTION: TOY2 0x00424390 [MATCHED]
		void Init()
		{
			MoveableObject::InitTable(0);
			Collectables::Init(0, 0);

			g_previousBossPhase = Actor::g_creatureActors[0].actorPhase;
			g_orbitAngle = 0;
			g_orbitPosition = 0;
			g_heightBobAngle = 0;
			g_orbitHeight = -0x58000;
			g_encounterState = ENCOUNTER_WAITING;
			g_phaseTimer = 0;
			g_tintFlashToggle = 0;
			g_waveActorIndex = 1;
			g_waitingForMinions = 0;
			g_laserCooldown = 200;
			g_firstMinionActorIndex = 7;
			g_secondMinionActorIndex = 7;
			g_nextMinionPairIndex = 0;
			g_cameraTargetActorIndex = 0;
			g_cameraTargetUpdateTimer = 0;
			g_introSoundTimer = 120;
			g_damageSoundCooldown = 0;
			Actor::g_creatureActors[0].creatureRam->speedTarget = 0;

			for (int32_t actorIndex = 7; actorIndex < 12; actorIndex++)
			{
				Actor::g_creatureActors[actorIndex].respawnDelay = 10000;
				Actor::g_creatureActors[actorIndex].actorPhase = 0;
				Actor::g_creatureActors[actorIndex].scalePivotHeight = 0;
				Actor::g_creatureActors[actorIndex].creatureRam->boundHalfX = Actor::g_creatureActors[6].creatureRam->boundHalfX;
				Actor::g_creatureActors[actorIndex].creatureRam->boundHalfZ = Actor::g_creatureActors[6].creatureRam->boundHalfZ;
			}

			for (int32_t bossActorIndex = 0; bossActorIndex < 12; bossActorIndex++)
			{
				Actor::g_creatureActors[bossActorIndex].actorFlags |= Actor::ACTOR_FLAG_BOSS;
			}
		}

		// FUNCTION: TOY2 0x00424490 [PROVISIONAL]
		void Interactions()
		{
			AudioManager::PlaySoundEffect(0x67, 0);

			int32_t bossX = Actor::g_creatureActors[0].pos.x >> 5;
			int32_t bossZ = Actor::g_creatureActors[0].pos.z >> 5;
			const int32_t initialArenaRadius = 0x1068;
			const int32_t finalArenaExpansion = 0x320;
			int32_t arenaRadius = ((g_encounterState < ENCOUNTER_FINAL_PHASE) - 1 & finalArenaExpansion) + initialArenaRadius;
			if (bossX * bossX + bossZ * bossZ > arenaRadius * arenaRadius)
			{
				uint32_t angle = Nu3D::Math::CartesianToFixedAngle(bossX, bossZ);
				angle &= 0xFFF;
				Actor::g_creatureActors[0].pos.x = Numerics::g_sinCosLUT[angle] * arenaRadius >> 9;
				Actor::g_creatureActors[0].pos.z = Numerics::g_sinCosLUT[(angle + 0x400) & 0xFFF] * arenaRadius >> 9;
			}

			if (g_nextMinionPairIndex > 0)
			{
				if ((g_gameplayStateFlags & GAMEPLAY_STATE_CUTSCENE_ACTIVE) != 0)
				{
					Actor::g_creatureActors[g_firstMinionActorIndex].velX = 0;
					Actor::g_creatureActors[g_firstMinionActorIndex].velForward = 0;
					Actor::g_creatureActors[g_secondMinionActorIndex].velX = 0;
					Actor::g_creatureActors[g_secondMinionActorIndex].velForward = 0;
				}

				int32_t minionX = Actor::g_creatureActors[g_firstMinionActorIndex].pos.x >> 5;
				int32_t minionZ = Actor::g_creatureActors[g_firstMinionActorIndex].pos.z >> 5;
				if (minionX * minionX + minionZ * minionZ > 0x1DE8400)
				{
					uint32_t angle = Nu3D::Math::CartesianToFixedAngle(minionX, minionZ);
					angle &= 0xFFF;
					Actor::g_creatureActors[g_firstMinionActorIndex].pos.x = Numerics::g_sinCosLUT[angle] * 0x15E0 >> 9;
					Actor::g_creatureActors[g_firstMinionActorIndex].pos.z = Numerics::g_sinCosLUT[(angle + 0x400) & 0xFFF] * 0x15E0 >> 9;
				}
				minionX = Actor::g_creatureActors[g_secondMinionActorIndex].pos.x >> 5;
				minionZ = Actor::g_creatureActors[g_secondMinionActorIndex].pos.z >> 5;
				if (minionX * minionX + minionZ * minionZ > 0x1DE8400)
				{
					uint32_t angle = Nu3D::Math::CartesianToFixedAngle(minionX, minionZ);
					angle &= 0xFFF;
					Actor::g_creatureActors[g_secondMinionActorIndex].pos.x = Numerics::g_sinCosLUT[angle] * 0x15E0 >> 9;
					Actor::g_creatureActors[g_secondMinionActorIndex].pos.z = Numerics::g_sinCosLUT[(angle + 0x400) & 0xFFF] * 0x15E0 >> 9;
				}
			}

			Actor::Toy2Actor* boss = &Actor::g_creatureActors[0];
			Actor::Toy2Actor* firstMinion = &Actor::g_creatureActors[g_firstMinionActorIndex];
			Actor::Toy2Actor* secondMinion = &Actor::g_creatureActors[g_secondMinionActorIndex];
			{
				AttachmentPosition orbitPositions[6];
				int32_t bossWorldX = boss->pos.x;
				int32_t bossWorldZ = boss->pos.z;
				int32_t orbitPosition = g_orbitPosition;
				uint32_t orbitAngle = g_orbitAngle;
				for (int32_t actorIndex = 1; actorIndex < 7; actorIndex++)
				{
					Actor::Toy2Actor* actor = &Actor::g_creatureActors[actorIndex];
					actor->pos.x = bossWorldX;
					actor->pos.z = bossWorldZ;
					actor->yawAngle = static_cast<int16_t>(orbitAngle);
					actor->targetYaw = orbitAngle;
					if (actor->primaryAnimIdx == 0)
						actor->animationFramePosition = orbitPosition;

					orbitPosition += 0xC0000;
					if (orbitPosition > 0x17FFFF)
						orbitPosition -= 0x180000;

					AttachmentPosition& attachmentPosition = orbitPositions[actorIndex - 1];
					attachmentPosition.vector.x = 0;
					attachmentPosition.vector.y = 0x96;
					attachmentPosition.vector.z = 0x5AA;
					orbitAngle = (orbitAngle + 0x2AB) & 0xFFF;
					Actor::ResolveBoneAttachmentPos(&attachmentPosition.vector, actor, 1);
					if (actor->actorPhase == 1)
					{
						Nu3D::Collision::GetGroundHeight(&attachmentPosition.groundProbe, 0x96);
						if (Nu3D::Math::IsWithinDistance(&g_buzzActor.posAngles.pos, &attachmentPosition.position, 0x4B) != 0)
						{
							uint32_t direction = Nu3D::Math::CartesianToFixedAngle(
								g_buzzActor.posAngles.pos.x - attachmentPosition.position.x, g_buzzActor.posAngles.pos.z - attachmentPosition.position.z);
							Buzz::HandleDamage(direction, 1);
						}
					}
				}

				int32_t orbitHeight = (Numerics::g_sinCosLUT[g_heightBobAngle] >> 3) + g_orbitHeight;
				for (int32_t heightActorIndex = 0; heightActorIndex < 7; heightActorIndex++)
				{
					Actor::g_creatureActors[heightActorIndex].pos.y = orbitHeight;
					Actor::g_creatureActors[heightActorIndex].motionTargetPos.y = orbitHeight;
				}

				g_orbitAngle = (g_orbitAngle + Renderer::g_frameDelta * 4) & 0xFFF;
				g_heightBobAngle = (g_heightBobAngle + Renderer::g_frameDelta * 0x20) & 0xFFF;
				g_orbitPosition += Renderer::g_frameDelta * 0x4000;
				if (g_orbitPosition > 0x17FFFF)
					g_orbitPosition -= 0x180000;
				g_tintFlashToggle = (g_tintFlashToggle - 1) & 1;

				if (boss->actorPhase != 0 && boss->actorPhase != g_previousBossPhase && g_encounterState < ENCOUNTER_DEFEATED)
				{
					AudioManager::PlaySoundEffect(0x83, &boss->pos);
					if (g_waveActorIndex < 6)
						boss->actorPhase = static_cast<int16_t>(0x1A - g_waveActorIndex);
					if (g_nextMinionPairIndex < 12)
					{
						g_firstMinionActorIndex = g_minionActorPairs[g_nextMinionPairIndex];
						g_secondMinionActorIndex = g_minionActorPairs[g_nextMinionPairIndex + 1];
						g_nextMinionPairIndex += 2;
					}

					g_previousBossPhase = boss->actorPhase;
					boss->creatureRam->defenseMode = 4;
					if (g_encounterState == ENCOUNTER_WAVES)
					{
						boss->creatureRam->speedTarget = 0;
						g_phaseTimer = 600;
						Camera::BeginScriptedCutsceneAtPoint(&boss->pos, 0x168, 0x10);
					}
					else
					{
						g_phaseTimer = 0x78;
					}

					if (boss->actorPhase < 11)
					{
						Camera::BeginScriptedCutsceneAtPoint(&boss->pos, 0xF0, 0x10);
						Camera::g_cutsceneCameraPosition.x =
							Camera::g_cutsceneFocusPosition.x + Numerics::g_sinCosLUT[(g_buzzActor.posAngles.angles.yaw + 0x100) & 0xFFF] * 4;
						Camera::g_cutsceneCameraPosition.y -= 0x8000;
						Camera::g_cutsceneCameraPosition.z =
							Camera::g_cutsceneFocusPosition.z + Numerics::g_sinCosLUT[(g_buzzActor.posAngles.angles.yaw + 0x500) & 0xFFF] * 4;
						boss->creatureRam->speedTarget = 0;
						g_laserCooldown = 10000;
						g_encounterState = ENCOUNTER_DEFEATED;
						g_phaseTimer = 0;
						SaveManager::g_save0Data.tokens[g_levelFileIndex] = 0x80;
						Camera::InitCutsceneCamera(&Camera::g_cutsceneFocusPosition, &Camera::g_cutsceneCameraPosition);
					}
				}

				g_phaseTimer -= Renderer::g_frameDelta;
				if (g_phaseTimer < 1)
				{
					g_phaseTimer = 0;
					boss->creatureRam->defenseMode = 7;
					boss->useTint = 0;
					if (g_encounterState == ENCOUNTER_WAVES && g_waveActorIndex == 7)
					{
						g_encounterState = ENCOUNTER_FINAL_PHASE;
						boss->creatureRam->rotSpeed = 2;
						boss->creatureRam->speedTarget = 0x14;
					}
				}
				else if (g_encounterState == ENCOUNTER_WAVES)
				{
					uint16_t pulse = g_framePulsePhases.thirtyTwoTick;
					if (pulse > 0x1F)
						pulse = 0x3F - pulse;
					boss->useTint = 1;
					boss->actorTint.g = (pulse + 0x20) * 0x80;
					boss->actorTint.r = 0x1000;
					boss->actorTint.b = 0x1000;
				}
				else if (g_tintFlashToggle == 0)
				{
					boss->useTint = 0;
				}
				else
				{
					boss->useTint = 1;
					boss->actorTint.r = 0x2000;
					boss->actorTint.g = 0x2000;
					boss->actorTint.b = 0x2000;
				}

				if (g_buzzActor.posAngles.pos.z > -0x1BB58 && g_encounterState == ENCOUNTER_WAITING)
				{
					AudioManager::PlayMusicLooping(g_levelIndex);
					g_encounterState = ENCOUNTER_INTRO;
					g_orbitHeight = -0x38000;
					Camera::BeginScriptedCutsceneAtPoint(&boss->pos, 0x168, 0x10);
					Camera::g_cutsceneCameraPosition.y = g_buzzActor.posAngles.pos.y - 0x8000;
					Camera::g_cutsceneCameraPosition.x = g_buzzActor.posAngles.pos.x;
					Camera::g_cutsceneCameraPosition.z = g_buzzActor.posAngles.pos.z;
					Camera::InitCutsceneCamera(&Camera::g_cutsceneFocusPosition, &Camera::g_cutsceneCameraPosition);
				}

				if (g_encounterState == ENCOUNTER_INTRO)
				{
					if (g_introSoundTimer > 0)
					{
						g_introSoundTimer -= Renderer::g_frameDelta;
						if (g_introSoundTimer < 1)
						{
							const Vector3I* soundMode = reinterpret_cast<const Vector3I*>(1);
							AudioManager::PlaySoundEffect(0xD5, soundMode);
						}
					}
					Camera::g_cutsceneCameraPosition.y -= Renderer::g_frameDelta * 0x80;
					Camera::g_cutsceneFocusPosition = boss->pos;
					if (g_orbitHeight < -0x8000)
					{
						g_orbitHeight += Renderer::g_frameDelta * 0x280;
						if (g_orbitHeight > -0x8001)
							g_orbitHeight = -0x8000;
					}
					if (Camera::g_cutsceneDuration == 0)
					{
						g_encounterState = ENCOUNTER_WAVES;
						boss->creatureRam->speedTarget = 0x10;
					}
				}

				firstMinion = &Actor::g_creatureActors[g_firstMinionActorIndex];
				secondMinion = &Actor::g_creatureActors[g_secondMinionActorIndex];
				if (g_encounterState == ENCOUNTER_WAVES && g_phaseTimer != 0)
				{
					if (g_phaseTimer > 0x1E0)
						g_phaseTimer = 600;
					if (Camera::g_cutsceneDuration == 0)
					{
						if (g_waitingForMinions != 0 || g_phaseTimer >= 0x1A4)
						{
							if (g_waitingForMinions == 0)
							{
								g_waitingForMinions = 1;
								Actor::g_creatureActors[g_firstMinionActorIndex].respawnDelay = 10000;
								Actor::g_creatureActors[g_secondMinionActorIndex].respawnDelay = 10000;
							}
							if (Actor::g_creatureActors[g_firstMinionActorIndex].actorPhase == 0
								&& Actor::g_creatureActors[g_secondMinionActorIndex].actorPhase == 0)
							{
								g_phaseTimer = 0x78;
								g_waitingForMinions = 0;
								boss->creatureRam->speedTarget = 0x10;
								g_waveActorIndex++;
							}
						}
					}
					else
					{
						g_laserCooldown = 200;
						Actor::Toy2Actor* waveActor = &Actor::g_creatureActors[g_waveActorIndex];
						Camera::g_cutsceneFocusPosition.x = orbitPositions[g_waveActorIndex - 1].position.x;
						Camera::g_cutsceneFocusPosition.y = orbitPositions[g_waveActorIndex - 1].position.y;
						Camera::g_cutsceneFocusPosition.z = orbitPositions[g_waveActorIndex - 1].position.z;
						Camera::g_cutsceneCameraPosition.x =
							Camera::g_cutsceneFocusPosition.x + Numerics::g_sinCosLUT[(waveActor->yawAngle + 0x400) & 0xFFF] * 3;
						Camera::g_cutsceneCameraPosition.z =
							Camera::g_cutsceneFocusPosition.z + Numerics::g_sinCosLUT[(waveActor->yawAngle - 0x800) & 0xFFF] * 3;
						Camera::g_cutsceneCameraPosition.y = Camera::g_cutsceneFocusPosition.y;

						if (Camera::g_cutsceneDuration < 300 && waveActor->primaryAnimIdx == 0)
						{
							Actor::SetAnimation(waveActor, 1, 0x17);
							Game::InitActor(&Actor::g_creatureActors[g_firstMinionActorIndex], 0);
							Game::InitActor(&Actor::g_creatureActors[g_secondMinionActorIndex], 0);
							Actor::g_creatureActors[g_firstMinionActorIndex].visibilityDistance = 0xAF0;
							Actor::g_creatureActors[g_firstMinionActorIndex].scaleX = 0;
							Actor::g_creatureActors[g_firstMinionActorIndex].scaleY = 0;
							Actor::g_creatureActors[g_firstMinionActorIndex].scaleZ = 0;
							Actor::g_creatureActors[g_firstMinionActorIndex].scalePivotHeight = -0x8000;
							Actor::g_creatureActors[g_secondMinionActorIndex].visibilityDistance = 0xAF0;
							Actor::g_creatureActors[g_secondMinionActorIndex].scaleX = 0;
							Actor::g_creatureActors[g_secondMinionActorIndex].scaleY = 0;
							Actor::g_creatureActors[g_secondMinionActorIndex].scaleZ = 0;
							Actor::g_creatureActors[g_secondMinionActorIndex].scalePivotHeight = -0x8000;
							AudioManager::PlaySoundEffect(0x86, &Actor::g_creatureActors[g_firstMinionActorIndex].pos);
						}

						if (Camera::g_cutsceneDuration > 0xB9 && Camera::g_cutsceneDuration < 300 && g_framePulseOutputs.fourTick != 0)
						{
							Vector4I particlePosition = { 0, 400, 0x4B0, 0 };
							Actor::ResolveBoneAttachmentPos(&particlePosition, waveActor, 4);
							if (g_framePulseOutputs.eightTick == 0)
								Nu3D::Particles::SpawnFromPreset(particlePosition.x, particlePosition.y, particlePosition.z, 4, 4);
							else
								Nu3D::Particles::SpawnFromPreset(particlePosition.x, particlePosition.y, particlePosition.z, 0x11, 2);
						}

						if (waveActor->actorPhase == 1)
						{
							Vector3I& wavePosition = orbitPositions[g_waveActorIndex - 1].position;
							Actor::Toy2Actor* waveFirstMinion = &Actor::g_creatureActors[g_firstMinionActorIndex];
							Actor::Toy2Actor* waveSecondMinion = &Actor::g_creatureActors[g_secondMinionActorIndex];
							waveFirstMinion->pos.x = wavePosition.x;
							waveFirstMinion->pos.y = wavePosition.y + 0x1000;
							waveFirstMinion->pos.z = wavePosition.z;
							waveFirstMinion->velX = 0;
							waveFirstMinion->gravityVel = 0;
							waveFirstMinion->velForward = 0;
							waveSecondMinion->pos = waveFirstMinion->pos;
							waveSecondMinion->velX = 0;
							waveSecondMinion->gravityVel = 0;
							waveSecondMinion->velForward = 0;
							if (waveFirstMinion->creatureId != 0xE)
								waveFirstMinion->previousActorPhase = 0x1CC;
							if (waveSecondMinion->creatureId != 0xE)
								waveSecondMinion->previousActorPhase = 0x1CC;

							if (Camera::g_cutsceneDuration < 0x3C && waveActor->primaryAnimIdx == 1)
							{
								Nu3D::Particles::ParticleInstance* particle = 0;
								for (int32_t i = 0; i < 4; i++)
								{
									particle = Nu3D::Particles::SpawnFromPreset(wavePosition.x, wavePosition.y, wavePosition.z, 0x23, 0xE);
									particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
								}
								AudioManager::PlaySoundEffect(0x85, &particle->pos);
								AudioManager::Preset::PlayOneShotSound(0xD6, &g_buzzActor);
								Lighting::SpawnLight(wavePosition.x, wavePosition.y, wavePosition.z, 0xF08000, 0x20, 0x52C840);
								Actor::Kill(waveActor, Actor::KILL_EFFECTS);
							}
						}
					}
				}
			}

			bool buzzNearBoss = Nu3D::Math::IsWithinDistance(&g_buzzActor.posAngles.pos, &boss->pos, 200) != 0;
			if (buzzNearBoss && g_encounterState == ENCOUNTER_FINAL_PHASE)
			{
				if (g_orbitHeight < g_buzzActor.posAngles.pos.y)
				{
					g_orbitHeight += Renderer::g_frameDelta * 0x80;
					if (g_orbitHeight >= g_buzzActor.posAngles.pos.y)
						g_orbitHeight = g_buzzActor.posAngles.pos.y;
				}
			}
			else
			{
				if (g_orbitHeight > -0x8000)
				{
					g_orbitHeight -= Renderer::g_frameDelta * 0x80;
					if (g_orbitHeight < -0x7FFF)
						g_orbitHeight = -0x8000;
				}
				if (g_encounterState == ENCOUNTER_DEFEATED)
				{
					Camera::g_cutsceneCameraPosition.y += Renderer::g_frameDelta * 0x80;
					if (Camera::g_cutsceneDuration < 0x78)
					{
						Actor::Kill(boss, Actor::KILL_EFFECTS);
						g_encounterState = ENCOUNTER_REWARD;
					}
				}
				if (g_encounterState == ENCOUNTER_REWARD && Camera::g_cutsceneDuration == 0 && Collectables::g_tokenCollectionState == 0)
					Collectables::g_tokenCollectionState = 1;
			}

			if (g_encounterState > ENCOUNTER_INTRO)
			{
				if (g_laserCooldown > 0)
				{
					g_laserCooldown -= Renderer::g_frameDelta;
					if (g_laserCooldown < 1)
						g_laserCooldown = 0;
				}
			}

			g_damageSoundCooldown -= Renderer::g_frameDelta;
			if (g_damageSoundCooldown < 1)
				g_damageSoundCooldown = 0;

			if (g_laserCooldown == 0 && Nu3D::Math::IsWithinDistance(&g_buzzActor.posAngles.pos, &boss->pos, 400) != 0)
			{
				AttachmentPosition beamPosition;
				beamPosition.vector.x = 0;
				beamPosition.vector.y = 0;
				beamPosition.vector.z = -400;
				Actor::ResolveBoneAttachmentPos(&beamPosition.vector, boss, 0);
				int32_t targetAngle = Nu3D::Math::CartesianToFixedAngle(
					g_buzzActor.posAngles.pos.x - beamPosition.position.x, g_buzzActor.posAngles.pos.z - beamPosition.position.z);
				int32_t angleOffset = (targetAngle - boss->yawAngle) & 0xFFF;
				if (angleOffset > 0x800)
					angleOffset -= 0xFFF;
				if (angleOffset < -0x200)
					angleOffset = -0x200;
				else if (angleOffset > 0x200)
					angleOffset = 0x200;
				uint32_t beamAngle = (angleOffset + boss->yawAngle) & 0xFFF;
				AttachmentPosition beamMovement;
				beamMovement.position.x = Numerics::g_sinCosLUT[beamAngle] << 2;
				beamMovement.position.y = 96000;
				beamMovement.position.z = Numerics::g_sinCosLUT[(beamAngle + 0x400) & 0xFFF] << 2;
				Collision::SweepAndSlide(&beamPosition.position, &beamMovement.position, 0x8000, 0, 0x100);
				Renderer::Beam::QueueBeam(9, 0x80, 400, &beamPosition.vector, &beamMovement.vector, 0, 0x80, 0);
				beamPosition.position.x += beamMovement.position.x;
				beamPosition.position.y += beamMovement.position.y;
				beamPosition.position.z += beamMovement.position.z;
				AudioManager::PlaySoundEffect(0x87, &beamPosition.position);
				for (int32_t i = 0; i < g_framePulseOutputs.fourTick; i++)
					Nu3D::Particles::SpawnFromPreset(beamPosition.position.x, beamPosition.position.y, beamPosition.position.z, 4, 4);
				if (g_framePulseOutputs.eightTick != 0)
					Nu3D::Particles::SpawnFromPreset(beamPosition.position.x, beamPosition.position.y, beamPosition.position.z, 0x46, 2);
				if (g_framePulseOutputs.sixteenTick != 0)
					Lighting::SpawnLight(beamPosition.position.x, beamPosition.position.y, beamPosition.position.z, 0xC000, 0x10, -2);
				Renderer::LensFlare::RegisterLight(beamPosition.position.x, beamPosition.position.y, beamPosition.position.z, 0, 0x80, 0, 0x40);

				if (Nu3D::Math::IsWithinDistance(&g_buzzActor.posAngles.pos, &beamPosition.position, 0x1E) != 0)
				{
					if (g_buzzActor.stunTimer == 0)
					{
						AudioManager::PlaySoundEffect(0x84, &boss->pos);
						if (g_damageSoundCooldown == 0)
							g_damageSoundCooldown = 0x4B0;
						AudioManager::Preset::PlayOneShotSound(0xD7, &g_buzzActor);
					}
					uint32_t damageAngle = Nu3D::Math::CartesianToFixedAngle(
						g_buzzActor.posAngles.pos.x - beamPosition.position.x, g_buzzActor.posAngles.pos.z - beamPosition.position.z);
					Buzz::HandleDamage(damageAngle, 3);
				}
			}

			firstMinion = &Actor::g_creatureActors[g_firstMinionActorIndex];
			secondMinion = &Actor::g_creatureActors[g_secondMinionActorIndex];
			if (firstMinion->scalePivotHeight == -0x8000)
			{
				firstMinion->scaleX += static_cast<int16_t>(Renderer::g_frameDelta) * 0xC;
				if (firstMinion->scaleX > 0xFFF)
				{
					firstMinion->scaleX = 0x1000;
					firstMinion->scalePivotHeight = 0;
				}
				firstMinion->scaleY = firstMinion->scaleX;
				firstMinion->scaleZ = firstMinion->scaleX;
			}
			if (firstMinion != secondMinion && secondMinion->scalePivotHeight == -0x8000)
			{
				secondMinion->scaleX += static_cast<int16_t>(Renderer::g_frameDelta) * 0xC;
				if (secondMinion->scaleX > 0xFFF)
				{
					secondMinion->scaleX = 0x1000;
					secondMinion->scalePivotHeight = 0;
				}
				secondMinion->scaleY = secondMinion->scaleX;
				secondMinion->scaleZ = secondMinion->scaleX;
			}

			if (g_encounterState > ENCOUNTER_INTRO && g_encounterState < ENCOUNTER_DEFEATED)
			{
				g_cameraTargetUpdateTimer -= Renderer::g_frameDelta;
				if (g_cameraTargetUpdateTimer <= 0)
				{
					g_cameraTargetUpdateTimer = 0x1E;
					if (firstMinion->actorPhase > 0 || secondMinion->actorPhase > 0)
					{
						int32_t firstDistance = 0x7FFFFFFF;
						if (firstMinion->actorPhase > 0)
						{
							int32_t x = (Camera::g_renderCameraTransform.pos.x - firstMinion->pos.x) >> 8;
							int32_t z = (Camera::g_renderCameraTransform.pos.z - firstMinion->pos.z) >> 8;
							firstDistance = x * x + z * z;
						}
						int32_t secondX = 0x40;
						int32_t secondZ = 0x40;
						if (secondMinion->actorPhase > 0)
						{
							secondX = (Camera::g_renderCameraTransform.pos.x - secondMinion->pos.x) >> 8;
							secondZ = (Camera::g_renderCameraTransform.pos.z - secondMinion->pos.z) >> 8;
						}
						g_cameraTargetActorIndex = firstDistance < secondX * secondX + secondZ * secondZ ? g_firstMinionActorIndex : g_secondMinionActorIndex;
					}
					else
					{
						g_cameraTargetActorIndex = 0;
					}
				}

				if (g_cameraTargetActorIndex != 0)
				{
					Actor::Toy2Actor* cameraTarget = &Actor::g_creatureActors[g_cameraTargetActorIndex];
					Camera::g_actorCameraTarget.x = cameraTarget->pos.x;
					Camera::g_actorCameraTarget.y = cameraTarget->pos.y - 0x2000;
					Camera::g_actorCameraTarget.z = cameraTarget->pos.z;
				}
				else
				{
					Camera::g_actorCameraTarget.x = boss->pos.x;
					Camera::g_actorCameraTarget.y = boss->pos.y - 0x4000;
					Camera::g_actorCameraTarget.z = boss->pos.z;
				}
			}

			if (g_encounterState == ENCOUNTER_WAVES || g_encounterState == ENCOUNTER_FINAL_PHASE)
				HUD::g_slideTimers[HUD::SLIDE_BOSS_STATUS] = 0x5A;
			int32_t hudFrame = (boss->actorPhase - 10) * 0x36;
			g_hudActorAnimationFrame = (hudFrame + ((hudFrame >> 31) & 0xF)) >> 4;
		}
	}
}
