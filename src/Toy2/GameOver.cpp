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

// The game-over screen and the timed backdrop it shows: retail holds
// 0x00437A50 and 0x00437B20 as one object, in the order of this file.
namespace Toy2
{
	// FUNCTION: TOY2 0x00437A50 [MATCHED]
	void ShowTimedBackdrop(int32_t backdropIndex, int32_t displayFrames, int32_t minimumDisplayFrames)
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

		int32_t skipThreshold = displayFrames - minimumDisplayFrames;
		while (displayFrames != 0)
		{
			Nu3D::Camera::FadeToTargetTint();

			int32_t frameDelta = Renderer::g_frameDelta;
			if (displayFrames > 0)
			{
				displayFrames -= frameDelta;
				if (displayFrames <= 0)
					displayFrames = 0;
			}

			if (displayFrames <= 23 && frameDelta + displayFrames > 23)
				Nu3D::Camera::SetTint(0, 0, 0, 12);

			if ((InputManager::g_curButtonsPressed & 0xF000) != 0 && displayFrames < skipThreshold && displayFrames > 23)
				displayFrames = 24;
		}
	}

}

namespace Toy2
{
	namespace GameOver
	{
		// FUNCTION: TOY2 0x00437B20 [MATCHED]
		void Tick()
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
			SetBackdropByIndex(1);

			int32_t exitTimer = 0x4b0;
			AudioManager::PlayMusicOneShot(0x11);

			while (true)
			{
				Nu3D::Camera::FadeToTargetTint();
				MainMenu::RenderMenu();

				if (exitTimer > 0)
				{
					exitTimer -= Renderer::g_frameDelta;
					if (exitTimer <= 0)
						exitTimer = 0;
				}

				if (AudioManager::IsStreamActive() == 0)
				{
					if (exitTimer > 0x17)
						exitTimer = 0x17;
				}
				else if (exitTimer > 0x17)
				{
					goto skip_tint;
				}
				if (Renderer::g_frameDelta + exitTimer > 0x17)
				{
					Nu3D::Camera::SetTint(0, 0, 0, 12);
				}
			skip_tint:
				if (g_attractModeTimer >= 0 && (InputManager::g_curButtonsPressed & 1))
				{
					InputManager::g_curButtonsPressed |= 0x4000;
				}
				if ((InputManager::g_curButtonsPressed & 0xf000) == 0 || (InputManager::g_prevButtonsPressed & 0xf000) != 0 || exitTimer >= 0x474
					|| exitTimer <= 0x17)
				{
					if (exitTimer == 0)
					{
						AudioManager::StopAndWait();
						return;
					}
				}
				else
				{
					exitTimer = 0x18;
				}
			}
		}
	}
}
