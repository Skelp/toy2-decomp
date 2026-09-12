#include "Toy2/Toy2.h"
#include "Toy2/Gadget.h"
#include "Toy2/Ini.h"
#include "Toy2/Screens.h"
#include "D3DApp/d3dapp.h"
#include "D3DApp/d3dappi.h"
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
#include "Renderer/SpriteSheets.h"
#include "Renderer/Glue.h"
#include "Renderer/Shadows.h"
#include "Toy2/KiteTail.h"
#include "SaveManager.h"
#include "Random.h"
#include "Toy2/LevelSelect.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Buzz.h"
#include "Toy2/Levels.h"
#include "Toy2/MainMenu.h"
#include "Toy2/Actor.h"
#include "Toy2/Animation.h"
#include "Toy2/PoleRecord.h"
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
#include "Nu3D/Light.h"
#include "Nu3D/Scene.h"
#include "Nu3D/SoftwareProjectionPoint.h"
#include "Renderer/Renderer.h"
#include "AudioManager/AudioManager.h"
#include "NGNLoader/NGNLoader.h"
#include <WINDOWS.H>
#include <STDIO.H>
#include <STRING.H>
#include <DINPUT.H>
#include <LIMITS.H>
#include <MATH.H>
#include <STDLIB.H>
#include <Numerics.h>
#include "Toy2/Toy2Internal.h"
#include "Toy2/Toy2Internal.h"

// The head-up display and the pause menu it opens: retail holds 0x0049FC60
// and 0x0049FD40 as one object, in the order of this file.
namespace Toy2
{
	// GLOBAL: TOY2 0x00502750
	char g_pauseSoundVolumeText[16] = "sfx **********";

	// GLOBAL: TOY2 0x00502760
	char g_pauseMusicVolumeText[16] = "bgm **********";

	// GLOBAL: TOY2 0x00830C94
	int32_t g_tokenPromptSelection;

	// GLOBAL: TOY2 0x00830CA0
	int32_t g_coinSpriteFrame;

	namespace HUD
	{
		// FUNCTION: TOY2 0x0049FC60 [MATCHED]
		int32_t TickSlide(int32_t slideIndex)
		{
			if (g_isPaused == 0)
			{
				int16_t timer = g_slideTimers[slideIndex];

				if (timer != 0 && (g_gameplayStateFlags & GAMEPLAY_STATE_CUTSCENE_ACTIVE) == 0)
				{
					if (g_slideAngles[slideIndex] < 0x400)
					{
						g_slideAngles[slideIndex] += Renderer::g_frameDelta << 5;
						if (g_slideAngles[slideIndex] >= 0x400)
							g_slideAngles[slideIndex] = 0x400;
					}

					if (timer < 1000)
					{
						g_slideTimers[slideIndex] = timer - (int16_t)Renderer::g_frameDelta;
						if (g_slideTimers[slideIndex] <= 0)
							g_slideTimers[slideIndex] = 0;
					}
				}
				else if (g_slideAngles[slideIndex] > 0)
				{
					g_slideAngles[slideIndex] -= Renderer::g_frameDelta << 5;
					if (g_slideAngles[slideIndex] <= 0)
						g_slideAngles[slideIndex] = 0;
				}
			}

			int16_t angle = g_slideAngles[slideIndex];
			if (angle == 0)
				return 1;

			return (Numerics::g_sinCosLUT[angle] >> 8) - 0x40;
		}

	}
}

namespace Toy2
{
	// FUNCTION: TOY2 0x0049FD40 [PROVISIONAL]
	void RenderHUD()
	{
		if ((InputManager::g_directionInputState & INPUT_TARGET_LOCK) != 0)
		{
			HUD::g_slideTimers[0] = 90;
			HUD::g_slideTimers[1] = 90;
			HUD::g_slideTimers[2] = 90;
		}
		HUD::g_slideTimers[10] = 5;

		if (g_spinHoverTimer != 0)
			HUD::g_slideTimers[3] = 120;
		if (g_gunFireTimer != 0 || g_poweredLaserCharge != 0 || g_grappleCharges != 0 || g_discLauncherAmmo != 0)
			HUD::g_slideTimers[4] = 60;
		if (g_levelObjectiveProgress > 0)
			HUD::g_slideTimers[5] = 5;
		if (HUD::g_challengeState > HUD::CHALLENGE_STATE_INACTIVE && HUD::g_challengeState < HUD::CHALLENGE_STATE_COMPLETE)
			HUD::g_slideTimers[6] = 5;
		if (Gadget::g_unlockNodeState < 0)
			HUD::g_slideTimers[8] = 5;
		if (g_buzzActor.coinsCollected >= 50 && Actor::g_coinTokenAwarded == 0)
			HUD::g_slideTimers[9] = 5;

		if (Collectables::g_tokenCollectionState == Collectables::TOKEN_COLLECTION_STATE_CUTSCENE)
		{
			if (g_isPaused == 0)
			{
				Renderer::DrawString(94, "you have collected a token!", 0x80, 0x80, 0, 0);
				int32_t selectionBrightness = g_framePulsePhases.sixtyFourTick;
				if (selectionBrightness > 31)
					selectionBrightness = 63 - selectionBrightness;
				selectionBrightness = selectionBrightness * 2 + 0x40;

				if ((InputManager::g_curButtonsPressed & INPUT_DOWN) != 0 && (InputManager::g_prevButtonsPressed & INPUT_DOWN) == 0
					&& g_tokenPromptSelection == 0)
				{
					g_tokenPromptSelection = 1;
					AudioManager::PlaySoundEffect(0x3F, NULL);
				}
				if ((InputManager::g_curButtonsPressed & INPUT_UP) != 0 && (InputManager::g_prevButtonsPressed & INPUT_UP) == 0 && g_tokenPromptSelection == 1)
				{
					g_tokenPromptSelection = 0;
					AudioManager::PlaySoundEffect(0x3F, NULL);
				}

				if (g_tokenPromptSelection == 0)
				{
					Renderer::DrawString(106, "keep on playing", selectionBrightness, selectionBrightness, selectionBrightness, 0);
					Renderer::DrawString(114, "exit level?", 0x80, 0x80, 0x80, 0);
				}
				else
				{
					Renderer::DrawString(106, "keep on playing", 0x80, 0x80, 0x80, 0);
					Renderer::DrawString(114, "exit level?", selectionBrightness, selectionBrightness, selectionBrightness, 0);
				}
				Renderer::DrawString(124, "jump to select", 0x80, 0x80, 0x80, 0);
				Renderer::DrawBlackBorderBox(77, 91, 0x166000, 0x2D000, 0, 0, 0x80);

				if ((InputManager::g_curButtonsPressed & INPUT_JUMP) != 0 && (InputManager::g_prevButtonsPressed & INPUT_JUMP) == 0)
				{
					AudioManager::PlaySoundEffect(0x3E, NULL);
					if (g_tokenPromptSelection == 1)
					{
						Collectables::g_tokenCollectionState = 3;
						g_levelTransition = 1;
						g_levelTransitionTimer = 1000;
						AudioManager::PlaySoundEffect(0xA9, NULL);
					}
					else
					{
						g_gameplayStateFlags &= ~1;
						g_buzzActor.actorFlags &= ~Buzz::ACTOR_FLAG_LOCK_FACING;
						Collectables::g_tokenCollectionState = 0;
						Camera::g_cutsceneDuration = 0;
						Camera::g_cutsceneInputLockTimer = 64;
						g_buzzActor.airborneMode = 5;
					}
				}
			}
		}

		if (g_isPaused != 0 && g_pauseCheatTimer != 0)
		{
			g_pauseMenuBlinkTimer &= 63;
			int32_t selectionBrightness = g_pauseMenuBlinkTimer;
			if (selectionBrightness > 31)
				selectionBrightness = 63 - selectionBrightness;
			selectionBrightness = selectionBrightness * 2 + 0x40;

			uint32_t red[4] = { 0x80, 0x80, 0x80, 0x80 };
			uint32_t green[4] = { 0x80, 0x80, 0x80, 0x80 };
			uint32_t blue[4] = { 0, 0, 0, 0 };
			if (InputManager::g_directionalInputCount == 3 && selectionBrightness > 0x60)
				Renderer::DrawString(200, "insert controller", 0x80, 0x80, 0, 0);

			red[g_pauseMenuSelection] = selectionBrightness;
			green[g_pauseMenuSelection] = selectionBrightness;
			blue[g_pauseMenuSelection] = 0;
			if (g_pauseMenuState == PAUSE_MENU_MAIN)
			{
				Renderer::DrawString(84, "pause menu", 0x80, 0x80, 0, 0);
				Renderer::DrawString(96, "continue", red[0], green[0], blue[0], 0);
				Renderer::DrawString(104, "camera mode", red[1], green[1], blue[1], 0);
				Renderer::DrawString(112, "volume control", red[2], green[2], blue[2], 0);
				Renderer::DrawString(120, "exit level", red[3], green[3], blue[3], 0);
			}
			if (g_pauseMenuState == PAUSE_MENU_CAMERA)
			{
				Renderer::DrawString(84, "camera mode", 0x80, 0x80, 0, 0);
				Renderer::DrawString(100, "passive camera", red[0], green[0], blue[0], 0);
				Renderer::DrawString(108, "active camera", red[1], green[1], blue[1], 0);
			}
			if (g_pauseMenuState == PAUSE_MENU_VOLUME)
			{
				Renderer::DrawString(84, "volume control", 0x80, 0x80, 0, 0);
				Renderer::DrawString(100, g_pauseSoundVolumeText, red[0], green[0], blue[0], 0);
				Renderer::DrawString(108, g_pauseMusicVolumeText, red[1], green[1], blue[1], 0);
			}
			if (g_pauseMenuState == PAUSE_MENU_QUIT)
			{
				Renderer::DrawString(84, "are you sure?", 0x80, 0x80, 0, 0);
				Renderer::DrawString(100, "no", red[0], green[0], blue[0], 0);
				Renderer::DrawString(108, "yes", red[1], green[1], blue[1], 0);
			}
			if (g_pauseMenuState == PAUSE_MENU_SECRET)
			{
				Renderer::DrawString(84, "pause menu", 0x80, 0x80, 0, 0);
				Renderer::DrawString(100, "continue", red[0], green[0], blue[0], 0);
				Renderer::DrawString(108, "exit game", red[1], green[1], blue[1], 0);
			}
			Renderer::DrawString(134, "jump to select", 0x80, 0x80, 0x80, 0);
			Renderer::DrawBlackBorderBox(160, 81, 0xC0000, 0x41000, 0x80, 0, 0x80);
		}

		int32_t livesOffset = HUD::TickSlide(0);
		if (livesOffset <= 0)
		{
			Renderer::Sprite::DrawScaledFixed(16, (int16_t)livesOffset + 16, 10, 0, 0x80, 0x80, 0x80, 0xFF, 0x800, 0x800);
			Renderer::Sprite::DrawScaled(60, (int16_t)livesOffset + 32, 20, g_buzzActor.lives + 26, 0xFF, 0xFF, 0xFF, 0x60, 0x1000, 0x1000);
		}

		int32_t challengeOffset = HUD::TickSlide(6);
		if (challengeOffset <= 0)
		{
			int16_t iconXOffset = 0;
			int16_t digitsXOffset = 0;
			int32_t colour;
			int16_t y = (int16_t)challengeOffset;
			if (AndysHouse::g_raceCheckpointPassCount >= 100)
			{
				if (g_isPaused == 0)
					AudioManager::PlaySoundEffect(0x5E, NULL);
				if (g_specialPickupCount >= 0)
				{
					iconXOffset = -20;
					digitsXOffset = -32;
					if (g_specialPickupCount == 5)
					{
						int32_t pulse = g_framePulsePhases.thirtyTwoTick & 31;
						if (pulse > 15)
							pulse = 31 - pulse;
						colour = pulse * 4 + 80;
						if (g_framePulsePhases.thirtyTwoTick >= 16)
							Renderer::Sprite::DrawTile(302, y + 32, 3, (int16_t)g_specialPickupCount);
					}
					else
					{
						colour = 0x80;
						Renderer::Sprite::DrawTile(302, y + 32, 3, (int16_t)g_specialPickupCount);
					}
					Renderer::Sprite::DrawColouredFixed(160, y + 16, 51, 0, colour, colour, colour);
				}
				int32_t checkpointTime = AndysHouse::g_raceCheckpointPassCount - 100;
				int16_t tens = (int16_t)(checkpointTime / 10);
				Renderer::Sprite::DrawTile(digitsXOffset + 243, y + 24, 3, tens);
				Renderer::Sprite::DrawTile(digitsXOffset + 257, y + 24, 3, (int16_t)checkpointTime - tens * 10);
				colour = 0x80;
			}
			else
			{
				Renderer::Sprite::DrawTile(250, y + 24, 3, 3 - (int16_t)AndysHouse::g_raceCheckpointPassCount);
				if (AndysHouse::g_raceCheckpointPassCount == 2)
				{
					int32_t pulse = g_framePulsePhases.sixtyFourTick;
					if (pulse > 31)
						pulse = 63 - pulse;
					colour = pulse * 4 + 20;
				}
				else
				{
					colour = 0x80;
				}
			}
			Renderer::Sprite::DrawColouredFixed(iconXOffset + 140, y + 16, 26, 0, colour, colour, colour);
		}

		int32_t healthOffset = HUD::TickSlide(1);
		if (healthOffset <= 0)
		{
			int16_t y = (int16_t)healthOffset + 16;
			Renderer::Sprite::DrawScaledFixed(272, y, 11, 0, 0x80, 0x80, 0x80, 0xFF, 0x800, 0x800);
			int32_t healthLength = g_buzzActor.health * 2 + 2;
			if (healthLength <= 4 && g_framePulsePhases.sixteenTick < 8)
				healthLength = 0;
			Renderer::Sprite::DrawScaled(
				476, (int16_t)healthOffset - (int16_t)healthLength + 47, 6, 0, 0x80, healthLength * 6, 0, 0x60, 0x5000, healthLength << 12);
			Renderer::Sprite::DrawTiledFixed(296, y, 12, 0);
		}

		int32_t objectiveOffset = HUD::TickSlide(5);
		if (healthOffset == 1)
			healthOffset = -64;
		int32_t rightColumnOffset = (healthOffset + 64) / 2;
		if (objectiveOffset <= 0)
		{
			int32_t colour;
			if (g_levelObjectiveProgress == 5)
			{
				if (g_previousLevelObjectiveProgress != 5)
					AudioManager::StartSoundSequenceOnActor(-5, &g_buzzActor.posAngles.pos);
				int32_t pulse = (g_framePulsePhases.thirtyTwoTick - 16) & 31;
				if (pulse > 15)
					pulse = 31 - pulse;
				colour = pulse * 4 + 80;
			}
			else
			{
				colour = 0x80;
			}
			if (g_levelObjectiveProgress != 5 || g_framePulsePhases.thirtyTwoTick < 16)
			{
				Renderer::Sprite::DrawScaled(474 - (rightColumnOffset << 9) / 320,
					(int16_t)objectiveOffset + 32,
					20,
					g_levelObjectiveProgress + 26,
					0xFF,
					0xFF,
					0xFF,
					0x60,
					0x1000,
					0x1000);
			}
			Renderer::Sprite::DrawColouredFixed(272 - rightColumnOffset, (int16_t)objectiveOffset + 16, 50, 0, colour, colour, colour);
		}
		g_previousLevelObjectiveProgress = g_levelObjectiveProgress;

		int32_t gadgetOffset = HUD::TickSlide(8);
		if (objectiveOffset == 1)
			objectiveOffset = -64;
		rightColumnOffset += (objectiveOffset + 64) / 2;
		if (gadgetOffset <= 0)
		{
			int32_t pulse = (g_framePulsePhases.thirtyTwoTick + 8) & 31;
			if (pulse > 15)
				pulse = 31 - pulse;
			int32_t colour = pulse * 4 + 80;
			Renderer::Sprite::DrawColouredFixed(272 - rightColumnOffset, (int16_t)gadgetOffset + 16, 27, 0, colour, colour, colour);
		}

		int32_t tokenOffset = HUD::TickSlide(9);
		if (gadgetOffset == 1)
			gadgetOffset = -64;
		rightColumnOffset += (gadgetOffset + 64) / 2;
		if (tokenOffset <= 0)
		{
			int32_t pulse = (g_framePulsePhases.thirtyTwoTick - 8) & 31;
			if (pulse > 15)
				pulse = 31 - pulse;
			int32_t colour = pulse * 4 + 80;
			Renderer::Sprite::DrawColouredFixed(272 - rightColumnOffset, (int16_t)tokenOffset + 16, 32, 0, colour, colour, colour);
		}

		int32_t coinsOffset = HUD::TickSlide(2);
		if (coinsOffset <= 0)
		{
			g_coinSpriteFrame += Renderer::g_frameDelta;
			if (g_coinSpriteFrame >= 24)
				g_coinSpriteFrame = 0;
			int16_t y = 224 - (int16_t)coinsOffset;
			Renderer::Sprite::DrawScaled(26, y, 16, g_coinSpriteFrame / 2, 0x80, 0x80, 0x80, 0x60, 0xCCD, 0x800);
			Renderer::Sprite::DrawScaled(51, y, 20, g_buzzActor.coinsCollected / 10 + 26, 0xFF, 0xFF, 0xFF, 0x60, 0x1000, 0x1000);
			Renderer::Sprite::DrawScaled(
				63, y, 20, g_buzzActor.coinsCollected - g_buzzActor.coinsCollected / 10 * 10 + 26, 0xFF, 0xFF, 0xFF, 0x60, 0x1000, 0x1000);
		}

		int32_t spinOffset = HUD::TickSlide(3);
		if (spinOffset <= 0)
		{
			int16_t yOffset = -(int16_t)spinOffset;
			Renderer::Sprite::DrawScaledFixed(272, yOffset + 222, 14, 0, 0x80, 0x80, 0x80, 0xFF, 0x800, 0x800);
			int32_t spinScale = 0;
			int32_t flashing = 0;
			if (g_spinHoverTimer > 0)
				spinScale = g_spinHoverTimer / 2;
			if (g_spinHoverTimer <= -120)
			{
				spinScale = -(g_spinHoverTimer + 120) / 6;
				flashing = 1;
			}
			if (g_spinHoverTimer >= 60)
				flashing = 1;
			uint32_t red = 0x80;
			uint32_t green = 0x80;
			if (g_framePulsePhases.eightTick < 4 && flashing)
			{
				red = 0;
				green = 0;
			}
			Renderer::Sprite::DrawScaled(438, yOffset + 218, 6, 0, red, green, 0, 0x60, spinScale * 0x1838, 0x3000);
			Renderer::Sprite::DrawTiledFixed(272, yOffset + 216, 13, 0);
		}

		int32_t weaponOffset = HUD::TickSlide(4);
		if (weaponOffset <= 0)
		{
			if (spinOffset == 1)
				spinOffset = -64;
			int16_t yOffset = -(int16_t)weaponOffset;
			int32_t leftColumnOffset = (spinOffset + 64) / 2;
			Renderer::Sprite::DrawScaledFixed(272 - leftColumnOffset, yOffset + 224, 15, 0, 0x80, 0x80, 0x80, 0xFF, 0x800, 0x800);

			int32_t ammoYOffset = -7;
			int32_t ammoCount = 0;
			int32_t ammoIcon = 0;
			if (g_discLauncherAmmo != 0)
			{
				ammoIcon = 28;
				ammoCount = g_discLauncherAmmo;
				ammoYOffset = 0;
			}
			if (g_grappleCharges != 0)
			{
				ammoIcon = 29;
				ammoCount = g_grappleCharges;
			}
			if (ammoCount != 0)
			{
				int16_t ammoY = ammoYOffset - (int16_t)weaponOffset;
				int32_t tens = ammoCount / 10;
				Renderer::Sprite::DrawScaledFixed(290 - leftColumnOffset, ammoY + 216, 20, tens + 26, 0xFF, 0xFF, 0xFF, 0x60, 0x800, 0x800);
				Renderer::Sprite::DrawScaledFixed(297 - leftColumnOffset, ammoY + 216, 20, ammoCount - tens * 10 + 26, 0xFF, 0xFF, 0xFF, 0x60, 0x800, 0x800);
				if (ammoIcon == 28)
					Renderer::Sprite::DrawScaled(438 - (leftColumnOffset << 9) / 320, ammoY + 214, 28, 0, 0x80, 0x80, 0x80, 0x60, 0xC00, 0x800);
				else
					Renderer::Sprite::DrawColouredFixed(274 - leftColumnOffset, ammoY + 206, 33, 0, 0x80, 0x60, 0x40);
			}

			if (ammoYOffset != 0)
			{
				if (g_poweredLaserCharge != 0)
				{
					Renderer::Sprite::DrawScaled(438 - (leftColumnOffset << 9) / 320,
						yOffset + 218,
						6,
						0,
						0,
						g_poweredLaserCharge / 16 + 64,
						0,
						0x60,
						g_poweredLaserCharge * 0x98,
						0x3000);
				}
				else
				{
					int32_t flashOffset = 0;
					if (g_gunChargeTimer == 64 && g_framePulsePhases.eightTick < 4)
						flashOffset = 0x80;
					Renderer::Sprite::DrawScaled(438 - (leftColumnOffset << 9) / 320,
						yOffset + 218,
						6,
						0,
						0x80 - flashOffset,
						g_gunChargeTimer * 2 - flashOffset,
						0,
						0x60,
						(g_gunChargeTimer * 0x1644) / 2,
						0x3000);
				}
				Renderer::Sprite::DrawTiledFixed(272 - leftColumnOffset, yOffset + 216, 13, 0);
			}
		}

		int32_t bossOffset = HUD::TickSlide(7);
		if (bossOffset <= 0)
		{
			if (g_hudActorAnimationFrame < 0)
				g_hudActorAnimationFrame = 0;
			else if (g_hudActorAnimationFrame > 54)
				g_hudActorAnimationFrame = 54;

			if (g_hudActorAnimationFrame > 10 || g_framePulsePhases.sixteenTick < 8)
			{
				Renderer::Sprite::DrawScaled(
					228, (int16_t)bossOffset + 20, 6, 0, 0x80, (uint8_t)(g_hudActorAnimationFrame * 2), 0, 0x60, g_hudActorAnimationFrame << 12, 0x4000);
			}
			Renderer::Sprite::DrawScaled(224, (int16_t)bossOffset + 16, 13, 0, 0x80, 0x80, 0x80, 0x60, 0x2000, 0x2000);
		}

		if (g_demoMode == 1 && g_framePulsePhases.sixtyFourTick < 32)
			Renderer::DrawString(112, "demo mode", 0x80, 0x80, 0, 0);
	}
}
