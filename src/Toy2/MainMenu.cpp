#include "Toy2/MainMenu.h"
#include "Toy2/Toy2.h"
#include "AudioManager/AudioManager.h"
#include "InputManager.h"
#include "Nu3D/Camera.h"
#include "SaveManager.h"
#include "SoftwareRenderer.h"
#include "Renderer/Renderer.h"
#include "Renderer/Sprite.h"
#include "Nullsub.h"

namespace Renderer
{
	void DrawMenuText(int16_t yPos, char* text, int32_t dimmed);
	void DrawMenuTextScaled(int32_t xPos, int32_t yPos, char* text, int32_t dimmed, int32_t alignment, int32_t scale);
}

namespace Toy2
{
	uint8_t ShowControlConfig();
	void ShowGraphicsConfig();

	namespace MainMenu
	{
		// GLOBAL: TOY2 0x0053CA60
		int32_t g_fadeTimer;

		// GLOBAL: TOY2 0x0053E4A8
		int32_t g_nextScreen;

		// GLOBAL: TOY2 0x00559E84
		RGB32 g_menuClearColor;

		// GLOBAL: TOY2 0x004F6BD0
		int32_t g_settingsCursorYPositions[6] = { -1, 65, 85, 105, 125, 145 };

		// GLOBAL: TOY2 0x0055A0E8
		int32_t g_settingsSaveValue1;

		// GLOBAL: TOY2 0x0055A0EC
		int32_t g_settingsSaveValue2;

		// FUNCTION: TOY2 0x00441980 [PROVISIONAL]
		void RenderMenu()
		{
			Toy2::ProcessMiscEventsEx();

			RGBA color;
			color.a = -1;
			color.b = g_menuClearColor.r;
			color.g = g_menuClearColor.g;
			color.r = g_menuClearColor.b;

			Toy2::g_showBlackFrames = 1;
			Renderer::Sprite::g_parallaxDepthZPos = 0.80000001;
			Renderer::ClearScreen(color, 2);

			if (Renderer::BeginScene())
			{
				Renderer::ResetParallax();
				Renderer::RenderParallaxBackground(0);

				Renderer::DrawTintOverlay();

				Renderer::FlushRenderQueues();
				Renderer::Sprite::DrawQueuedSprite();
				DevDraw::DrawSlots();

				Renderer::DoFrameDelay(0);
				Renderer::EndScene(1);
			}
		}

		// FUNCTION: TOY2 0x00437C40 [PROVISIONAL]
		int32_t Draw()
		{
			InputManager::g_curButtonsPressed = 0;
			InputManager::g_prevButtonsPressed = 0;

			g_fadeTimer = 0;
			g_nextScreen = 0;

			Nu3D::Camera::g_cameraTintBlue = 0;
			Nu3D::Camera::g_cameraTintGreen = 0;
			Nu3D::Camera::g_cameraTintRed = 0;

			Nu3D::Camera::SetTint(128, 128, 128, 12);
			SoftwareRenderer::SetBackdropScrollOverride(0, 0);
			Renderer::g_frameDelta = 1;
			Toy2::SetBackdropByIndex(4);

			int32_t selectedY = 120;
			int32_t idleTimer = 0;
			int32_t cursorY = 120;
			AudioManager::PlayMusicOneShot(19);

			while (! g_nextScreen || g_fadeTimer)
			{
				Nu3D::Camera::FadeToTargetTint();

				int32_t frameDelta = Renderer::g_frameDelta;

				if (g_fadeTimer > 0)
				{
					g_fadeTimer -= Renderer::g_frameDelta;

					if (g_fadeTimer <= 0)
						g_fadeTimer = 0;
				}

				idleTimer += Renderer::g_frameDelta;

				if (! g_nextScreen && cursorY == selectedY)
				{
					if ((InputManager::g_curButtonsPressed & INPUT_JUMP) == 0 || (InputManager::g_prevButtonsPressed & INPUT_JUMP) != 0 || idleTimer <= 30)
					{
						if ((InputManager::g_curButtonsPressed & INPUT_DOWN) != 0 && (InputManager::g_prevButtonsPressed & INPUT_DOWN) == 0 && selectedY < 200)
						{
							selectedY += 20;
							AudioManager::PlayOneShotSoundGlobal(1, 4608, 80, 80);

							frameDelta = Renderer::g_frameDelta;
						}

						if ((InputManager::g_curButtonsPressed & INPUT_UP) != 0 && (InputManager::g_prevButtonsPressed & INPUT_UP) == 0 && selectedY > 120)
						{
							selectedY -= 20;
							AudioManager::PlayOneShotSoundGlobal(1, 4608, 80, 80);
						}
					}
					else
					{
						if (cursorY == 120)
						{
							g_nextScreen = 2;
						}
						else if (cursorY == 140)
						{
							g_nextScreen = 3;
						}
						else if (cursorY == 160)
						{
							g_nextScreen = 4;
						}
						else if (cursorY == 180)
						{
							g_nextScreen = 5;
						}
						else if (cursorY == 200)
						{
							exit(-1);
						}

						g_fadeTimer = 23;
						Nu3D::Camera::SetTint(0, 0, 0, 12);
						AudioManager::PlayOneShotSoundGlobal(0, 4608, 80, 80);
					}
				}

				frameDelta = Renderer::g_frameDelta;

				if (cursorY < selectedY)
				{
					cursorY += 2 * frameDelta;

					if (cursorY >= selectedY)
						cursorY = selectedY;
				}
				else if (cursorY > selectedY)
				{
					cursorY -= 2 * frameDelta;

					if (cursorY <= selectedY)
						cursorY = selectedY;
				}

				int32_t bounce = idleTimer & 63;

				if (bounce > 31)
					bounce = 63 - bounce;

				int32_t spriteScale = 4 * bounce;

				Renderer::Sprite::DrawScaled(64, cursorY, 62, 0, 4 * bounce, 4 * bounce, 128, 255, 2048, 2048);
				Renderer::Sprite::DrawScaled(240, cursorY, 62, 1, spriteScale, spriteScale, 128, 255, 2048, 2048);

				if (Toy2::g_saveLoaded)
					Renderer::DrawMainMenuText(120, "continue game", 128);
				else
					Renderer::DrawMainMenuText(120, "start game", 128);

				Renderer::DrawMainMenuText(140, "options", 128);
				Renderer::DrawMainMenuText(160, "load game", 128);
				Renderer::DrawMainMenuText(180, "movie viewer", 128);
				Renderer::DrawMainMenuText(200, "exit", 128);

				Nullsub3();

				RenderMenu();

				if (g_attractModeTimer >= 0 && ((InputManager::g_curButtonsPressed & INPUT_SECRET_MENU) != 0 || idleTimer >= g_attractModeTimer))
				{
					if (g_nextScreen)
						continue;

					g_nextScreen = 1;
					g_fadeTimer = 23;

					Nu3D::Camera::SetTint(0, 0, 0, 12);
				}
			}

			AudioManager::StopAndWait();

			return g_nextScreen;
		}

		// FUNCTION: TOY2 0x00437FB0 [PROVISIONAL]
		int32_t Tick()
		{
			if (g_mainMenuState != -1)
				goto LBL_INIT_TITLE_SCREEN;

			int32_t nextScreenResult;

			do
			{
				nextScreenResult = Draw();
				g_nextScreen = nextScreenResult;

				if (nextScreenResult != 1)
					break;

			LBL_INIT_TITLE_SCREEN:

				InputManager::g_curButtonsPressed = 0;
				InputManager::g_prevButtonsPressed = 0;

				g_fadeTimer = 0;
				g_nextScreen = 0;

				Nu3D::Camera::g_cameraTintBlue = 0;
				Nu3D::Camera::g_cameraTintGreen = 0;
				Nu3D::Camera::g_cameraTintRed = 0;

				Nu3D::Camera::SetTint(128, 128, 128, 12);
				SoftwareRenderer::SetBackdropScrollOverride(0, 0);

				int32_t showPressJumpPrompt = 1;
				Renderer::g_frameDelta = 1;

				int32_t timeoutTicks;

				if (g_returnedToTitle == 1)
				{
					timeoutTicks = 300;
					showPressJumpPrompt = 0;
				}
				else
				{
					timeoutTicks = 900;
				}

				SetBackdropByIndex(0);

				int32_t elapsedTicks = 0;

				AudioManager::PlayMusicOneShot(20);

				while (! g_nextScreen || g_fadeTimer)
				{
					Nu3D::Camera::FadeToTargetTint();

					if (g_fadeTimer > 0)
					{
						g_fadeTimer -= Renderer::g_frameDelta;

						if (g_fadeTimer <= 0)
							g_fadeTimer = 0;
					}

					elapsedTicks += Renderer::g_frameDelta;
					int32_t pulsePhase = elapsedTicks & 63;

					if (pulsePhase > 31)
						pulsePhase = 63 - pulsePhase;

					if (showPressJumpPrompt)
						Renderer::DrawMainMenuText(204, "press jump", 4 * pulsePhase);

					Nullsub3();
					RenderMenu();

					if (! g_nextScreen)
					{
						if (g_attractModeTimer < 0)
						{
							if (elapsedTicks > timeoutTicks)
								g_nextScreen = 1;
						}
						else if ((InputManager::g_curButtonsPressed & INPUT_SECRET_MENU) != 0 || elapsedTicks > timeoutTicks)
						{
							if (showPressJumpPrompt)
								g_nextScreen = 10;
							else
								g_nextScreen = (InputManager::g_curButtonsPressed & INPUT_SECRET_MENU) != 0 ? 10 : 1;
						}

						if (g_nextScreen)
						{
							g_fadeTimer = 23;
							Nu3D::Camera::SetTint(0, 0, 0, 12);
						}
					}

					if ((InputManager::g_curButtonsPressed & INPUT_JUMP) != 0 && (InputManager::g_prevButtonsPressed & INPUT_JUMP) == 0)
					{
						if (g_nextScreen)
							continue;

						if (showPressJumpPrompt)
						{
							if (elapsedTicks > 30)
							{
								g_nextScreen = 2;
								g_fadeTimer = 23;

								Nu3D::Camera::SetTint(0, 0, 0, 12);
								AudioManager::PlayOneShotSoundGlobal(0, 4608, 80, 80);
							}
						}
					}
				}

				AudioManager::StopAndWait();

				if (g_attractModeTimer >= 0)
					return g_nextScreen - 1;

				nextScreenResult = g_nextScreen;

			} while (g_nextScreen == 2);

			return nextScreenResult - 1;
		}

		// FUNCTION: TOY2 0x004371B0 [PROVISIONAL]
		void ShowSettings()
		{
			bool settingsActive = false;
			int32_t selectedOption = 0;
			int32_t cursorY = g_settingsCursorYPositions[1] << 8;
			int32_t cursorBounceDirection = 0;
			int32_t arrowBlinkTimer = 0;
			int32_t selectionPhase = 0;
			int32_t musicSpeakerFrame = 0;
			uint32_t musicSpeakerPhase = 0;
			int32_t soundSpeakerFrame = 0;
			uint32_t soundSpeakerPhase = 0;
			uint8_t configResult = 0;

			uint8_t savedCameraType;
			uint8_t savedMusicVolume;
			uint8_t savedSoundVolume;
			uint16_t savedValue1;
			uint16_t savedValue2;
			SaveManager::Save99Data savedControls;

			InputManager::g_curButtonsPressed = 0;
			InputManager::g_prevButtonsPressed = 0;
			g_fadeTimer = 0;
			g_nextScreen = 0;
			Nu3D::Camera::g_cameraTintBlue = 0;
			Nu3D::Camera::g_cameraTintGreen = 0;
			Nu3D::Camera::g_cameraTintRed = 0;
			Nu3D::Camera::SetTint(128, 128, 128, 12);
			SoftwareRenderer::SetBackdropScrollOverride(0, 0);
			Renderer::g_frameDelta = 1;
			AudioManager::PlayMusicLooping(19);

			while (true)
			{
				if (g_nextScreen && g_fadeTimer == 0)
				{
					AudioManager::FlushSoundVoices();
					AudioManager::StopAndWait();
					return;
				}

				Nu3D::Camera::FadeToTargetTint();

				if (g_fadeTimer > 0)
				{
					g_fadeTimer -= Renderer::g_frameDelta;
					if (g_fadeTimer < 1)
						g_fadeTimer = 0;
				}

				arrowBlinkTimer += Renderer::g_frameDelta;
				if (arrowBlinkTimer > 39)
					arrowBlinkTimer -= 40;

				if (cursorBounceDirection > 0)
				{
					cursorBounceDirection += Renderer::g_frameDelta;
					if (cursorBounceDirection > 31)
						cursorBounceDirection -= 32;
				}
				if (cursorBounceDirection < 0)
				{
					cursorBounceDirection -= Renderer::g_frameDelta;
					if (cursorBounceDirection < -31)
						cursorBounceDirection += 32;
				}

				selectionPhase = (selectionPhase + Renderer::g_frameDelta) & 15;

				if (! settingsActive)
				{
					int32_t targetY = g_settingsCursorYPositions[selectedOption + 1] << 8;
					if (cursorY == targetY)
					{
						if ((InputManager::g_curButtonsPressed & INPUT_JUMP) != 0 && (InputManager::g_prevButtonsPressed & INPUT_JUMP) == 0 && ! g_nextScreen)
						{
							InputManager::g_prevButtonsPressed = InputManager::g_curButtonsPressed;
							settingsActive = true;
							AudioManager::PlayOneShotSoundGlobal(0, 0x1200, 80, 80);
							savedCameraType = SaveManager::g_save0Data.cameraType;
							savedMusicVolume = SaveManager::g_save0Data.musicVolume;
							savedSoundVolume = SaveManager::g_save0Data.soundVolume;
							savedValue1 = SaveManager::g_save0Data.unkData1;
							savedValue2 = SaveManager::g_save0Data.unkData2;
							savedControls = SaveManager::g_save99Data;
							musicSpeakerFrame = 0;
							musicSpeakerPhase = 0;
							soundSpeakerFrame = 0;
							soundSpeakerPhase = 0;
							arrowBlinkTimer = 0;
						}
						else
						{
							if ((InputManager::g_curButtonsPressed & INPUT_UP) != 0 && selectedOption > 0)
							{
								--selectedOption;
								cursorBounceDirection = -1;
								AudioManager::PlayOneShotSoundGlobal(1, 0x1200, 80, 80);
							}
							if ((InputManager::g_curButtonsPressed & INPUT_DOWN) != 0 && selectedOption < 3)
							{
								++selectedOption;
								cursorBounceDirection = 1;
								AudioManager::PlayOneShotSoundGlobal(1, 0x1200, 80, 80);
								selectionPhase = -1;
							}
						}
					}
					else
					{
						selectionPhase = -1;
						if (cursorY < targetY)
						{
							cursorY +=
								(g_settingsCursorYPositions[selectedOption + 1] - g_settingsCursorYPositions[selectedOption]) * Renderer::g_frameDelta * 8;
							if (cursorY > targetY)
								cursorY = targetY;
						}
						if (cursorY != targetY && cursorY > targetY)
						{
							cursorY -=
								(g_settingsCursorYPositions[selectedOption + 2] - g_settingsCursorYPositions[selectedOption + 1]) * Renderer::g_frameDelta * 8;
							if (cursorY < targetY)
								cursorY = targetY;
						}
					}
				}

				bool cancelPressed = (InputManager::g_curButtonsPressed & INPUT_CANCEL) != 0 && (InputManager::g_prevButtonsPressed & INPUT_CANCEL) == 0;

				if (cancelPressed && ! g_nextScreen && ! settingsActive)
				{
					g_nextScreen = 1;
					g_fadeTimer = 44;
					Nu3D::Camera::SetTint(0, 0, 0, 6);
					AudioManager::PlayOneShotSoundGlobal(2, 0x1200, 80, 80);
				}

				if ((cancelPressed || configResult == 1) && settingsActive)
				{
					settingsActive = false;
					AudioManager::PlayOneShotSoundGlobal(2, 0x1200, 80, 80);
					SaveManager::g_save0Data.soundVolume = savedSoundVolume;
					SaveManager::g_save0Data.musicVolume = savedMusicVolume;
					g_settingsSaveValue1 = (int16_t)savedValue1;
					g_settingsSaveValue2 = (int16_t)savedValue2;
					SaveManager::g_save0Data.unkData1 = savedValue1;
					SaveManager::g_save0Data.unkData2 = savedValue2;
					SaveManager::g_save99Data = savedControls;
					SaveManager::g_save0Data.cameraType = savedCameraType;
					configResult = 0;
				}

				bool acceptPressed = (InputManager::g_curButtonsPressed & INPUT_JUMP) != 0 && (InputManager::g_prevButtonsPressed & INPUT_JUMP) == 0;
				if ((acceptPressed || configResult == 2) && settingsActive)
				{
					settingsActive = false;
					AudioManager::PlayOneShotSoundGlobal(0, 0x1200, 80, 80);
					configResult = 0;
				}

				if (settingsActive)
				{
					if (cursorY != (g_settingsCursorYPositions[1] << 8) && cursorY != (g_settingsCursorYPositions[4] << 8))
						Renderer::DrawMenuTextScaled(160, 190, "jump:accept  cancel:go back", 0, 1, 0x600);

					if (cursorY == (g_settingsCursorYPositions[1] << 8))
						configResult = Toy2::ShowControlConfig();

					if (cursorY == (g_settingsCursorYPositions[2] << 8))
					{
						Renderer::DrawMenuText(50, "music volume", 0);
						if ((InputManager::g_curButtonsPressed & INPUT_RIGHT) != 0 && (InputManager::g_prevButtonsPressed & INPUT_RIGHT) == 0
							&& SaveManager::g_save0Data.musicVolume < 10)
						{
							++SaveManager::g_save0Data.musicVolume;
							AudioManager::PlayOneShotSoundGlobal(1, 0x1200, 0x30, 0x50);
						}
						if ((InputManager::g_curButtonsPressed & INPUT_LEFT) != 0 && (InputManager::g_prevButtonsPressed & INPUT_LEFT) == 0
							&& SaveManager::g_save0Data.musicVolume != 0)
						{
							--SaveManager::g_save0Data.musicVolume;
							AudioManager::PlayOneShotSoundGlobal(1, 0x1200, 0x50, 0x30);
						}

						int16_t speakerSheet = musicSpeakerFrame < 9 ? 0x35 : 0x36;
						int16_t speakerTile = musicSpeakerFrame < 9 ? musicSpeakerFrame : musicSpeakerFrame - 9;
						Renderer::Sprite::DrawTile(0x75, 0x4c, speakerSheet, speakerTile);
						Renderer::Sprite::DrawTile(0x77, 0xa6, 0x37, SaveManager::g_save0Data.musicVolume);

						musicSpeakerPhase += Renderer::g_frameDelta * 8;
						if (musicSpeakerPhase > 15)
						{
							++musicSpeakerFrame;
							if (musicSpeakerFrame > 17)
								musicSpeakerFrame = 0;
							musicSpeakerPhase &= 15;
						}

						if (arrowBlinkTimer < 20)
						{
							if (SaveManager::g_save0Data.musicVolume != 0)
								Renderer::Sprite::DrawTile(0x32, 0x9e, 0x38, 3);
							if (SaveManager::g_save0Data.musicVolume < 10)
								Renderer::Sprite::DrawTile(0xef, 0x9e, 0x38, 2);
						}
					}

					if (cursorY == (g_settingsCursorYPositions[3] << 8))
					{
						Renderer::DrawMenuText(50, "sfx volume", 0);
						if ((InputManager::g_curButtonsPressed & INPUT_RIGHT) != 0 && (InputManager::g_prevButtonsPressed & INPUT_RIGHT) == 0
							&& SaveManager::g_save0Data.soundVolume < 10)
						{
							++SaveManager::g_save0Data.soundVolume;
							AudioManager::PlayOneShotSoundGlobal(1, 0x1200, 0x30, 0x50);
						}
						if ((InputManager::g_curButtonsPressed & INPUT_LEFT) != 0 && (InputManager::g_prevButtonsPressed & INPUT_LEFT) == 0
							&& SaveManager::g_save0Data.soundVolume != 0)
						{
							--SaveManager::g_save0Data.soundVolume;
							AudioManager::PlayOneShotSoundGlobal(1, 0x1200, 0x50, 0x30);
						}

						int16_t speakerSheet = soundSpeakerFrame < 9 ? 0x39 : 0x3a;
						int16_t speakerTile = soundSpeakerFrame < 9 ? soundSpeakerFrame : soundSpeakerFrame - 9;
						Renderer::Sprite::DrawTile(0x75, 0x4c, speakerSheet, speakerTile);
						Renderer::Sprite::DrawTile(0x77, 0xa6, 0x37, SaveManager::g_save0Data.soundVolume);

						soundSpeakerPhase += ((SaveManager::g_save0Data.soundVolume >> 1) + 5) * Renderer::g_frameDelta;
						if (soundSpeakerPhase > 15)
						{
							uint32_t framesElapsed = soundSpeakerPhase >> 4;
							soundSpeakerPhase -= framesElapsed * 16;
							do
							{
								++soundSpeakerFrame;
								if (soundSpeakerFrame > 17)
									soundSpeakerFrame = 0;
							} while (--framesElapsed != 0);
						}

						if (arrowBlinkTimer < 20)
						{
							if (SaveManager::g_save0Data.soundVolume != 0)
								Renderer::Sprite::DrawTile(0x32, 0x9e, 0x38, 3);
							if (SaveManager::g_save0Data.soundVolume < 10)
								Renderer::Sprite::DrawTile(0xef, 0x9e, 0x38, 2);
						}
					}

					if (cursorY == (g_settingsCursorYPositions[4] << 8))
						Toy2::ShowGraphicsConfig();
				}
				else
				{
					int16_t drawY = (int16_t)(cursorY >> 8);
					if (selectionPhase < 0)
					{
						int32_t bounce = cursorBounceDirection;
						if (bounce < 0)
							bounce += 31;
						int16_t tileIndex = (int16_t)(bounce / 2);
						Renderer::Sprite::DrawTile(0x30, drawY, 0x34, tileIndex);
						Renderer::Sprite::DrawTile(0xf0, drawY, 0x34, tileIndex);
					}
					else
					{
						Renderer::Sprite::DrawTile(0x30, drawY, 0x34, (int16_t)(selectionPhase / 2 + 16));
						Renderer::Sprite::DrawTile(0xf0, drawY, 0x34, (int16_t)(((selectionPhase / 2 + 2) & 7) + 16));
					}

					Renderer::DrawMenuText(50, "options", 0);
					Renderer::DrawMenuText(g_settingsCursorYPositions[1] + 14, "configure controller", 0);
					Renderer::DrawMenuText(g_settingsCursorYPositions[2] + 14, "music volume", 0);
					Renderer::DrawMenuText(g_settingsCursorYPositions[3] + 14, "sfx volume", 0);
					Renderer::DrawMenuText(g_settingsCursorYPositions[4] + 14, "configure gfx", 0);
					Renderer::DrawMenuTextScaled(160, 190, "jump:select  cancel:go back", 0, 1, 0x600);
				}

				Nullsub3();
				RenderMenu();
				SaveManager::g_save0Data.unkData1 = g_settingsSaveValue1;
				SaveManager::g_save0Data.unkData2 = g_settingsSaveValue2;
				AudioManager::SetVolumesProcessed(SaveManager::g_save0Data.musicVolume, SaveManager::g_save0Data.soundVolume);
			}
		}
	}

	// STUB: TOY2 0x0049C420
	uint8_t ShowControlConfig() { return 0; }

	// STUB: TOY2 0x0049CAA0
	void ShowGraphicsConfig() {}
}
