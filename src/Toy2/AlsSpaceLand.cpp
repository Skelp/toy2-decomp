#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Actor.h"
#include "Toy2/Buzz.h"
#include "Toy2/Collectables.h"
#include "Toy2/Particles.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Link.h"
#include "Renderer/Renderer.h"
#include "Random.h"

#include <string.h>

namespace Toy2
{
	namespace AlsSpaceLand
	{
		struct RotatingLinkState
		{
			int32_t rotationAngle;
			int32_t speedAngle;
		};

		// GLOBAL: TOY2 0x004F2F0C
		int16_t g_tokenLinkIds[] = { 0x31, 0x33, 0x32, 0x35, 0x34, 0 };

		// GLOBAL: TOY2 0x004F2F44
		MoveableObject::InitEntry g_moveableObjectInitTable[] = {
			{ 0, 0, 1 },
			{ 15, 3, 2 },
			{ 19, 4, 3 },
			{ -1, -1, -1 },
		};

		// GLOBAL: TOY2 0x0052FA58
		int32_t g_buggyTintToggle;
		// GLOBAL: TOY2 0x0052FA80
		Collectables::PickupRecord* g_spaceshipTokenPickup;
		// GLOBAL: TOY2 0x0052FA84
		int32_t g_challengeBobAngle;
		// GLOBAL: TOY2 0x0052FA88
		int32_t g_previousBuggyPhase;
		// GLOBAL: TOY2 0x0052FA8C
		int32_t g_buggyPhaseTimer;
		// GLOBAL: TOY2 0x0052FA90
		int32_t g_textureAnimationFrame;
		// GLOBAL: TOY2 0x0052FA98
		Vector3I g_link21Origin;
		// GLOBAL: TOY2 0x0052FAA8
		Vector3I g_link20Origin;
		// GLOBAL: TOY2 0x0052FAB8
		Vector3I g_link23Origin;
		// GLOBAL: TOY2 0x0052FAC8
		Vector3I g_link22Origin;
		// GLOBAL: TOY2 0x0052FAD8
		int32_t g_challengePathProgress;
		// GLOBAL: TOY2 0x0052FADC
		int32_t g_challengePathSpeed;
		// GLOBAL: TOY2 0x0052FAE0
		int32_t g_hammDialogueState;
		// GLOBAL: TOY2 0x0052FAE4
		int32_t g_stompSwitchTimer;
		// GLOBAL: TOY2 0x0052FAE8
		Vector3I g_spaceshipBasePosition;
		// GLOBAL: TOY2 0x0052FAF8
		int32_t g_projectilePathPoint;
		// GLOBAL: TOY2 0x0052FAFC
		int32_t g_laserTravelDurations[2];
		// GLOBAL: TOY2 0x0052FB08
		Vector3I g_spaceshipTokenPosition;
		// GLOBAL: TOY2 0x0052FB18
		int32_t g_spaceshipTokenDetached;
		// GLOBAL: TOY2 0x0052FB1C
		int32_t g_spaceshipTokenVerticalVelocity;
		// GLOBAL: TOY2 0x0052FB20
		RotatingLinkState g_rotatingLinkStates[9];
		// GLOBAL: TOY2 0x0052FB68
		int32_t g_link22BobAngle;
		// GLOBAL: TOY2 0x0052FB6C
		int32_t g_link21BobAngle;
		// GLOBAL: TOY2 0x0052FB70
		int32_t g_link23BobAngle;
		// GLOBAL: TOY2 0x0052FB74
		int32_t g_spaceshipTokenMotionState;
		// GLOBAL: TOY2 0x0052FB78
		int32_t g_link20BobAngle;
		// GLOBAL: TOY2 0x0052FB7C
		int32_t g_buggySoundTimer;
		// GLOBAL: TOY2 0x0052FB80
		int32_t g_ambientSoundTimer;
		// GLOBAL: TOY2 0x0052FB84
		int32_t g_spaceshipSequenceTimer;
		// GLOBAL: TOY2 0x0052FB88
		int32_t g_spaceshipSequenceState;
		// GLOBAL: TOY2 0x0052FB8C
		int32_t g_swingPlatformTriggered;
		// GLOBAL: TOY2 0x0052FB90
		int32_t g_laserCooldowns[2];
		// GLOBAL: TOY2 0x0052FB98
		int32_t g_projectileScanTimer;
		// GLOBAL: TOY2 0x0052FBA8
		int32_t g_spaceshipLiftOffset;
		// GLOBAL: TOY2 0x0052FBAC
		int32_t g_laserPathPoints[2];

		STATIC_ASSERT(sizeof(RotatingLinkState) == 0x8);

		// FUNCTION: TOY2 0x00423020 [PROVISIONAL]
		void Init()
		{
			MoveableObject::InitTable(g_moveableObjectInitTable);
			Collectables::Init(g_tokenLinkIds, 0x41);
			Collectables::Activate(3, 1);

			Collectables::PickupRecord* tokenPickup = reinterpret_cast<Collectables::PickupRecord*>(Collectables::g_tokenStates[3].verticalPosition - 1);
			g_environmentSurfaceY = 0;
			g_previousBuzzEnvironmentY = 0;
			g_environmentEffectType = 0;
			g_spaceshipTokenPickup = tokenPickup;
			tokenPickup->facingAngle &= Collectables::PICKUP_FLAG_PERSISTENT;
			Actor::g_creatureActors[1].actorFlags |= Actor::ACTOR_FLAG_COLLIDABLE;
			memset(g_rotatingLinkStates, 0, sizeof(g_rotatingLinkStates));

			g_challengePathProgress = 0;
			g_challengePathSpeed = 0;
			g_challengeBobAngle = 0;
			g_laserPathPoints[0] = 0;
			g_laserTravelDurations[0] = 0;
			g_laserCooldowns[0] = 0;
			g_laserPathPoints[1] = 0;
			g_laserTravelDurations[1] = 0;
			g_laserCooldowns[1] = 0;

			Nu3D::Link::GetCurrentPosFixed(10, &g_spaceshipBasePosition);
			Nu3D::Link::GetCurrentPosFixed(10, &g_spaceshipTokenPosition);
			Nu3D::Link::GetCurrentPosFixed(20, &g_link20Origin);
			Nu3D::Link::GetCurrentPosFixed(21, &g_link21Origin);
			Nu3D::Link::GetCurrentPosFixed(22, &g_link22Origin);
			Nu3D::Link::GetCurrentPosFixed(23, &g_link23Origin);

			g_spaceshipTokenPosition.y += 0x6000;
			g_spaceshipBasePosition.x -= 0x4000;
			g_spaceshipBasePosition.z -= 0x4000;
			Nu3D::Link::SetScaleFromFixedOffsets(11, 0, 0, 0);
			Nu3D::Link::SetScaleFromFixedOffsets(14, 0, 0, 0);

			g_spaceshipSequenceState = 0;
			g_spaceshipLiftOffset = 0;
			g_spaceshipSequenceTimer = 0;
			g_spaceshipTokenMotionState = 0;
			g_spaceshipTokenVerticalVelocity = 0;
			g_spaceshipTokenDetached = 0;
			g_projectileScanTimer = 0;
			g_stompSwitchTimer = 0;
			int32_t previousBuggyPhase = Actor::g_creatureActors[40].actorPhase;
			g_projectilePathPoint = 0;
			g_textureAnimationFrame = 0;
			g_link20BobAngle = 0;
			g_link21BobAngle = 0;
			g_link22BobAngle = 0;
			g_link23BobAngle = 0;
			g_swingPlatformTriggered = 0;
			g_ambientSoundTimer = 0;
			g_buggySoundTimer = 60;
			g_hammDialogueState = 0;
			g_buggyTintToggle = 0;
			g_buggyPhaseTimer = 0;
			g_previousBuggyPhase = previousBuggyPhase;

			Nu3D::Link::SetPositionRawAndCommit(25, -0x2A6D, -0x4962, 0x25E1);
			HUD::g_challengeState = 0;
			AndysHouse::g_raceCheckpointPassCount = 3;
		}

		// STUB: TOY2 0x00423200
		void Interactions() {}
	}
}

namespace Toy2
{
	namespace CreatureBehaviour
	{
		// FUNCTION: TOY2 0x00422C70 [MATCHED]
		void Martian(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
			if (actor->previousActorPhase <= 0)
			{
				actor->previousActorPhase = (*g_randDatBufferPtr++ & 0x7F) + 0x12C;
				AudioManager::PlaySoundEffect(0x80, &actor->pos);
			}

			if ((actor->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0 && actor->actorPhase == 0x66)
			{
				g_levelObjectiveProgress++;
				Particles::SpawnCollectSparkle(actor->pos.x, actor->pos.y - 0x2000, actor->pos.z, 0x32);
				AudioManager::PlaySoundEffect(0xB8, &actor->pos);
				Actor::Kill(actor, 2);
			}
		}
	}
}
