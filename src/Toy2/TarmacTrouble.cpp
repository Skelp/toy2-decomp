#include "Toy2/Toy2.h"
#include "D3DApp/d3dapp.h"
#include "Toy2/Direct6.h"
#include "Toy2/Win95.h"
#include "Logger.h"
#include "FileUtils.h"
#include "InputManager.h"
#include "DrawingDevice.h"
#include "ModeSelect.h"
#include "Nullsub.h"
#include "SoftwareRenderer.h"
#include "Renderer/Renderer.h"
#include "Renderer/Sprite.h"
#include "SaveManager.h"
#include "Random.h"
#include "Toy2/LevelSelect.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Buzz.h"
#include "Toy2/Levels.h"
#include "Toy2/MainMenu.h"
#include "Toy2/Actor.h"
#include "Toy2/Camera.h"
#include "Toy2/Collision.h"
#include "Toy2/Collectables.h"
#include "Toy2/Weather.h"

#include "Nu3D/Font.h"
#include "Nu3D/FMV.h"
#include "Nu3D/Link.h"
#include "Nu3D/Viewport.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
#include "AudioManager/AudioManager.h"
#include "NGNLoader/NGNLoader.h"

#include <WINDOWS.H>
#include <STDIO.H>
#include <STRING.H>
#include <DINPUT.H>
#include <LIMITS.H>
#include <MATH.H>

#include <Numerics.h>

namespace Toy2
{
	namespace TarmacTrouble
	{
		enum LightPuzzleStateBits
		{
			LIGHT_PUZZLE_BOTTOM_0 = 0x01,
			LIGHT_PUZZLE_BOTTOM_1 = 0x02,
			LIGHT_PUZZLE_BOTTOM_2 = 0x04,
			LIGHT_PUZZLE_BOTTOM_3 = 0x08,
			LIGHT_PUZZLE_TOP_0 = 0x10,
			LIGHT_PUZZLE_TOP_1 = 0x20,
			LIGHT_PUZZLE_TOP_2 = 0x40,
			LIGHT_PUZZLE_TOP_3 = 0x80,
			LIGHT_PUZZLE_BOTTOM_ROW = 0x0F,
			LIGHT_PUZZLE_TOP_ROW = 0xF0,
			LIGHT_PUZZLE_BUTTON_0_MASK = 0xF6,
			LIGHT_PUZZLE_BUTTON_1_MASK = 0xFC,
			LIGHT_PUZZLE_BUTTON_2_MASK = 0xF9,
			LIGHT_PUZZLE_BUTTON_3_MASK = 0xF3,
		};

		// GLOBAL: TOY2 0x004F4B08
		extern const char g_lightPuzzleInstructions[] = {
#include "LightPuzzleInstructions.inc"
		};

		// GLOBAL: TOY2 0x004F4C40
		extern const Collectables::TokenDialogueValue g_tokenDialogueValues[] = {
			{ 150 }, { 30 }, { reinterpret_cast<int32_t>(g_lightPuzzleInstructions) }, { 0x800 }, { -1 }
		};

		// GLOBAL: TOY2 0x004F4C68
		int16_t g_tokenLinkIds[] = { 0x77, 0x76, 0x75, 0x74, 0x73, 0 };

		// GLOBAL: TOY2 0x0052FF48
		int32_t g_pressedButtonLink;
		// GLOBAL: TOY2 0x0052FF4C
		int32_t g_smithTintToggle;
		// GLOBAL: TOY2 0x0052FF60
		Vector3I g_turntableCenter;
		// GLOBAL: TOY2 0x0052FF70
		int32_t g_remainingButtonPresses;
		// GLOBAL: TOY2 0x0052FF74
		int32_t g_previousSmithPhase;
		// GLOBAL: TOY2 0x0052FF78
		int32_t g_smithTintTimer;
		// GLOBAL: TOY2 0x0052FF7C
		int32_t g_platformLiftOffset;
		// GLOBAL: TOY2 0x0052FF80
		Vector3I g_turntableRotation;
		// GLOBAL: TOY2 0x0052FF90
		int32_t g_buttonResetTimer;
		// GLOBAL: TOY2 0x0052FFA8
		int32_t g_lightningFlashTimer;
		// GLOBAL: TOY2 0x0052FFAC
		int32_t g_smithEncounterState;
		// GLOBAL: TOY2 0x0052FFB4
		int32_t g_lightPuzzleInitialized;
		// GLOBAL: TOY2 0x0052FFB8
		Vector3I g_turntableOrigin;
		// GLOBAL: TOY2 0x0052FFCC
		int32_t g_puzzleTokenPickupIndex;
		// GLOBAL: TOY2 0x0052FFD0
		int32_t g_lightPuzzleState;
		// GLOBAL: TOY2 0x0052FFD4
		int32_t g_indicatorWobbleAngle;
		// GLOBAL: TOY2 0x0052FFE8
		int32_t g_smithAttackTimer;
		// GLOBAL: TOY2 0x0052FFEC
		int32_t g_platformBobAngle;

		// FUNCTION: TOY2 0x0042D710 [MATCHED]
		int32_t PressLightPuzzleButton(int32_t buttonIndex)
		{
			int32_t affectedLightCount = 0;
			switch (buttonIndex)
			{
				case 0:
					if ((g_lightPuzzleState & LIGHT_PUZZLE_BOTTOM_0) == 0)
						affectedLightCount = 1;
					if ((g_lightPuzzleState & LIGHT_PUZZLE_BOTTOM_3) != 0)
						affectedLightCount++;
					g_lightPuzzleState = (g_lightPuzzleState & LIGHT_PUZZLE_BUTTON_0_MASK) | LIGHT_PUZZLE_BOTTOM_0;
					g_pressedButtonLink = 0x4C;
					g_buttonResetTimer = 12;
					Nu3D::Link::SetScaleFromFixedOffsets(0x50, 0x1000, 0x1000, 0x1000);
					Nu3D::Link::SetScaleFromFixedOffsets(g_pressedButtonLink, 0, 0, 0);
					break;
				case 1:
					if ((g_lightPuzzleState & LIGHT_PUZZLE_BOTTOM_1) == 0)
						affectedLightCount = 1;
					if ((g_lightPuzzleState & LIGHT_PUZZLE_BOTTOM_0) != 0)
						affectedLightCount++;
					g_lightPuzzleState = (g_lightPuzzleState & LIGHT_PUZZLE_BUTTON_1_MASK) | LIGHT_PUZZLE_BOTTOM_1;
					g_pressedButtonLink = 0x4D;
					g_buttonResetTimer = 12;
					Nu3D::Link::SetScaleFromFixedOffsets(0x51, 0x1000, 0x1000, 0x1000);
					Nu3D::Link::SetScaleFromFixedOffsets(g_pressedButtonLink, 0, 0, 0);
					break;
				case 2:
					if ((g_lightPuzzleState & LIGHT_PUZZLE_BOTTOM_2) == 0)
						affectedLightCount = 1;
					if ((g_lightPuzzleState & LIGHT_PUZZLE_BOTTOM_1) != 0)
						affectedLightCount++;
					g_lightPuzzleState = (g_lightPuzzleState & LIGHT_PUZZLE_BUTTON_2_MASK) | LIGHT_PUZZLE_BOTTOM_2;
					g_pressedButtonLink = 0x4E;
					g_buttonResetTimer = 12;
					Nu3D::Link::SetScaleFromFixedOffsets(0x52, 0x1000, 0x1000, 0x1000);
					Nu3D::Link::SetScaleFromFixedOffsets(g_pressedButtonLink, 0, 0, 0);
					break;
				case 3:
					if ((g_lightPuzzleState & LIGHT_PUZZLE_BOTTOM_3) == 0)
						affectedLightCount = 1;
					if ((g_lightPuzzleState & LIGHT_PUZZLE_BOTTOM_2) != 0)
						affectedLightCount++;
					g_lightPuzzleState = (g_lightPuzzleState & LIGHT_PUZZLE_BUTTON_3_MASK) | LIGHT_PUZZLE_BOTTOM_3;
					g_pressedButtonLink = 0x4F;
					g_buttonResetTimer = 12;
					Nu3D::Link::SetScaleFromFixedOffsets(0x53, 0x1000, 0x1000, 0x1000);
					Nu3D::Link::SetScaleFromFixedOffsets(g_pressedButtonLink, 0, 0, 0);
					break;
			}
			return affectedLightCount;
		}

		// FUNCTION: TOY2 0x0042D8B0 [MATCHED]
		void UpdateBottomPuzzleLights()
		{
			if ((g_lightPuzzleState & LIGHT_PUZZLE_BOTTOM_3) != 0)
			{
				Nu3D::Link::SetScaleFromFixedOffsets(0x2B, 0x1000, 0x1000, 0x1000);
				Nu3D::Link::SetScaleFromFixedOffsets(0x23, 0, 0, 0);
			}
			else
			{
				Nu3D::Link::SetScaleFromFixedOffsets(0x23, 0x1000, 0x1000, 0x1000);
				Nu3D::Link::SetScaleFromFixedOffsets(0x2B, 0, 0, 0);
			}

			if ((g_lightPuzzleState & LIGHT_PUZZLE_BOTTOM_2) != 0)
			{
				Nu3D::Link::SetScaleFromFixedOffsets(0x2C, 0x1000, 0x1000, 0x1000);
				Nu3D::Link::SetScaleFromFixedOffsets(0x24, 0, 0, 0);
			}
			else
			{
				Nu3D::Link::SetScaleFromFixedOffsets(0x24, 0x1000, 0x1000, 0x1000);
				Nu3D::Link::SetScaleFromFixedOffsets(0x2C, 0, 0, 0);
			}

			if ((g_lightPuzzleState & LIGHT_PUZZLE_BOTTOM_1) != 0)
			{
				Nu3D::Link::SetScaleFromFixedOffsets(0x33, 0x1000, 0x1000, 0x1000);
				Nu3D::Link::SetScaleFromFixedOffsets(0x25, 0, 0, 0);
			}
			else
			{
				Nu3D::Link::SetScaleFromFixedOffsets(0x25, 0x1000, 0x1000, 0x1000);
				Nu3D::Link::SetScaleFromFixedOffsets(0x33, 0, 0, 0);
			}

			if ((g_lightPuzzleState & LIGHT_PUZZLE_BOTTOM_0) != 0)
			{
				Nu3D::Link::SetScaleFromFixedOffsets(0x34, 0x1000, 0x1000, 0x1000);
				Nu3D::Link::SetScaleFromFixedOffsets(0x26, 0, 0, 0);
			}
			else
			{
				Nu3D::Link::SetScaleFromFixedOffsets(0x26, 0x1000, 0x1000, 0x1000);
				Nu3D::Link::SetScaleFromFixedOffsets(0x34, 0, 0, 0);
			}
		}

		// FUNCTION: TOY2 0x0042D9C0 [MATCHED]
		void UpdateTopPuzzleLights()
		{
			if ((g_lightPuzzleState & LIGHT_PUZZLE_TOP_3) != 0)
			{
				Nu3D::Link::SetScaleFromFixedOffsets(0x27, 0x1000, 0x1000, 0x1000);
				Nu3D::Link::SetScaleFromFixedOffsets(0x1F, 0, 0, 0);
			}
			else
			{
				Nu3D::Link::SetScaleFromFixedOffsets(0x1F, 0x1000, 0x1000, 0x1000);
				Nu3D::Link::SetScaleFromFixedOffsets(0x27, 0, 0, 0);
			}

			if ((g_lightPuzzleState & LIGHT_PUZZLE_TOP_2) != 0)
			{
				Nu3D::Link::SetScaleFromFixedOffsets(0x28, 0x1000, 0x1000, 0x1000);
				Nu3D::Link::SetScaleFromFixedOffsets(0x20, 0, 0, 0);
			}
			else
			{
				Nu3D::Link::SetScaleFromFixedOffsets(0x20, 0x1000, 0x1000, 0x1000);
				Nu3D::Link::SetScaleFromFixedOffsets(0x28, 0, 0, 0);
			}

			if ((g_lightPuzzleState & LIGHT_PUZZLE_TOP_1) != 0)
			{
				Nu3D::Link::SetScaleFromFixedOffsets(0x29, 0x1000, 0x1000, 0x1000);
				Nu3D::Link::SetScaleFromFixedOffsets(0x21, 0, 0, 0);
			}
			else
			{
				Nu3D::Link::SetScaleFromFixedOffsets(0x21, 0x1000, 0x1000, 0x1000);
				Nu3D::Link::SetScaleFromFixedOffsets(0x29, 0, 0, 0);
			}

			if ((g_lightPuzzleState & LIGHT_PUZZLE_TOP_0) != 0)
			{
				Nu3D::Link::SetScaleFromFixedOffsets(0x2A, 0x1000, 0x1000, 0x1000);
				Nu3D::Link::SetScaleFromFixedOffsets(0x22, 0, 0, 0);
			}
			else
			{
				Nu3D::Link::SetScaleFromFixedOffsets(0x22, 0x1000, 0x1000, 0x1000);
				Nu3D::Link::SetScaleFromFixedOffsets(0x2A, 0, 0, 0);
			}
		}

		// FUNCTION: TOY2 0x0042DB10 [MATCHED]
		void ResetLightPuzzle()
		{
			int32_t affectedLightCount;
			do
			{
				g_lightPuzzleState = (*g_randDatBufferPtr++ & 0xF) * 0x11;
				if (g_randDatBufferPtr > &g_randDatBuffer[1500])
					g_randDatBufferPtr -= 1500;
				affectedLightCount = PressLightPuzzleButton(*g_randDatBufferPtr++ & 3);
				affectedLightCount += PressLightPuzzleButton(*g_randDatBufferPtr++ & 3);
				affectedLightCount += PressLightPuzzleButton(*g_randDatBufferPtr++ & 3);
			} while ((g_lightPuzzleState & LIGHT_PUZZLE_TOP_ROW) == (g_lightPuzzleState & LIGHT_PUZZLE_BOTTOM_ROW) * 0x10 || affectedLightCount < 6);

			Nu3D::Link::SetScaleFromFixedOffsets(0x4C, 0x1000, 0x1000, 0x1000);
			Nu3D::Link::SetScaleFromFixedOffsets(0x4D, 0x1000, 0x1000, 0x1000);
			Nu3D::Link::SetScaleFromFixedOffsets(0x4E, 0x1000, 0x1000, 0x1000);
			Nu3D::Link::SetScaleFromFixedOffsets(0x4F, 0x1000, 0x1000, 0x1000);
			Nu3D::Link::SetScaleFromFixedOffsets(0x50, 0, 0, 0);
			Nu3D::Link::SetScaleFromFixedOffsets(0x51, 0, 0, 0);
			Nu3D::Link::SetScaleFromFixedOffsets(0x52, 0, 0, 0);
			Nu3D::Link::SetScaleFromFixedOffsets(0x53, 0, 0, 0);

			g_buttonResetTimer = 0;
			int32_t lowRow = g_lightPuzzleState & LIGHT_PUZZLE_BOTTOM_ROW;
			g_remainingButtonPresses = 3;
			g_lightPuzzleState = (lowRow << 4) + ((g_lightPuzzleState >> 4) & LIGHT_PUZZLE_BOTTOM_ROW);
			UpdateBottomPuzzleLights();
			UpdateTopPuzzleLights();

			for (int32_t linkId = 0x36; linkId < 0x3A; linkId++)
			{
				if (linkId == g_remainingButtonPresses + 0x36)
					Nu3D::Link::SetScaleFromFixedOffsets(linkId, 0x1000, 0x1000, 0x1000);
				else
					Nu3D::Link::SetScaleFromFixedOffsets(linkId, 0, 0, 0);
			}
		}

		// FUNCTION: TOY2 0x0042E600 [PROVISIONAL]
		void Init()
		{
			MoveableObject::InitTable(0);
			Collectables::Init(g_tokenLinkIds, 0x78);
			Collectables::LoadTokenTable(g_tokenDialogueValues);
			Collectables::Activate(3, 1);

			Vector3I position;
			int32_t actorIndex;
			int32_t pickupIndex;
			Vector3I nextPosition;
			Nu3D::Link::GetCurrentPosFixed(10, &position);
			Nu3D::Link::GetCurrentPosFixed(11, &nextPosition);
			position.x = (position.x + nextPosition.x) >> 1;
			position.y = (position.y + nextPosition.y) >> 1;
			position.z = (position.z + nextPosition.z) >> 1;
			Nu3D::Link::GetCurrentPosFixed(13, &nextPosition);

			g_turntableCenter.x = (position.x + nextPosition.x) >> 1;
			g_turntableOrigin.x = g_turntableCenter.x;
			g_turntableCenter.y = (position.y + nextPosition.y) >> 1;
			g_turntableOrigin.y = g_turntableCenter.y;
			g_turntableRotation.z = 0;
			g_turntableCenter.z = (position.z + nextPosition.z) >> 1;
			g_turntableOrigin.z = g_turntableCenter.z;
			g_turntableRotation.y = 0;
			g_turntableRotation.x = 0;

			Platform::AddFlags(0, 0x100);
			ResetLightPuzzle();
			g_lightPuzzleInitialized = 0;
			g_platformBobAngle = 0;
			g_platformLiftOffset = 0;
			g_indicatorWobbleAngle = 0;

			for (actorIndex = 0; actorIndex < 64; actorIndex++)
			{
				if (Actor::g_creatureActors[actorIndex].creatureId != 0)
					Actor::g_creatureActors[actorIndex].visibilityDistance = 1800;
			}

			g_previousSmithPhase = Actor::g_creatureActors[46].actorPhase;
			g_smithEncounterState = 0;
			g_smithTintTimer = 0;
			g_smithTintToggle = 0;
			g_smithAttackTimer = 0;
			g_puzzleTokenPickupIndex = -1;

			Levels::RecordData* pickupRecords = Levels::g_recordData[63];
			Collectables::PickupRecord* pickup = reinterpret_cast<Collectables::PickupRecord*>(pickupRecords + 1);
			for (pickupIndex = 0; pickupIndex < pickupRecords->recordCount; pickup++, pickupIndex++)
			{
				if (pickup->objectIndex == 0x74)
				{
					g_puzzleTokenPickupIndex = pickupIndex;
					break;
				}
			}

			g_lightningFlashTimer = 200;
			Weather::Init();
		}

		// STUB: TOY2 0x0042E790
		void Interactions() {}
	}
}

namespace Toy2
{
	namespace Particles
	{
		void SpawnCollectSparkle(int32_t x, int32_t y, int32_t z, int32_t particleSpread);
	}

	namespace CreatureBehaviour
	{
		// STUB: TOY2 0x0042D3E0
		void SmithLevel14(Actor::Toy2Actor::ActorBehaviourContext* context) {}

		// FUNCTION: TOY2 0x0042D620 [MATCHED]
		void Luggage(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			if ((actor->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0 && actor->actorPhase == 0x66)
			{
				g_levelObjectiveProgress++;
				Particles::SpawnCollectSparkle(actor->pos.x, actor->pos.y - 0x2000, actor->pos.z, 0x32);
				AudioManager::PlaySoundEffect(0x1F, &actor->pos);
				Actor::Kill(actor, 2);
			}
		}
	}
}
