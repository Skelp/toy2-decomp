#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Actor.h"
#include "Toy2/Collectables.h"

namespace Toy2
{
	namespace SlimeTime
	{
		// GLOBAL: TOY2 0x0052F98C
		int32_t g_previousPhase;
		// GLOBAL: TOY2 0x0052F990
		int32_t g_phaseCutsceneTimer;
		// GLOBAL: TOY2 0x0052F994
		int32_t g_bobAngle;
		// GLOBAL: TOY2 0x0052F998
		int32_t g_attackSoundTimer;
		// GLOBAL: TOY2 0x0052F99C
		int32_t g_actorOutsideArena;
		// GLOBAL: TOY2 0x0052F9A0
		int32_t g_movementCycleTimer;
		// GLOBAL: TOY2 0x0052F9A4
		int32_t g_encounterState;
		// GLOBAL: TOY2 0x0052F9A8
		int32_t g_tintFlashToggle;
		// GLOBAL: TOY2 0x0052F9AC
		int32_t g_unusedStateLimit;
		// GLOBAL: TOY2 0x0052F9B0
		int32_t g_nearbySoundCooldown;
		// GLOBAL: TOY2 0x0052F9B4
		int32_t g_previousCameraYaw;
		// GLOBAL: TOY2 0x0052F9B8
		int32_t g_horizontalMovementDirection;

		// FUNCTION: TOY2 0x0041FFB0 [MATCHED]
		void Init()
		{
			MoveableObject::InitTable(0);
			Collectables::Init(0, 0);

			Actor::Toy2Actor* boss = &Actor::g_creatureActors[0];
			g_previousPhase = boss->actorPhase;
			boss->actorFlags |= Actor::ACTOR_FLAG_BOSS;
			boss->pos.x += 0x80000;
			g_encounterState = 0;
			g_movementCycleTimer = 300;
			g_horizontalMovementDirection = 0x40000;
			g_actorOutsideArena = 0;
			g_attackSoundTimer = 150;
			g_bobAngle = 0;
			g_unusedStateLimit = 0x7FFFFFFF;
			g_previousCameraYaw = 0;
			g_nearbySoundCooldown = 0;
			g_tintFlashToggle = 0;
			g_phaseCutsceneTimer = 0;

			boss->yawAngle = 0xC00;
			boss->creatureRam->boundHalfX = 0x1000;
			boss->creatureRam->boundHalfZ = 0x1000;
		}

		// STUB: TOY2 0x00420060
		void Interactions() {}
	}
}
