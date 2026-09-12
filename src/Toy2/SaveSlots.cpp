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

// The save-slot screen: retail holds 0x0047B560 through 0x0047B8A0 as one
// object, in the order of this file.
namespace Toy2
{
	struct GameSaveSlot
	{
		int32_t x;
		int32_t y;
		int32_t empty;
		char label[12];
		SaveManager::Save0Data saveData;
	};

	enum GameSaveSlotMenuState
	{
		GAME_SAVE_SLOT_MENU_STATE_EDIT_NAME = 3,
		GAME_SAVE_SLOT_MENU_STATE_SELECT_SLOT = 30,
	};

	STATIC_ASSERT(offsetof(GameSaveSlot, saveData) == 0x18);

	STATIC_ASSERT(sizeof(GameSaveSlot) == 0x1A0);

	// GLOBAL: TOY2 0x00704E50
	int32_t g_selectedGameSaveSlotRed;

	// GLOBAL: TOY2 0x00704E54
	int32_t g_selectedGameSaveSlotGreen;

	// GLOBAL: TOY2 0x00704E58
	int32_t g_selectedGameSaveSlotBlue;

	// GLOBAL: TOY2 0x00704E5C
	int32_t g_selectedGameSaveSlotIndex;

	// GLOBAL: TOY2 0x004FB200
	GameSaveSlot g_gameSaveSlots[9] = {
		{ 8, 20, 1, "" },
		{ 8, 40, 1, "" },
		{ 8, 60, 1, "" },
		{ 8, 80, 1, "" },
		{ 8, 100, 1, "" },
		{ 8, 120, 1, "" },
		{ 8, 140, 1, "" },
		{ 8, 160, 1, "" },
		{ 8, -1, 1, "" },
	};

	// GLOBAL: TOY2 0x004FC0A0
	GameSaveSlot* g_gameSaveSlotPointers[9] = {
		&g_gameSaveSlots[0],
		&g_gameSaveSlots[1],
		&g_gameSaveSlots[2],
		&g_gameSaveSlots[3],
		&g_gameSaveSlots[4],
		&g_gameSaveSlots[5],
		&g_gameSaveSlots[6],
		&g_gameSaveSlots[7],
		(GameSaveSlot*)-1,
	};

	// FUNCTION: TOY2 0x0047B560 [MATCHED]
	void DrawGameSaveSlots(int32_t menuState)
	{
		int32_t slotIndex = 0;
		char emptyLabel[32];
		memset(emptyLabel, 0, sizeof(emptyLabel));
		strcat(emptyLabel, "Leer");

		GameSaveSlot** slotPointer = &g_gameSaveSlotPointers[1];
		GameSaveSlot* slot = g_gameSaveSlotPointers[0];
		while (slot != (GameSaveSlot*)-1)
		{
			if (slotIndex == g_selectedGameSaveSlotIndex && menuState == GAME_SAVE_SLOT_MENU_STATE_EDIT_NAME && Renderer::g_frameDelta % 20 < 10)
			{
				int32_t labelWidth = InputManager::CalculateKeyboardGlyphTextWidth(slot->label);
				InputManager::DrawKeyboardGlyphText(slot->x + labelWidth, slot->y + 10, "-");
			}

			const char* label = slot->empty ? emptyLabel : slot->label;

			if (slotIndex == g_selectedGameSaveSlotIndex)
			{
				InputManager::DrawColouredKeyboardGlyphText(
					slot->x, slot->y, label, g_selectedGameSaveSlotBlue, g_selectedGameSaveSlotGreen, g_selectedGameSaveSlotRed);
			}
			else
			{
				InputManager::DrawKeyboardGlyphText(slot->x, slot->y, label);
			}

			if (slotIndex == g_selectedGameSaveSlotIndex && menuState == GAME_SAVE_SLOT_MENU_STATE_SELECT_SLOT && Renderer::g_frameDelta % 20 < 10)
			{
				InputManager::DrawKeyboardGlyphText(slot->x + InputManager::g_renderedKeyboardGlyphTextWidth, slot->y, "]");
				InputManager::DrawKeyboardGlyphText(slot->x - 4, slot->y, "[");
			}

			++slotIndex;
			slot = *slotPointer;
			++slotPointer;
		}
	}

	// FUNCTION: TOY2 0x0047B6B0 [PROVISIONAL]
	int32_t LoadGameSaveSlot(GameSaveSlot* slot)
	{
		char fileName[10] = "game0.sav";
		char savePath[4096];
		char fullPath[8192];
		int32_t result = 0;

		memset(savePath, 0, sizeof(savePath));
		strcat(savePath, Ini::g_iniInstallSearchPath);
		strcat(savePath, "\\cd");
		strcat(savePath, "\\save\\");

		if (g_selectedGameSaveSlotIndex < 10)
		{
			fileName[4] = (char)(g_selectedGameSaveSlotIndex + '0');
			Logger::Log("SAVE : Attempting to load file %s in path %s.\n", fileName, savePath);

			memset(fullPath, 0, sizeof(fullPath));
			strcat(fullPath, savePath);
			strcat(fullPath, fileName);

			FILE* file = fopen(fullPath, "rb");
			if (file != NULL)
			{
				int32_t readCount = fread(slot, sizeof(GameSaveSlot), 1, file);
				if (readCount == 1)
				{
					result = readCount;
				}
				else
				{
					Logger::Log("ERROR - %s.\n", strerror(errno));
				}
				fclose(file);
			}
		}

		switch (result)
		{
			case 0:
				Logger::Log("LOAD : Failed to load file %s.\n", fullPath);
				break;
			case 1:
				Logger::Log("SAVE : Successfully loaded file %s.\n", fullPath);
				break;
		}

		return result;
	}

	// FUNCTION: TOY2 0x0047B8A0 [PROVISIONAL]
	int32_t SaveGameSaveSlot(GameSaveSlot* slot)
	{
		char fileName[10] = "game0.sav";
		char savePath[4096];
		char fullPath[8192];
		int32_t result = 0;

		memset(savePath, 0, sizeof(savePath));
		strcat(savePath, Ini::g_iniInstallSearchPath);
		strcat(savePath, "\\cd");
		strcat(savePath, "\\save\\");

		if (g_selectedGameSaveSlotIndex < 10)
		{
			fileName[4] = (char)(g_selectedGameSaveSlotIndex + '0');
			Logger::Log("SAVE : Attempting to save file %s in path %s.\n", fileName, savePath);

			memset(fullPath, 0, sizeof(fullPath));
			strcat(fullPath, savePath);
			strcat(fullPath, fileName);

			FILE* file = fopen(fullPath, "wb");
			if (file != NULL)
			{
				int32_t writeCount = fwrite(slot, sizeof(GameSaveSlot), 1, file);
				if (writeCount == 1)
				{
					result = writeCount;
				}
				else
				{
					Logger::Log("ERROR - %s.\n", strerror(errno));
				}
				fclose(file);
			}
		}

		switch (result)
		{
			case 0:
				Logger::Log("SAVE : Failed to save file %s.\n", fullPath);
				break;
			case 1:
				Logger::Log("SAVE : Successfully save file %s.\n", fullPath);
				break;
		}

		return result;
	}
}
