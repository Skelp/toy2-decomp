#include "SaveManager.h"
#include "InputManager.h"
#include "AudioManager/AudioManager.h"
#include "Toy2/Buzz.h"
#include "Toy2/Toy2.h"

#include <MEMORY.H>
#include <STDIO.H>
#include <STRING.H>

namespace SaveManager
{
	// GLOBAL: TOY2 0x0052EF90
	Save0Data g_save0Data;

	// GLOBAL: TOY2 0x00529B08
	Save99Data g_save99Data;

	// GLOBAL: TOY2 0x00830CA8
	uint32_t g_curLevelTokenData;

	// GLOBAL: TOY2 0x00528160
	char g_emptyString;

	// FUNCTION: TOY2 0x00415180 [MATCHED]
	void AddInputEntry(int32_t inputCode, int32_t controlId)
	{
		int32_t writeIndex = 0;

		for (int32_t readIndex = 0; readIndex < 38; readIndex++)
		{
			if (g_save99Data.saveStructs[readIndex].dInputCode != -1)
			{
				if (writeIndex != readIndex)
				{
					g_save99Data.saveStructs[writeIndex].dInputCode = g_save99Data.saveStructs[readIndex].dInputCode;
					g_save99Data.saveStructs[writeIndex].gameControlId = g_save99Data.saveStructs[readIndex].gameControlId;

					g_save99Data.saveStructs[readIndex].dInputCode = -1;
					g_save99Data.saveStructs[readIndex].gameControlId = 0;
				}

				++writeIndex;
			}
		}

		if (writeIndex < 38)
		{
			g_save99Data.saveStructs[writeIndex].dInputCode = inputCode;
			g_save99Data.saveStructs[writeIndex].gameControlId = controlId;
		}
	}

	// FUNCTION: TOY2 0x004151E0 [MATCHED]
	void ClearBindByControlId(int32_t controlId)
	{
		for (int32_t i = 0; i < 38; i++)
		{
			if (g_save99Data.saveStructs[i].gameControlId == controlId)
			{
				g_save99Data.saveStructs[i].dInputCode = TOY_INPUT_UNKNOWN;
				g_save99Data.saveStructs[i].gameControlId = 0;
			}
		}
	}

	// FUNCTION: TOY2 0x00415210 [MATCHED]
	void ClearBindByInputCode(int32_t inputCode)
	{
		for (int32_t i = 0; i < 38; i++)
		{
			if (g_save99Data.saveStructs[i].dInputCode == inputCode)
			{
				g_save99Data.saveStructs[i].dInputCode = TOY_INPUT_UNKNOWN;
				g_save99Data.saveStructs[i].gameControlId = 0;
			}
		}
	}

	// FUNCTION: TOY2 0x00415240 [MATCHED]
	int32_t GetInputCodeByControlId(int32_t controlId)
	{
		for (int32_t i = 0; i < 38; i++)
		{
			if (g_save99Data.saveStructs[i].gameControlId == controlId)
				return g_save99Data.saveStructs[i].dInputCode;
		}

		return TOY_INPUT_UNKNOWN;
	}

	// FUNCTION: TOY2 0x00415270 [MATCHED]
	int32_t GetControlSettingId(int32_t inputCode)
	{
		for (int32_t i = 0; i < 38; i++)
		{
			if (g_save99Data.saveStructs[i].dInputCode == inputCode)
				return g_save99Data.saveStructs[i].gameControlId;
		}

		return TOY_INPUT_UNKNOWN;
	}

	// FUNCTION: TOY2 0x00414F20 [PROVISIONAL]
	void Init()
	{
		memset(InputManager::g_previousInputStates, 255, sizeof(InputManager::g_previousInputStates));

		// Clear Save99Data entries
		SaveControlMapping* mapping = g_save99Data.saveStructs;

		do
		{
			mapping->dInputCode = TOY_INPUT_UNKNOWN;
			mapping->gameControlId = 0;
			mapping++;

		} while (mapping < g_save99Data.unusedStructs);

		AddInputEntry(TOY_INPUT_CAPITALA, INPUT_UP);
		AddInputEntry(TOY_INPUT_CAPITALB, INPUT_DOWN);
		AddInputEntry(TOY_INPUT_CAPITALC, INPUT_LEFT);
		AddInputEntry(TOY_INPUT_CAPITALD, INPUT_RIGHT);
		AddInputEntry(TOY_INPUT_X, INPUT_CANCEL);
		AddInputEntry(TOY_INPUT_SPACE, INPUT_JUMP);
		AddInputEntry(TOY_INPUT_LCONTROL, INPUT_FIRE);
		AddInputEntry(TOY_INPUT_LSHIFT, INPUT_SPIN);
		AddInputEntry(TOY_INPUT_NEXT, INPUT_CAMERA_RIGHT);
		AddInputEntry(TOY_INPUT_DEL, INPUT_CAMERA_LEFT);
		AddInputEntry(TOY_INPUT_Q, INPUT_TARGET_LOCK);
		AddInputEntry(TOY_INPUT_TAB, INPUT_VISOR_TOGGLE);
		AddInputEntry(TOY_INPUT_X, INPUT_SECRET_MENU);
		AddInputEntry(TOY_INPUT_RETURNKEY, INPUT_MENU);
		AddInputEntry(TOY_INPUT_JOY10, INPUT_CANCEL);
		AddInputEntry(TOY_INPUT_JOY1, INPUT_JUMP);
		AddInputEntry(TOY_INPUT_JOY2, INPUT_FIRE);
		AddInputEntry(TOY_INPUT_JOY3, INPUT_SPIN);
		AddInputEntry(TOY_INPUT_JOY8, INPUT_CAMERA_RIGHT);
		AddInputEntry(TOY_INPUT_JOY7, INPUT_CAMERA_LEFT);
		AddInputEntry(TOY_INPUT_JOY5, INPUT_TARGET_LOCK);
		AddInputEntry(TOY_INPUT_JOY4, INPUT_VISOR_TOGGLE);
		AddInputEntry(TOY_INPUT_JOY10, INPUT_SECRET_MENU);
		AddInputEntry(TOY_INPUT_JOY9, INPUT_MENU);
		AddInputEntry(TOY_INPUT_CAPITALA, INPUT_UP);
		AddInputEntry(TOY_INPUT_CAPITALB, INPUT_DOWN);
		AddInputEntry(TOY_INPUT_CAPITALC, INPUT_LEFT);
		AddInputEntry(TOY_INPUT_CAPITALD, INPUT_RIGHT);
		AddInputEntry(TOY_INPUT_ESC, INPUT_CANCEL);
		AddInputEntry(TOY_INPUT_F1, INPUT_JUMP);
		AddInputEntry(TOY_INPUT_F2, INPUT_MENU);
	}

	// FUNCTION: TOY2 0x004A2C20 [PROVISIONAL]
	void InitProgressData(Save0Data* save)
	{
		save->lastLevel = 0;
		save->unlocks = 0;
		save->health = 0xe;
		save->lives = 5;
		for (int32_t i = 1; i < 16; i++)
			save->tokens[i] = 0;
		memset(&save->moviesUnlocked, 0, sizeof(save->moviesUnlocked) + sizeof(save->padInt) + sizeof(save->padBytes));
	}

	// FUNCTION: TOY2 0x004A2CC0 [MATCHED]
	void LoadProgressData(Save0Data* save)
	{
		Toy2::g_buzzActor.lives = save->lives;
		Toy2::g_unlocks = save->unlocks;
		Toy2::g_levelIndex = save->lastLevel;
		Toy2::g_buzzActor.health = save->health;
		if (Toy2::g_levelIndex > 14)
			Toy2::g_levelIndex = 14;
		else if (Toy2::g_levelIndex < 0)
			Toy2::g_levelIndex = 0;
		g_curLevelTokenData = save->tokens[Toy2::g_levelFileConversion[Toy2::g_levelIndex]];
		AudioManager::SetVolumes(AudioManager::g_musicVolTable[save->musicVolume] * 2 / 3, AudioManager::g_soundVolTable[save->soundVolume] * 3 / 2);
	}

	// FUNCTION: TOY2 0x004A2C80 [MATCHED]
	void TransferProgressData(Save0Data* save)
	{
		save->lives = (uint8_t)Toy2::g_buzzActor.lives;
		save->lastLevel = (uint8_t)Toy2::g_levelIndex;
		save->unlocks = (uint8_t)Toy2::g_unlocks;
		save->health = (uint16_t)Toy2::g_buzzActor.health;
	}

	// FUNCTION: TOY2 0x0049B830 [MATCHED]
	void SaveToFile(int32_t saveNum, const char* saveName)
	{
		char nameChar = g_emptyString;
		if (saveName == NULL)
			saveName = &nameChar;

		char fileName[256];
		sprintf(fileName, "Toy2%02d.sav", saveNum);

		FILE* file = fopen(fileName, "wb");
		if (file)
		{
			int32_t nameLen = strlen(saveName);
			fwrite(&nameLen, 1, 4, file);
			if (nameLen != 0)
				fwrite(saveName, 1, nameLen, file);

			if (saveNum == 99)
				fwrite(&g_save99Data, 1, 0x188, file);
			else
				fwrite(&g_save0Data, 1, 0x188, file);

			fclose(file);
		}
	}
}
