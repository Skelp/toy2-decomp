#include "Toy2/Screens.h"
#include "Toy2/Toy2.h"
#include "Toy2/Levels.h"
#include "Toy2/LevelSelect.h"
#include "Toy2/MainMenu.h"
#include "SoftwareRenderer.h"
#include "Renderer/Renderer.h"
#include "Renderer/Sprite.h"
#include "Nu3D/Camera.h"
#include "AudioManager/AudioManager.h"
#include "InputManager.h"
#include "SaveManager.h"

namespace Toy2
{
	extern int32_t g_saveMenuState;

	int32_t PlayMovieWithTransition(int32_t movieId, int32_t backgroundId);
	int32_t TickSaveMenuMachine(int32_t postGameSave);

	namespace MovieViewer
	{
		int32_t Tick(int32_t movieIndex);
	}

	namespace PostGameSaveMenu
	{
		int32_t Tick();
	}

	// FUNCTION: TOY2 0x00453CF0 [MATCHED]
	int32_t ShowLevelSelect()
	{
		int32_t prevLevelFileIdx = Toy2::g_levelFileIndex;

		g_levelFileIndex = 16;
		Levels::g_levelLoadConfig = 56;

		g_hasStaticBackdrop = 0;
		Renderer::g_virtualScreenWidth = 512.0;
		Renderer::g_virtualScreenHeight = 256.0;
		g_nextBackdropId = 36;
		Levels::InitLevelPlay(16);

		Renderer::g_frameDelta = 2;
		g_hasBackdrop = 0;
		MainMenu::g_menuClearColor.b = 140;
		MainMenu::g_menuClearColor.g = 140;
		MainMenu::g_menuClearColor.r = 140;

		int32_t newState = LevelSelect::Tick();

		g_levelFileIndex = prevLevelFileIdx;

		MainMenu::g_menuClearColor.b = 32;
		MainMenu::g_menuClearColor.g = 32;
		MainMenu::g_menuClearColor.r = 32;

		return newState;
	}

	// FUNCTION: TOY2 0x00453D90 [MATCHED]
	void ShowActClearScreen()
	{
		int32_t previousLevelFileIndex = g_levelFileIndex;
		g_levelFileIndex += 32;
		Renderer::g_virtualScreenWidth = 320.0f;
		Renderer::g_virtualScreenHeight = 256.0f;
		LoadLevelGraphics(g_levelFileIndex);
		SoftwareRenderer::g_backdropScrollOverride.x = 0;
		SoftwareRenderer::g_backdropScrollOverride.y = 0;
		Nu3D::Camera::SetTint(128, 128, 128, 12);
		int32_t fadeTimer = 28;
		AudioManager::PlayMusicOneShot(21);

		do
		{
			fadeTimer -= Renderer::g_frameDelta;
			if (fadeTimer <= 0)
				fadeTimer = 0;
			Nu3D::Camera::FadeToTargetTint();
			MainMenu::RenderMenu();
		} while (fadeTimer != 0);

		int32_t isFadingOut = 0;
		int32_t blinkTimer = 0;
		SoftwareRenderer::g_backdropScrollOverride.x = 0;
		SoftwareRenderer::g_backdropScrollOverride.y = 0;
		fadeTimer = 28;
		do
		{
			Nu3D::Camera::FadeToTargetTint();
			if (isFadingOut)
			{
				fadeTimer -= Renderer::g_frameDelta;
				if (fadeTimer <= 0)
					fadeTimer = 0;
			}

			if ((InputManager::g_curButtonsPressed & INPUT_JUMP) != 0 && (InputManager::g_prevButtonsPressed & INPUT_JUMP) == 0 && ! isFadingOut)
			{
				Nu3D::Camera::SetTint(0, 0, 0, 12);
				isFadingOut = 1;
				AudioManager::PlaySoundEffect(7, 0);
			}

			blinkTimer = (blinkTimer + Renderer::g_frameDelta) & 0x3f;
			if (blinkTimer > 30)
				Renderer::Sprite::DrawScaled(96, 200, 128, 0, 255, 255, 255, 255, 2048, 2048);
			MainMenu::RenderMenu();
		} while (fadeTimer != 0);

		AudioManager::StopAndWait();
		g_hasStaticBackdrop = 0;
		SoftwareRenderer::g_backdropScrollOverride.x = -32768;
		SoftwareRenderer::g_backdropScrollOverride.y = -32768;
		Renderer::g_virtualScreenWidth = 512.0f;
		Renderer::g_virtualScreenHeight = 256.0f;
		g_nextBackdropId = 36;
		g_saveMenuState = 1;
		g_levelFileIndex = previousLevelFileIndex;
	}

	// FUNCTION: TOY2 0x00453F20 [MATCHED]
	int32_t ShowSaveScreen()
	{
		int32_t prevLevelFileIdx = g_levelFileIndex;

		g_levelFileIndex = 16;
		Levels::g_levelLoadConfig = 0xf8;
		g_hasStaticBackdrop = 0;
		Renderer::g_virtualScreenWidth = 512.0;
		Renderer::g_virtualScreenHeight = 256.0;
		g_nextBackdropId = 36;
		Levels::InitLevelPlay(16);

		g_saveMenuState = 0;
		SaveManager::TransferProgressData(&SaveManager::g_save0Data);
		int32_t result = TickSaveMenuMachine(0);
		SaveManager::LoadProgressData(&SaveManager::g_save0Data);

		g_levelFileIndex = prevLevelFileIdx;
		return result;
	}

	// FUNCTION: TOY2 0x00453FA0 [MATCHED]
	void ShowMovieViewer()
	{
		int32_t prevLevelFileIdx = g_levelFileIndex;

		g_levelFileIndex = 16;

		int32_t movieIdx = 0;
		while (true)
		{
			Levels::g_levelLoadConfig = 0xb8;
			Renderer::g_virtualScreenWidth = 320.0;
			Renderer::g_virtualScreenHeight = 256.0;
			Levels::InitLevelPlay(g_levelFileIndex);

			MainMenu::g_menuClearColor.b = 0;
			MainMenu::g_menuClearColor.g = 0;
			MainMenu::g_menuClearColor.r = 0;
			movieIdx = MovieViewer::Tick(movieIdx);

			if (movieIdx < 0)
				break;

			PlayMovieWithTransition(movieIdx + 10, 0);
		}

		g_levelFileIndex = prevLevelFileIdx;
	}

	// FUNCTION: TOY2 0x00454020 [MATCHED]
	void ShowPostGameSaveMenu()
	{
		int32_t prevLevelFileIdx = g_levelFileIndex;

		g_levelFileIndex = 16;

		if (g_saveMenuState)
		{
			Levels::g_levelLoadConfig = 0xf8;
			g_hasStaticBackdrop = 0;
			Renderer::g_virtualScreenWidth = 512.0;
			Renderer::g_virtualScreenHeight = 256.0;
			g_nextBackdropId = 36;
			Levels::InitLevelPlay(16);

			int32_t result = PostGameSaveMenu::Tick();
			g_saveMenuState = result;

			if (result)
			{
				g_saveMenuState = 1;
				SaveManager::TransferProgressData(&SaveManager::g_save0Data);
				TickSaveMenuMachine(1);
				SaveManager::LoadProgressData(&SaveManager::g_save0Data);
				g_saveMenuState = 0;
			}
		}

		g_levelFileIndex = prevLevelFileIdx;
	}

} // namespace Toy2
