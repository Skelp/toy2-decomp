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

// The level graphics load: retail holds 0x004500A0 as one object.
namespace Toy2
{
	// FUNCTION: TOY2 0x004500A0 [MATCHED]
	void LoadLevelGraphics(int32_t levelFileIndex)
	{
		Levels::FlushRenderer();
		Levels::InitLevelDefaults();
		Levels::g_levelLoadArena = Levels::g_levelDataHeapBase;

		switch (levelFileIndex)
		{
			case -1: {
				char filePath[256];
				RawLoader::LoadPacketData("gfx\\film.raw");
				FileUtils::AppendRegPathToBuffer();
				sprintf(filePath, "%s%s", FileUtils::g_fileNameBuffer, "gfx\\film.raw");
				char* rawExtension = strstr(filePath, ".raw");
				if (rawExtension != NULL)
				{
					strcpy(rawExtension, ".ngn");
					NGNLoader::SetNewImage(filePath);
					NGNLoader::DetectBackdropTextures();
				}
				return;
			}
			case 0: {
				char filePath[256];
				RawLoader::LoadPacketData("gfx\\loading.raw");
				FileUtils::AppendRegPathToBuffer();
				sprintf(filePath, "%s%s", FileUtils::g_fileNameBuffer, "gfx\\loading.raw");
				char* rawExtension = strstr(filePath, ".raw");
				if (rawExtension != NULL)
				{
					strcpy(rawExtension, ".ngn");
					NGNLoader::SetNewImage(filePath);
					NGNLoader::DetectBackdropTextures();
				}
				return;
			}
			case 1: {
				char filePath[256];
				RawLoader::LoadPacketData("gfx\\level1a.raw");
				FileUtils::AppendRegPathToBuffer();
				sprintf(filePath, "%s%s", FileUtils::g_fileNameBuffer, "gfx\\level1a.raw");
				char* rawExtension = strstr(filePath, ".raw");
				if (rawExtension != NULL)
				{
					strcpy(rawExtension, ".ngn");
					NGNLoader::SetNewImage(filePath);
					NGNLoader::DetectBackdropTextures();
				}
				return;
			}
			case 2: {
				char filePath[256];
				RawLoader::LoadPacketData("gfx\\level1b.raw");
				FileUtils::AppendRegPathToBuffer();
				sprintf(filePath, "%s%s", FileUtils::g_fileNameBuffer, "gfx\\level1b.raw");
				char* rawExtension = strstr(filePath, ".raw");
				if (rawExtension != NULL)
				{
					strcpy(rawExtension, ".ngn");
					NGNLoader::SetNewImage(filePath);
					NGNLoader::DetectBackdropTextures();
				}
				return;
			}
			case 3: {
				char filePath[256];
				RawLoader::LoadPacketData("gfx\\level1c.raw");
				FileUtils::AppendRegPathToBuffer();
				sprintf(filePath, "%s%s", FileUtils::g_fileNameBuffer, "gfx\\level1c.raw");
				char* rawExtension = strstr(filePath, ".raw");
				if (rawExtension != NULL)
				{
					strcpy(rawExtension, ".ngn");
					NGNLoader::SetNewImage(filePath);
					NGNLoader::DetectBackdropTextures();
				}
				return;
			}
			case 4: {
				char filePath[256];
				RawLoader::LoadPacketData("gfx\\level2a.raw");
				FileUtils::AppendRegPathToBuffer();
				sprintf(filePath, "%s%s", FileUtils::g_fileNameBuffer, "gfx\\level2a.raw");
				char* rawExtension = strstr(filePath, ".raw");
				if (rawExtension != NULL)
				{
					strcpy(rawExtension, ".ngn");
					NGNLoader::SetNewImage(filePath);
					NGNLoader::DetectBackdropTextures();
				}
				return;
			}
			case 5: {
				char filePath[256];
				RawLoader::LoadPacketData("gfx\\level2b.raw");
				FileUtils::AppendRegPathToBuffer();
				sprintf(filePath, "%s%s", FileUtils::g_fileNameBuffer, "gfx\\level2b.raw");
				char* rawExtension = strstr(filePath, ".raw");
				if (rawExtension != NULL)
				{
					strcpy(rawExtension, ".ngn");
					NGNLoader::SetNewImage(filePath);
					NGNLoader::DetectBackdropTextures();
				}
				return;
			}
			case 6: {
				char filePath[256];
				RawLoader::LoadPacketData("gfx\\level2c.raw");
				FileUtils::AppendRegPathToBuffer();
				sprintf(filePath, "%s%s", FileUtils::g_fileNameBuffer, "gfx\\level2c.raw");
				char* rawExtension = strstr(filePath, ".raw");
				if (rawExtension != NULL)
				{
					strcpy(rawExtension, ".ngn");
					NGNLoader::SetNewImage(filePath);
					NGNLoader::DetectBackdropTextures();
				}
				return;
			}
			case 7: {
				char filePath[256];
				RawLoader::LoadPacketData("gfx\\level3a.raw");
				FileUtils::AppendRegPathToBuffer();
				sprintf(filePath, "%s%s", FileUtils::g_fileNameBuffer, "gfx\\level3a.raw");
				char* rawExtension = strstr(filePath, ".raw");
				if (rawExtension != NULL)
				{
					strcpy(rawExtension, ".ngn");
					NGNLoader::SetNewImage(filePath);
					NGNLoader::DetectBackdropTextures();
				}
				return;
			}
			case 8: {
				char filePath[256];
				RawLoader::LoadPacketData("gfx\\level3b.raw");
				FileUtils::AppendRegPathToBuffer();
				sprintf(filePath, "%s%s", FileUtils::g_fileNameBuffer, "gfx\\level3b.raw");
				char* rawExtension = strstr(filePath, ".raw");
				if (rawExtension != NULL)
				{
					strcpy(rawExtension, ".ngn");
					NGNLoader::SetNewImage(filePath);
					NGNLoader::DetectBackdropTextures();
				}
				return;
			}
			case 9: {
				char filePath[256];
				RawLoader::LoadPacketData("gfx\\level3c.raw");
				FileUtils::AppendRegPathToBuffer();
				sprintf(filePath, "%s%s", FileUtils::g_fileNameBuffer, "gfx\\level3c.raw");
				char* rawExtension = strstr(filePath, ".raw");
				if (rawExtension != NULL)
				{
					strcpy(rawExtension, ".ngn");
					NGNLoader::SetNewImage(filePath);
					NGNLoader::DetectBackdropTextures();
				}
				return;
			}
			case 10:
				RawLoader::LoadRawAndNGN("gfx\\level4a.raw");
				return;
			case 11:
				RawLoader::LoadRawAndNGN("gfx\\level4b.raw");
				return;
			case 12:
				RawLoader::LoadRawAndNGN("gfx\\level4c.raw");
				return;
			case 13:
				RawLoader::LoadRawAndNGN("gfx\\level5a.raw");
				return;
			case 14:
				RawLoader::LoadRawAndNGN("gfx\\level5b.raw");
				return;
			case 15:
				RawLoader::LoadRawAndNGN("gfx\\level5c.raw");
				return;
			case 16:
			case 17:
				RawLoader::LoadRawAndNGN("gfx\\congrats.raw");
				return;
			case 30:
				RawLoader::LoadRawAndNGN("gfx\\bonus.raw");
				return;
			case 31:
				RawLoader::LoadRawAndNGN("gfx\\gamewin.raw");
				return;
			case 35:
			case 38:
			case 41:
			case 44:
			case 47:
				RawLoader::LoadRawAndNGN("gfx\\boss.raw");
				return;
			default:
				RawLoader::LoadRawAndNGN("gfx\\level1a.raw");
				return;
		}
	}
}
