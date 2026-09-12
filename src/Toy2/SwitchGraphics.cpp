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

// The wallpaper and the texture pages of the level-switch screen, with the
// CPU profile that retail compiled beside them: 0x0047EF80 through
// 0x0047F6B0, one object, in the order of this file.
namespace Toy2
{
	// GLOBAL: TOY2 0x00731F0C
	uint32_t g_cpuClockHz;

	// GLOBAL: TOY2 0x00726F44
	int32_t g_cpuProfileTotalMs;

	// GLOBAL: TOY2 0x00726F48
	int32_t g_cpuProfileSampleIndex;

	// GLOBAL: TOY2 0x00726F4C
	int32_t g_cpuProfileStartMs;

	// GLOBAL: TOY2 0x00726F50
	int32_t g_cpuProfileWorkIndex;

	// GLOBAL: TOY2 0x00726F54
	int32_t g_cpuProfileSamples[10];

	// GLOBAL: TOY2 0x00726F7C
	float g_cpuProfileWorkValue;

#pragma function(abs)
#pragma optimize("", off)
	// FUNCTION: TOY2 0x0047EF80 [MATCHED]
	void ProfileCPU()
	{
		g_cpuProfileTotalMs = 0;
		for (g_cpuProfileSampleIndex = 0; g_cpuProfileSampleIndex < 10; g_cpuProfileSampleIndex++)
		{
			g_cpuProfileStartMs = timeGetTime();
			for (g_cpuProfileWorkIndex = 0; g_cpuProfileWorkIndex < 1000000; g_cpuProfileWorkIndex++)
			{
				g_cpuProfileWorkValue = (float)g_cpuProfileWorkIndex;
				g_cpuProfileWorkValue = g_cpuProfileWorkValue / 50000.0f;
				g_cpuProfileWorkValue = g_cpuProfileWorkValue * 50000.0f;
			}

			g_cpuProfileSamples[g_cpuProfileSampleIndex] = abs((int32_t)timeGetTime() - g_cpuProfileStartMs);
			g_cpuProfileTotalMs += g_cpuProfileSamples[g_cpuProfileSampleIndex];
		}

		g_cpuProfileTotalMs /= 10;
		g_cpuClockHz = 56000 / g_cpuProfileTotalMs;
		g_cpuClockHz *= 1000000;
		Logger::Log("CalculateClockSpeed->time[0]=%i\n", g_cpuProfileSamples[0]);
		Logger::Log("                   ->time[1]=%i\n", g_cpuProfileSamples[1]);
		Logger::Log("                   ->time[2]=%i\n", g_cpuProfileSamples[2]);
		Logger::Log("                   ->time[3]=%i\n", g_cpuProfileSamples[3]);
		Logger::Log("                   ->time[4]=%i\n", g_cpuProfileSamples[4]);
		Logger::Log("                   ->time[5]=%i\n", g_cpuProfileSamples[5]);
		Logger::Log("                   ->time[6]=%i\n", g_cpuProfileSamples[6]);
		Logger::Log("                   ->time[7]=%i\n", g_cpuProfileSamples[7]);
		Logger::Log("                   ->time[8]=%i\n", g_cpuProfileSamples[8]);
		Logger::Log("                   ->time[9]=%i\n", g_cpuProfileSamples[9]);
		Logger::Log("                   ->avgtime=%i\n", g_cpuProfileTotalMs);
		Logger::Log("                   ->CPUSPEED=%u\n", g_cpuClockHz);
		g_cpuProfileWorkValue = (float)g_cpuClockHz;
		g_cpuProfileWorkValue = g_cpuProfileWorkValue / 1000000.0f;
		Logger::Log("INIT : CPU clock speed is %f MHZ\n", g_cpuProfileWorkValue);
	}
#pragma optimize("", on)
#pragma intrinsic(abs)

	// FUNCTION: TOY2 0x0047F250 [PROVISIONAL]
	void LoadSwitchWallpaper()
	{
		Logger::Log("LoadSwitchWallpaper\n");

		void* buffer = malloc(0xE1000);
		FileUtils::LoadFile("sky1.raw", buffer);

		for (int32_t textureIndex = 0; textureIndex < 6; ++textureIndex)
		{
			if (! SoftwareRenderer::g_softwareTextureData[textureIndex])
				SoftwareRenderer::g_softwareTextureData[textureIndex] = malloc(0x20000);
		}

		uint8_t* sourcePixel = (uint8_t*)buffer;
		uint16_t* destinationPixels = (uint16_t*)SoftwareRenderer::g_softwareTextureData[0];
		int32_t rowsRemaining = 0x100;
		if (SoftwareRenderer::g_bitsPerPixel == 16)
		{
			do
			{
				int32_t pixelsRemaining = 0x100;
				do
				{
					*destinationPixels++ =
						(((uint16_t)(sourcePixel[0] & 0xF8) << 5 | ((uint16_t)sourcePixel[1] & 0xFFFC)) << 3) | (uint16_t)(sourcePixel[2] >> 3);
					sourcePixel += 3;
				} while (--pixelsRemaining != 0);
				sourcePixel += 0x480;
			} while (--rowsRemaining != 0);

			sourcePixel = (uint8_t*)buffer + 0x300;
			destinationPixels = (uint16_t*)SoftwareRenderer::g_softwareTextureData[1];
			rowsRemaining = 0x100;
			do
			{
				int32_t pixelsRemaining = 0x100;
				do
				{
					*destinationPixels++ =
						(((uint16_t)(sourcePixel[0] & 0xF8) << 5 | ((uint16_t)sourcePixel[1] & 0xFFFC)) << 3) | (uint16_t)(sourcePixel[2] >> 3);
					sourcePixel += 3;
				} while (--pixelsRemaining != 0);
				sourcePixel += 0x480;
			} while (--rowsRemaining != 0);

			sourcePixel = (uint8_t*)buffer + 0x600;
			destinationPixels = (uint16_t*)SoftwareRenderer::g_softwareTextureData[2];
			rowsRemaining = 0x100;
			do
			{
				int32_t pixelsRemaining = 0x80;
				do
				{
					*destinationPixels++ =
						(((uint16_t)(sourcePixel[0] & 0xF8) << 5 | ((uint16_t)sourcePixel[1] & 0xFFFC)) << 3) | (uint16_t)(sourcePixel[2] >> 3);
					sourcePixel += 3;
				} while (--pixelsRemaining != 0);
				sourcePixel += 0x600;
				destinationPixels += 0x80;
			} while (--rowsRemaining != 0);

			sourcePixel = (uint8_t*)buffer + 0x78000;
			destinationPixels = (uint16_t*)SoftwareRenderer::g_softwareTextureData[3];
			rowsRemaining = 0xE0;
			do
			{
				int32_t pixelsRemaining = 0x100;
				do
				{
					*destinationPixels++ =
						(((uint16_t)(sourcePixel[0] & 0xF8) << 5 | ((uint16_t)sourcePixel[1] & 0xFFFC)) << 3) | (uint16_t)(sourcePixel[2] >> 3);
					sourcePixel += 3;
				} while (--pixelsRemaining != 0);
				sourcePixel += 0x480;
			} while (--rowsRemaining != 0);

			sourcePixel = (uint8_t*)buffer + 0x78300;
			destinationPixels = (uint16_t*)SoftwareRenderer::g_softwareTextureData[4];
			rowsRemaining = 0xE0;
			do
			{
				int32_t pixelsRemaining = 0x100;
				do
				{
					*destinationPixels++ =
						(((uint16_t)(sourcePixel[0] & 0xF8) << 5 | ((uint16_t)sourcePixel[1] & 0xFFFC)) << 3) | (uint16_t)(sourcePixel[2] >> 3);
					sourcePixel += 3;
				} while (--pixelsRemaining != 0);
				sourcePixel += 0x480;
			} while (--rowsRemaining != 0);

			sourcePixel = (uint8_t*)buffer + 0x78600;
			destinationPixels = (uint16_t*)SoftwareRenderer::g_softwareTextureData[5];
			rowsRemaining = 0xE0;
			do
			{
				int32_t pixelsRemaining = 0x80;
				do
				{
					*destinationPixels++ =
						(((uint16_t)(sourcePixel[0] & 0xF8) << 5 | ((uint16_t)sourcePixel[1] & 0xFFFC)) << 3) | (uint16_t)(sourcePixel[2] >> 3);
					sourcePixel += 3;
				} while (--pixelsRemaining != 0);
				sourcePixel += 0x600;
				destinationPixels += 0x80;
			} while (--rowsRemaining != 0);

			free(buffer);
			return;
		}
		else
		{
			do
			{
				int32_t pixelsRemaining = 0x100;
				do
				{
					*destinationPixels++ =
						((((uint16_t)sourcePixel[0] & 0xFFF8) << 5 | ((uint16_t)sourcePixel[1] & 0xFFF8)) << 2) | (uint16_t)(sourcePixel[2] >> 3);
					sourcePixel += 3;
				} while (--pixelsRemaining != 0);
				sourcePixel += 0x480;
			} while (--rowsRemaining != 0);

			sourcePixel = (uint8_t*)buffer + 0x300;
			destinationPixels = (uint16_t*)SoftwareRenderer::g_softwareTextureData[1];
			rowsRemaining = 0x100;
			do
			{
				int32_t pixelsRemaining = 0x100;
				do
				{
					*destinationPixels++ =
						((((uint16_t)sourcePixel[0] & 0xFFF8) << 5 | ((uint16_t)sourcePixel[1] & 0xFFF8)) << 2) | (uint16_t)(sourcePixel[2] >> 3);
					sourcePixel += 3;
				} while (--pixelsRemaining != 0);
				sourcePixel += 0x480;
			} while (--rowsRemaining != 0);

			sourcePixel = (uint8_t*)buffer + 0x600;
			destinationPixels = (uint16_t*)SoftwareRenderer::g_softwareTextureData[2];
			rowsRemaining = 0x100;
			do
			{
				int32_t pixelsRemaining = 0x80;
				do
				{
					*destinationPixels++ =
						((((uint16_t)sourcePixel[0] & 0xFFF8) << 5 | ((uint16_t)sourcePixel[1] & 0xFFF8)) << 2) | (uint16_t)(sourcePixel[2] >> 3);
					sourcePixel += 3;
				} while (--pixelsRemaining != 0);
				sourcePixel += 0x600;
				destinationPixels += 0x80;
			} while (--rowsRemaining != 0);

			sourcePixel = (uint8_t*)buffer + 0x78000;
			destinationPixels = (uint16_t*)SoftwareRenderer::g_softwareTextureData[3];
			rowsRemaining = 0xE0;
			do
			{
				int32_t pixelsRemaining = 0x100;
				do
				{
					*destinationPixels++ =
						((((uint16_t)sourcePixel[0] & 0xFFF8) << 5 | ((uint16_t)sourcePixel[1] & 0xFFF8)) << 2) | (uint16_t)(sourcePixel[2] >> 3);
					sourcePixel += 3;
				} while (--pixelsRemaining != 0);
				sourcePixel += 0x480;
			} while (--rowsRemaining != 0);

			sourcePixel = (uint8_t*)buffer + 0x78300;
			destinationPixels = (uint16_t*)SoftwareRenderer::g_softwareTextureData[4];
			rowsRemaining = 0xE0;
			do
			{
				int32_t pixelsRemaining = 0x100;
				do
				{
					*destinationPixels++ =
						((((uint16_t)sourcePixel[0] & 0xFFF8) << 5 | ((uint16_t)sourcePixel[1] & 0xFFF8)) << 2) | (uint16_t)(sourcePixel[2] >> 3);
					sourcePixel += 3;
				} while (--pixelsRemaining != 0);
				sourcePixel += 0x480;
			} while (--rowsRemaining != 0);

			sourcePixel = (uint8_t*)buffer + 0x78600;
			destinationPixels = (uint16_t*)SoftwareRenderer::g_softwareTextureData[5];
			rowsRemaining = 0xE0;
			do
			{
				int32_t pixelsRemaining = 0x80;
				do
				{
					*destinationPixels++ =
						((((uint16_t)sourcePixel[0] & 0xFFF8) << 5 | ((uint16_t)sourcePixel[1] & 0xFFF8)) << 2) | (uint16_t)(sourcePixel[2] >> 3);
					sourcePixel += 3;
				} while (--pixelsRemaining != 0);
				sourcePixel += 0x600;
				destinationPixels += 0x80;
			} while (--rowsRemaining != 0);
		}

		free(buffer);
	}

	// FUNCTION: TOY2 0x0047F6B0 [PROVISIONAL]
	void LoadSwitchTPage()
	{
		Logger::Log("LoadSwitchTPage\n");

		void* buffer = malloc(0x30000);
		FileUtils::LoadFile("soft.raw", buffer);

		if (! SoftwareRenderer::g_softwareTextureData[6])
			SoftwareRenderer::g_softwareTextureData[6] = malloc(0x20000);

		uint16_t* destinationPixels = (uint16_t*)SoftwareRenderer::g_softwareTextureData[6];
		int32_t pixelsRemaining = 0x10000;
		uint8_t* sourcePixel = (uint8_t*)buffer + 2;

		if (SoftwareRenderer::g_bitsPerPixel == 16)
		{
			do
			{
				*destinationPixels++ =
					(((uint16_t)(sourcePixel[-2] & 0xF8) << 5 | ((uint16_t)sourcePixel[-1] & 0xFFFC)) << 3) | (uint16_t)(sourcePixel[0] >> 3);
				sourcePixel += 3;
			} while (--pixelsRemaining != 0);
		}
		else
		{
			do
			{
				*destinationPixels++ =
					((((uint16_t)sourcePixel[-2] & 0xFFF8) << 5 | ((uint16_t)sourcePixel[-1] & 0xFFF8)) << 2) | (uint16_t)(sourcePixel[0] >> 3);
				sourcePixel += 3;
			} while (--pixelsRemaining != 0);
		}

		free(buffer);
	}
}
