#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Actor.h"
#include "Toy2/Particles.h"
#include "Toy2/Weather.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
#include "Random.h"
#include "Numerics.h"

namespace Toy2
{
	extern int32_t g_hudActorAnimationFrame;

	namespace AlleysAndGullies
	{
		enum ClownChallengeState
		{
			CLOWN_CHALLENGE_IDLE = 0,
			CLOWN_CHALLENGE_INTRO = 1,
			CLOWN_CHALLENGE_ACTIVE = 2,
			CLOWN_CHALLENGE_COMPLETE = 200,
		};

		// GLOBAL: TOY2 0x0052F8B0
		int32_t g_clownPhaseTimer;
		// GLOBAL: TOY2 0x0052F8B8
		int32_t g_previousClownPhase;
		// GLOBAL: TOY2 0x0052F8E8
		int32_t g_clownChallengeState;
		// GLOBAL: TOY2 0x0052F8FC
		int32_t g_clownTintFlashToggle;

		// STUB: TOY2 0x0041E390
		void Init() {}

		// STUB: TOY2 0x0041E880
		void Interactions() {}
	}
}

namespace Toy2
{
	namespace CreatureBehaviour
	{
		// FUNCTION: TOY2 0x0041DDB0 [MATCHED]
		void Clown(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			AlleysAndGullies::g_clownTintFlashToggle = (AlleysAndGullies::g_clownTintFlashToggle - 1) & 1;

			if (actor->actorPhase != AlleysAndGullies::g_previousClownPhase)
			{
				AlleysAndGullies::g_previousClownPhase = actor->actorPhase;
				AlleysAndGullies::g_clownPhaseTimer = 60;
				actor->creatureRam->defenseMode = 4;
			}

			if ((actor->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0)
			{
				actor->actorFlags &= ~Actor::ACTOR_FLAG_INTERACTION_REQUESTED;
				AudioManager::PlaySoundEffect(0xA1, &actor->pos);
			}

			if (AlleysAndGullies::g_clownChallengeState > AlleysAndGullies::CLOWN_CHALLENGE_INTRO)
			{
				AlleysAndGullies::g_clownPhaseTimer -= Renderer::g_frameDelta;
				if (AlleysAndGullies::g_clownPhaseTimer < 0)
				{
					AlleysAndGullies::g_clownPhaseTimer = 0;
					actor->creatureRam->defenseMode = 7;
					actor->useTint = 0;
				}
				else if (AlleysAndGullies::g_clownTintFlashToggle != 0)
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

			if (AlleysAndGullies::g_clownChallengeState == AlleysAndGullies::CLOWN_CHALLENGE_ACTIVE)
			{
				g_hudActorAnimationFrame = actor->actorPhase * 54 / 20;
				if (Sector::g_activeSectorIndex == 2)
				{
					HUD::g_slideTimers[HUD::SLIDE_BOSS_STATUS] = 90;
				}
			}
		}

		// FUNCTION: TOY2 0x0041DEC0 [MATCHED]
		void Ducks(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
			if (actor->actorPhase == 0x66 && actor->previousActorPhase <= 0)
			{
				actor->previousActorPhase = (*g_randDatBufferPtr++ & 0x7F) + 0x3C;
				AudioManager::PlaySoundEffect(0xA2, &actor->pos);
			}

			if ((actor->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0 && actor->actorPhase == 0x66)
			{
				g_levelObjectiveProgress++;
				Particles::SpawnCollectSparkle(actor->pos.x, actor->pos.y - 0x2000, actor->pos.z, 0x32);
				AudioManager::PlaySoundEffect(0xA2, &actor->pos);
				Actor::Kill(actor, 2);
			}
		}

		// FUNCTION: TOY2 0x0041DF70 [EFFECTIVE]
		void ZBoat(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
			if (actor->previousActorPhase < 0)
			{
				actor->previousActorPhase = 200;
				Vector4I particlePosition;
				particlePosition.x = 0;
				particlePosition.y = -500;
				particlePosition.z = -300;
				Actor::ResolveBoneAttachmentPos(&particlePosition, actor, 0);

				int32_t sine = Numerics::g_sinCosLUT[actor->yawAngle] >> 2;
				int32_t cosine = Numerics::g_sinCosLUT[(actor->yawAngle + 0x400) & 0xFFF] >> 2;
				Nu3D::Particles::SpawnInstance(particlePosition.x, particlePosition.y, particlePosition.z, sine, -0xC00, cosine, 0x80, 0, 0, 0x5C);
			}
		}
	}
}
