#pragma once

#include "Common.h"
#include "RawLoader.h"

#include <stddef.h>

namespace Toy2
{
	namespace Dialogue
	{
		void Begin(int32_t actorIndex, int32_t recordType, char* subtitle, int32_t buzzFacingAngle, int32_t actorFacingAngle, int32_t rewardTokenIndex);
	}

	namespace Actor
	{
		struct ActorCollisionVolume
		{
			Vector3I16 offset;
			int16_t reserved;
			Vector3I16 scale;
			int16_t radius;
		};

		enum ActorFlags
		{
			ACTOR_FLAG_TARGETABLE = 0x1,
			ACTOR_FLAG_ACTIVE = 0x2,
			ACTOR_FLAG_TRACKS_TARGET = 0x4,
			ACTOR_FLAG_TARGETS_BUZZ = 0x8,
			ACTOR_FLAG_COLLIDABLE = 0x80,
			ACTOR_FLAG_DAMAGES_BUZZ = 0x100,
			ACTOR_FLAG_INTERACTION_REQUESTED = 0x200,
			ACTOR_FLAG_BOSS = 0x800,
			ACTOR_FLAG_RC_CAR = 0x1000,
			ACTOR_FLAG_CULLED = 0x2000,
		};

		enum KillFlags
		{
			KILL_EFFECTS = 0x1,
			KILL_REMOVE_ACTOR = 0x2,
		};

		enum DamageType
		{
			DAMAGE_NONE = 0,
			DAMAGE_SPIN = 1,
			DAMAGE_GROUND_SLAM = 5,
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
			int32_t secondaryAnimationFramePosition;
			int32_t movementCommandValue;
			RGB16 actorTint;
			int16_t actorAlpha;
			int16_t scaleX;
			int16_t scaleY;
			int16_t scaleZ;
			int16_t scalePivotHeight;
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
			int8_t areaIndex;
			uint8_t reservedArea[3];
			int16_t hitpoints;
			int16_t unkVar29_;
			uint8_t* animationFrameSequence;
			int16_t movementCommandTimer;
			int16_t respawnDelay;
			int16_t damageCooldownTimer;
			int16_t actorPhase;
			uint16_t* movementData;
			ActorCollisionVolume* collisionVolumes;
			int16_t unkShort3;
			int16_t previousActorPhase;
			int16_t unkWord15;
			int16_t unkWord16;
			int32_t lastValidYPosition;
			ActorBehaviourFn actorBehaviour;
			RawLoader::CreatureListRam* creatureRam;
		};

		extern Toy2Actor* g_activeActors[65];

		extern Toy2Actor g_creatureActors[64];
		extern uint8_t* g_animationFrameSequences[26];
		extern uint16_t* g_movementDataByControl[2];
		extern Toy2Actor* g_renderActors[66];

		extern Toy2Actor* g_lastKilledActor;

		// Actor-system state reset by InitCreatureRam. Roles are not yet confirmed;
		// the consuming functions (Game::UpdateActors et al.) are unreconstructed.
		extern int32_t g_unk52ADD8[0x80];
		extern int32_t g_unk52EF48;
		extern int32_t g_unk52EF88;

		void InitCreatureRam();
		void UpdatePrimaryAnimation(Toy2Actor* actor);
		void StepCreatureAnimFrame(Toy2Actor* actor);
		void SetAnimation(Toy2Actor* actor, int16_t animationIndex, int32_t frameSequenceIndex);
		void Kill(Toy2Actor* actor, uint8_t killFlags);
		int32_t IsInsideBounds(const Vector3I* position, int32_t minX, int32_t maxX, int32_t minZ, int32_t maxZ);
		void PopulateActiveActors();
		void HitType1Particles(int32_t x, int32_t y, int32_t z);
		void CollectQuestReward(int32_t actorIndex, int32_t dialogueRecordIndex, int32_t actorFacingAngle, int32_t cameraFacingAngle, int32_t tokenIndex);
		void PlayPeriodicHintSound(int32_t actorIndex, int32_t soundPresetIndex);
		void GetCreatureList(uint8_t* creatureIdList);
		int32_t FindInActorList(Toy2Actor* actor);
		void SetNodeAngle(Toy2Actor* actor, int32_t nodeIndex, int32_t pitch, int32_t yaw, int32_t roll);
		void ResolveBoneAttachmentPos(Vector4I* position, Toy2Actor* actor, int32_t boneIndex);
		void HandleDamage(Toy2Actor* actor, int32_t attackAngle, int32_t damageType);

		STATIC_ASSERT(sizeof(Toy2Actor) == 0x9C);
		STATIC_ASSERT(sizeof(ActorCollisionVolume) == 0x10);
		STATIC_ASSERT(offsetof(Toy2Actor, animationFramePosition) == 0x18);
		STATIC_ASSERT(offsetof(Toy2Actor, secondaryAnimationFramePosition) == 0x1C);
		STATIC_ASSERT(offsetof(Toy2Actor, actorAlpha) == 0x2A);
		STATIC_ASSERT(offsetof(Toy2Actor, scaleX) == 0x2C);
		STATIC_ASSERT(offsetof(Toy2Actor, scalePivotHeight) == 0x32);
		STATIC_ASSERT(offsetof(Toy2Actor, areaIndex) == 0x6C);
		STATIC_ASSERT(offsetof(Toy2Actor, animationFrameSequence) == 0x74);
		STATIC_ASSERT(offsetof(Toy2Actor, movementCommandTimer) == 0x78);
		STATIC_ASSERT(offsetof(Toy2Actor, respawnDelay) == 0x7A);
		STATIC_ASSERT(offsetof(Toy2Actor, actorPhase) == 0x7E);
		STATIC_ASSERT(offsetof(Toy2Actor, collisionVolumes) == 0x84);
		STATIC_ASSERT(offsetof(Toy2Actor, previousActorPhase) == 0x8A);
		STATIC_ASSERT(sizeof(Toy2Actor::ActorBehaviourContext) == 0xC);
	}

	namespace CreatureBehaviour
	{
		extern uint16_t* g_gunslingerMovementData;
		extern uint16_t* g_prospectorMovementData;
		extern uint16_t* g_smithMovementData;
		extern int32_t g_rcCarRearWheelRotation;
		extern int32_t g_rcCarFrontWheelRotation;
		void SetRCCarNodeAngle(Actor::Toy2Actor* actor, int32_t nodeIndex, int32_t pitch, int32_t yaw, int32_t roll);
	}
}
