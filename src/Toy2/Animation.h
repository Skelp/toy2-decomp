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

		// Pointers into the currently parsed animation data blob (set by ParseHeader,
		// read by SampleNodeTransform). g_nodeKeyframeOffsets is a per-node short
		// table (nodeId -> keyframe base index, -1/-2/-3 = no data), g_nodeScaleFlags
		// is a per-node bitmask (bit set = node has scale data), g_keyframeData is
		// the base of the keyframe samples.
		extern uint8_t* g_nodeKeyframeOffsets;
		extern uint8_t* g_nodeScaleFlags;
		extern uint8_t* g_keyframeData;

		void ResetNodeAngles();
		void ParseHeader(int16_t* header);
	}
}
