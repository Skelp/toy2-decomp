#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Actor.h"
#include "Toy2/Particles.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
#include "Random.h"
#include "Numerics.h"

namespace Toy2
{
	namespace AlleysAndGullies
	{
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
