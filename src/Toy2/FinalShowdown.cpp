#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Nullsub.h"
#include "Toy2/Actor.h"
#include "Toy2/Buzz.h"
#include "Toy2/Collectables.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
#include "Random.h"
#include "Numerics.h"

namespace Toy2
{
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

		extern int32_t g_defeatedBossIndex;
		extern int32_t g_gunslingerState;
		extern int32_t g_smithState;
		extern int32_t g_smithPhaseTimer;
		extern int32_t g_gunslingerPhaseTimer;
		extern int32_t g_previousSmithPhase;
		extern int32_t g_previousGunslingerPhase;
		extern int32_t g_smithTintToggle;
		extern int32_t g_gunslingerTintToggle;
		extern int32_t g_smithEffectTimer;
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

		// STUB: TOY2 0x0042F7B0
		void ProsP(Actor::Toy2Actor::ActorBehaviourContext* context) {}
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

		// STUB: TOY2 0x0042FC50
		void Interactions() {}
	}
}
