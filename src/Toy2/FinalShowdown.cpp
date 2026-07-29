#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Nullsub.h"
#include "Toy2/Actor.h"
#include "Toy2/Collectables.h"
#include "Nu3D/Link.h"

namespace Toy2
{
	namespace FinalShowdown
	{
		// GLOBAL: TOY2 0x0052FFF0
		int32_t g_floorCollapseStep;
		// GLOBAL: TOY2 0x0052FFF4
		int32_t g_defeatedBossIndex;
		// GLOBAL: TOY2 0x0052FFF8
		int32_t g_introSequenceTimer;
		// GLOBAL: TOY2 0x0052FFFC
		int32_t g_prospectorAttackTimer;
		// GLOBAL: TOY2 0x00530000
		int32_t g_soundSweepDelay;
		// GLOBAL: TOY2 0x00530004
		int32_t g_gunslingerState;
		// GLOBAL: TOY2 0x00530008
		int32_t g_smithState;
		// GLOBAL: TOY2 0x0053000C
		int32_t g_prospectorState;
		// GLOBAL: TOY2 0x00530010
		int32_t g_unusedState;
		// GLOBAL: TOY2 0x00530014
		int32_t g_previousIntroSequenceTimer;
		// GLOBAL: TOY2 0x00530018
		int32_t g_smithPhaseTimer;
		// GLOBAL: TOY2 0x0053001C
		int32_t g_prospectorPhaseTimer;
		// GLOBAL: TOY2 0x00530020
		int32_t g_gunslingerPhaseTimer;
		// GLOBAL: TOY2 0x00530024
		int32_t g_floorRockAngle;
		// GLOBAL: TOY2 0x00530028
		int32_t g_previousSmithPhase;
		// GLOBAL: TOY2 0x0053002C
		int32_t g_previousProspectorPhase;
		// GLOBAL: TOY2 0x00530030
		int32_t g_previousGunslingerPhase;
		// GLOBAL: TOY2 0x00530038
		Vector3I g_introCutsceneFocus;
		// GLOBAL: TOY2 0x00530048
		int32_t g_floorRestHeight;
		// GLOBAL: TOY2 0x0053004C
		int32_t g_prospectorTintToggle;
		// GLOBAL: TOY2 0x00530050
		int32_t g_gunslingerTintToggle;
		// GLOBAL: TOY2 0x00530054
		int32_t g_smithTintToggle;
		// GLOBAL: TOY2 0x00530058
		int32_t g_floorVerticalVelocity;
		// GLOBAL: TOY2 0x0053005C
		int32_t g_prospectorEffectTimer;
		// GLOBAL: TOY2 0x00530060
		int32_t g_smithEffectTimer;
		// GLOBAL: TOY2 0x00530064
		int32_t g_defeatedBossCount;
		// GLOBAL: TOY2 0x00530068
		int32_t g_soundSweepAngle;

		// FUNCTION: TOY2 0x0042FAA0 [MATCHED]
		void Init()
		{
			MoveableObject::InitTable(0);
			Collectables::Init(0, 0);
			Nullsub7(0x12, 0x11);

			g_prospectorAttackTimer = 200;
			g_smithState = 0;
			g_smithPhaseTimer = 0;
			g_smithTintToggle = 0;
			g_smithEffectTimer = 0;
			g_previousSmithPhase = Actor::g_creatureActors[0].actorPhase;
			g_prospectorState = 0;
			g_prospectorPhaseTimer = 0;
			g_prospectorTintToggle = 0;
			g_prospectorEffectTimer = 0;
			g_previousProspectorPhase = Actor::g_creatureActors[2].actorPhase;
			g_gunslingerState = 0;
			g_gunslingerPhaseTimer = 0;
			g_gunslingerTintToggle = 0;
			g_previousGunslingerPhase = Actor::g_creatureActors[1].actorPhase;
			g_unusedState = 0;
			g_defeatedBossCount = 0;
			g_defeatedBossIndex = 0;
			g_soundSweepAngle = 0x400;
			g_soundSweepDelay = 0xF0;
			g_introSequenceTimer = 0;
			g_previousIntroSequenceTimer = 0;
			g_floorVerticalVelocity = 0;
			g_floorCollapseStep = 0;
			g_floorRockAngle = 0;

			Nu3D::Link::GetCurrentPosFixed(0, &g_introCutsceneFocus);
			g_floorRestHeight = g_introCutsceneFocus.y;

			Actor::g_creatureActors[0].targetYaw = 0x400;
			Actor::g_creatureActors[0].yawAngle = 0x400;
			Actor::g_creatureActors[1].targetYaw = 0x400;
			Actor::g_creatureActors[1].yawAngle = 0x400;
			Actor::g_creatureActors[2].targetYaw = 0x400;
			Actor::g_creatureActors[2].yawAngle = 0x400;
			Actor::g_creatureActors[0].actorPhase = 0;
			Actor::g_creatureActors[1].actorPhase = 0;
			Actor::g_creatureActors[2].actorPhase = 0;

			Actor::g_creatureActors[0].pos.x = -0x32C6D;
			Actor::g_creatureActors[0].pos.y = -0x92D6;
			Actor::g_creatureActors[0].pos.z = -0x14F5;
			Actor::g_creatureActors[1].pos.x = -0x327CC;
			Actor::g_creatureActors[1].pos.y = -0x92DA;
			Actor::g_creatureActors[1].pos.z = -0x45CD;
			Actor::g_creatureActors[2].pos.x = -0x32BBF;
			Actor::g_creatureActors[2].pos.y = -0x92DB;
			Actor::g_creatureActors[2].pos.z = -0x8251;

			Actor::g_creatureActors[0].respawnDelay = 10000;
			Actor::g_creatureActors[1].respawnDelay = 10000;
			Actor::g_creatureActors[2].respawnDelay = 10000;
			Actor::g_creatureActors[3].respawnDelay = 10000;
			Actor::g_creatureActors[4].respawnDelay = 10000;
		}

		// STUB: TOY2 0x0042FC50
		void Interactions() {}
	}
}
