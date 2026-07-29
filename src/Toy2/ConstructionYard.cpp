#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Actor.h"
#include "Toy2/Particles.h"
#include "AudioManager/AudioManager.h"
#include "Renderer/Renderer.h"
#include "Random.h"

namespace Toy2
{
	namespace ConstructionYard
	{
		// STUB: TOY2 0x0041C190
		void Init() {}

		// STUB: TOY2 0x0041C640
		void Interactions() {}
	}
}

namespace Toy2
{
	namespace CreatureBehaviour
	{
		// FUNCTION: TOY2 0x0041BB80 [MATCHED]
		void LTyke(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
			if (actor->previousActorPhase <= 0)
			{
				actor->previousActorPhase = (*g_randDatBufferPtr++ & 0x7F) + 0x5A;
				AudioManager::PlaySoundEffect(0x6B, &actor->pos);
			}

			if ((actor->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0 && actor->actorPhase == 0x66)
			{
				g_levelObjectiveProgress++;
				Particles::SpawnCollectSparkle(actor->pos.x, actor->pos.y - 0x2000, actor->pos.z, 0x32);
				AudioManager::PlaySoundEffect(0x6C, &actor->pos);
				Actor::Kill(actor, 2);
			}
		}
	}
}
