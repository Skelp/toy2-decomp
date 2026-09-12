#include "Toy2/Toy2.h"
#include "Toy2/Toy2Internal.h"
#include "Toy2/Actor.h"
#include "Toy2/Buzz.h"
#include "Toy2/Camera.h"
#include "Toy2/Collectables.h"
#include "Toy2/Collision.h"
#include "Toy2/Direct6.h"
#include "Toy2/Gadget.h"
#include "Toy2/Ini.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/LevelSelect.h"
#include "Toy2/Levels.h"
#include "Toy2/MainMenu.h"
#include "Toy2/Screens.h"
#include "Toy2/Weather.h"
#include "Toy2/Win95.h"
#include "SoftwareRenderer.h"
#include "DrawingDevice.h"
#include "InputManager.h"
#include "SaveManager.h"
#include "ModeSelect.h"
#include "Random.h"
#include "Nullsub.h"
#include "Logger.h"
#include "FileUtils.h"
#include "Renderer/Renderer.h"
#include "Renderer/Sprite.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Font.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "AudioManager/AudioManager.h"
#include <WINDOWS.H>
#include <STRING.H>

// The application shell. Run brings the managers up, picks the display mode and
// then alternates between MenuLoop and MainLoop for the life of the process;
// MainLoop drives one frame of play and hands over to PauseLoop while the pause
// menu is open. The demo playback and attract mode state lives here because Run
// is what starts and stops them.
//
// Retail run 0x0049D910-0x0049E330, between the Sprite.cpp and AudioManager.cpp
// runs.

namespace Toy2
{
	// GLOBAL: TOY2 0x0052AD7C
	int32_t g_demoPathWriteIdx;

	// GLOBAL: TOY2 0x0052AD84
	int32_t g_demoExitInput;

	// GLOBAL: TOY2 0x0052ADA4
	int32_t g_unused1;

	// GLOBAL: TOY2 0x0052ADA8
	int32_t g_unused2;

	// GLOBAL: TOY2 0x0052ADB4
	int32_t g_attractModeInputTimer;

	// GLOBAL: TOY2 0x0052C83C
	int32_t g_curDemoLevel;

	// GLOBAL: TOY2 0x0052EF40
	int32_t g_demoInputRunLength;

	// GLOBAL: TOY2 0x00830D58
	int32_t g_saveLoaded;

	// FUNCTION: TOY2 0x0049D910 [PROVISIONAL]
	int32_t Run(int32_t argCount, char** argList)
	{
		g_returnedToTitle = 0;

		g_unused1 = 2;
		g_unused2 = 0;

		g_attractModeTimer = -25;
		g_attractModeInputTimer = -50;

		SoftwareRenderer::SwapRenderBuffer();

		g_pastInitialBoot = 0;
		g_curDemoLevel = 0;
		g_levelTransition = 0;

		SaveManager::g_save0Data.unkData1 = 1;
		SaveManager::g_save0Data.unkData2 = 23;
		SaveManager::g_save0Data.cameraType = SaveManager::CAMERA_PASSIVE | SaveManager::CAMERA_ACTIVE;
		SaveManager::g_save0Data.musicVolume = 8;
		SaveManager::g_save0Data.soundVolume = 8;

		AudioManager::SetVolumesProcessed(8, 8);

		int32_t enteredLevelIdx;
		int32_t levelIdxCache = g_levelFileIndex;

	LBL_RESTART_GAME:

		g_demoMode = 0;
		g_mainMenuState = 0;
		g_levelFileIndex = 0;

		Levels::g_levelLoadConfig = 1084;

		Renderer::SetVirtualRatioTo54();
		Levels::InitLevelPlay(0);
		ScreenDispatcher(10);

		g_levelFileIndex = levelIdxCache;

		if (! PlayMovieWithTransition(2, 0) && ! PlayMovieWithTransition(0, 0) && ! PlayMovieWithTransition(1, 0))
			UnlockAndPlayMovie(0, 0, 1);

		g_pastInitialBoot = 1;

	LBL_RESTART_MENU_STATE:

		SaveManager::InitProgressData(&SaveManager::g_save0Data);
		SaveManager::LoadProgressData(&SaveManager::g_save0Data);

		g_mainMenuState = 0;
		g_saveLoaded = 0;

		LevelSelect::ResetCursor();

	LBL_REDO_MENU_LOOP:

		while (true)
		{
			int32_t savedLevelFileIndex = g_levelFileIndex;
			g_levelFileIndex = 0;
			Levels::g_levelLoadConfig = 1084;

			Renderer::SetVirtualRatioTo54();
			Levels::InitLevelPlay(0);
			ScreenDispatcher(2);

			g_levelFileIndex = savedLevelFileIndex;

			switch (g_mainMenuState)
			{
				case 0:
					g_demoMode = 1;
					goto LBL_DEMO_MODE;

				case 1:
					g_demoMode = 0;
					goto LBL_DEMO_MODE;

				case 2: // Options Screen
					g_levelFileIndex = 0;
					Levels::g_levelLoadConfig = 1212;

					Renderer::SetVirtualRatioTo54();
					Levels::InitLevelPlay(0);
					ScreenDispatcher(9);

					g_levelFileIndex = enteredLevelIdx;
					g_mainMenuState = -1;
					continue;

				case 3: // Save Screen
					if (ShowSaveScreen())
						g_saveLoaded = 1;

					g_mainMenuState = -1;
					continue;

				case 4: // Movie Viewer
					ShowMovieViewer();
					g_mainMenuState = -1;
					continue;

				case 8:
					g_levelFileIndex = 0;
					Levels::g_levelLoadConfig = 1084;

					Renderer::SetVirtualRatioTo54();
					Levels::InitLevelPlay(0);
					ScreenDispatcher(8);

					g_levelFileIndex = enteredLevelIdx;
					g_mainMenuState = 0;
					goto LBL_RESTART_MENU_STATE;

				case 9:
					Nullsub5();
					return 0;

				default:

				LBL_DEMO_MODE:
					g_mainMenuState = 0;

					if (g_demoMode == 1)
					{
						switch (g_curDemoLevel)
						{
							case 0:
								g_levelIndex = 0;
								break;
							case 1:
								g_levelIndex = 3;
								break;
							case 2:
								g_levelIndex = 7;
								break;
							case 3:
								g_levelIndex = 10;
								break;
							case 4:
								g_levelIndex = 13;
								break;
							default:
								break;
						}

						if (++g_curDemoLevel > 4)
							g_curDemoLevel = 0;

						g_levelFileIndex = g_levelFileConversion[g_levelIndex];

						LoadPathBin();

						int32_t levelIdxCache = g_levelIndex;

						g_demoPathWriteIdx = -2;
						g_demoInputRunLength = 0;

						SaveManager::InitProgressData(&SaveManager::g_save0Data);
						SaveManager::LoadProgressData(&SaveManager::g_save0Data);

						g_levelIndex = levelIdxCache;
					}

					if (! g_demoMode && g_attractModeTimer < 0)
						goto LBL_SHOW_LEVEL_SELECT;

					break;
			}

			break;
		}

		while (true)
		{
			g_levelFileIndex = g_levelFileConversion[g_levelIndex];

			if (EnterLevel(g_levelFileIndex))
				break;

			while (true)
			{
				g_attractModeInputTimer = 2 * g_attractModeTimer;

				InputManager::g_curButtonsPressed = 0;
				InputManager::g_prevButtonsPressed = 0;

				g_isPaused = 0;
				g_pauseMenuBlinkTimer = 0;

				InputManager::g_directionInputState = 0;
				InputManager::g_prevDirectionInputState = 0;
				InputManager::g_directionInputState2Frames = 0;
				InputManager::g_directionInputState3Frames = 0;

				Renderer::g_frameDelta = 1;
				g_levelTransition = 0;
				g_levelTransitionTimer = 90;

				Nu3D::Camera::SetTint(128, 128, 128, 6);

				while (! g_levelTransition || g_levelTransitionTimer)
				{
					if (g_demoMode)
						Renderer::g_frameDelta = 2; // Half frame rate in demo mode

					if (g_isPaused)
						Game::PauseLoop();
					else
						Game::MainLoop();
				}

				AudioManager::FlushSoundVoices();

				if (g_levelTransition != 2)
					AudioManager::StopAndWait();

				if (g_quitToTitleFlag)
				{
					if (g_levelTransition == 2)
						AudioManager::StopAndWait();

					goto LBL_RESTART_MENU_STATE;
				}

				if (g_levelTransition != 2)
					break;

				if (! g_buzzActor.lives)
				{
					AudioManager::StopAndWait();
					goto LBL_GAME_OVER;
				}

				Buzz::Respawn();
			}

			if (g_levelTransition == 3 || g_levelTransition == 4)
			{
				if (g_attractModeTimer >= 0)
					break;

				if (g_levelTransition != 3)
					goto LBL_RESTART_MENU_STATE;

				levelIdxCache = g_levelFileIndex;

				goto LBL_RESTART_GAME;
			}

			enteredLevelIdx = g_levelFileIndex;

			if (g_levelTransition != 5)
			{
				if (g_levelTransition != 1)
				{
					if (g_attractModeTimer >= 0)
						break;

					goto LBL_SHOW_LEVEL_SELECT;
				}

				goto LBL_SHOW_LEVEL_RESULTS;
			}

			if (g_buzzActor.health < 0)
			{
				if (! g_buzzActor.lives)
				{
				LBL_GAME_OVER:

					levelIdxCache = g_levelFileIndex;

					g_levelFileIndex = 0;
					Levels::g_levelLoadConfig = 1148;

					Renderer::SetVirtualRatioTo54();

					Levels::InitLevelPlay(0);
					ScreenDispatcher(5);

					g_levelFileIndex = levelIdxCache;

					if (g_attractModeTimer >= 0)
						break;

					goto LBL_RESTART_GAME;
				}

				--g_buzzActor.lives;
			}

			if (g_attractModeTimer >= 0)
				break;

			if (g_levelFileIndex % 3)
			{
				g_levelTransition = 1;

			LBL_SHOW_LEVEL_RESULTS:

				if (g_levelFileIndex % 3)
				{
					g_levelFileIndex = 0;
					Levels::g_levelLoadConfig = 1148;

					Renderer::SetVirtualRatioTo54();
					Levels::InitLevelPlay(0);
					ScreenDispatcher(4);
					g_levelFileIndex = enteredLevelIdx;

					if (g_attractModeTimer >= 0)
						break;

					int32_t movie17Status = SaveManager::g_save0Data.moviesUnlocked[17];

					if (SaveManager::g_curLevelTokenData != SaveManager::g_save0Data.tokens[g_levelFileConversion[g_levelIndex]]
						&& (ComputeTokenProgress() & 0xFF0000) == 0x320000)
					{
						SaveManager::g_save0Data.moviesUnlocked[17] = 1;
					}

					ShowPostGameSaveMenu();

					if (! movie17Status && SaveManager::g_save0Data.moviesUnlocked[17])
						UnlockAndPlayMovie(17, 16, 1);
				}
				else
				{
					ShowActClearScreen();

					int32_t levelMovieStatus = SaveManager::g_save0Data.moviesUnlocked[g_levelIndex + 1];

					SaveManager::g_save0Data.moviesUnlocked[g_levelIndex + 1] = 1;

					if (g_levelFileIndex == 15)
						SaveManager::g_save0Data.moviesUnlocked[18] = 1;

					ShowPostGameSaveMenu();

					if (! levelMovieStatus)
						UnlockAndPlayMovie(g_levelIndex + 1, 30, 1);

					if (g_levelFileIndex == 15)
					{
						UnlockAndPlayMovie(18, 31, 1);
						levelIdxCache = g_levelFileIndex;

						g_levelFileIndex = 0;
						Levels::g_levelLoadConfig = 1276;

						Renderer::SetVirtualRatioTo54();
						Levels::InitLevelPlay(0);
						ScreenDispatcher(11); // Show credits

						goto LBL_RESTART_GAME;
					}
				}
			}

			if (g_attractModeTimer >= 0)
				break;

		LBL_SHOW_LEVEL_SELECT:

			g_mainMenuState = 0;
			g_saveLoaded = 1;

			if (ShowLevelSelect())
			{
				g_mainMenuState = -1;
				goto LBL_REDO_MENU_LOOP;
			}

			if ((g_levelIndex + 1) % 3)
			{
				UnlockAndPlayMovie(g_levelIndex + 1, 0, 0);
			}
			else if (g_levelIndex == 11)
			{
				UnlockAndPlayMovie(16, 0, 0);
			}
		}

		Nullsub5();

		return 0;
	}

	namespace Game
	{
		// FUNCTION: TOY2 0x0049DFE0 [PROVISIONAL]
		void MainLoop()
		{
			{
				int16_t input = InputManager::g_curButtonsPressed;
				int32_t demoMode = g_demoMode;

				if (demoMode == 2)
				{
					if (input == InputManager::g_prevButtonsPressed)
					{
						g_demoInputRunLength++;
					}
					else
					{
						g_demoInputBuffer[g_demoPathWriteIdx] = InputManager::g_prevButtonsPressed;
						g_demoInputBuffer[g_demoPathWriteIdx + 1] = (int16_t)g_demoInputRunLength;
						g_demoPathWriteIdx += 2;
						g_demoInputRunLength = 0;
						if (g_demoPathWriteIdx >= 0x800)
							g_demoPathWriteIdx = 0x7FE;
					}
				}

				if (g_attractModeTimer >= 0)
				{
					if (g_returnedToTitle == 0 && input != 0)
						g_attractModeInputTimer = g_attractModeTimer * 2;

					g_attractModeInputTimer -= Renderer::g_frameDelta;
					if (g_attractModeInputTimer <= 0)
					{
						input |= INPUT_SECRET_MENU;
						InputManager::g_curButtonsPressed = input;
					}
				}

				if (demoMode == 1)
				{
					g_demoExitInput = input;
					g_demoInputRunLength--;
					if (g_demoInputRunLength < 0)
					{
						g_demoInputRunLength = g_demoInputBuffer[g_demoPathWriteIdx + 3];
						g_demoPathWriteIdx += 2;
					}
					input = g_demoInputBuffer[g_demoPathWriteIdx];
					InputManager::g_curButtonsPressed = input;
				}

				InputManager::g_directionInputState3Frames = InputManager::g_directionInputState2Frames;
				InputManager::g_directionInputState2Frames = InputManager::g_prevDirectionInputState;
				InputManager::g_prevDirectionInputState = InputManager::g_directionInputState;
				if (demoMode == 1)
				{
					InputManager::g_directionInputState = input;
				}
				else
				{
					InputManager::UpdateDirectionInputState();
				}

				if ((g_buzzActor.actorFlags & Buzz::ACTOR_FLAG_LOCK_FACING) != 0 || (g_levelTransition == 0 && g_levelTransitionTimer > 60))
					InputManager::g_directionInputState &= INPUT_SECRET_MENU | INPUT_MENU | INPUT_CAMERA_LEFT | INPUT_CAMERA_RIGHT;
			}

			Shadow::ResetShadowCount();
			Cutscene::Update();
			Buzz::HandleGameplay(&g_buzzActor);
			Camera::UpdateActiveTransform();
			Nu3D::Camera::ApplyTransformToCamera(&Camera::g_renderCameraTransform);
			UpdateActors();
			Buzz::UpdateRespawnAnchor();
			Collectables::Interactions();
			Buzz::TickGadgets();
			Buzz::CheckParticleCollisions();
			if (g_buzzActor.health >= 0)
				ActorCollisionCheck();
			HandleLevelInteractions(g_levelFileIndex);
			Lighting::UpdateBuzzLight();
			g_buzzActor.unusedFrameState = 0;
			Buzz::UpdateAnimationState();
			AdvanceFramePhase();
			Buzz::UpdateContactEffects();
			Levels::UpdateAmbientEmitters();
			Nu3D::Particles::Update();

			if (g_demoMode == 0 && g_attractModeTimer >= 0 && (InputManager::g_curButtonsPressed & INPUT_SECRET_MENU) != 0 && g_levelTransition == 0)
			{
				g_levelTransition = 4;
				g_levelTransitionTimer = 0x2E;
				Nu3D::Camera::SetTint(0, 0, 0, 6);
			}

			if (g_demoMode != 0 && (InputManager::g_curButtonsPressed & INPUT_MENU) != 0 && g_levelTransition == 0)
			{
				g_levelTransition = 3;
				g_levelTransitionTimer = 0x2E;
				Nu3D::Camera::SetTint(0, 0, 0, 6);
			}

			if (g_demoMode != 0 && g_demoExitInput != 0 && g_levelTransition == 0)
			{
				g_levelTransition = 4;
				g_levelTransitionTimer = 0x2E;
				Nu3D::Camera::SetTint(0, 0, 0, 6);
			}

			if ((InputManager::g_curButtonsPressed & INPUT_MENU) != 0 && (InputManager::g_prevButtonsPressed & INPUT_MENU) == 0 && g_demoMode == 0
				&& Collectables::g_tokenCollectionState != Collectables::TOKEN_COLLECTION_STATE_CUTSCENE)
				goto open_pause_menu;

			if (g_demoMode != 0 || InputManager::g_directionalInputCount != 3)
				goto finish_frame;

		open_pause_menu:
			if (Nu3D::Camera::g_targetTintFadeSpeed == 0 && Nu3D::Camera::g_cameraTintBlue != 0)
			{
				AudioManager::StopAndWait();
				AudioManager::FlushSoundVoices();
				g_pauseMenuBlinkTimer = 0;
				g_isPaused = 1;
				g_pauseMenuSelection = 0;
				g_pauseMenuState = 0;
				g_quitToTitleFlag = 0;
				AudioManager::PlaySoundEffect(0x3D, 0);
				g_pauseCameraTarget = Camera::g_renderCameraTransform;
				g_pauseCheatTimer = 0xEC4;
			}

		finish_frame:
			UpdateFrameTimers();
			RenderHUD();
			Actor::PopulateActiveActors();
			Nullsub3();
			RenderGame(1);
		}

		// FUNCTION: TOY2 0x0049E330 [PROVISIONAL]
		void PauseLoop()
		{
			if (g_pauseMenuBlinkTimer < 100)
				g_pauseMenuBlinkTimer = ((uint8_t)Renderer::g_frameDelta + (uint8_t)g_pauseMenuBlinkTimer) & 0x3F;

			if (g_attractModeTimer >= 0)
			{
				if (g_returnedToTitle == 0 && InputManager::g_curButtonsPressed != 0)
					g_attractModeInputTimer = g_attractModeTimer * 2;

				g_attractModeInputTimer -= Renderer::g_frameDelta;
				if (g_attractModeInputTimer <= 0 || (InputManager::g_curButtonsPressed & INPUT_SECRET_MENU) != 0)
				{
					g_isPaused = 0;
					g_levelTransition = 4;
					g_levelTransitionTimer = 0x2E;
					Nu3D::Camera::SetTint(0, 0, 0, 6);
				}
			}

			Vector3I cameraTarget;
			Vector3I movement;
			int32_t yawDelta;
			if (g_pauseCheatTimer <= 0xE10)
			{
				g_pauseCheatTimer -= Renderer::g_frameDelta;

			check_orbit_camera:
				if (g_pauseCheatTimer <= 0 && (g_gameplayStateFlags & 1) == 0 && Camera::g_scriptedCameraState == 0)
				{
					cameraTarget.x = g_buzzActor.posAngles.pos.x;
					cameraTarget.y = g_buzzActor.posAngles.pos.y - 0x4000;
					cameraTarget.z = g_buzzActor.posAngles.pos.z;

					Camera::g_renderCameraTransform.rotation.euler.angles.yaw =
						(Camera::g_renderCameraTransform.rotation.euler.angles.yaw + Renderer::g_frameDelta * 8) & 0xFFF;
					int32_t yaw = (int16_t)Camera::g_renderCameraTransform.rotation.euler.angles.yaw;
					movement.x = -Numerics::g_sinCosLUT[yaw] * 2;
					movement.y = 0;
					movement.z = -Numerics::g_sinCosLUT[(yaw + 0x400) & 0xFFF] * 2;
					g_pauseCheatTimer = 0;
					Collision::SweepAndSlide(&cameraTarget, &movement, 0x8000, 0, 0x100);
					cameraTarget.x += movement.x;
					cameraTarget.y += movement.y;
					cameraTarget.z += movement.z;
					goto smooth_camera;
				}
			}
			else
			{
				if (InputManager::g_curButtonsPressed == (INPUT_CAMERA_RIGHT | INPUT_VISOR_TOGGLE))
				{
					g_pauseCheatTimer -= Renderer::g_frameDelta;
					if (g_pauseCheatTimer <= 0xE10)
					{
						if (g_buzzActor.coinsCollected == 7)
							g_buzzActor.lives = 9;
						else if (g_buzzActor.coinsCollected == 5)
							g_buzzActor.health = 14;
						g_pauseCheatTimer = 0;
					}
					goto check_orbit_camera;
				}
				else
				{
					g_pauseCheatTimer = 0xEC4;
				}
			}

			cameraTarget = g_pauseCameraTarget.pos;
			Camera::g_renderCameraTransform.rotation.euler.angles.yaw &= 0xFFF;
			yawDelta = (Camera::g_renderCameraTransform.rotation.euler.angles.yaw - g_pauseCameraTarget.rotation.euler.angles.yaw) & 0xFFF;
			if (yawDelta > 0x800)
				yawDelta -= 0x1000;
			Camera::g_renderCameraTransform.rotation.euler.angles.yaw -= Renderer::g_frameDelta * yawDelta / 16;

		smooth_camera:
			Camera::g_renderCameraTransform.pos.x -= (Camera::g_renderCameraTransform.pos.x - cameraTarget.x) * Renderer::g_frameDelta / 16;
			Camera::g_renderCameraTransform.pos.y -= (Camera::g_renderCameraTransform.pos.y - cameraTarget.y) * Renderer::g_frameDelta / 16;
			Camera::g_renderCameraTransform.pos.z -= (Camera::g_renderCameraTransform.pos.z - cameraTarget.z) * Renderer::g_frameDelta / 16;

			Nu3D::Camera::ApplyTransformToCamera(&Camera::g_renderCameraTransform);
			RenderHUD();
			Nullsub3();
			RenderGame(1);
			MenuLoop();
		}

	}
}
