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
#include "Toy2/Lighting.h"
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
	extern int32_t g_hudActorAnimationFrame;

	namespace TarmacTrouble
	{
		enum SmithEncounterState
		{
			SMITH_ENCOUNTER_ACTIVE = 2,
			SMITH_ENCOUNTER_DEFEATED = 3,
			SMITH_ENCOUNTER_TOKEN_DELAY_END = 120,
			SMITH_ENCOUNTER_TOKEN_AWARDED = 200,
		};

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

		// GLOBAL: TOY2 0x004F4C54
		char* g_rotatingHintSubtitles[] = {
#include "TarmacTroubleHint.inc"
		};

		// GLOBAL: TOY2 0x004F4C68
		int16_t g_tokenLinkIds[] = { 0x77, 0x76, 0x75, 0x74, 0x73, 0 };

		// GLOBAL: TOY2 0x0052FF48
		int32_t g_pressedButtonLink;
		// GLOBAL: TOY2 0x0052FF4C
		int32_t g_smithTintToggle;
		// GLOBAL: TOY2 0x0052FF50
		Vector3I g_platformSoundPosition;
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
		// GLOBAL: TOY2 0x0052FF98
		Vector3I g_turntableSoundPosition;
		// GLOBAL: TOY2 0x0052FFA8
		int32_t g_lightningFlashTimer;
		// GLOBAL: TOY2 0x0052FFAC
		int32_t g_smithEncounterState;
		// GLOBAL: TOY2 0x0052FFB0
		int32_t g_thunderTimer;
		// GLOBAL: TOY2 0x0052FFB4
		int32_t g_lightPuzzleInitialized;
		// GLOBAL: TOY2 0x0052FFB8
		Vector3I g_turntableOrigin;
		// GLOBAL: TOY2 0x0052FFC8
		int32_t g_puzzleCutsceneTimer;
		// GLOBAL: TOY2 0x0052FFCC
		int32_t g_puzzleTokenPickupIndex;
		// GLOBAL: TOY2 0x0052FFD0
		int32_t g_lightPuzzleState;
		// GLOBAL: TOY2 0x0052FFD4
		int32_t g_indicatorWobbleAngle;
		// GLOBAL: TOY2 0x0052FFD8
		Vector3I g_lightningSoundPosition;
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

		// FUNCTION: TOY2 0x0042DCB0 [PROVISIONAL]
		void UpdateTurntable()
		{
			Platform::GetRotation(0, &g_turntableRotation);
			Platform::SetAngularVelocity(0, 0, Renderer::g_frameDelta * 4, 0);
			g_turntableRotation.y >>= 2;

			Vector3I linkPosition;
			Vector3I platformOrigin;
			Nu3D::Link::GetCurrentPosFixed(0, &linkPosition);
			Platform::GetOrigin(0, &platformOrigin);
			platformOrigin.x = (linkPosition.x - platformOrigin.x) * 3 >> 2;
			platformOrigin.z = (linkPosition.z - platformOrigin.z) * 3 >> 2;
			Platform::SetVelocity(0, platformOrigin.x, 0, platformOrigin.z);

			int32_t offsetY = g_turntableCenter.y - g_turntableOrigin.y;
			int32_t offsetX =
				(((g_turntableCenter.x >> 8) * Numerics::g_sinCosLUT[(g_turntableRotation.y + 0x400) & 0xFFF]
					 - (g_turntableCenter.z >> 8) * Numerics::g_sinCosLUT[(-g_turntableRotation.y) & 0xFFF])
					>> 6)
				- g_turntableOrigin.x;
			int32_t offsetZ =
				((Numerics::g_sinCosLUT[(-g_turntableRotation.y) & 0xFFF] * (g_turntableCenter.x >> 8)
					 + (g_turntableCenter.z >> 8) * Numerics::g_sinCosLUT[(g_turntableRotation.y + 0x400) & 0xFFF])
					>> 6)
				- g_turntableOrigin.z;

			int32_t linkId;
			for (linkId = 0; linkId < 0x57; linkId++)
			{
				if (linkId == 3)
					linkId = 4;
				else if (linkId == 0x10)
					linkId = 0x54;

				Nu3D::Link::GetTargetPosFixed(linkId, &linkPosition);
				int32_t relativeX = (linkPosition.x - g_turntableOrigin.x) >> 4;
				int32_t relativeZ = (linkPosition.z - g_turntableOrigin.z) >> 4;
				int32_t rotatedZ =
					((relativeX * Numerics::g_sinCosLUT[(-g_turntableRotation.y) & 0xFFF]
						 + relativeZ * Numerics::g_sinCosLUT[(g_turntableRotation.y + 0x400) & 0xFFF])
						>> 10)
					+ g_turntableOrigin.z + offsetZ;
				int32_t rotatedX =
					((relativeX * Numerics::g_sinCosLUT[(g_turntableRotation.y + 0x400) & 0xFFF]
						 - relativeZ * Numerics::g_sinCosLUT[(-g_turntableRotation.y) & 0xFFF])
						>> 10)
					+ g_turntableOrigin.x + offsetX;
				Nu3D::Link::SetPositionRawAndCommit(linkId, rotatedX >> 5, (linkPosition.y + offsetY) >> 5, rotatedZ >> 5);

				if (linkId == 8 || linkId == 9)
				{
					Nu3D::Link::SetRotationRelative8bit(linkId, 0, g_turntableRotation.y, g_turntableRotation.y * 0x327);
				}
				else if (linkId == 10)
				{
					Nu3D::Link::SetRotationRelative8bit(linkId, 0, g_turntableRotation.y + 0x400, g_turntableRotation.y * -15);
					linkPosition.x = rotatedX;
					linkPosition.y += 0xC00;
					linkPosition.z = rotatedZ;
					if (Nu3D::Math::IsWithinDistance(&g_buzzActor.posAngles.pos, &linkPosition, 0x50) != 0)
					{
						int32_t damageAngle =
							Nu3D::Math::CartesianToFixedAngle(g_buzzActor.posAngles.pos.x - linkPosition.x, g_buzzActor.posAngles.pos.z - linkPosition.z);
						Buzz::HandleDamage(damageAngle, 3);
					}
				}
				else if (linkId == 11)
				{
					Nu3D::Link::SetRotationRelative8bit(linkId, 0, g_turntableRotation.y + 0x400, g_turntableRotation.y * -21);
					linkPosition.x = rotatedX;
					linkPosition.y += 0xC00;
					linkPosition.z = rotatedZ;
					if (Nu3D::Math::IsWithinDistance(&g_buzzActor.posAngles.pos, &linkPosition, 0x50) != 0)
					{
						int32_t damageAngle =
							Nu3D::Math::CartesianToFixedAngle(g_buzzActor.posAngles.pos.x - linkPosition.x, g_buzzActor.posAngles.pos.z - linkPosition.z);
						Buzz::HandleDamage(damageAngle, 3);
					}
				}
				else if (linkId == 13)
				{
					Nu3D::Link::SetRotationRelative8bit(linkId, 0, g_turntableRotation.y + 700, g_turntableRotation.y * -18);
					linkPosition.x = rotatedX;
					linkPosition.y += 0xC00;
					linkPosition.z = rotatedZ;
					if (Nu3D::Math::IsWithinDistance(&g_buzzActor.posAngles.pos, &linkPosition, 0x50) != 0)
					{
						int32_t damageAngle =
							Nu3D::Math::CartesianToFixedAngle(g_buzzActor.posAngles.pos.x - linkPosition.x, g_buzzActor.posAngles.pos.z - linkPosition.z);
						Buzz::HandleDamage(damageAngle, 3);
					}
				}
				else
				{
					Nu3D::Link::SetRotationRelative8bit(linkId, 0, g_turntableRotation.y, 0);
				}
			}

			offsetX >>= 2;
			offsetY >>= 2;
			offsetZ >>= 2;
			for (linkId = 0x10; linkId < 0x1F; linkId++)
			{
				Nu3D::Link::GetTargetPosFixed(linkId, &linkPosition);
				int32_t relativeX = linkPosition.x - (g_turntableOrigin.x >> 2);
				int32_t relativeZ = linkPosition.z - (g_turntableOrigin.z >> 2);
				int32_t rotatedX =
					((relativeX * Numerics::g_sinCosLUT[(g_turntableRotation.y + 0x400) & 0xFFF]
						 - relativeZ * Numerics::g_sinCosLUT[(-g_turntableRotation.y) & 0xFFF])
						>> 14)
					+ (g_turntableOrigin.x >> 2) + offsetX;
				int32_t rotatedZ =
					((relativeX * Numerics::g_sinCosLUT[(-g_turntableRotation.y) & 0xFFF]
						 + relativeZ * Numerics::g_sinCosLUT[(g_turntableRotation.y + 0x400) & 0xFFF])
						>> 14)
					+ (g_turntableOrigin.z >> 2) + offsetZ;
				Nu3D::Link::SetPositionRawAndCommit(linkId, rotatedX >> 5, (linkPosition.y + offsetY) >> 5, rotatedZ >> 5);

				if (linkId == 0x17 || linkId == 0x18)
					Nu3D::Link::SetRotationRelative8bit(linkId, 0, g_turntableRotation.y, g_turntableRotation.y * 0x327);
				else if (linkId == 0x19)
					Nu3D::Link::SetRotationRelative8bit(linkId, 0, g_turntableRotation.y + 0x400, g_turntableRotation.y * -15);
				else if (linkId == 0x1A)
					Nu3D::Link::SetRotationRelative8bit(linkId, 0, g_turntableRotation.y + 0x400, g_turntableRotation.y * -21);
				else if (linkId == 0x1C)
					Nu3D::Link::SetRotationRelative8bit(linkId, 0, g_turntableRotation.y + 700, g_turntableRotation.y * -18);
				else
					Nu3D::Link::SetRotationRelative8bit(linkId, 0, g_turntableRotation.y, 0);
			}
		}

		// FUNCTION: TOY2 0x0042E1D0 [PROVISIONAL]
		void UpdatePlatforms()
		{
			Vector3I position;

			Nu3D::Link::GetTargetPosFixed(3, &position);
			Nu3D::Link::SetPositionRawAndCommit(
				3, position.x >> 5, (position.y + g_platformLiftOffset + Numerics::g_sinCosLUT[g_platformBobAngle & 0xFFF] / 2) >> 5, position.z >> 5);

			Nu3D::Link::GetTargetPosFixed(0x30, &position);
			Nu3D::Link::SetPositionRawAndCommit(
				0x30, position.x >> 5, (position.y + g_platformLiftOffset + Numerics::g_sinCosLUT[g_platformBobAngle & 0xFFF] / 2) >> 5, position.z >> 5);

			Nu3D::Link::GetTargetPosFixed(0x42, &position);
			Nu3D::Link::SetPositionRawAndCommit(
				0x42, position.x >> 5, (position.y + g_platformLiftOffset + Numerics::g_sinCosLUT[g_platformBobAngle & 0xFFF] / 2) >> 5, position.z >> 5);

			Nu3D::Link::GetTargetPosFixed(0x43, &position);
			Nu3D::Link::SetPositionRawAndCommit(
				0x43, position.x >> 5, (position.y + g_platformLiftOffset + Numerics::g_sinCosLUT[g_platformBobAngle & 0xFFF] / 2) >> 5, position.z >> 5);

			Nu3D::Link::GetTargetPosFixed(0x44, &position);
			Nu3D::Link::SetPositionRawAndCommit(
				0x44, position.x >> 5, (position.y + g_platformLiftOffset + Numerics::g_sinCosLUT[g_platformBobAngle & 0xFFF] / 2) >> 5, position.z >> 5);

			Nu3D::Link::GetTargetPosFixed(0x74, &position);
			Nu3D::Link::SetPositionRawAndCommit(0x74,
				position.x >> 5,
				(position.y - 0xA000 + g_platformLiftOffset + Numerics::g_sinCosLUT[g_platformBobAngle & 0xFFF] / 4) >> 5,
				position.z >> 5);

			Collectables::PickupRecord* puzzleTokenPickup =
				reinterpret_cast<Collectables::PickupRecord*>(Levels::g_recordData[63] + 1) + g_puzzleTokenPickupIndex;
			if (puzzleTokenPickup->position.y != INT_MIN)
				puzzleTokenPickup->position.y = (position.y - 0xA000 + g_platformLiftOffset + Numerics::g_sinCosLUT[g_platformBobAngle & 0xFFF] / 4) >> 5;

			Nu3D::Link::GetTargetPosFixed(0x31, &position);
			Nu3D::Link::SetPositionRawAndCommit(0x31,
				position.x >> 5,
				(position.y + ((Numerics::g_sinCosLUT[g_platformBobAngle & 0xFFF] / 2 + g_platformLiftOffset) >> 2)) >> 5,
				position.z >> 5);

			Nu3D::Link::GetTargetPosFixed(0x32, &position);
			Nu3D::Link::SetPositionRawAndCommit(0x32,
				position.x >> 5,
				(position.y + ((Numerics::g_sinCosLUT[g_platformBobAngle & 0xFFF] / 2 + g_platformLiftOffset) >> 2)) >> 5,
				position.z >> 5);

			Nu3D::Link::SetRotationRelative8bit(0x30, 0, g_platformBobAngle * 21, 0);
			Nu3D::Link::SetRotationRelative8bit(0x32, 0, g_platformBobAngle * 21, 0);
			g_platformBobAngle += Renderer::g_frameDelta * 33;
		}

		// FUNCTION: TOY2 0x0042E600 [PROVISIONAL]
		void Init()
		{
			MoveableObject::InitTable(0);
			Collectables::Init(g_tokenLinkIds, 0x78);
			Collectables::LoadTokenTable(g_tokenDialogueValues);
			Collectables::Activate(3, 1);

			PosAndAngles position;
			int32_t actorIndex;
			int32_t pickupIndex;
			PosAndAngles nextPosition;
			Nu3D::Link::GetCurrentPosFixed(10, &position.pos);
			Nu3D::Link::GetCurrentPosFixed(11, &nextPosition.pos);
			position.pos.x = (position.pos.x + nextPosition.pos.x) >> 1;
			position.pos.y = (position.pos.y + nextPosition.pos.y) >> 1;
			position.pos.z = (position.pos.z + nextPosition.pos.z) >> 1;
			Nu3D::Link::GetCurrentPosFixed(13, &nextPosition.pos);

			g_turntableCenter.x = (position.pos.x + nextPosition.pos.x) >> 1;
			g_turntableOrigin.x = g_turntableCenter.x;
			g_turntableCenter.y = (position.pos.y + nextPosition.pos.y) >> 1;
			g_turntableOrigin.y = g_turntableCenter.y;
			g_turntableRotation.z = 0;
			g_turntableCenter.z = (position.pos.z + nextPosition.pos.z) >> 1;
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

		// FUNCTION: TOY2 0x0042E790 [PROVISIONAL]
		void Interactions()
		{
			AudioManager::PlaySoundEffect(0x6F, 0);
			UpdateTurntable();

			PosAndAngles position;
			position.pos.x = -0xD2AC0;
			position.pos.y = -0xF9F7;
			position.pos.z = -0x98D72;
			if (Nu3D::Math::IsWithinDistance(&g_buzzActor.posAngles.pos, &position.pos, 500) != 0)
			{
				if (g_lightPuzzleInitialized == 0)
					ResetLightPuzzle();

				int8_t previousPuzzleState = (int8_t)g_lightPuzzleState;
				g_lightPuzzleInitialized = 1;
				if ((g_lightPuzzleState & LIGHT_PUZZLE_TOP_ROW) == (g_lightPuzzleState & LIGHT_PUZZLE_BOTTOM_ROW) * 0x10 && g_remainingButtonPresses >= 0)
				{
					g_remainingButtonPresses = (g_remainingButtonPresses + Renderer::g_frameDelta) & 0x3F;
					if ((uint32_t)g_remainingButtonPresses > 20 && previousPuzzleState == 0)
					{
						g_lightPuzzleState = 0xFF;
						UpdateBottomPuzzleLights();
						UpdateTopPuzzleLights();
						previousPuzzleState = g_lightPuzzleState;
					}
					if (g_remainingButtonPresses < 20 && previousPuzzleState != 0)
					{
						g_lightPuzzleState = 0;
						UpdateBottomPuzzleLights();
						UpdateTopPuzzleLights();
					}
					if (g_platformLiftOffset < 48000)
						g_platformLiftOffset += Renderer::g_frameDelta * 0x80;

					if (g_puzzleCutsceneTimer >= 0)
					{
						if (g_puzzleCutsceneTimer == 0)
						{
							Nu3D::Link::GetCurrentPosFixed(0x74, &position.pos);
							Camera::BeginScriptedCutsceneAtPoint(&position.pos, 300, 0x20);
							Camera::g_cutsceneCameraPosition.y += 0x8000;
							AudioManager::StartSoundSequenceOnActor(-5, &g_buzzActor.posAngles.pos);
						}
						Nu3D::Link::GetCurrentPosFixed(0x74, &position.pos);
						Camera::g_cutsceneFocusPosition = position.pos;
						g_puzzleCutsceneTimer += Renderer::g_frameDelta;
						Nu3D::Link::GetCurrentPosFixed(0x74, &position.pos);
						if (g_puzzleCutsceneTimer > 299)
							g_puzzleCutsceneTimer = -1;
					}
				}
				else if (g_remainingButtonPresses <= 0)
				{
					if (g_remainingButtonPresses == 0)
					{
						g_lightPuzzleState = 0;
						UpdateBottomPuzzleLights();
						UpdateTopPuzzleLights();
					}
					g_remainingButtonPresses -= Renderer::g_frameDelta;
					if (g_remainingButtonPresses < -60)
					{
						ResetLightPuzzle();
						if (Camera::g_cutsceneDuration == 0)
							AudioManager::StartSoundSequenceOnActor(-6, &g_buzzActor.posAngles.pos);
					}
				}
				else if (g_groundSlamTimer == -40 && g_footingType > 0x1F && g_footingType < 0x24)
				{
					g_remainingButtonPresses--;
					for (int32_t linkId = 0x36; linkId < 0x3A; linkId++)
					{
						int32_t scale = linkId == g_remainingButtonPresses + 0x36 ? 0x1000 : 0;
						Nu3D::Link::SetScaleFromFixedOffsets(linkId, scale, scale, scale);
					}
					Levels::DeactivateAmbientEmitter(0, 1);
					Levels::DeactivateAmbientEmitter(1, 1);
					Levels::DeactivateAmbientEmitter(2, 1);
					Levels::DeactivateAmbientEmitter(3, 1);
					if (g_remainingButtonPresses >= 0)
					{
						PressLightPuzzleButton(g_footingType - 0x20);
						UpdateBottomPuzzleLights();
					}
				}
			}

			if (g_buttonResetTimer > 0)
			{
				g_buttonResetTimer -= Renderer::g_frameDelta;
				if (g_buttonResetTimer < 1)
				{
					Nu3D::Link::SetScaleFromFixedOffsets(g_pressedButtonLink, 0x1000, 0x1000, 0x1000);
					Nu3D::Link::SetScaleFromFixedOffsets(g_pressedButtonLink + 4, 0, 0, 0);
				}
			}

			UpdatePlatforms();
			g_indicatorWobbleAngle += Renderer::g_frameDelta;
			int32_t wobbleX = Numerics::g_sinCosLUT[g_indicatorWobbleAngle * 7 & 0xFFF] * Numerics::g_sinCosLUT[g_indicatorWobbleAngle * 11 & 0xFFF];
			int32_t wobbleY = Numerics::g_sinCosLUT[g_indicatorWobbleAngle * 17 & 0xFFF] * Numerics::g_sinCosLUT[g_indicatorWobbleAngle * 11 & 0xFFF];
			Nu3D::Link::SetRotationRelative8bit(0x45, 0, wobbleY / 0x200000, wobbleX / 0x400000);
			wobbleX = Numerics::g_sinCosLUT[g_indicatorWobbleAngle * 7 & 0xFFF] * Numerics::g_sinCosLUT[g_indicatorWobbleAngle * 11 & 0xFFF];
			wobbleY = Numerics::g_sinCosLUT[g_indicatorWobbleAngle * 17 & 0xFFF] * Numerics::g_sinCosLUT[g_indicatorWobbleAngle * 11 & 0xFFF];
			Nu3D::Link::SetRotationRelative8bit(0x46, 0, wobbleY / 0x200000, wobbleX / 0x400000);

			Actor::CollectQuestReward(0x27, 2, -1, 0, 0);
			Actor::RotatingHint(0x2F, 10, g_rotatingHintSubtitles);
			Actor::Toy2Actor* luggageOwner = &Actor::g_creatureActors[0x28];
			if ((luggageOwner->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0)
			{
				luggageOwner->actorFlags &= ~Actor::ACTOR_FLAG_INTERACTION_REQUESTED;
				if (g_levelObjectiveProgress >= 0)
				{
					if (g_levelObjectiveProgress == 5)
					{
						Dialogue::Begin(0x28, 3, "thanks for finding my ^luggage^ buzz! here is a pizza planet ^token^!", -1, 0, 1);
						g_levelObjectiveProgress = -1;
					}
					else
					{
						Dialogue::Begin(0x28,
							3,
							"hi buzz! i have lost ^five^ pieces of ^luggage^. if you can find them for me, i will give you a pizza planet ^token^.",
							-1,
							0,
							-1);
					}
				}
			}

			Actor::PlayPeriodicHintSound(0x26, 0xB5);
			Actor::Toy2Actor* challengeActor = &Actor::g_creatureActors[0x26];
			if ((challengeActor->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0)
			{
				challengeActor->actorFlags &= ~Actor::ACTOR_FLAG_INTERACTION_REQUESTED;
				if (Collectables::g_tokenStates[2].active != 2)
				{
					if (HUD::g_challengeState == HUD::CHALLENGE_STATE_INACTIVE)
					{
						AudioManager::Preset::PlayOneShotSound2(0xB6, challengeActor);
						Dialogue::Begin(0x26,
							4,
							"hi buzz! if you can reach the end of this path without ^jumping^ or touching the ^green^ slime, i will give you a pizza planet "
							"^token^.",
							-1,
							0,
							2);
						HUD::g_challengeState = HUD::CHALLENGE_STATE_WAITING_FOR_CAMERA;
					}
					else
					{
						Dialogue::Begin(0x26, 4, "quick! you can still reach the token!", -1, 0, -1);
					}
				}
			}
			if (HUD::g_challengeState == HUD::CHALLENGE_STATE_WAITING_FOR_CAMERA && Nu3D::Camera::g_viewHistoryInitialized == 0)
			{
				HUD::g_challengeState = HUD::CHALLENGE_STATE_ACTIVE;
				AndysHouse::g_raceCheckpointPassCount = 170;
			}
			if (HUD::g_challengeState == HUD::CHALLENGE_STATE_ACTIVE)
			{
				if (Collectables::g_tokenStates[2].active == 2)
				{
					HUD::g_challengeState = HUD::CHALLENGE_STATE_INACTIVE;
				}
				else
				{
					if ((g_buzzActor.airborneMode > 0 && g_buzzActor.airborneMode < 5) || g_footingType == 0)
						AndysHouse::g_raceCheckpointPassCount = 99;
					if (g_framePulseOutputs.sixtyFourTick != 0)
						AndysHouse::g_raceCheckpointPassCount--;
					if (AndysHouse::g_raceCheckpointPassCount < 100)
					{
						AndysHouse::g_raceCheckpointPassCount = 100;
						HUD::g_challengeState = HUD::CHALLENGE_STATE_INACTIVE;
						Collectables::Deactivate(2);
					}
				}
			}

			Nu3D::Link::GetCurrentPosFixed(0, &position.pos);
			Actor::Toy2Actor* turntableActor = &Actor::g_creatureActors[0x2B];
			turntableActor->boundary.x = position.pos.x;
			turntableActor->pos.x = position.pos.x;
			turntableActor->boundary.y = position.pos.y;
			turntableActor->boundary.z = position.pos.z;
			turntableActor->pos.z = position.pos.z;

			Actor::Toy2Actor* smith = &Actor::g_creatureActors[0x2E];
			if (g_smithEncounterState == 0 && Nu3D::Math::IsWithinDistance(&g_buzzActor.posAngles.pos, &smith->pos, 300) != 0)
			{
				g_smithEncounterState = 1;
				Dialogue::Begin(0x2E, 1, "ha ha ha ha ... defeat the ^blacksmith^ boss to get a pizza planet ^token^!", -1, 0, -1);
			}
			if (g_smithEncounterState == 1 && Nu3D::Camera::g_viewHistoryInitialized == 0)
			{
				smith->movementData = CreatureBehaviour::g_smithMovementData + 14;
				g_levelInteractionTimer = 180;
				g_smithEncounterState = SMITH_ENCOUNTER_ACTIVE;
				smith->movementCommandTimer = 0;
				smith->creatureRam->initialFacingAngle = 0;
			}
			if (g_smithEncounterState > SMITH_ENCOUNTER_ACTIVE)
			{
				if (g_smithEncounterState < SMITH_ENCOUNTER_TOKEN_DELAY_END)
					g_smithEncounterState += Renderer::g_frameDelta;
				else if (g_smithEncounterState != SMITH_ENCOUNTER_TOKEN_AWARDED)
				{
					Collectables::Activate(4, 0);
					g_smithEncounterState = SMITH_ENCOUNTER_TOKEN_AWARDED;
				}
			}

			Nu3D::Link::GetCurrentPosFixed(0, &position.pos);
			g_turntableSoundPosition.x = (Camera::g_renderCameraTransform.pos.x - position.pos.x) * 3 / 4 + position.pos.x;
			g_turntableSoundPosition.y = (Camera::g_renderCameraTransform.pos.y - position.pos.y) * 3 / 4 + position.pos.y;
			g_turntableSoundPosition.z = (Camera::g_renderCameraTransform.pos.z - position.pos.z) * 3 / 4 + position.pos.z;
			AudioManager::PlaySoundEffect(0x9C, &g_turntableSoundPosition);
			Nu3D::Link::GetCurrentPosFixed(0x44, &position.pos);
			g_platformSoundPosition.x = (Camera::g_renderCameraTransform.pos.x - position.pos.x) * 3 / 4 + position.pos.x;
			g_platformSoundPosition.y = (Camera::g_renderCameraTransform.pos.y - position.pos.y) * 3 / 4 + position.pos.y;
			g_platformSoundPosition.z = (Camera::g_renderCameraTransform.pos.z - position.pos.z) * 3 / 4 + position.pos.z;
			AudioManager::PlaySoundEffect(0x9B, &g_platformSoundPosition);

			Weather::StepPrecipitation(0x100, 0x1000, 0x33);
			if (g_framePulseOutputs.eightTick != 0)
			{
				position.pos.x = (*g_randDatBufferPtr - 0x80) * 0x100 + Numerics::g_sinCosLUT[Camera::g_renderCameraTransform.rotation.euler.angles.yaw] * 3
					+ Camera::g_renderCameraTransform.pos.x;
				position.pos.y = Camera::g_renderCameraTransform.pos.y - 0x8000;
				position.pos.z = (g_randDatBufferPtr[1] - 0x80) * 0x100
					+ Numerics::g_sinCosLUT[(Camera::g_renderCameraTransform.rotation.euler.angles.yaw + 0x400) & 0xFFF] * 3
					+ Camera::g_renderCameraTransform.pos.z;
				g_randDatBufferPtr += 2;
				int32_t groundHeight = Nu3D::Collision::GetGroundHeight(&position, 0);
				if (groundHeight != INT_MIN && Camera::g_renderCameraTransform.pos.y < groundHeight)
					Nu3D::Particles::SpawnFromPreset(position.pos.x, groundHeight, position.pos.z, 0x1B, 2);
			}

			int32_t flashTimer = g_lightningFlashTimer - Renderer::g_frameDelta;
			if (flashTimer < 0x20)
			{
				if (g_lightningFlashTimer > 0x1F)
				{
					g_thunderTimer = *g_randDatBufferPtr;
					uint32_t lightningAngle = (Camera::g_renderCameraTransform.rotation.euler.angles.yaw - 0x80 + g_randDatBufferPtr[1]) & 0xFFF;
					g_randDatBufferPtr += 2;
					g_lightningSoundPosition.x = (Numerics::g_sinCosLUT[lightningAngle] * g_thunderTimer >> 5) + Camera::g_renderCameraTransform.pos.x;
					g_lightningSoundPosition.y = Camera::g_renderCameraTransform.pos.y;
					g_lightningSoundPosition.z =
						(Numerics::g_sinCosLUT[(lightningAngle + 0x400) & 0xFFF] * g_thunderTimer >> 5) + Camera::g_renderCameraTransform.pos.z;
				}
				if (Nu3D::Camera::g_targetTintFadeSpeed == 0)
				{
					if (flashTimer < 0)
						flashTimer = *g_randDatBufferPtr++ * 2 + 0x20;
					else if (Nu3D::Camera::g_cameraTintBlue > 0x40)
					{
						Nu3D::Camera::g_cameraTintBlue = (int16_t)flashTimer * 3 + 0x80;
						Nu3D::Camera::g_cameraTintRed = Nu3D::Camera::g_cameraTintBlue;
						Nu3D::Camera::g_cameraTintGreen = Nu3D::Camera::g_cameraTintBlue;
					}
				}
				else
				{
					Nu3D::Camera::g_cameraTintRed = 0x80;
					Nu3D::Camera::g_cameraTintGreen = 0x80;
					Nu3D::Camera::g_cameraTintBlue = 0x80;
					g_lightningFlashTimer = 10000;
					flashTimer = g_lightningFlashTimer;
				}
			}
			g_lightningFlashTimer = flashTimer;
			g_thunderTimer -= Renderer::g_frameDelta;
			if (g_thunderTimer < 1)
			{
				g_thunderTimer = 20000;
				AudioManager::PlaySoundEffect(0x70, &g_lightningSoundPosition);
			}

			Levels::RecordData* flareRecords = Levels::g_recordData[0x28];
			int32_t nearestDistanceSquared = INT_MAX;
			int32_t nearestFlareIndex = 0;
			int32_t flareIndex;
			int32_t flareCount;
			int32_t buzzRadiusSquared = (g_buzzActor.posAngles.pos.z >> 8) * (g_buzzActor.posAngles.pos.z >> 8)
				+ (g_buzzActor.posAngles.pos.x >> 8) * (g_buzzActor.posAngles.pos.x >> 8);
			if (buzzRadiusSquared < 0x8E5144)
			{
				flareIndex = 0;
				flareCount = 16;
			}
			else
			{
				flareIndex = 16;
				flareCount = flareRecords->recordCount;
			}

			for (; flareIndex < flareCount; flareIndex++)
			{
				Vector3I* flare = &flareRecords->data[flareIndex];
				int32_t cameraOffsetY = (Camera::g_renderCameraTransform.pos.y - flare->y * 0x20) >> 8;
				int32_t cameraOffsetZ = (Camera::g_renderCameraTransform.pos.z - flare->z * 0x20) >> 8;
				int32_t cameraOffsetX = (Camera::g_renderCameraTransform.pos.x - flare->x * 0x20) >> 8;
				int32_t cameraDistanceSquared = cameraOffsetX * cameraOffsetX + cameraOffsetY * cameraOffsetY + cameraOffsetZ * cameraOffsetZ;
				if (cameraDistanceSquared < 0x40000)
				{
					int32_t brightness = 0x80 - (int32_t)sqrt((double)cameraDistanceSquared) / 4;
					if (brightness > 0)
					{
						int32_t red = flareIndex < 16 ? brightness : 0;
						int32_t blue = flareIndex < 16 ? 0 : brightness;
						Renderer::LensFlare::RegisterLight(flare->x * 0x20, flare->y * 0x20, flare->z * 0x20, red, brightness, blue, 0x80);
					}
					int32_t buzzOffsetY = (g_buzzActor.posAngles.pos.y - flare->y * 0x20 - 0x2000) >> 8;
					int32_t buzzOffsetZ = (g_buzzActor.posAngles.pos.z - flare->z * 0x20) >> 8;
					int32_t buzzOffsetX = (g_buzzActor.posAngles.pos.x - flare->x * 0x20) >> 8;
					int32_t distanceSquared = buzzOffsetX * buzzOffsetX + buzzOffsetZ * buzzOffsetZ + buzzOffsetY * buzzOffsetY;
					if (distanceSquared < nearestDistanceSquared)
					{
						nearestDistanceSquared = distanceSquared;
						nearestFlareIndex = flareIndex;
					}
				}
			}

			if (nearestDistanceSquared < 0x10000)
			{
				Vector3I* nearestFlare = &flareRecords->data[nearestFlareIndex];
				Lighting::DynamicLight* light = &Lighting::g_lightingState.dynamicLights[1];
				light->sourceId = reinterpret_cast<int32_t>(nearestFlare);
				light->position.x = nearestFlare->x << 5;
				light->position.y = nearestFlare->y << 5;
				light->position.z = nearestFlare->z << 5;
				if (nearestFlareIndex < 16)
				{
					light->colour.r = 0xFF;
					light->colour.b = 0;
				}
				else
				{
					light->colour.r = 0;
					light->colour.b = 0xFF;
				}
				light->colour.g = 0xFF;
				light->lifetime = 1;
			}
			else
			{
				Lighting::g_lightingState.dynamicLights[1].lifetime = 0;
			}
			PlayLevelMusic();
		}
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
		// FUNCTION: TOY2 0x0042D3E0 [PROVISIONAL]
		void SmithLevel14(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			TarmacTrouble::g_smithTintToggle = (TarmacTrouble::g_smithTintToggle - 1) & 1;

			if (actor->actorPhase != TarmacTrouble::g_previousSmithPhase)
			{
				TarmacTrouble::g_previousSmithPhase = actor->actorPhase;
				TarmacTrouble::g_smithTintTimer = 60;
				actor->creatureRam->defenseMode = 4;
			}

			if (TarmacTrouble::g_smithEncounterState == TarmacTrouble::SMITH_ENCOUNTER_ACTIVE)
			{
				TarmacTrouble::g_smithTintTimer -= Renderer::g_frameDelta;
				if (TarmacTrouble::g_smithTintTimer < 0)
				{
					TarmacTrouble::g_smithTintTimer = 0;
					actor->creatureRam->defenseMode = 6;
				}
				else if (TarmacTrouble::g_smithTintToggle != 0)
				{
					actor->useTint = 1;
					actor->actorTint.r = 0x2000;
					actor->actorTint.g = 0x2000;
					actor->actorTint.b = 0x2000;
				}
				else
				{
					actor->useTint = 0;
				}
			}
			else
			{
				actor->useTint = 0;
			}

			if ((context->targetFlags & 1) != 0 && Nu3D::Math::IsWithinDistance(&g_buzzActor.posAngles.pos, &actor->pos, 200) != 0
				&& actor->primaryAnimIdx == 1)
			{
				Actor::SetAnimation(actor, 3, 0x18);
				actor->creatureRam->speedTarget = 0;
				TarmacTrouble::g_smithAttackTimer = 0x3F;
			}

			if (actor->primaryAnimIdx == 3 && (actor->animationFramePosition & (int32_t)0xFFFF0000) > 0x2E0000)
			{
				actor->movementCommandTimer = 0;
				actor->movementData = g_smithMovementData + 16;
				actor->creatureRam->speedTarget = 0x10;
				actor->actorFlags &= ~(Actor::ACTOR_FLAG_TRACKS_TARGET | Actor::ACTOR_FLAG_TARGETS_BUZZ);
			}

			if (TarmacTrouble::g_smithAttackTimer != 0)
			{
				TarmacTrouble::g_smithAttackTimer -= Renderer::g_frameDelta;
				if (TarmacTrouble::g_smithAttackTimer <= 0)
				{
					Vector4I effectPosition;
					effectPosition.x = 0xB4;
					effectPosition.y = -0x96;
					effectPosition.z = -0x32;
					Actor::ResolveBoneAttachmentPos(&effectPosition, actor, 4);
					Nu3D::Particles::ParticleInstance* particle =
						Nu3D::Particles::SpawnInstance(effectPosition.x, effectPosition.y, effectPosition.z, 0, -2, 0, actor->yawAngle << 2, 0, 0, 0x66);
					particle->discPitchAngle = -1;
					TarmacTrouble::g_smithAttackTimer = 0;
					AudioManager::PlaySoundEffect(0xA7, &actor->pos);
				}
			}

			if (TarmacTrouble::g_smithEncounterState == TarmacTrouble::SMITH_ENCOUNTER_ACTIVE)
			{
				g_hudActorAnimationFrame = (actor->actorPhase - 9) * 54 / 20;
				HUD::g_slideTimers[HUD::SLIDE_BOSS_STATUS] = 90;
			}

			if (actor->actorPhase < 10 && TarmacTrouble::g_smithEncounterState == TarmacTrouble::SMITH_ENCOUNTER_ACTIVE)
			{
				actor->movementData = g_smithMovementData + 45;
				actor->creatureRam->defenseMode = 4;
				actor->actorFlags &= ~(Actor::ACTOR_FLAG_TARGETS_BUZZ | Actor::ACTOR_FLAG_DAMAGES_BUZZ);
				actor->movementCommandTimer = 0;
				AudioManager::PlaySoundEffect(-2, &actor->pos);
				TarmacTrouble::g_smithEncounterState = TarmacTrouble::SMITH_ENCOUNTER_DEFEATED;
				g_hudActorAnimationFrame = 0;
				TarmacTrouble::g_smithAttackTimer = 0;
				actor->creatureRam->speedTarget = 0x10;
			}
		}

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
