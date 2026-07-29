#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Actor.h"
#include "Toy2/Collectables.h"

namespace Toy2
{
	namespace BarnEncounter
	{
		// GLOBAL: TOY2 0x0052FBB8
		int32_t g_previousBossPhase;
		// GLOBAL: TOY2 0x0052FBBC
		int32_t g_cameraTargetActorIndex;
		// GLOBAL: TOY2 0x0052FBC0
		int32_t g_phaseTimer;
		// GLOBAL: TOY2 0x0052FBC4
		int32_t g_secondMinionActorIndex;
		// GLOBAL: TOY2 0x0052FBC8
		int32_t g_firstMinionActorIndex;
		// GLOBAL: TOY2 0x0052FBCC
		int32_t g_orbitHeight;
		// GLOBAL: TOY2 0x0052FBD0
		int32_t g_cameraTargetUpdateTimer;
		// GLOBAL: TOY2 0x0052FBD4
		int32_t g_waveActorIndex;
		// GLOBAL: TOY2 0x0052FBE8
		int32_t g_tintFlashToggle;
		// GLOBAL: TOY2 0x0052FBEC
		int32_t g_orbitPosition;
		// GLOBAL: TOY2 0x0052FBF0
		int32_t g_waitingForMinions;
		// GLOBAL: TOY2 0x0052FBF4
		int32_t g_nextMinionPairIndex;
		// GLOBAL: TOY2 0x0052FBF8
		int32_t g_introSoundTimer;
		// GLOBAL: TOY2 0x0052FBFC
		int32_t g_damageSoundCooldown;
		// GLOBAL: TOY2 0x0052FC00
		int32_t g_laserCooldown;
		// GLOBAL: TOY2 0x0052FC04
		int32_t g_encounterState;
		// GLOBAL: TOY2 0x0052FC08
		int32_t g_heightBobAngle;
		// GLOBAL: TOY2 0x0052FC0C
		int32_t g_orbitAngle;

		// FUNCTION: TOY2 0x00424390 [MATCHED]
		void Init()
		{
			MoveableObject::InitTable(0);
			Collectables::Init(0, 0);

			g_previousBossPhase = Actor::g_creatureActors[0].actorPhase;
			g_orbitAngle = 0;
			g_orbitPosition = 0;
			g_heightBobAngle = 0;
			g_orbitHeight = -0x58000;
			g_encounterState = 0;
			g_phaseTimer = 0;
			g_tintFlashToggle = 0;
			g_waveActorIndex = 1;
			g_waitingForMinions = 0;
			g_laserCooldown = 200;
			g_firstMinionActorIndex = 7;
			g_secondMinionActorIndex = 7;
			g_nextMinionPairIndex = 0;
			g_cameraTargetActorIndex = 0;
			g_cameraTargetUpdateTimer = 0;
			g_introSoundTimer = 120;
			g_damageSoundCooldown = 0;
			Actor::g_creatureActors[0].creatureRam->speedTarget = 0;

			for (int32_t actorIndex = 7; actorIndex < 12; actorIndex++)
			{
				Actor::g_creatureActors[actorIndex].respawnDelay = 10000;
				Actor::g_creatureActors[actorIndex].actorPhase = 0;
				Actor::g_creatureActors[actorIndex].scalePivotHeight = 0;
				Actor::g_creatureActors[actorIndex].creatureRam->boundHalfX = Actor::g_creatureActors[6].creatureRam->boundHalfX;
				Actor::g_creatureActors[actorIndex].creatureRam->boundHalfZ = Actor::g_creatureActors[6].creatureRam->boundHalfZ;
			}

			for (int32_t bossActorIndex = 0; bossActorIndex < 12; bossActorIndex++)
			{
				Actor::g_creatureActors[bossActorIndex].actorFlags |= Actor::ACTOR_FLAG_BOSS;
			}
		}

		// STUB: TOY2 0x00424490
		void Interactions() {}
	}
}
