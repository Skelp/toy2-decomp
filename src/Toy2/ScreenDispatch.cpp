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

// The screen dispatcher and the static screen it shows: retail holds
// 0x004381F0 and 0x00438520 as one object, in the order of this file.
namespace Toy2
{
	int32_t ShowStaticScreen(int32_t backdropIndex);

	// FUNCTION: TOY2 0x004381F0 [PROVISIONAL]
	int32_t ScreenDispatcher(int32_t index)
	{
		int32_t defaultFadeFramesRemaining;

		switch (index)
		{
			case 1: {
				int32_t caseOneFadeFrames = 160;
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

				SetBackdropByIndex(1);

				while (true)
				{
					Nu3D::Camera::FadeToTargetTint();

					if (caseOneFadeFrames > 0)
					{
						caseOneFadeFrames -= Renderer::g_frameDelta;

						if (caseOneFadeFrames <= 0)
							caseOneFadeFrames = 0;
					}

					if (caseOneFadeFrames <= 23 && caseOneFadeFrames + Renderer::g_frameDelta > 23)
						Nu3D::Camera::SetTint(0, 0, 0, 12);

					// This retail condition is unreachable because the frame count cannot satisfy both limits.
					if ((InputManager::g_curButtonsPressed & (INPUT_CANCEL | INPUT_SPIN | INPUT_JUMP | INPUT_FIRE)) != 0 && caseOneFadeFrames < 0
						&& caseOneFadeFrames > 23)
					{
						caseOneFadeFrames = 24;
					}
					else if (! caseOneFadeFrames)
						return 1;
				}
			}

			case 2:
				g_mainMenuState = MainMenu::Tick();
				return 1;

			case 4:
				PostGameRecap::Tick();
				return 1;

			case 5:
				GameOver::Tick();
				return 1;

			case 6: {
				if (! g_returnedToTitle && g_attractModeTimer >= 0)
				{
					int32_t caseSixFadeFrames = 2 * g_attractModeTimer;
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

					int32_t skipInputThreshold = caseSixFadeFrames - 120;

					if (! caseSixFadeFrames)
						return 1;

					while (true)
					{
						Nu3D::Camera::FadeToTargetTint();

						if (caseSixFadeFrames > 0)
						{
							caseSixFadeFrames -= Renderer::g_frameDelta;

							if (caseSixFadeFrames <= 0)
								caseSixFadeFrames = 0;
						}

						if (caseSixFadeFrames <= 23 && caseSixFadeFrames + Renderer::g_frameDelta > 23)
							Nu3D::Camera::SetTint(0, 0, 0, 12);

						if ((InputManager::g_curButtonsPressed & (INPUT_CANCEL | INPUT_SPIN | INPUT_JUMP | INPUT_FIRE)) != 0
							&& caseSixFadeFrames < skipInputThreshold && caseSixFadeFrames > 23)
						{
							caseSixFadeFrames = 24;
						}
						else if (! caseSixFadeFrames)
						{
							return 1;
						}
					}
				}

				defaultFadeFramesRemaining = 600;

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
				break;
			}

			case 8:
				g_mainMenuState = MainMenu::g_nextScreen - 1;
				return 1;

			case 9:
				MainMenu::ShowSettings();
				return 1;

			case 10:
				ShowStaticScreen(2);
				ShowStaticScreen(3);
				return 1;

			case 11:
				ShowCredits();
				return 1;

			default:
				return 1;
		}

		do
		{
			Nu3D::Camera::FadeToTargetTint();

			defaultFadeFramesRemaining -= Renderer::g_frameDelta;

			if (defaultFadeFramesRemaining > 0)
			{
				if (defaultFadeFramesRemaining > 23)
					continue;
			}
			else
			{
				defaultFadeFramesRemaining = 0;
			}

			if ((defaultFadeFramesRemaining + Renderer::g_frameDelta) > 23)
				Nu3D::Camera::SetTint(0, 0, 0, 12);

		} while (defaultFadeFramesRemaining);

		return 1;
	}

	// FUNCTION: TOY2 0x00438520 [MATCHED]
	int32_t ShowStaticScreen(int32_t backdropIndex)
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
		SetBackdropByIndex(backdropIndex);

		int32_t result = 0;
		int32_t fadeFrames = 600;
		int32_t threshold = g_pastInitialBoot != 0 ? 600 : 300;

		while (true)
		{
			Nu3D::Camera::FadeToTargetTint();
			SoftwareRenderer::SetBackdropScrollOverride(0, 0);
			Nullsub3();
			MainMenu::RenderMenu();

			if (fadeFrames > 0)
			{
				fadeFrames -= Renderer::g_frameDelta;
				if (fadeFrames <= 0)
					fadeFrames = 0;
			}

			if (fadeFrames <= 0x17)
			{
				if (Renderer::g_frameDelta + fadeFrames > 0x17)
					Nu3D::Camera::SetTint(0, 0, 0, 12);
			}

			if (g_attractModeTimer >= 0 && (InputManager::g_curButtonsPressed & 1) && fadeFrames > 0x17)
			{
				fadeFrames = 0x18;
				result = 1;
			}

			if ((InputManager::g_curButtonsPressed & 0xf000) != 0 && (InputManager::g_prevButtonsPressed & 0xf000) == 0 && fadeFrames < threshold
				&& fadeFrames > 0x17)
			{
				fadeFrames = 0x18;
			}
			else if (fadeFrames <= 0)
			{
				return result;
			}
		}
	}
}
