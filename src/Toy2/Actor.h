#pragma once

#include "Common.h"
#include "RawLoader.h"

#include <stddef.h>

namespace Toy2
{
	namespace Actor
	{
		enum ActorFlags
		{
			ACTOR_FLAG_TARGETABLE = 0x1,
			ACTOR_FLAG_ACTIVE = 0x2,
			ACTOR_FLAG_INTERACTION_REQUESTED = 0x200,
		};

		struct Toy2Actor
		{
			struct ActorBehaviourContext
			{
				Toy2Actor* actor;
				int16_t updateFlags;
				int16_t targetFlags;
				int16_t localStrafeSpeed;
				int16_t localForwardSpeed;
			};

			typedef void (*ActorBehaviourFn)(ActorBehaviourContext* ctx);

			Vector3I pos;
			int16_t pitchAngle;
			int16_t yawAngle;
			int16_t rollAngle;
			int16_t primaryAnimIdx;
			int16_t creatureId;
			int16_t secondaryAnimIdx;
			int32_t animationFramePosition;
			int32_t unkVar8;
			int32_t unkVar9;
			RGB16 actorTint;
			int16_t unkVar10;
			int16_t unkVar12;
			int16_t unkVar12_;
			int16_t unkVar13;
			int16_t unkVar13_;
			int32_t useTint;
			Vector3I16 boundingOffset;
			int16_t boundingSphereRadius;
			int16_t actorFlags;
			int16_t visibilityDistance;
			int32_t velX;
			int32_t gravityVel;
			int32_t velForward;
			Vector3I boundary;
			Vector3I motionTargetPos;
			int32_t targetYaw;
			int32_t areaIndex;
			int16_t hitpoints;
			int16_t unkVar29_;
			uint8_t* animationFrameSequence;
			int16_t unkShort1;
			int16_t unkShort2;
			int16_t damageCooldownTimer;
			int16_t actorPhase;
			uint16_t* movementData;
			int32_t unkVar34;
			int16_t unkShort3;
			int16_t unkShort4;
			int16_t unkWord15;
			int16_t unkWord16;
			int32_t lastValidYPosition;
			ActorBehaviourFn actorBehaviour;
			RawLoader::CreatureListRam* creatureRam;
		};

		extern Toy2Actor* g_activeActors[65];

		extern Toy2Actor g_creatureActors[64];
		extern uint8_t* g_animationFrameSequences[26];
		extern Toy2Actor* g_renderActors[66];

		// Actor-system state reset by InitCreatureRam. Roles are not yet confirmed;
		// the consuming functions (Game::UpdateActors et al.) are unreconstructed.
		extern int32_t g_unk50A54C;
		extern int32_t g_unk52ADD8[0x80];
		extern int32_t g_unk52EF48;
		extern int32_t g_unk52EF88;

		void InitCreatureRam();
		void StepCreatureAnimFrame(Toy2Actor* actor);
		void SetAnimation(Toy2Actor* actor, int16_t animationIndex, int32_t frameSequenceIndex);
		int32_t IsInsideBounds(const Vector3I* position, int32_t minX, int32_t maxX, int32_t minZ, int32_t maxZ);
		void PopulateActiveActors();
		void HitType1Particles(int32_t x, int32_t y, int32_t z);
		void CollectQuestReward(int32_t actorIndex, int32_t dialogueRecordIndex, int32_t actorFacingAngle, int32_t cameraFacingAngle, int32_t tokenIndex);
		void PlayPeriodicHintSound(int32_t actorIndex, int32_t soundPresetIndex);
		void GetCreatureList(uint8_t* creatureIdList);
		int32_t FindInActorList(Toy2Actor* actor);
		void SetNodeAngle(Toy2Actor* actor, int32_t nodeIndex, float x, float y, float z);

		STATIC_ASSERT(sizeof(Toy2Actor) == 0x9C);
		STATIC_ASSERT(offsetof(Toy2Actor, animationFramePosition) == 0x18);
		STATIC_ASSERT(offsetof(Toy2Actor, animationFrameSequence) == 0x74);
		STATIC_ASSERT(sizeof(Toy2Actor::ActorBehaviourContext) == 0xC);
	}
}
