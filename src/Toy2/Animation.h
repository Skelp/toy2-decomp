#pragma once

#include "Common.h"
#include "Numerics.h"

namespace Toy2
{
	namespace Actor
	{
		struct Toy2Actor;
	}

	namespace Animation
	{
		// Per-actor, per-node euler rotation offsets applied during animation.
		// Indexed as g_nodeAngles[actorIndex][nodeIndex]; up to 64 actors with
		// 32 nodes each (64 * 32 * sizeof(Vector3F) == 0x6000).
		extern Vector3F g_nodeAngles[64][32];

		// Pointer to the NULL-terminated list of actors currently being animated
		// (set by AnimateActors, read by Actor::FindInActorList). NULL when idle.
		extern Actor::Toy2Actor** g_actorAnimList;

		void ResetNodeAngles();
	}
}
