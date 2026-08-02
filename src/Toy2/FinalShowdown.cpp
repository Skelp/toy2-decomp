#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Nullsub.h"
#include "Toy2/Actor.h"
#include "Toy2/Buzz.h"
#include "Toy2/Camera.h"
#include "Toy2/Collectables.h"
#include "Toy2/Levels.h"
#include "Toy2/Lighting.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Link.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
#include "SaveManager.h"
#include "Random.h"
#include "Numerics.h"

#include <limits.h>

namespace Toy2
{
	extern int32_t g_hudActorAnimationFrame;

	namespace FinalShowdown
	{
		enum GunslingerState
		{
			GUNSLINGER_STATE_ACTIVE = 2,
			GUNSLINGER_STATE_DEFEATED = 3,
		};

		enum SmithState
		{
			SMITH_STATE_ACTIVE = 2,
			SMITH_STATE_DEFEATED = 3,
		};

		enum ProspectorState
		{
			PROSPECTOR_STATE_ACTIVE = 2,
			PROSPECTOR_STATE_DEFEATED = 3,
		};

		extern int32_t g_defeatedBossIndex;
		extern int32_t g_gunslingerState;
		extern int32_t g_prospectorState;
		extern int32_t g_smithState;
		extern int32_t g_prospectorAttackTimer;
		extern int32_t g_smithPhaseTimer;
		extern int32_t g_prospectorPhaseTimer;
		extern int32_t g_gunslingerPhaseTimer;
		extern int32_t g_previousSmithPhase;
		extern int32_t g_previousProspectorPhase;
		extern int32_t g_previousGunslingerPhase;
		extern int32_t g_smithTintToggle;
		extern int32_t g_prospectorTintToggle;
		extern int32_t g_gunslingerTintToggle;
		extern int32_t g_smithEffectTimer;
		extern int32_t g_prospectorEffectTimer;
		extern int32_t g_defeatedBossCount;
	}

	namespace CreatureBehaviour
	{
		// FUNCTION: TOY2 0x0042F310 [PROVISIONAL]
		void Smith(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			FinalShowdown::g_smithTintToggle = (FinalShowdown::g_smithTintToggle - 1) & 1;
			Actor::Toy2Actor* actor = context->actor;

			if (actor->actorPhase != FinalShowdown::g_previousSmithPhase)
			{
				FinalShowdown::g_previousSmithPhase = actor->actorPhase;
				FinalShowdown::g_smithPhaseTimer = 60;
				actor->creatureRam->defenseMode = 4;
			}

			if (FinalShowdown::g_smithState == FinalShowdown::SMITH_STATE_ACTIVE)
			{
				FinalShowdown::g_smithPhaseTimer -= Renderer::g_frameDelta;
				if (FinalShowdown::g_smithPhaseTimer < 0)
				{
					FinalShowdown::g_smithPhaseTimer = 0;
					actor->creatureRam->defenseMode = 6;
				}
				else if (FinalShowdown::g_smithTintToggle != 0)
				{
					actor->useTint = 1;
					actor->actorTint.r = 0x2000;
					actor->actorTint.g = 0x2000;
					actor->actorTint.b = 0x2000;
				}
				else
				{
					actor->useTint = 0;
				}
			}
			else
			{
				actor->useTint = 0;
			}

			if ((context->targetFlags & 1) != 0 && Nu3D::Math::IsWithinDistance(&g_buzzActor.posAngles.pos, &actor->pos, 300) != 0
				&& actor->primaryAnimIdx == 1)
			{
				Actor::SetAnimation(actor, 3, 0x18);
				actor->creatureRam->speedTarget = 0;
				FinalShowdown::g_smithEffectTimer = 0x3F;
			}

			if (actor->primaryAnimIdx == 3 && (actor->animationFramePosition & (int32_t)0xFFFF0000) > 0x2E0000)
			{
				actor->movementCommandTimer = 0;
				actor->movementData = g_smithMovementData + 16;
				actor->creatureRam->speedTarget = 0x10;
				actor->actorFlags &= ~(Actor::ACTOR_FLAG_TRACKS_TARGET | Actor::ACTOR_FLAG_TARGETS_BUZZ);
			}

			if (FinalShowdown::g_smithEffectTimer != 0)
			{
				FinalShowdown::g_smithEffectTimer -= Renderer::g_frameDelta;
				if (FinalShowdown::g_smithEffectTimer <= 0)
				{
					Vector4I effectPosition;
					effectPosition.x = 0xB4;
					effectPosition.y = -0x96;
					effectPosition.z = -0x32;
					Actor::ResolveBoneAttachmentPos(&effectPosition, actor, 4);
					Nu3D::Particles::ParticleInstance* particle =
						Nu3D::Particles::SpawnInstance(effectPosition.x, effectPosition.y, effectPosition.z, 0, -2, 0, actor->yawAngle << 2, 0, 0, 0x66);
					particle->discPitchAngle = -1;
					FinalShowdown::g_smithEffectTimer = 0;
					AudioManager::PlaySoundEffect(0xA7, &actor->pos);
				}
			}

			if (actor->actorPhase < 10 && FinalShowdown::g_smithState == FinalShowdown::SMITH_STATE_ACTIVE)
			{
				actor->movementData = g_smithMovementData + 45;
				actor->creatureRam->defenseMode = 4;
				actor->actorFlags &= ~(Actor::ACTOR_FLAG_TARGETS_BUZZ | Actor::ACTOR_FLAG_DAMAGES_BUZZ);
				actor->movementCommandTimer = 0;
				AudioManager::PlaySoundEffect(-2, &actor->pos);
				FinalShowdown::g_defeatedBossCount++;
				FinalShowdown::g_smithState = FinalShowdown::SMITH_STATE_DEFEATED;
				if (FinalShowdown::g_defeatedBossCount == 3)
					FinalShowdown::g_defeatedBossIndex = 0;
				FinalShowdown::g_smithEffectTimer = 0;
				actor->creatureRam->speedTarget = 0x10;
			}
		}

		// FUNCTION: TOY2 0x0042F530 [PROVISIONAL]
		void GunsL(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			FinalShowdown::g_gunslingerTintToggle = (FinalShowdown::g_gunslingerTintToggle - 1) & 1;

			if (actor->actorPhase != FinalShowdown::g_previousGunslingerPhase)
			{
				FinalShowdown::g_previousGunslingerPhase = actor->actorPhase;
				FinalShowdown::g_gunslingerPhaseTimer = 60;
				actor->creatureRam->defenseMode = 4;
				AudioManager::PlaySoundEffect(0xC1, &actor->pos);
			}

			if (FinalShowdown::g_gunslingerState == FinalShowdown::GUNSLINGER_STATE_ACTIVE)
			{
				FinalShowdown::g_gunslingerPhaseTimer -= Renderer::g_frameDelta;
				if (FinalShowdown::g_gunslingerPhaseTimer < 0)
				{
					FinalShowdown::g_gunslingerPhaseTimer = 0;
					actor->creatureRam->defenseMode = 7;
				}
				else if (FinalShowdown::g_gunslingerTintToggle != 0)
				{
					actor->useTint = 1;
					actor->actorTint.r = 0x2000;
					actor->actorTint.g = 0x2000;
					actor->actorTint.b = 0x2000;
				}
				else
				{
					actor->useTint = 0;
				}
			}
			else
			{
				actor->useTint = 0;
			}

			if (actor->previousActorPhase != 0)
			{
				int32_t fireProjectile = 0;
				Vector4I projectilePosition;
				if (actor->previousActorPhase > 20)
				{
					projectilePosition.x = 0;
					projectilePosition.y = 0;
					projectilePosition.z = 0;
					Actor::ResolveBoneAttachmentPos(&projectilePosition, actor, 15);
					fireProjectile = 1;
					actor->previousActorPhase = 10;
				}

				actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
				if (actor->previousActorPhase <= 0)
				{
					projectilePosition.x = 0;
					projectilePosition.y = 0;
					projectilePosition.z = 0;
					Actor::ResolveBoneAttachmentPos(&projectilePosition, actor, 16);
					actor->previousActorPhase = 0;
					fireProjectile = 1;
				}

				if (fireProjectile != 0)
				{
					AudioManager::PlaySoundEffect(0x56, &actor->pos);
					int32_t projectileAngle =
						Nu3D::Math::CartesianToFixedAngle(
							g_buzzActor.posAngles.pos.x - projectilePosition.x, g_buzzActor.posAngles.pos.z - projectilePosition.z)
						& 0xFFF;
					if (((projectileAngle - actor->yawAngle + 0x100) & 0xFFF) > 0x200)
						projectileAngle = actor->yawAngle;

					Nu3D::Particles::SpawnInstance(projectilePosition.x,
						projectilePosition.y,
						projectilePosition.z,
						Numerics::g_sinCosLUT[projectileAngle] >> 2,
						0x200,
						Numerics::g_sinCosLUT[(projectileAngle + 0x400) & 0xFFF] >> 2,
						0,
						0,
						0,
						0x61);

					if ((actor->actorFlags & Actor::ACTOR_FLAG_TARGETABLE) != 0)
					{
						for (int32_t particleCount = 5; particleCount != 0; particleCount--)
						{
							Nu3D::Particles::ParticleInstance* particle =
								Nu3D::Particles::SpawnFromPreset(projectilePosition.x, projectilePosition.y, projectilePosition.z, 0x64, 0xF);
							particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
						}
					}
				}
			}

			if (actor->actorPhase < 10 && FinalShowdown::g_gunslingerState == FinalShowdown::GUNSLINGER_STATE_ACTIVE)
			{
				actor->movementData = g_gunslingerMovementData + 52;
				actor->creatureRam->defenseMode = 4;
				actor->actorFlags &= ~Actor::ACTOR_FLAG_DAMAGES_BUZZ;
				AudioManager::PlaySoundEffect(-2, &actor->pos);
				FinalShowdown::g_defeatedBossCount++;
				FinalShowdown::g_gunslingerState = FinalShowdown::GUNSLINGER_STATE_DEFEATED;
				if (FinalShowdown::g_defeatedBossCount == 3)
					FinalShowdown::g_defeatedBossIndex = 1;
				actor->movementCommandTimer = 0;
			}
		}

		// FUNCTION: TOY2 0x0042F7B0 [PROVISIONAL]
		void ProsP(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			FinalShowdown::g_prospectorTintToggle = (FinalShowdown::g_prospectorTintToggle - 1) & 1;

			if (actor->actorPhase != FinalShowdown::g_previousProspectorPhase)
			{
				FinalShowdown::g_previousProspectorPhase = actor->actorPhase;
				FinalShowdown::g_prospectorPhaseTimer = 60;
				actor->creatureRam->defenseMode = 4;

				int32_t soundIndex = *g_randDatBufferPtr++ & 3;
				if (soundIndex == 3)
					soundIndex = 0;
				AudioManager::Preset::PlayOneShotSound2(soundIndex + 0xC4, actor);
			}

			if (FinalShowdown::g_prospectorState == FinalShowdown::PROSPECTOR_STATE_ACTIVE)
			{
				FinalShowdown::g_prospectorAttackTimer -= Renderer::g_frameDelta;
				if (FinalShowdown::g_prospectorAttackTimer < 0)
				{
					FinalShowdown::g_prospectorAttackTimer = *g_randDatBufferPtr++ * 2 + 400;
					int32_t soundIndex = *g_randDatBufferPtr++ % 6;
					if (soundIndex == 0)
						soundIndex = -4;
					AudioManager::Preset::PlayOneShotSound(soundIndex + 199, actor);
				}

				FinalShowdown::g_prospectorPhaseTimer -= Renderer::g_frameDelta;
				if (FinalShowdown::g_prospectorPhaseTimer < 0)
				{
					FinalShowdown::g_prospectorPhaseTimer = 0;
					actor->creatureRam->defenseMode = 6;
				}
				else if (FinalShowdown::g_prospectorTintToggle != 0)
				{
					actor->useTint = 1;
					actor->actorTint.r = 0x2000;
					actor->actorTint.g = 0x2000;
					actor->actorTint.b = 0x2000;
				}
				else
				{
					actor->useTint = 0;
				}
			}
			else
			{
				actor->useTint = 0;
			}

			if ((context->targetFlags & 1) != 0 && Nu3D::Math::IsWithinDistance(&g_buzzActor.posAngles.pos, &actor->pos, 300) != 0
				&& actor->primaryAnimIdx == 4)
			{
				actor->reservedArea[1] = 0xD0;
				actor->reservedArea[2] = 0xD0;
				Actor::SetAnimation(actor, 3, 9);
				actor->creatureRam->speedTarget = 0;
				FinalShowdown::g_prospectorEffectTimer = 0x2C;
				if (AudioManager::IsActorSoundPlaying(actor) == 0)
					AudioManager::Preset::PlayOneShotSound2(0xC2, actor);
			}

			if (actor->primaryAnimIdx == 3 && (actor->animationFramePosition & (int32_t)0xFFFF0000) > 0x150000)
			{
				actor->movementCommandTimer = 0;
				actor->movementData = g_prospectorMovementData + 16;
				actor->creatureRam->speedTarget = 0x10;
				actor->actorFlags &= ~(Actor::ACTOR_FLAG_TRACKS_TARGET | Actor::ACTOR_FLAG_TARGETS_BUZZ);
			}

			if (FinalShowdown::g_prospectorEffectTimer != 0)
			{
				FinalShowdown::g_prospectorEffectTimer -= Renderer::g_frameDelta;
				if (FinalShowdown::g_prospectorEffectTimer <= 0)
				{
					int32_t yawAngle = actor->yawAngle;
					Nu3D::Particles::SpawnInstance(actor->pos.x + (Numerics::g_sinCosLUT[(yawAngle + 0x400) & 0xFFF] >> 3),
						actor->pos.y - 0x800,
						actor->pos.z + (Numerics::g_sinCosLUT[(yawAngle - 0x800) & 0xFFF] >> 3),
						Numerics::g_sinCosLUT[yawAngle] >> 2,
						0,
						Numerics::g_sinCosLUT[(yawAngle + 0x400) & 0xFFF] >> 2,
						0,
						(0x7FF - yawAngle) & 0xFFF,
						0,
						0x68);
					FinalShowdown::g_prospectorEffectTimer = 0;
					AudioManager::PlaySoundEffect(0xA6, &actor->pos);
				}
			}

			if (actor->actorPhase < 10 && FinalShowdown::g_prospectorState == FinalShowdown::PROSPECTOR_STATE_ACTIVE)
			{
				actor->movementData = g_prospectorMovementData + 45;
				actor->creatureRam->defenseMode = 4;
				actor->actorFlags &= ~(Actor::ACTOR_FLAG_TARGETS_BUZZ | Actor::ACTOR_FLAG_DAMAGES_BUZZ);
				actor->movementCommandTimer = 0;
				AudioManager::PlaySoundEffect(-2, &actor->pos);
				FinalShowdown::g_defeatedBossCount++;
				FinalShowdown::g_prospectorState = FinalShowdown::PROSPECTOR_STATE_DEFEATED;
				if (FinalShowdown::g_defeatedBossCount == 3)
					FinalShowdown::g_defeatedBossIndex = 2;
				FinalShowdown::g_prospectorEffectTimer = 0;
				actor->creatureRam->speedTarget = 0x10;
			}
		}
	}
}

namespace Toy2
{
	namespace FinalShowdown
	{
		// GLOBAL: TOY2 0x0052FFF0
		int32_t g_floorCollapseStep;
		// GLOBAL: TOY2 0x0052FFF4
		int32_t g_defeatedBossIndex;
		// GLOBAL: TOY2 0x0052FFF8
		int32_t g_introSequenceTimer;
		// GLOBAL: TOY2 0x0052FFFC
		int32_t g_prospectorAttackTimer;
		// GLOBAL: TOY2 0x00530000
		int32_t g_soundSweepDelay;
		// GLOBAL: TOY2 0x00530004
		int32_t g_gunslingerState;
		// GLOBAL: TOY2 0x00530008
		int32_t g_smithState;
		// GLOBAL: TOY2 0x0053000C
		int32_t g_prospectorState;
		// GLOBAL: TOY2 0x00530010
		int32_t g_unusedState;
		// GLOBAL: TOY2 0x00530014
		int32_t g_previousIntroSequenceTimer;
		// GLOBAL: TOY2 0x00530018
		int32_t g_smithPhaseTimer;
		// GLOBAL: TOY2 0x0053001C
		int32_t g_prospectorPhaseTimer;
		// GLOBAL: TOY2 0x00530020
		int32_t g_gunslingerPhaseTimer;
		// GLOBAL: TOY2 0x00530024
		int32_t g_floorRockAngle;
		// GLOBAL: TOY2 0x00530028
		int32_t g_previousSmithPhase;
		// GLOBAL: TOY2 0x0053002C
		int32_t g_previousProspectorPhase;
		// GLOBAL: TOY2 0x00530030
		int32_t g_previousGunslingerPhase;
		// GLOBAL: TOY2 0x00530038
		Vector3I g_introCutsceneFocus;
		// GLOBAL: TOY2 0x00530048
		int32_t g_floorRestHeight;
		// GLOBAL: TOY2 0x0053004C
		int32_t g_prospectorTintToggle;
		// GLOBAL: TOY2 0x00530050
		int32_t g_gunslingerTintToggle;
		// GLOBAL: TOY2 0x00530054
		int32_t g_smithTintToggle;
		// GLOBAL: TOY2 0x00530058
		int32_t g_floorVerticalVelocity;
		// GLOBAL: TOY2 0x0053005C
		int32_t g_prospectorEffectTimer;
		// GLOBAL: TOY2 0x00530060
		int32_t g_smithEffectTimer;
		// GLOBAL: TOY2 0x00530064
		int32_t g_defeatedBossCount;
		// GLOBAL: TOY2 0x00530068
		int32_t g_soundSweepAngle;

		// FUNCTION: TOY2 0x0042FAA0 [MATCHED]
		void Init()
		{
			MoveableObject::InitTable(0);
			Collectables::Init(0, 0);
			Nullsub7(0x12, 0x11);

			g_prospectorAttackTimer = 200;
			g_smithState = 0;
			g_smithPhaseTimer = 0;
			g_smithTintToggle = 0;
			g_smithEffectTimer = 0;
			g_previousSmithPhase = Actor::g_creatureActors[0].actorPhase;
			g_prospectorState = 0;
			g_prospectorPhaseTimer = 0;
			g_prospectorTintToggle = 0;
			g_prospectorEffectTimer = 0;
			g_previousProspectorPhase = Actor::g_creatureActors[2].actorPhase;
			g_gunslingerState = 0;
			g_gunslingerPhaseTimer = 0;
			g_gunslingerTintToggle = 0;
			g_previousGunslingerPhase = Actor::g_creatureActors[1].actorPhase;
			g_unusedState = 0;
			g_defeatedBossCount = 0;
			g_defeatedBossIndex = 0;
			g_soundSweepAngle = 0x400;
			g_soundSweepDelay = 0xF0;
			g_introSequenceTimer = 0;
			g_previousIntroSequenceTimer = 0;
			g_floorVerticalVelocity = 0;
			g_floorCollapseStep = 0;
			g_floorRockAngle = 0;

			Nu3D::Link::GetCurrentPosFixed(0, &g_introCutsceneFocus);
			g_floorRestHeight = g_introCutsceneFocus.y;

			Actor::g_creatureActors[0].targetYaw = 0x400;
			Actor::g_creatureActors[0].yawAngle = 0x400;
			Actor::g_creatureActors[1].targetYaw = 0x400;
			Actor::g_creatureActors[1].yawAngle = 0x400;
			Actor::g_creatureActors[2].targetYaw = 0x400;
			Actor::g_creatureActors[2].yawAngle = 0x400;
			Actor::g_creatureActors[0].actorPhase = 0;
			Actor::g_creatureActors[1].actorPhase = 0;
			Actor::g_creatureActors[2].actorPhase = 0;

			Actor::g_creatureActors[0].pos.x = -0x32C6D;
			Actor::g_creatureActors[0].pos.y = -0x92D6;
			Actor::g_creatureActors[0].pos.z = -0x14F5;
			Actor::g_creatureActors[1].pos.x = -0x327CC;
			Actor::g_creatureActors[1].pos.y = -0x92DA;
			Actor::g_creatureActors[1].pos.z = -0x45CD;
			Actor::g_creatureActors[2].pos.x = -0x32BBF;
			Actor::g_creatureActors[2].pos.y = -0x92DB;
			Actor::g_creatureActors[2].pos.z = -0x8251;

			Actor::g_creatureActors[0].respawnDelay = 10000;
			Actor::g_creatureActors[1].respawnDelay = 10000;
			Actor::g_creatureActors[2].respawnDelay = 10000;
			Actor::g_creatureActors[3].respawnDelay = 10000;
			Actor::g_creatureActors[4].respawnDelay = 10000;
		}

		// FUNCTION: TOY2 0x0042FC50 [PROVISIONAL]
		void Interactions()
		{
			if (g_introSequenceTimer != 0)
			{
				g_previousIntroSequenceTimer = g_introSequenceTimer;
				if (g_introSequenceTimer < 1000)
					g_introSequenceTimer += Renderer::g_frameDelta;

				if (g_introSequenceTimer > 150 && g_previousIntroSequenceTimer <= 150)
					AudioManager::Preset::PlayOneShotSound(0xCD, &Actor::g_creatureActors[3]);

				if (g_introSequenceTimer > 280 && g_previousIntroSequenceTimer <= 280)
				{
					g_floorVerticalVelocity = -0xC00;
					g_floorRockAngle = 0x800;
					g_floorCollapseStep = 0;
					Actor::g_creatureActors[3].gravityVel = -0x300;
					Actor::g_creatureActors[4].gravityVel = -0x400;
					AudioManager::PlaySoundEffect(0x2F, 0);
				}
				if (g_introSequenceTimer > 335 && g_previousIntroSequenceTimer <= 335)
				{
					g_floorVerticalVelocity = -0xC00;
					g_floorRockAngle = 0x800;
					g_floorCollapseStep = 1;
					Actor::g_creatureActors[3].gravityVel = -0x400;
					Actor::g_creatureActors[4].gravityVel = -0x300;
					AudioManager::PlaySoundEffect(0x2F, 0);
				}
				if (g_introSequenceTimer > 360 && g_previousIntroSequenceTimer <= 360)
				{
					g_floorVerticalVelocity = -0xC00;
					g_floorRockAngle = 0x800;
					g_floorCollapseStep = 2;
					Actor::g_creatureActors[3].gravityVel = -0x700;
					Actor::g_creatureActors[3].velX = -0x400;
					Actor::g_creatureActors[3].motionTargetPos.y = 0;
					Actor::g_creatureActors[4].gravityVel = -0x780;
					Actor::g_creatureActors[4].velX = -0x400;
					Actor::g_creatureActors[4].motionTargetPos.y = 0;
					AudioManager::PlaySoundEffect(0x2F, 0);
				}
				if (g_introSequenceTimer > 430 && g_previousIntroSequenceTimer <= 430)
				{
					g_floorVerticalVelocity = -0xC00;
					g_floorRockAngle = 0x800;
					g_floorCollapseStep = 3;
					Actor::g_creatureActors[0].gravityVel = -0x700;
					Actor::g_creatureActors[1].gravityVel = -0x600;
					Actor::g_creatureActors[2].gravityVel = -0x800;
					Actor::g_creatureActors[0].actorPhase = 0x1D;
					Actor::g_creatureActors[1].actorPhase = 0x1D;
					Actor::g_creatureActors[2].actorPhase = 0x1D;
					Actor::g_creatureActors[3].actorPhase = 0;
					Actor::g_creatureActors[4].actorPhase = 0;
					AudioManager::PlaySoundEffect(10, 0);
				}

				g_floorVerticalVelocity += 0x200;
				if (g_floorVerticalVelocity > 0xC00)
					g_floorVerticalVelocity = 0xC00;

				Vector3I floorPosition;
				Nu3D::Link::GetCurrentPosFixed(0, &floorPosition);
				floorPosition.y += g_floorVerticalVelocity;
				if (floorPosition.y > g_floorRestHeight)
					floorPosition.y = g_floorRestHeight;
				Nu3D::Link::SetPositionRawAndCommit(0, floorPosition.x >> 5, floorPosition.y >> 5, floorPosition.z >> 5);
				Nu3D::Link::SetPositionRawAndCommit(1, floorPosition.x >> 7, floorPosition.y >> 7, floorPosition.z >> 7);
				Nu3D::Link::SetPositionRawAndCommit(3, floorPosition.x >> 5, floorPosition.y >> 5, floorPosition.z >> 5);
				Nu3D::Link::SetPositionRawAndCommit(4, floorPosition.x >> 7, floorPosition.y >> 7, floorPosition.z >> 7);

				if (g_floorRockAngle != 0)
				{
					g_floorRockAngle -= 0x100;
					if (g_floorRockAngle < 0)
						g_floorRockAngle = 0;
					switch (g_floorCollapseStep)
					{
					case 0:
						Nu3D::Link::SetRotationAbsolute8bit(0, Numerics::g_sinCosLUT[g_floorRockAngle & 0xFFF] >> 7, 0, 0);
						Nu3D::Link::SetRotationAbsolute8bit(1, Numerics::g_sinCosLUT[g_floorRockAngle & 0xFFF] >> 7, 0, 0);
						Nu3D::Link::SetRotationAbsolute8bit(2, Numerics::g_sinCosLUT[g_floorRockAngle & 0xFFF] >> 7, 0, 0);
						Nu3D::Link::SetRotationAbsolute8bit(3, Numerics::g_sinCosLUT[g_floorRockAngle & 0xFFF] >> 7, 0, 0);
						break;
					case 1:
						Nu3D::Link::SetRotationAbsolute8bit(0, 0, 0, -Numerics::g_sinCosLUT[g_floorRockAngle & 0xFFF] >> 8);
						Nu3D::Link::SetRotationAbsolute8bit(1, 0, 0, -Numerics::g_sinCosLUT[g_floorRockAngle & 0xFFF] >> 8);
						Nu3D::Link::SetRotationAbsolute8bit(2, 0, 0, -Numerics::g_sinCosLUT[g_floorRockAngle & 0xFFF] >> 8);
						Nu3D::Link::SetRotationAbsolute8bit(3, 0, 0, -Numerics::g_sinCosLUT[g_floorRockAngle & 0xFFF] >> 8);
						break;
					case 2:
						Nu3D::Link::SetRotationAbsolute8bit(0, -Numerics::g_sinCosLUT[g_floorRockAngle & 0xFFF] >> 7, 0, 0);
						Nu3D::Link::SetRotationAbsolute8bit(1, -Numerics::g_sinCosLUT[g_floorRockAngle & 0xFFF] >> 7, 0, 0);
						Nu3D::Link::SetRotationAbsolute8bit(2, -Numerics::g_sinCosLUT[g_floorRockAngle & 0xFFF] >> 7, 0, 0);
						Nu3D::Link::SetRotationAbsolute8bit(3, -Numerics::g_sinCosLUT[g_floorRockAngle & 0xFFF] >> 7, 0, 0);
						break;
					case 3:
						Nu3D::Link::SetRotationAbsolute8bit(
							0, 0, 0, -Numerics::g_sinCosLUT[((g_floorRockAngle >> 1) + 0x400) & 0xFFF] >> 4);
						Nu3D::Link::SetRotationAbsolute8bit(
							1, 0, 0, -Numerics::g_sinCosLUT[((g_floorRockAngle >> 1) + 0x400) & 0xFFF] >> 4);
						break;
					}
				}
			}

			if (g_buzzActor.posAngles.pos.x < -0x16BD9 && g_introSequenceTimer == 0)
			{
				g_introSequenceTimer = 0x50;
				Camera::BeginScriptedCutsceneAtPoint(&g_introCutsceneFocus, 0x1E0, 0x38);
				Camera::g_cutsceneCameraPosition.y -= 0x4000;
				Camera::g_cutsceneFocusPosition.y -= 0x2000;
				Camera::InitCutsceneCamera(&Camera::g_cutsceneFocusPosition, &Camera::g_cutsceneCameraPosition);
			}
			if (Camera::g_cutsceneDuration > 0xF0 && Camera::g_cutsceneDuration < 0x168 && g_defeatedBossCount < 3)
				Camera::g_cutsceneCameraPosition.x += Renderer::g_frameDelta * 0x180;

			if (g_introSequenceTimer != 0 && Camera::g_cutsceneDuration <= 0x1D && g_smithState == 0)
			{
				AudioManager::PlayMusicLooping(g_levelIndex);
				g_smithState = SMITH_STATE_ACTIVE;
				g_prospectorState = PROSPECTOR_STATE_ACTIVE;
				g_gunslingerState = GUNSLINGER_STATE_ACTIVE;
				Actor::g_creatureActors[0].movementData = CreatureBehaviour::g_smithMovementData + 14;
				Actor::g_creatureActors[0].movementCommandTimer = 0;
				Actor::g_creatureActors[0].creatureRam->initialFacingAngle = 0;
				Actor::g_creatureActors[1].movementData = CreatureBehaviour::g_gunslingerMovementData + 14;
				Actor::g_creatureActors[1].movementCommandTimer = 0;
				Actor::g_creatureActors[1].creatureRam->initialFacingAngle = 0;
				Actor::g_creatureActors[2].movementData = CreatureBehaviour::g_prospectorMovementData + 14;
				Actor::g_creatureActors[2].movementCommandTimer = 0;
				Actor::g_creatureActors[2].creatureRam->initialFacingAngle = 0;
				Actor::g_creatureActors[0].gravityVel = -0x600;
				Actor::g_creatureActors[1].gravityVel = -0x800;
				Actor::g_creatureActors[2].gravityVel = -0x700;
				Actor::g_creatureActors[0].velX = 0x400;
				Actor::g_creatureActors[1].velX = 0x400;
				Actor::g_creatureActors[2].velX = 0x400;
				AudioManager::PlaySoundEffect(0xE, 0);
				AudioManager::Preset::PlayOneShotSound(199, &Actor::g_creatureActors[2]);
			}

			if (g_smithState > 0)
			{
				Actor::Toy2Actor* first = &Actor::g_creatureActors[0];
				Actor::Toy2Actor* second = &Actor::g_creatureActors[1];
				int32_t distanceZ = (first->pos.z - second->pos.z) >> 5;
				int32_t distanceY = (first->pos.y - second->pos.y) >> 5;
				int32_t distanceX = (first->pos.x - second->pos.x) >> 5;
				if (distanceX * distanceX + distanceY * distanceY + distanceZ * distanceZ < 0x40000)
				{
					uint32_t angle = Nu3D::Math::CartesianToFixedAngle(distanceX, distanceZ) & 0xFFF;
					second->pos.x = first->pos.x - (((int32_t)Numerics::g_sinCosLUT[angle] << 9) >> 9);
					second->pos.z = first->pos.z - (((int32_t)Numerics::g_sinCosLUT[(angle + 0x400) & 0xFFF] << 9) >> 9);
				}

				first = &Actor::g_creatureActors[1];
				second = &Actor::g_creatureActors[2];
				distanceX = (first->pos.x - second->pos.x) >> 5;
				distanceY = (first->pos.y - second->pos.y) >> 5;
				distanceZ = (first->pos.z - second->pos.z) >> 5;
				if (distanceX * distanceX + distanceY * distanceY + distanceZ * distanceZ < 0x40000)
				{
					uint32_t angle = Nu3D::Math::CartesianToFixedAngle(distanceX, distanceZ) & 0xFFF;
					second->pos.x = first->pos.x - (((int32_t)Numerics::g_sinCosLUT[angle] << 9) >> 9);
					second->pos.z = first->pos.z - (((int32_t)Numerics::g_sinCosLUT[(angle + 0x400) & 0xFFF] << 9) >> 9);
				}

				first = &Actor::g_creatureActors[0];
				second = &Actor::g_creatureActors[2];
				distanceY = (first->pos.y - second->pos.y) >> 5;
				distanceZ = (first->pos.z - second->pos.z) >> 5;
				distanceX = (first->pos.x - second->pos.x) >> 5;
				if (distanceX * distanceX + distanceY * distanceY + distanceZ * distanceZ < 0x40000)
				{
					uint32_t angle = Nu3D::Math::CartesianToFixedAngle(distanceX, distanceZ) & 0xFFF;
					second->pos.x = first->pos.x - (((int32_t)Numerics::g_sinCosLUT[angle] << 9) >> 9);
					second->pos.z = first->pos.z - (((int32_t)Numerics::g_sinCosLUT[(angle + 0x400) & 0xFFF] << 9) >> 9);
				}
			}

			int32_t cameraPitchAngle = 0x400;
			if (g_smithState > 0)
			{
				g_soundSweepDelay -= Renderer::g_frameDelta;
				if (g_soundSweepDelay <= 0)
				{
					g_soundSweepDelay = 0;
					g_soundSweepAngle += Renderer::g_frameDelta * 8;
					if (g_soundSweepAngle < 0x3000)
					{
						cameraPitchAngle = g_soundSweepAngle;
						if (g_soundSweepAngle >= 0x800)
						{
							if (g_soundSweepAngle < 0x1800)
								cameraPitchAngle = 0x800;
							else if (g_soundSweepAngle < 0x2000)
								cameraPitchAngle = g_soundSweepAngle - 0x1000;
							else
								cameraPitchAngle = 0;
						}
					}
					else
					{
						g_soundSweepAngle = 0;
						cameraPitchAngle = 0;
					}
				}
			}

			int32_t soundWave = Numerics::g_sinCosLUT[(cameraPitchAngle + 0x400) & 0xFFF] >> 6;
			int32_t cameraPitch = soundWave & 0xFFF;
			AudioManager::g_dynamicSoundFrequencies[0] = (0x180 - soundWave) * 8;
			AudioManager::PlaySoundEffect(0xA8, 0);
			if (Camera::g_scriptedCameraState != 0)
				cameraPitch = 0;
			Camera::g_gameplayCamera.angles.pitch = cameraPitch;
			if (g_smithState != 0)
			{
				HUD::g_slideTimers[HUD::SLIDE_BOSS_STATUS] = 90;
				g_hudActorAnimationFrame =
					(Actor::g_creatureActors[2].actorPhase + Actor::g_creatureActors[1].actorPhase - 27 + Actor::g_creatureActors[0].actorPhase) * 54 / 60;
			}
			if (g_defeatedBossCount == 3)
			{
				g_defeatedBossCount = 4;
				Camera::BeginScriptedCutsceneAtPoint(&Actor::g_creatureActors[g_defeatedBossIndex].pos, 120, 0x20);
				Camera::g_cutsceneFocusPosition.y -= 0x2000;
				Camera::g_cutsceneCameraPosition.y -= 0x8000;
			}
			if (g_defeatedBossCount == 4 && Camera::g_cutsceneDuration == 0)
			{
				Actor::g_creatureActors[3].yawAngle = 0x400;
				Actor::g_creatureActors[3].targetYaw = 0x400;
				Actor::g_creatureActors[4].yawAngle = 0x400;
				SaveManager::g_save0Data.tokens[g_levelFileIndex] = 0x80;
				Actor::g_creatureActors[4].targetYaw = 0x400;
				g_defeatedBossCount = 5;
				Actor::g_creatureActors[3].pos.x = -0x4E4E0;
				Actor::g_creatureActors[3].pos.y = -0xBC80;
				Actor::g_creatureActors[3].pos.z = 0x14E0;
				Actor::g_creatureActors[4].pos.x = -0x46944;
				Actor::g_creatureActors[4].pos.y = -0xB680;
				Actor::g_creatureActors[4].pos.z = -0x1BEA;
				Actor::g_creatureActors[3].actorPhase = 1;
				Actor::g_creatureActors[4].actorPhase = 1;
				Actor::g_creatureActors[3].motionTargetPos.y = -0xBC80;
				Actor::g_creatureActors[4].motionTargetPos.y = -0xB680;
				Camera::BeginScriptedCutsceneAtPoint(&Actor::g_creatureActors[4].pos, 300, 0x40);
				Camera::g_cutsceneCameraPosition.x = Camera::g_cutsceneFocusPosition.x + 0x3000;
				Camera::g_cutsceneCameraPosition.y = Camera::g_cutsceneFocusPosition.y - 0x3000;
				Camera::g_cutsceneCameraPosition.z = Camera::g_cutsceneFocusPosition.z - 0x1000;
				Camera::g_cutsceneFocusPosition.y -= 0x2000;
				Camera::InitCutsceneCamera(&Camera::g_cutsceneFocusPosition, &Camera::g_cutsceneCameraPosition);
				g_levelTransition = 1;
				g_levelTransitionTimer = 0xF0;
			}
			if (g_defeatedBossCount == 5)
				Camera::g_cutsceneCameraPosition.z += Renderer::g_frameDelta * 0x20;

			int32_t nearestDistanceSquared = INT_MAX;
			int32_t nearestFlareIndex = 0;
			for (int32_t flareIndex = 0; flareIndex < Levels::g_recordData[0]->recordCount; flareIndex++)
			{
				Vector3I* flare = &Levels::g_recordData[0]->data[flareIndex];
				int32_t cameraOffsetZ = (Camera::g_renderCameraTransform.pos.z - flare->z * 0x20) >> 8;
				int32_t cameraOffsetY = (Camera::g_renderCameraTransform.pos.y - flare->y * 0x20) >> 8;
				int32_t cameraOffsetX = (Camera::g_renderCameraTransform.pos.x - flare->x * 0x20) >> 8;
				if (cameraOffsetZ * cameraOffsetZ + cameraOffsetY * cameraOffsetY + cameraOffsetX * cameraOffsetX < 0x100000)
				{
					int32_t green = flareIndex == 2 ? 0 : 0x80;
					int32_t blue = flareIndex == 2 ? 0 : 0x80;
					Renderer::LensFlare::RegisterLight(flare->x * 0x20, flare->y * 0x20, flare->z * 0x20, 0x80, green, blue, 0x80);
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

			if (Actor::g_creatureActors[0].pos.x < -0x2BDD6 && Actor::g_creatureActors[0].velX < 0)
				Actor::g_creatureActors[0].pos.x = -0x2BDD6;
			if (Actor::g_creatureActors[1].pos.x < -0x2BDD6 && Actor::g_creatureActors[1].velX < 0)
				Actor::g_creatureActors[1].pos.x = -0x2BDD6;
			if (Actor::g_creatureActors[2].pos.x < -0x2BDD6 && Actor::g_creatureActors[2].velX < 0)
				Actor::g_creatureActors[2].pos.x = -0x2BDD6;

			Lighting::DynamicLight* light = &Lighting::g_lightingState.dynamicLights[1];
			if (nearestDistanceSquared > 0xFFFF)
			{
				light->lifetime = 0;
				return;
			}
			Vector3I* nearestFlare = &Levels::g_recordData[0]->data[nearestFlareIndex];
			light->position.x = nearestFlare->x << 5;
			light->position.y = nearestFlare->y << 5;
			light->position.z = nearestFlare->z << 5;
			light->colour.r = 0xFF;
			if (nearestFlareIndex == 2)
			{
				light->colour.g = 0;
				light->colour.b = 0;
			}
			else
			{
				light->colour.g = 0xFF;
				light->colour.b = 0xFF;
			}
			light->lifetime = 1;
			light->sourceId = reinterpret_cast<int32_t>(nearestFlare);
		}
	}
}
