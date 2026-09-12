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

// The frame timers of the game shell: retail holds 0x004A2960 as one
// object.
namespace Toy2
{
	// FUNCTION: TOY2 0x004A2960 [MATCHED]
	void UpdateFrameTimers()
	{
		uint8_t frameDelta = (uint8_t)Renderer::g_frameDelta;

		g_framePulseOutputs.frameDelta = frameDelta;
		g_framePulseOutputs.twoTickCount = 0;
		g_framePulsePhases.twoTick += frameDelta;
		while (g_framePulsePhases.twoTick >= 2)
		{
			g_framePulsePhases.twoTick -= 2;
			g_framePulseOutputs.twoTickCount++;
		}

		g_framePulseOutputs.threeTick = 0;
		g_framePulsePhases.threeTick += frameDelta;
		while (g_framePulsePhases.threeTick >= 3)
		{
			g_framePulsePhases.threeTick -= 3;
			g_framePulseOutputs.threeTick++;
		}

		g_framePulseOutputs.fourTick = 0;
		g_framePulsePhases.fourTick += frameDelta;
		if (g_framePulsePhases.fourTick >= 4)
		{
			g_framePulsePhases.fourTick -= 4;
			g_framePulseOutputs.fourTick = 1;
		}

		g_framePulseOutputs.fiveTick = 0;
		g_framePulsePhases.fiveTick += frameDelta;
		if (g_framePulsePhases.fiveTick >= 5)
		{
			g_framePulsePhases.fiveTick -= 5;
			g_framePulseOutputs.fiveTick = 1;
		}

		g_framePulseOutputs.sixTick = 0;
		g_framePulsePhases.sixTick += frameDelta;
		if (g_framePulsePhases.sixTick >= 6)
		{
			g_framePulsePhases.sixTick -= 6;
			g_framePulseOutputs.sixTick = 1;
		}

		g_framePulseOutputs.sevenTick = 0;
		g_framePulsePhases.sevenTick += frameDelta;
		if (g_framePulsePhases.sevenTick >= 7)
		{
			g_framePulsePhases.sevenTick -= 7;
			g_framePulseOutputs.sevenTick = 1;
		}

		g_framePulseOutputs.eightTick = 0;
		g_framePulsePhases.eightTick += frameDelta;
		if (g_framePulsePhases.eightTick >= 8)
		{
			g_framePulsePhases.eightTick -= 8;
			g_framePulseOutputs.eightTick = 1;
		}

		g_framePulseOutputs.sixteenTick = 0;
		g_framePulsePhases.sixteenTick += frameDelta;
		if (g_framePulsePhases.sixteenTick >= 16)
		{
			g_framePulsePhases.sixteenTick -= 16;
			g_framePulseOutputs.sixteenTick = 1;
		}

		g_framePulseOutputs.thirtyTwoTick = 0;
		g_framePulsePhases.thirtyTwoTick += frameDelta;
		if (g_framePulsePhases.thirtyTwoTick >= 32)
		{
			g_framePulsePhases.thirtyTwoTick -= 32;
			g_framePulseOutputs.thirtyTwoTick = 1;
		}

		g_framePulseOutputs.sixtyFourTick = 0;
		g_framePulsePhases.sixtyFourTick += frameDelta;
		if (g_framePulsePhases.sixtyFourTick >= 64)
		{
			g_framePulsePhases.sixtyFourTick -= 64;
			g_framePulseOutputs.sixtyFourTick = 1;
		}

		int32_t activeSoundMask = 0;
		if (AudioManager::g_maxLeftVolume > 0)
		{
			AudioManager::g_maxLeftVolume -= (int16_t)Renderer::g_frameDelta;
			activeSoundMask = 1;
		}

		if (AudioManager::g_maxRightVolume > 0)
		{
			AudioManager::g_maxRightVolume -= (int16_t)Renderer::g_frameDelta;
			activeSoundMask |= (uint8_t)AudioManager::g_maxVolume & 0xFE;
		}
		else
		{
			AudioManager::g_maxVolume = 0;
		}

		if (g_demoMode != 1 && (SaveManager::g_save0Data.cameraType & SaveManager::CAMERA_PASSIVE) != 0)
		{
			if (activeSoundMask != 0)
			{
				g_cameraIdleTimer += Renderer::g_frameDelta;
				if (g_cameraIdleTimer > 360)
					g_cameraIdleTimer = 300;
			}
			else
			{
				g_cameraIdleTimer = 0;
			}
		}

		AudioManager::UpdateSoundSequences();

		if (g_randDatBufferPtr > &g_randDatBuffer[1500])
			g_randDatBufferPtr -= 1500;

		if (g_levelTransitionTimer > 0)
		{
			g_levelTransitionTimer -= (int16_t)Renderer::g_frameDelta;
			if (g_levelTransitionTimer < 0)
				g_levelTransitionTimer = 0;
		}

		if (g_levelTransitionTimer < 46 && g_levelTransitionTimer + Renderer::g_frameDelta >= 46 && g_levelTransition > 0)
		{
			Nu3D::Camera::g_targetTintRed = 0;
			Nu3D::Camera::g_targetTintGreen = 0;
			Nu3D::Camera::g_targetTintBlue = 0;
			Nu3D::Camera::g_targetTintFadeSpeed = 6;
			Nu3D::Camera::g_tintBlend = -1;
		}

		Nu3D::Camera::FadeToTargetTint();
	}
}
