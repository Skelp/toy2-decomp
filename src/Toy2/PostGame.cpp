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

// The screens that follow a level: the recap, the save menu, the credits
// and the movie viewer. Retail holds 0x004398B0 through 0x0043A600 as one
// object, in the order of this file.
namespace Toy2
{
	// GLOBAL: TOY2 0x004F5C9C
	char g_saveGamePrompt[] = "SAVE GAME?";

	// GLOBAL: TOY2 0x004F5CA8
	char g_saveGameSeparator[] = "   /   ";

	// GLOBAL: TOY2 0x004F5CB0
	char g_yesText[] = "YES    ";

	// GLOBAL: TOY2 0x004F5CB8
	char g_noText[] = "    NO ";

	// GLOBAL: TOY2 0x004F5F54
	extern const char g_creditsText[] = {
#include "CreditsText.inc"
	};

	namespace MovieViewer
	{
		struct SpriteTile
		{
			uint8_t sheetIndex;
			uint8_t tileIndex;
		};

		struct MovieDefinition
		{
			SpriteTile thumbnail;
			SpriteTile unusedTile;
		};

		struct MenuItem
		{
			SpriteTile thumbnail;
			uint8_t movieIndex;
			uint8_t terminator;
		};

		// GLOBAL: TOY2 0x004F6860
		char g_selectPrompt[] = "press jump to select";

		// GLOBAL: TOY2 0x004F6E3C
		MovieDefinition g_movieDefinitions[20] = {
			{ { 0x42, 1 }, { 0x41, 3 } },
			{ { 0x43, 4 }, { 0x41, 3 } },
			{ { 0x43, 5 }, { 0x41, 3 } },
			{ { 0x43, 3 }, { 0x41, 3 } },
			{ { 0x43, 2 }, { 0x41, 3 } },
			{ { 0x41, 3 }, { 0x41, 3 } },
			{ { 0x42, 3 }, { 0x41, 3 } },
			{ { 0x42, 0 }, { 0x41, 3 } },
			{ { 0x43, 1 }, { 0x41, 3 } },
			{ { 0x41, 2 }, { 0x41, 3 } },
			{ { 0x41, 1 }, { 0x41, 3 } },
			{ { 0x41, 0 }, { 0x41, 3 } },
			{ { 0x44, 2 }, { 0x41, 3 } },
			{ { 0x44, 0 }, { 0x41, 3 } },
			{ { 0x44, 1 }, { 0x41, 3 } },
			{ { 0x44, 3 }, { 0x41, 3 } },
			{ { 0x43, 0 }, { 0x41, 3 } },
			{ { 0x42, 6 }, { 0x41, 3 } },
			{ { 0x42, 2 }, { 0x41, 3 } },
			{ { 0xFF, 0xFF }, { 0xFF, 0xFF } },
		};

		// GLOBAL: TOY2 0x004F6E8C
		uint8_t g_movieOrder[20] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 16, 12, 13, 14, 15, 17, 18, 0xFF };

		STATIC_ASSERT(sizeof(SpriteTile) == 2);
		STATIC_ASSERT(sizeof(MovieDefinition) == 4);
		STATIC_ASSERT(sizeof(MenuItem) == 4);
	}

	// GLOBAL: TOY2 0x0055A120
	int32_t g_screenMusicStarted;

	namespace PostGameRecap
	{
		// STUB: TOY2 0x004398B0
		void Tick() {}

	}
}

namespace Toy2
{
	namespace PostGameSaveMenu
	{
		// FUNCTION: TOY2 0x0043A130 [EFFECTIVE]
		int32_t Tick()
		{
			InputManager::g_curButtonsPressed = 0;
			InputManager::g_prevButtonsPressed = 0;
			MainMenu::g_fadeTimer = 0;
			MainMenu::g_nextScreen = 0;
			Nu3D::Camera::g_cameraTintRed = 0;
			Nu3D::Camera::g_cameraTintGreen = 0;
			Nu3D::Camera::g_cameraTintBlue = 0;
			Nu3D::Camera::SetTint(128, 128, 128, 12);
			SoftwareRenderer::SetBackdropScrollOverride(0, 0);
			Renderer::g_frameDelta = 1;
			SetBackdropByIndex(0);

			int32_t fadeTimer = 4000;
			int32_t saveRequested = 1;
			AudioManager::PlayMusicLooping(19);
			g_screenMusicStarted = 1;
			Nu3D::Camera::SetTint(128, 128, 128, 6);

			while (true)
			{
				Nu3D::Camera::FadeToTargetTint();
				SoftwareRenderer::SetBackdropScrollOverride(0, 0);

				if (fadeTimer > 1000)
				{
					if ((InputManager::g_curButtonsPressed & INPUT_LEFT) != 0 && saveRequested == 0)
					{
						saveRequested = 1;
						AudioManager::PlayOneShotSoundGlobal(1, 0x1200, 0x40, 0x60);
					}
					if ((InputManager::g_curButtonsPressed & INPUT_RIGHT) != 0 && saveRequested == 1)
					{
						saveRequested = 0;
						AudioManager::PlayOneShotSoundGlobal(1, 0x1200, 0x60, 0x40);
					}
				}

				if (saveRequested != 0)
					Renderer::Sprite::DrawTile(0x1C0, 0x70, 0x39, 1);
				else
					Renderer::Sprite::DrawTile(0x20, 0x70, 0x39, 0);

				Nullsub3();
				Renderer::Sprite::DrawWhiteText(LevelSelect::g_jumpToSelectTxt, 200, 256);
				Renderer::Sprite::DrawWhiteText(g_saveGamePrompt, 48, 256);
				Renderer::Sprite::DrawWhiteText(g_saveGameSeparator, 128, 256);
				if (saveRequested != 0)
				{
					Renderer::Sprite::DrawWhiteText(g_yesText, 128, 256);
					Renderer::DrawBitmapText(g_noText, 128, 256, 64, 64, 64, 0);
				}
				else
				{
					Renderer::Sprite::DrawWhiteText(g_noText, 128, 256);
					Renderer::DrawBitmapText(g_yesText, 128, 256, 64, 64, 64, 0);
				}

				Nullsub6();
				MainMenu::RenderMenu();

				if (fadeTimer > 0 && fadeTimer < 1000)
					fadeTimer -= Renderer::g_frameDelta;

				if ((InputManager::g_curButtonsPressed & INPUT_JUMP) != 0 && (InputManager::g_prevButtonsPressed & INPUT_JUMP) == 0 && fadeTimer > 0x17
					&& Nu3D::Camera::g_cameraTintRed == 128)
				{
					Nu3D::Camera::SetTint(0, 0, 0, 6);
					fadeTimer = 0x35;
					AudioManager::PlayOneShotSoundGlobal(0, 0x1200, 0x60, 0x60);
				}
				else if (fadeTimer <= 0)
				{
					AudioManager::StopAndWait();
					return saveRequested;
				}
			}
		}

	}
}

namespace Toy2
{
	// FUNCTION: TOY2 0x0043A380 [PROVISIONAL]
	int32_t ShowCredits()
	{
		const int32_t lineCount = 40;
		const int32_t lineLength = 64;
		char lines[lineCount][lineLength];
		const char* creditsCursor = g_creditsText;
		int32_t scrollPosition = 0;
		int32_t loadedLineCount = 0;

		for (int32_t line = 0; line < lineCount; ++line)
			lines[line][0] = '\0';

		InputManager::g_curButtonsPressed = 0;
		InputManager::g_prevButtonsPressed = 0;
		MainMenu::g_fadeTimer = 0;
		MainMenu::g_nextScreen = 0;
		Nu3D::Camera::g_cameraTintRed = 0;
		Nu3D::Camera::g_cameraTintGreen = 0;
		Nu3D::Camera::g_cameraTintBlue = 0;
		Nu3D::Camera::SetTint(128, 128, 128, 12);
		SoftwareRenderer::SetBackdropScrollOverride(0, 0);
		Renderer::g_frameDelta = 1;
		SetBackdropByIndex(0);
		MainMenu::g_menuClearColor.b = 0;
		MainMenu::g_menuClearColor.g = 0;
		MainMenu::g_menuClearColor.r = 0;

		int32_t creditsTimer = 4000;
		AudioManager::PlayMusicLooping(AudioManager::MUSIC_TRACK_CREDITS);
		g_screenMusicStarted = 1;
		Nu3D::Camera::SetTint(128, 128, 128, 6);

		int32_t backdropFramesRemaining = 600;
		int32_t backdropIndex = 0;

		do
		{
			Nu3D::Camera::FadeToTargetTint();

			int32_t visibleLineCount = scrollPosition / 128;
			if (visibleLineCount > loadedLineCount)
			{
				int32_t linesToLoad = visibleLineCount - loadedLineCount;
				int32_t ringIndex = loadedLineCount + 2;
				loadedLineCount += linesToLoad;

				do
				{
					int32_t characterIndex = 0;
					while (*creditsCursor != '~' && *creditsCursor != '\0')
					{
						lines[ringIndex % lineCount][characterIndex++] = *creditsCursor++;
					}

					if (*creditsCursor == '\0')
					{
						lines[ringIndex % lineCount][characterIndex] = '\0';
					}
					else
					{
						++creditsCursor;
						lines[ringIndex % lineCount][characterIndex] = '\0';
					}
					++ringIndex;
				} while (--linesToLoad != 0);
			}

			SoftwareRenderer::SetBackdropScrollOverride(0, 0);
			int32_t scrollPhase = (scrollPosition / 16) % 320;
			char* line = lines[0];
			for (int32_t lineY = 320; lineY < 640; lineY += 8)
			{
				Renderer::Sprite::DrawWhiteText(line, ((lineY - scrollPhase) % 320) - 33, 160);
				line += lineLength;
			}

			Renderer::Sprite::DrawBackdropTransition(&backdropFramesRemaining, &backdropIndex, 600);
			Nullsub6();
			scrollPosition += Renderer::g_frameDelta * 8;

			if (creditsTimer > 0 && creditsTimer < 1000)
				creditsTimer -= Renderer::g_frameDelta;

			if (((InputManager::g_curButtonsPressed & 0xf000) != 0 && (InputManager::g_prevButtonsPressed & 0xf000) == 0 || *creditsCursor == '\0')
				&& creditsTimer > 53 && Nu3D::Camera::g_cameraTintRed == 128)
			{
				creditsTimer = 53;
				Nu3D::Camera::SetTint(0, 0, 0, 6);
			}

			MainMenu::RenderMenu();
		} while (creditsTimer > 0);

		AudioManager::StopAndWait();
		MainMenu::g_menuClearColor.b = 32;
		MainMenu::g_menuClearColor.g = 32;
		MainMenu::g_menuClearColor.r = 32;
		return 32;
	}

}

namespace Toy2
{
	namespace MovieViewer
	{
		// FUNCTION: TOY2 0x0043A600 [PROVISIONAL]
		int32_t Tick(int32_t movieIndex)
		{
			InputManager::g_curButtonsPressed = 0;
			InputManager::g_prevButtonsPressed = 0;
			MainMenu::g_fadeTimer = 0;
			MainMenu::g_nextScreen = 0;
			Nu3D::Camera::g_cameraTintRed = 0;
			Nu3D::Camera::g_cameraTintGreen = 0;
			Nu3D::Camera::g_cameraTintBlue = 0;
			Nu3D::Camera::SetTint(128, 128, 128, 12);
			SoftwareRenderer::SetBackdropScrollOverride(0, 0);
			Renderer::g_frameDelta = 1;
			SetBackdropByIndex(0);

			SaveManager::g_save0Data.moviesUnlocked[0] = 1;
			int32_t selectedIndex = 0;
			if (g_movieOrder[0] != movieIndex)
			{
				uint8_t* movieOrder = g_movieOrder;
				do
				{
					if (movieOrder[1] == 0xFF)
						break;
					if (SaveManager::g_save0Data.moviesUnlocked[*movieOrder] != 0)
						selectedIndex++;
					movieOrder++;
				} while (*movieOrder != movieIndex);
			}

			int32_t selectionPosition = (g_movieOrder[selectedIndex] != 0xFF ? selectedIndex : 0) * 0x1000;
			MenuItem menuItems[48];
			int32_t menuItemCount = 0;
			for (int32_t definitionIndex = 0; g_movieDefinitions[definitionIndex].thumbnail.sheetIndex != 0xFF; definitionIndex++)
			{
				uint8_t orderedMovieIndex = g_movieOrder[definitionIndex];
				if (SaveManager::g_save0Data.moviesUnlocked[orderedMovieIndex] != 0)
				{
					menuItems[menuItemCount].thumbnail = g_movieDefinitions[orderedMovieIndex].thumbnail;
					menuItems[menuItemCount].movieIndex = orderedMovieIndex;
					menuItemCount++;
				}
			}
			menuItems[menuItemCount].thumbnail.sheetIndex = 0xFF;
			menuItems[menuItemCount].thumbnail.tileIndex = 0xFF;
			menuItems[menuItemCount].movieIndex = 0xFF;
			menuItems[menuItemCount].terminator = 0xFF;

			int32_t fadeTimer = 4000;
			int32_t scrollVelocity = 0;
			int32_t scrollPosition = selectionPosition;
			int32_t framePhase = 0;
			AudioManager::PlayMusicLooping(19);
			g_screenMusicStarted = 1;
			int32_t result = -1;
			Nu3D::Camera::SetTint(128, 128, 128, 6);
			int32_t rightTarget = selectionPosition + 0x400;
			int32_t leftTarget = selectionPosition - 0x400;

			while (true)
			{
				Nu3D::Camera::FadeToTargetTint();
				framePhase += Renderer::g_frameDelta;

				if (leftTarget > -0x400 && (framePhase & 0x3F) < 0x30)
					Renderer::Sprite::DrawTile(0x10, 0x70, 0x39, 0);
				if (menuItems[(selectionPosition >> 12) + 1].thumbnail.sheetIndex != 0xFF && (framePhase & 0x3F) < 0x30)
					Renderer::Sprite::DrawTile(0x110, 0x70, 0x39, 1);

				Renderer::Sprite::DrawTile(0, 0, 0x3C, 0);
				Renderer::Sprite::DrawTile(0x80, 0, 0x3C, 1);
				Renderer::Sprite::DrawTile(0x100, 0, 0x3D, 0);

				int32_t thumbnailOffset = -((scrollPosition >> 5) & 0x7F);
				Renderer::Sprite::DrawClipped(thumbnailOffset + 0x10C, 0x50, 0xD0, 0x108, 0x40, 0);
				Renderer::Sprite::DrawClipped(thumbnailOffset + 0x72, 0x50, 0x38, 0x70, 0x40, 0);

				int32_t centeredItemIndex = (scrollPosition + 0x800) >> 12;
				thumbnailOffset = -(((scrollPosition - 0x800) >> 5) & 0x7F);
				Renderer::Sprite::DrawClipped(thumbnailOffset + 0x10C,
					0x50,
					0xD0,
					0x108,
					menuItems[centeredItemIndex].thumbnail.sheetIndex,
					menuItems[centeredItemIndex].thumbnail.tileIndex);
				Renderer::Sprite::DrawClipped(thumbnailOffset + 0x72,
					0x50,
					0x38,
					0x70,
					menuItems[centeredItemIndex].thumbnail.sheetIndex,
					menuItems[centeredItemIndex].thumbnail.tileIndex);

				int16_t conveyorPosition = (int16_t)(-(scrollPosition >> 6) % 0x140);
				Renderer::Sprite::DrawTile(conveyorPosition, 0, 0x3E, 0);
				Renderer::Sprite::DrawTile(conveyorPosition + 0x80, 0, 0x3E, 1);
				Renderer::Sprite::DrawTile(conveyorPosition + 0x100, 0, 0x3F, 0);

				conveyorPosition = (int16_t)(-((scrollPosition >> 6) % 0x140));
				Renderer::Sprite::DrawTile(conveyorPosition + 0x140, 0, 0x3E, 0);
				Renderer::Sprite::DrawTile(conveyorPosition + 0x1C0, 0, 0x3E, 1);
				Renderer::Sprite::DrawTile(conveyorPosition + 0x240, 0, 0x3F, 0);

				Nullsub3();
				MainMenu::RenderMenu();

				if (fadeTimer > 1000)
				{
					if ((InputManager::g_curButtonsPressed & INPUT_LEFT) != 0 && (InputManager::g_prevButtonsPressed & INPUT_LEFT) == 0 && leftTarget > -0x400
						&& fadeTimer > 0x17 && abs(scrollPosition - selectionPosition) < 0x800)
					{
						selectionPosition -= 0x1000;
						leftTarget -= 0x1000;
						rightTarget -= 0x1000;
						AudioManager::PlayOneShotSoundGlobal(1, 0x1200, 0x40, 0x60);
					}

					if ((InputManager::g_curButtonsPressed & INPUT_RIGHT) != 0 && (InputManager::g_prevButtonsPressed & INPUT_RIGHT) == 0
						&& menuItems[(selectionPosition >> 12) + 1].thumbnail.sheetIndex != 0xFF && fadeTimer > 0x17
						&& abs(scrollPosition - selectionPosition) < 0x800)
					{
						selectionPosition += 0x1000;
						leftTarget += 0x1000;
						rightTarget += 0x1000;
						AudioManager::PlayOneShotSoundGlobal(1, 0x1200, 0x60, 0x40);
					}

					if (scrollVelocity < 0)
					{
						scrollVelocity += Renderer::g_frameDelta * 8;
						if (scrollVelocity > 0)
							scrollVelocity = 0;
					}
					else if (scrollVelocity > 0)
					{
						scrollVelocity -= Renderer::g_frameDelta * 8;
						if (scrollVelocity < 0)
							scrollVelocity = 0;
					}

					if (scrollPosition < leftTarget)
						scrollVelocity += Renderer::g_frameDelta * 0x10;
					if (scrollPosition > rightTarget)
						scrollVelocity -= Renderer::g_frameDelta * 0x10;

					if (scrollVelocity > 0x80)
						scrollVelocity = 0x80;
					else if (scrollVelocity < -0x80)
						scrollVelocity = -0x80;
					scrollPosition += scrollVelocity;
				}

				Renderer::Sprite::DrawWhiteText(g_selectPrompt, 0xD4, 0xA0);
				Nullsub6();

				if (fadeTimer > 0 && fadeTimer < 1000)
					fadeTimer -= Renderer::g_frameDelta;

				if ((InputManager::g_curButtonsPressed & INPUT_CANCEL) != 0 && (InputManager::g_prevButtonsPressed & INPUT_CANCEL) == 0 && fadeTimer > 0x17
					&& Nu3D::Camera::g_cameraTintRed == 128)
				{
					fadeTimer = 0x35;
					AudioManager::PlayOneShotSoundGlobal(2, 0x1200, 0x60, 0x60);
					result = -1;
					Nu3D::Camera::SetTint(0, 0, 0, 6);
				}

				if ((InputManager::g_curButtonsPressed & INPUT_JUMP) != 0 && (InputManager::g_prevButtonsPressed & INPUT_JUMP) == 0 && fadeTimer > 0x17
					&& Nu3D::Camera::g_cameraTintRed == 128)
				{
					fadeTimer = 0x35;
					AudioManager::PlayOneShotSoundGlobal(0, 0x1200, 0x60, 0x60);
					result = menuItems[(scrollPosition + 0x800) >> 12].movieIndex;
					Nu3D::Camera::SetTint(0, 0, 0, 6);
				}
				else if (fadeTimer <= 0)
				{
					AudioManager::StopAndWait();
					return result;
				}
			}
		}
	}
}
