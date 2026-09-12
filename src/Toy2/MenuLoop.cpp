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

// The menu loop of the game shell, with the frame phase it advances:
// retail holds 0x0049F490 and 0x0049F4B0 as one object.
namespace Toy2
{
	// FUNCTION: TOY2 0x0049F490 [MATCHED]
	void AdvanceFramePhase() { g_framePhase = (g_framePhase + 1) & 0xF; }

}

namespace Toy2
{
	namespace Game
	{
		// FUNCTION: TOY2 0x0049F4B0 [PROVISIONAL]
		void MenuLoop()
		{
			int32_t restoreCamera = 0;
			if ((InputManager::g_curButtonsPressed & INPUT_SECRET_MENU) != 0 && (InputManager::g_prevButtonsPressed & INPUT_SECRET_MENU) == 0)
			{
				AudioManager::PlaySoundEffect(0x3D, 0);
				g_pauseMenuState = PAUSE_MENU_SECRET;
				g_pauseMenuSelection = 0;
			}

			if ((InputManager::g_curButtonsPressed & INPUT_DOWN) != 0 && (InputManager::g_prevButtonsPressed & INPUT_DOWN) == 0
				&& g_pauseMenuSelection < g_pauseMenuEntryCounts[g_pauseMenuState] - 1)
			{
				AudioManager::PlaySoundEffect(0x3F, 0);
				g_pauseMenuSelection++;
				g_pauseCheatTimer = 0xEC4;
			}

			if ((InputManager::g_curButtonsPressed & INPUT_UP) != 0 && (InputManager::g_prevButtonsPressed & INPUT_UP) == 0 && g_pauseMenuSelection > 0)
			{
				AudioManager::PlaySoundEffect(0x3F, 0);
				g_pauseMenuSelection--;
				g_pauseCheatTimer = 0xEC4;
			}

			int32_t cancelPressed = (InputManager::g_curButtonsPressed & INPUT_CANCEL) != 0 && (InputManager::g_prevButtonsPressed & INPUT_CANCEL) == 0;
			if (((InputManager::g_curButtonsPressed & INPUT_JUMP) != 0 && (InputManager::g_prevButtonsPressed & INPUT_JUMP) == 0) || cancelPressed)
			{
				switch (g_pauseMenuState)
				{
					case PAUSE_MENU_MAIN:
						if (cancelPressed)
							break;

						if (g_pauseMenuSelection == 0)
						{
							AudioManager::PlaySoundEffect(0x3E, 0);
							g_isPaused = 0;
							AudioManager::PlayMusicLooping((int16_t)AudioManager::g_curTrackIndex);
							if (g_buzzActor.airborneMode == 0)
								g_buzzActor.airborneMode = 5;
							g_movementInputLockTimer = 10;
							restoreCamera = 1;
						}
						if (g_pauseMenuSelection == 1)
						{
							AudioManager::PlaySoundEffect(0x3D, 0);
							g_pauseMenuState = PAUSE_MENU_CAMERA;
							g_pauseMenuSelection = 0;
						}
						else if (g_pauseMenuSelection == 2)
						{
							g_pauseMusicVolume = SaveManager::g_save0Data.musicVolume;
							g_pauseSoundVolume = SaveManager::g_save0Data.soundVolume;
							AudioManager::PlaySoundEffect(0x3D, 0);
							g_pauseMenuState = PAUSE_MENU_VOLUME;
							g_pauseMenuSelection = 0;
						}
						else if (g_pauseMenuSelection == 3)
						{
							AudioManager::PlaySoundEffect(0x3D, 0);
							g_pauseMenuState = PAUSE_MENU_QUIT;
							g_pauseMenuSelection = 0;
						}
						break;

					case PAUSE_MENU_CAMERA:
						if (g_pauseMenuSelection == 0)
						{
							if (! cancelPressed)
								SaveManager::g_save0Data.cameraType &= ~SaveManager::CAMERA_ACTIVE;
							g_pauseMenuState = PAUSE_MENU_MAIN;
						}
						else if (g_pauseMenuSelection == 1)
						{
							if (! cancelPressed)
								SaveManager::g_save0Data.cameraType |= SaveManager::CAMERA_ACTIVE;
							g_pauseMenuState = PAUSE_MENU_MAIN;
							g_pauseMenuSelection = 0;
						}
						AudioManager::PlaySoundEffect(0x3E, 0);
						break;

					case PAUSE_MENU_VOLUME:
						if (g_pauseMenuSelection == 0)
							g_pauseMenuState = PAUSE_MENU_MAIN;
						else if (g_pauseMenuSelection == 1)
						{
							g_pauseMenuState = PAUSE_MENU_MAIN;
							g_pauseMenuSelection = 0;
						}

						if (! cancelPressed)
						{
							SaveManager::g_save0Data.soundVolume = (uint8_t)g_pauseSoundVolume;
							SaveManager::g_save0Data.musicVolume = (uint8_t)g_pauseMusicVolume;
						}
						AudioManager::SetVolumes(AudioManager::g_musicVolTable[SaveManager::g_save0Data.musicVolume] * 2 / 3,
							AudioManager::g_soundVolTable[SaveManager::g_save0Data.soundVolume] * 3 / 2);
						AudioManager::PlaySoundEffect(0x3E, 0);
						break;

					case PAUSE_MENU_QUIT:
						if (cancelPressed)
							g_pauseMenuSelection = 0;

						if (g_pauseMenuSelection == 0)
						{
							g_pauseMenuState = PAUSE_MENU_MAIN;
							AudioManager::PlaySoundEffect(0x3E, 0);
							g_quitToTitleFlag = 0;
						}
						if (g_pauseMenuSelection == 1)
						{
							g_isPaused = 0;
							AudioManager::PlaySoundEffect(0x3D, 0);
							restoreCamera = 1;
							if (g_levelTransition != 1)
								g_levelTransition = 5;
							g_buzzActor.actorFlags |= Buzz::ACTOR_FLAG_LOCK_FACING;
							g_levelTransitionTimer = 0x2E;
							Nu3D::Camera::g_targetTintRed = 0;
							Nu3D::Camera::g_targetTintGreen = 0;
							Nu3D::Camera::g_targetTintBlue = 0;
							Nu3D::Camera::g_targetTintFadeSpeed = 6;
							Nu3D::Camera::g_tintBlend = -1;
						}
						break;

					case PAUSE_MENU_SECRET:
						if (cancelPressed)
							g_pauseMenuSelection = 0;

						if (g_pauseMenuSelection == 0)
						{
							AudioManager::PlaySoundEffect(0x3E, 0);
							g_isPaused = 0;
							AudioManager::PlayMusicLooping((int16_t)AudioManager::g_curTrackIndex);
							if (g_buzzActor.airborneMode == 0)
								g_buzzActor.airborneMode = 5;
							g_movementInputLockTimer = 10;
							restoreCamera = 1;
						}
						if (g_pauseMenuSelection == 1)
						{
							AudioManager::PlaySoundEffect(0x3D, 0);
							g_pauseMenuState = PAUSE_MENU_QUIT;
							g_pauseMenuSelection = 0;
							g_quitToTitleFlag = 1;
						}
						break;
				}
			}

			if (g_pauseMenuState == PAUSE_MENU_VOLUME)
			{
				if (g_pauseSoundVolume > 0)
					memset(g_pauseSoundVolumeText + 4, '*', g_pauseSoundVolume);
				if (g_pauseSoundVolume < 10)
					memset(g_pauseSoundVolumeText + 4 + g_pauseSoundVolume, ' ', 10 - g_pauseSoundVolume);
				if (g_pauseMusicVolume > 0)
					memset(g_pauseMusicVolumeText + 4, '*', g_pauseMusicVolume);
				if (g_pauseMusicVolume < 10)
					memset(g_pauseMusicVolumeText + 4 + g_pauseMusicVolume, ' ', 10 - g_pauseMusicVolume);

				int32_t selectedVolume = g_pauseMenuSelection == 0 ? g_pauseSoundVolume : g_pauseMusicVolume;
				if ((InputManager::g_curButtonsPressed & INPUT_RIGHT) != 0 && (InputManager::g_prevButtonsPressed & INPUT_RIGHT) == 0 && selectedVolume < 10)
				{
					selectedVolume++;
					AudioManager::PlaySoundEffect(0x3D, 0);
				}
				if ((InputManager::g_curButtonsPressed & INPUT_LEFT) != 0 && (InputManager::g_prevButtonsPressed & INPUT_LEFT) == 0 && selectedVolume > 0)
				{
					selectedVolume--;
					AudioManager::PlaySoundEffect(0x3D, 0);
				}

				if (g_pauseMenuSelection == 0)
					g_pauseSoundVolume = selectedVolume;
				else
					g_pauseMusicVolume = selectedVolume;

				AudioManager::SetVolumes(AudioManager::g_musicVolTable[g_pauseMusicVolume] * 2 / 3, AudioManager::g_soundVolTable[g_pauseSoundVolume] * 3 / 2);
			}

			if ((InputManager::g_curButtonsPressed & INPUT_MENU) != 0 && (InputManager::g_prevButtonsPressed & INPUT_MENU) == 0)
			{
				AudioManager::PlaySoundEffect(0x3E, 0);
				g_isPaused = 0;
				AudioManager::PlayMusicLooping((int16_t)AudioManager::g_curTrackIndex);
				if (g_buzzActor.airborneMode == 0)
					g_buzzActor.airborneMode = 5;
				g_movementInputLockTimer = 10;
				restoreCamera = 1;
			}

			if (restoreCamera)
				Camera::g_renderCameraTransform = g_pauseCameraTarget;
		}
	}
}
