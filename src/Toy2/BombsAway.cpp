#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Actor.h"
#include "Toy2/Camera.h"
#include "Toy2/Collectables.h"
#include "Toy2/Levels.h"
#include "Toy2/Weather.h"
#include "Nu3D/Link.h"

#include <limits.h>

namespace Toy2
{
	namespace BombsAway
	{
		// GLOBAL: TOY2 0x0052F6F0
		int32_t g_encounterProgress;
		// GLOBAL: TOY2 0x0052F6F4
		int32_t g_bombPositionY;
		// GLOBAL: TOY2 0x0052F6F8
		int32_t g_bombRotationAngle;
		// GLOBAL: TOY2 0x0052F6FC
		int32_t g_bombVerticalVelocity;
		// GLOBAL: TOY2 0x0052F700
		int32_t g_thunderSoundTimer;
		// GLOBAL: TOY2 0x0052F704
		int32_t g_bossScale;
		// GLOBAL: TOY2 0x0052F708
		int32_t g_lightningFlashTimer;
		// GLOBAL: TOY2 0x0052F70C
		int32_t g_previousCutsceneDuration;
		// GLOBAL: TOY2 0x0052F710
		int32_t g_damageTintTimer;
		// GLOBAL: TOY2 0x0052F714
		int32_t g_unusedState;
		// GLOBAL: TOY2 0x0052F718
		int32_t g_introSoundTimer;
		// GLOBAL: TOY2 0x0052F71C
		int32_t g_attackSoundTimer;
		// GLOBAL: TOY2 0x0052F720
		int32_t g_hudWrapY;
		// GLOBAL: TOY2 0x0052F724
		int32_t g_hudWrapX;
		// GLOBAL: TOY2 0x0052F72C
		int32_t g_savedPickupVerticalPosition;
		// GLOBAL: TOY2 0x0052F730
		int32_t g_previousBossPhase;
		// GLOBAL: TOY2 0x0052F748
		int32_t g_tintAngleRed;
		// GLOBAL: TOY2 0x0052F74C
		int32_t g_tintAngleBlue;
		// GLOBAL: TOY2 0x0052F750
		int32_t g_tintAngleGreen;
		// GLOBAL: TOY2 0x0052F754
		int32_t g_encounterState;
		// GLOBAL: TOY2 0x0052F758
		int32_t g_phaseCutsceneDuration;
		// GLOBAL: TOY2 0x0052F75C
		int32_t g_targetBossScale;

		// FUNCTION: TOY2 0x0041A8A0 [PROVISIONAL]
		void Init()
		{
			MoveableObject::InitTable(0);
			Collectables::Init(0, 0);
			Weather::Init();

			Actor::Toy2Actor* boss = &Actor::g_creatureActors[0];
			g_targetBossScale = 0x1000;
			g_encounterProgress = 0x1000;
			g_previousBossPhase = boss->actorPhase;
			g_phaseCutsceneDuration = 180;
			g_introSoundTimer = 180;
			g_attackSoundTimer = 180;
			g_previousCutsceneDuration = Camera::g_cutsceneDuration;

			boss->actorFlags |= Actor::ACTOR_FLAG_BOSS;
			boss->yawAngle = 0x800;
			boss->targetYaw = 0x800;
			boss->pos.z += 0x10000;

			g_bossScale = 1;
			g_encounterState = 0;
			g_unusedState = 0;
			g_bombRotationAngle = 0;
			g_bombPositionY = 0;
			g_bombVerticalVelocity = 0;
			g_hudWrapX = 0;
			g_hudWrapY = 0;
			g_damageTintTimer = 0;
			g_tintAngleRed = 0;
			g_tintAngleGreen = 0;
			g_tintAngleBlue = 0;
			g_lightningFlashTimer = 200;
			g_thunderSoundTimer = 20000;

			boss->previousActorPhase = 0;
			boss->actorTint.r = 0x2000;
			boss->actorTint.g = 0x2000;
			boss->actorTint.b = 0x2000;
			boss->actorAlpha = 1000;

			Nu3D::Link::SetScaleFromFixedOffsets(0x32, 0, 0, 0);
			g_savedPickupVerticalPosition = Levels::g_recordData[63]->data[0].y;
			Levels::g_recordData[63]->data[0].y = INT_MIN;

			g_bombPositionY = boss->pos.y - 11000;
			Nu3D::Link::SetPositionRawAndCommit(0, boss->pos.x >> 5, g_bombPositionY, boss->pos.z >> 5);
			Nu3D::Link::SetScaleFromFixedOffsets(0, 5000, 5000, 5000);
		}

		// STUB: TOY2 0x0041AA10
		void Interactions() {}
	}
}
