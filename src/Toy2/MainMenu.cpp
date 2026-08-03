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
	extern int32_t g_inputSuppressFrames;

	int32_t ShowControlConfig();
	void ShowGraphicsConfig();

	struct ControlConfigEntry
	{
		GameControlId controlId;
		char* label;
	};
	STATIC_ASSERT(sizeof(ControlConfigEntry) == 8);

	// GLOBAL: TOY2 0x0050084C
	char g_controlConfigLeftLabel[] = "left";

	// GLOBAL: TOY2 0x00500854
	char g_controlConfigRightLabel[] = "right";

	// GLOBAL: TOY2 0x00500E58
	ControlConfigEntry g_controlConfigEntries[14] = {
		{ INPUT_UP, "up" },
		{ INPUT_DOWN, "down" },
		{ INPUT_LEFT, g_controlConfigLeftLabel },
		{ INPUT_RIGHT, g_controlConfigRightLabel },
		{ INPUT_JUMP, "jump" },
		{ INPUT_FIRE, "fire" },
		{ INPUT_SPIN, "spin" },
		{ INPUT_CAMERA_LEFT, "camera left" },
		{ INPUT_CAMERA_RIGHT, "camera right" },
		{ INPUT_VISOR_TOGGLE, "visor toggle" },
		{ INPUT_TARGET_LOCK, "target lock" },
		{ INPUT_MENU, "menu" },
		{ INPUT_CANCEL, "cancel" },
		{ (GameControlId)0, 0 },
	};

	// GLOBAL: TOY2 0x00500EC8
	int32_t g_controlConfigInputDelay = 30;

	// GLOBAL: TOY2 0x00500ECC
	int32_t g_controlConfigBlinkTimer = 20;

	// GLOBAL: TOY2 0x00830C6C
	int32_t g_controlConfigSelectedRow;

	// GLOBAL: TOY2 0x00830C70
	int32_t g_controlConfigFirstVisibleRow;

	// GLOBAL: TOY2 0x00830C74
	int32_t g_controlConfigAwaitingInput;

	// GLOBAL: TOY2 0x005009F4
	char* g_graphicsDetailLabels[3] = { "low detail", "medium detail", "high detail" };

	// GLOBAL: TOY2 0x00500A00
	char* g_graphicsGammaLabels[3] = { "gamma correction normal", "gamma correction medium", "gamma correction high" };

	// GLOBAL: TOY2 0x00500ED0
	int32_t g_graphicsBlinkTimer = 20;

	// GLOBAL: TOY2 0x00500ED4
	int32_t g_graphicsCursorY = 72;

	// GLOBAL: TOY2 0x00830C78
	int32_t g_graphicsSelectedOption;

	// GLOBAL: TOY2 0x00830C7C
	int32_t g_graphicsInputDelay;

	namespace MainMenu
	{
		// GLOBAL: TOY2 0x0053CA60
		int32_t g_fadeTimer;

		// GLOBAL: TOY2 0x0053E4A8
		int32_t g_nextScreen;

		// GLOBAL: TOY2 0x00559E84
		RGB32 g_menuClearColor;

		// GLOBAL: TOY2 0x004F6BD0
		int32_t g_settingsCursorYPositions[5] = { -1, 65, 85, 105, 125 };

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

			Nu3D::Camera::g_cameraTintRed = 0;
			Nu3D::Camera::g_cameraTintGreen = 0;
			Nu3D::Camera::g_cameraTintBlue = 0;

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

				Nu3D::Camera::g_cameraTintRed = 0;
				Nu3D::Camera::g_cameraTintGreen = 0;
				Nu3D::Camera::g_cameraTintBlue = 0;

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
			Nu3D::Camera::g_cameraTintRed = 0;
			Nu3D::Camera::g_cameraTintGreen = 0;
			Nu3D::Camera::g_cameraTintBlue = 0;
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

	// FUNCTION: TOY2 0x0049C420 [PROVISIONAL]
	int32_t ShowControlConfig()
	{
		g_inputSuppressFrames = 30 / Renderer::g_frameDelta;

		int32_t result = 0;
		int32_t hasMoreRows = 0;

		if (SaveManager::GetControlSettingId(TOY_INPUT_ESC) != TOY_INPUT_UNKNOWN)
		{
			SaveManager::ClearBindByInputCode(TOY_INPUT_ESC);
			SaveManager::ClearBindByInputCode(TOY_INPUT_CAPITALA);
			SaveManager::ClearBindByInputCode(TOY_INPUT_CAPITALB);
			SaveManager::ClearBindByInputCode(TOY_INPUT_CAPITALC);
			SaveManager::ClearBindByInputCode(TOY_INPUT_CAPITALD);
			SaveManager::ClearBindByInputCode(TOY_INPUT_F1);
			SaveManager::ClearBindByInputCode(TOY_INPUT_F2);
		}

		Renderer::DrawMenuTextScaled(160, 46, "control configuration", 0, 1, 0x800);

		int32_t rowIndex = g_controlConfigFirstVisibleRow;
		ControlConfigEntry* entry = &g_controlConfigEntries[rowIndex];
		while (entry->controlId != 0)
		{
			int32_t rowY = (rowIndex - g_controlConfigFirstVisibleRow + 5) * 16;
			if (rowY > 144)
			{
				hasMoreRows = 1;
				break;
			}

			int32_t inputCode = SaveManager::GetInputCodeByControlId(entry->controlId);
			char* inputName = InputManager::GetGameControlName(inputCode);

			if (rowIndex != g_controlConfigSelectedRow || g_controlConfigBlinkTimer > 10 || g_controlConfigAwaitingInput != 0)
				Renderer::DrawMenuTextScaled(150, rowY, entry->label, 0, 2, 0x800);

			if (rowIndex != g_controlConfigSelectedRow || g_controlConfigBlinkTimer > 10 || g_controlConfigAwaitingInput == 0)
			{
				if (inputName == 0)
				{
					switch (entry->controlId)
					{
						case INPUT_UP:
							inputName = "A";
							break;
						case INPUT_RIGHT:
							inputName = "D";
							break;
						case INPUT_DOWN:
							inputName = "B";
							break;
						case INPUT_LEFT:
							inputName = "C";
							break;
						case INPUT_MENU:
							inputName = InputManager::GetGameControlName(TOY_INPUT_F1);
							break;
						case INPUT_CANCEL:
							inputName = InputManager::GetGameControlName(TOY_INPUT_ESC);
							break;
						case INPUT_JUMP:
							inputName = InputManager::GetGameControlName(TOY_INPUT_F2);
							break;
						default:
							inputName = "???";
							break;
					}
				}

				Renderer::DrawMenuTextScaled(160, rowY, inputName, 0, 0, 0x800);
			}

			++entry;
			++rowIndex;
		}

		if (g_controlConfigBlinkTimer > 10)
		{
			if (g_controlConfigFirstVisibleRow != 0)
				Renderer::Sprite::DrawScaled(152, 64, 0x43, 0x2d, 255, 255, 255, 255, 0x1000, 0x1000);
			if (hasMoreRows)
				Renderer::Sprite::DrawScaled(152, 150, 0x43, 0x2e, 255, 255, 255, 255, 0x1000, 0x1000);
		}

		if (g_controlConfigAwaitingInput == 0)
		{
			Renderer::DrawMenuTextScaled(160, 178, "ABto select, space to change", 0, 1, 0x600);
			Renderer::DrawMenuTextScaled(160, 186, "return to accept, esc to cancel", 0, 1, 0x600);
		}
		else
		{
			Renderer::DrawMenuTextScaled(160, 182, "press desired key / button", 0, 1, 0x600);
		}

		if (g_controlConfigInputDelay > 0)
		{
			g_controlConfigInputDelay -= Renderer::g_frameDelta;
		}
		else if (g_controlConfigAwaitingInput == 0)
		{
			if (InputManager::IsKeyPressed(TOY_INPUT_CAPITALA))
			{
				if (g_controlConfigSelectedRow != 0)
				{
					AudioManager::PlayOneShotSoundGlobal(1, 0x1200, 80, 80);
					--g_controlConfigSelectedRow;
				}
				if (g_controlConfigFirstVisibleRow != 0 && g_controlConfigSelectedRow <= g_controlConfigFirstVisibleRow)
					--g_controlConfigFirstVisibleRow;
			}

			if (InputManager::IsKeyPressed(TOY_INPUT_CAPITALB))
			{
				if ((g_controlConfigEntries[rowIndex].controlId != 0 && g_controlConfigSelectedRow < rowIndex) || g_controlConfigSelectedRow < rowIndex - 1)
				{
					AudioManager::PlayOneShotSoundGlobal(1, 0x1200, 80, 80);
					++g_controlConfigSelectedRow;
				}
				if (hasMoreRows && rowIndex <= g_controlConfigSelectedRow)
					++g_controlConfigFirstVisibleRow;
			}

			if (InputManager::IsKeyPressed(TOY_INPUT_ESC))
			{
				result = 1;
				g_controlConfigAwaitingInput = 0;
				g_controlConfigFirstVisibleRow = 0;
				g_controlConfigSelectedRow = 0;
				g_controlConfigInputDelay = 30;
				InputManager::g_curButtonsPressed = 0;
				AudioManager::PlayOneShotSoundGlobal(2, 0x1200, 80, 80);
			}

			if (InputManager::IsKeyPressed(TOY_INPUT_RETURNKEY))
			{
				result = 2;
				g_controlConfigAwaitingInput = 0;
				g_controlConfigFirstVisibleRow = 0;
				g_controlConfigSelectedRow = 0;
				g_controlConfigInputDelay = 30;
				SaveManager::AddInputEntry(TOY_INPUT_ESC, INPUT_CANCEL);
				SaveManager::AddInputEntry(TOY_INPUT_CAPITALA, INPUT_UP);
				SaveManager::AddInputEntry(TOY_INPUT_CAPITALB, INPUT_DOWN);
				SaveManager::AddInputEntry(TOY_INPUT_CAPITALC, INPUT_LEFT);
				SaveManager::AddInputEntry(TOY_INPUT_CAPITALD, INPUT_RIGHT);
				SaveManager::AddInputEntry(TOY_INPUT_F1, INPUT_JUMP);
				SaveManager::AddInputEntry(TOY_INPUT_F2, INPUT_MENU);
				SaveManager::SaveToFile(99, 0);
				AudioManager::PlayOneShotSoundGlobal(0, 0x1200, 80, 80);
			}

			if (InputManager::IsKeyPressed(TOY_INPUT_SPACE))
			{
				g_controlConfigAwaitingInput = 1;
				g_controlConfigInputDelay = 30;
				AudioManager::PlayOneShotSoundGlobal(0, 0x1200, 80, 80);
			}
		}
		else
		{
			int32_t inputCode = InputManager::GetPressedInput();
			if (inputCode != TOY_INPUT_UNKNOWN && inputCode != TOY_INPUT_DIRECTIONPAD && inputCode != TOY_INPUT_CAPITALA && inputCode != TOY_INPUT_CAPITALB
				&& inputCode != TOY_INPUT_CAPITALC && inputCode != TOY_INPUT_CAPITALD)
			{
				switch (inputCode)
				{
					case TOY_INPUT_ESC:
						g_controlConfigAwaitingInput = 0;
						InputManager::IsKeyPressed(TOY_INPUT_ESC);
						AudioManager::PlayOneShotSoundGlobal(2, 0x1200, 80, 80);
						break;
					case TOY_INPUT_F1:
					case TOY_INPUT_F2:
					case TOY_INPUT_F3:
					case TOY_INPUT_F4:
					case TOY_INPUT_F5:
					case TOY_INPUT_F6:
					case TOY_INPUT_F7:
					case TOY_INPUT_F8:
					case TOY_INPUT_F9:
					case TOY_INPUT_F10:
					case TOY_INPUT_F11:
					case TOY_INPUT_F12:
					case TOY_INPUT_F13:
					case TOY_INPUT_F14:
					case TOY_INPUT_F15:
						break;
					default:
						SaveManager::ClearBindByControlId(g_controlConfigEntries[g_controlConfigSelectedRow].controlId);
						SaveManager::ClearBindByInputCode(inputCode);
						g_controlConfigInputDelay = 30;
						SaveManager::AddInputEntry(inputCode, g_controlConfigEntries[g_controlConfigSelectedRow].controlId);
						g_controlConfigAwaitingInput = 0;
						AudioManager::PlayOneShotSoundGlobal(0, 0x1200, 80, 80);
						break;
				}
			}
		}

		g_controlConfigBlinkTimer -= Renderer::g_frameDelta;
		if (g_controlConfigBlinkTimer < 0)
			g_controlConfigBlinkTimer += 20;

		return result;
	}

	// FUNCTION: TOY2 0x0049CAA0 [PROVISIONAL]
	void ShowGraphicsConfig()
	{
		const uint32_t lensFlareFlag = 1;
		const uint32_t animatedTexturesFlag = 4;
		const int32_t optionCount = 4;
		int32_t canDecrease = 0;
		int32_t canIncrease = 0;

		Renderer::DrawMenuText(50, "graphics configuration", 0);

		if (g_graphicsSelectedOption != 0 || g_graphicsBlinkTimer < 10)
			Renderer::DrawMenuText(75, (g_toyCfgData.flags & lensFlareFlag) != 0 ? "lens flare on" : "lens flare off", 0);

		if (g_graphicsSelectedOption != 1 || g_graphicsBlinkTimer < 10)
			Renderer::DrawMenuText(100, g_graphicsDetailLabels[g_toyCfgData.detail], 0);

		if (g_graphicsSelectedOption != 2 || g_graphicsBlinkTimer < 10)
		{
			int32_t gammaLabelIndex = -(int32_t)((g_toyCfgData.gammaCorrection - 2.0f) * -2.0f);
			Renderer::DrawMenuText(125, g_graphicsGammaLabels[gammaLabelIndex], 0);
		}

		if (g_graphicsSelectedOption != 3 || g_graphicsBlinkTimer < 10)
			Renderer::DrawMenuText(150, (g_toyCfgData.flags & animatedTexturesFlag) != 0 ? "animated textures on" : "animated textures off", 0);

		Renderer::DrawMenuTextScaled(160, 178, "AB to select, CD to change", 0, 1, 0x600);
		Renderer::DrawMenuTextScaled(160, 186, "jump:accept  cancel:go back", 0, 1, 0x600);

		if (g_graphicsInputDelay <= 0)
		{
			if ((InputManager::g_curButtonsPressed & INPUT_UP) != 0)
			{
				int32_t previousOption = g_graphicsSelectedOption - 1;
				g_graphicsSelectedOption = previousOption < 0 ? 0 : previousOption;
				g_graphicsInputDelay = 15;
				AudioManager::PlayOneShotSoundGlobal(0, 0x1200, 80, 80);
			}

			if ((InputManager::g_curButtonsPressed & INPUT_DOWN) != 0)
			{
				int32_t nextOption = g_graphicsSelectedOption + 1;
				g_graphicsSelectedOption = nextOption < optionCount ? nextOption : optionCount - 1;
				g_graphicsInputDelay = 15;
				AudioManager::PlayOneShotSoundGlobal(0, 0x1200, 80, 80);
			}
		}

		switch (g_graphicsSelectedOption)
		{
			case 0:
			case 3:
				canDecrease = 1;
				canIncrease = 1;
				break;
			case 1:
				canDecrease = g_toyCfgData.detail != 0;
				canIncrease = g_toyCfgData.detail < 2;
				break;
			case 2:
				canDecrease = g_toyCfgData.gammaCorrection > 2.0f;
				canIncrease = g_toyCfgData.gammaCorrection < 3.0f;
				break;
		}

		int32_t cursorDelta = g_graphicsSelectedOption * 25 + 72 - g_graphicsCursorY;
		if (cursorDelta <= 0)
		{
			if (cursorDelta < 0)
			{
				int32_t scaledStep = Renderer::g_frameDelta * cursorDelta / 5;
				int32_t minimumStep = scaledStep;
				if (scaledStep > -1)
					minimumStep = -1;
				if (cursorDelta <= minimumStep)
				{
					cursorDelta = scaledStep;
					if (scaledStep > -1)
						cursorDelta = -1;
				}
			}
		}
		else
		{
			int32_t scaledStep = Renderer::g_frameDelta * cursorDelta / 5;
			int32_t maximumStep = 1;
			if (scaledStep >= 1)
				maximumStep = scaledStep;
			if (maximumStep <= cursorDelta)
			{
				cursorDelta = scaledStep;
				if (scaledStep < 1)
					cursorDelta = 1;
			}
		}
		g_graphicsCursorY += cursorDelta;

		if (canDecrease)
			Renderer::Sprite::DrawScaled(50, (int16_t)g_graphicsCursorY, 0x38, 3, 255, 255, 255, 255, 0x800, 0x800);
		if (canIncrease)
			Renderer::Sprite::DrawScaled(255, (int16_t)g_graphicsCursorY, 0x38, 2, 255, 255, 255, 255, 0x800, 0x800);

		g_graphicsBlinkTimer -= Renderer::g_frameDelta;
		if (g_graphicsBlinkTimer < 0)
			g_graphicsBlinkTimer += 20;

		if (g_graphicsInputDelay > 0)
		{
			g_graphicsInputDelay -= Renderer::g_frameDelta;
			return;
		}

		if ((InputManager::g_curButtonsPressed & (INPUT_LEFT | INPUT_RIGHT)) != 0)
		{
			if ((InputManager::g_curButtonsPressed & INPUT_LEFT) != 0 && canDecrease)
				AudioManager::PlayOneShotSoundGlobal(1, 0x1200, 80, 80);
			g_graphicsInputDelay = 15;
		}
		uint8_t buttonsPressed = (uint8_t)InputManager::g_curButtonsPressed;

		switch (g_graphicsSelectedOption)
		{
			case 0:
				if ((buttonsPressed & (INPUT_LEFT | INPUT_RIGHT)) != 0)
					g_toyCfgData.flags = (g_toyCfgData.flags & ~lensFlareFlag) | ((g_toyCfgData.flags & lensFlareFlag) != 0 ? 0 : lensFlareFlag);
				break;
			case 1:
				if ((buttonsPressed & INPUT_LEFT) != 0 && canDecrease)
					--g_toyCfgData.detail;
				if ((buttonsPressed & INPUT_RIGHT) != 0 && canIncrease)
					++g_toyCfgData.detail;
				break;
			case 2:
				if ((buttonsPressed & INPUT_LEFT) != 0 && canDecrease)
					g_toyCfgData.gammaCorrection -= 0.5f;
				if ((buttonsPressed & INPUT_RIGHT) != 0 && canIncrease)
					g_toyCfgData.gammaCorrection += 0.5f;
				break;
			case 3:
				if ((buttonsPressed & (INPUT_LEFT | INPUT_RIGHT)) != 0)
					g_toyCfgData.flags =
						(g_toyCfgData.flags & ~animatedTexturesFlag) | ((g_toyCfgData.flags & animatedTexturesFlag) != 0 ? 0 : animatedTexturesFlag);
				break;
		}
	}
}
