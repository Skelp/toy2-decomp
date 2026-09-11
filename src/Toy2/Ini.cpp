#include "Toy2/Toy2.h"
#include "Toy2/Ini.h"
#include "Logger.h"
#include "FileUtils.h"
#include "InputManager.h"
#include "SaveManager.h"
#include "AudioManager/AudioManager.h"

#include <STDIO.H>
#include <STRING.H>
#include <STDLIB.H>

namespace AudioManager
{
	extern char g_sfxSubPath[4];
}

namespace Toy2
{
	extern int32_t g_mpegPlaybackDisabled;

	namespace Ini
	{
		enum Keyword
		{
			KEYWORD_UNKNOWN = -1,
			KEYWORD_COMMENT = 11,
			KEYWORD_LANGUAGE = 12,
			KEYWORD_DEFTEXT = 13,
			KEYWORD_KEY = 14,
			KEYWORD_JOY = 15,
			KEYWORD_MESSAGE = 16,
			KEYWORD_QUIET = 17,
			KEYWORD_CHEAT = 18,
			KEYWORD_HIMPEG = 19,
			KEYWORD_NOMPEG = 20,
			KEYWORD_IF = 21,
			KEYWORD_LOG = 22,
			KEYWORD_ENDIF = 23,
			KEYWORD_DEFKEY = 24,
			KEYWORD_FIRE = 25,
			KEYWORD_JUMP = 26,
			KEYWORD_CAMLEFT = 27,
			KEYWORD_CAMRIGHT = 28,
			KEYWORD_HELMETVIEW = 29,
			KEYWORD_PAUSE = 30,
			KEYWORD_SELECT = 31,
			KEYWORD_DERVISH = 32,
			KEYWORD_PAD_RL = 37,
			KEYWORD_PAD_RU = 38,
			KEYWORD_PAD_RD = 39,
			KEYWORD_PAD_RR = 40,
			KEYWORD_PAD_FLT = 41,
			KEYWORD_PAD_FLB = 42,
			KEYWORD_PAD_FRT = 43,
			KEYWORD_PAD_FRB = 44,
			KEYWORD_PAD_SEL = 45,
			KEYWORD_PAD_START = 46,
			KEYWORD_FINISHED = 47,
		};

		enum ControlMappingSlot
		{
			CONTROL_MAPPING_PAD_RU = 4,
			CONTROL_MAPPING_JUMP = 5,
			CONTROL_MAPPING_FIRE = 6,
			CONTROL_MAPPING_DERVISH = 7,
			CONTROL_MAPPING_CAMLEFT = 8,
			CONTROL_MAPPING_CAMRIGHT = 9,
			CONTROL_MAPPING_PAD_FRT = 10,
			CONTROL_MAPPING_HELMETVIEW = 11,
			CONTROL_MAPPING_SELECT = 12,
			CONTROL_MAPPING_PAUSE = 13,
		};

		struct KeywordEntry
		{
			const char* name;
			int32_t keyword;
		};

		struct PaddedControlTextEntry
		{
			ControlTextEntry entry;
			int32_t reserved2;
		};

		struct PaddedMessageTextEntry
		{
			MessageTextEntry entry;
			int32_t reserved;
		};

		// GLOBAL: TOY2 0x004EFFC8
		PaddedControlTextEntry g_keyTextEntries[13] = {
			{ { 60, 40, 0, "forward", 0 }, 0 },
			{ { 60, 60, 0, "back", 0 }, 0 },
			{ { 60, 80, 0, "left", 0 }, 0 },
			{ { 60, 100, 0, "right", 0 }, 0 },
			{ { 60, 120, 0, "fire/get/drop", 0 }, 0 },
			{ { 60, 140, 0, "jump/change", 0 }, 0 },
			{ { 60, 160, 0, "seed color", 0 }, 0 },
			{ { 200, 40, 0, "camera", 0 }, 0 },
			{ { 200, 60, 0, "lock camera", 0 }, 0 },
			{ { 240, 80, 0, "walk", 0 }, 0 },
			{ { 200, 100, 0, "kick", 0 }, 0 },
			{ { 80, 180, 0, "game pad", 0 }, 0 },
			{ { 80, 200, 0, "restore defaults", 0 }, 0 },
		};

		// GLOBAL: TOY2 0x004F0100
		ControlTextEntry g_keyAcceptTextEntry = { 80, 220, 0, "accept", 0 };

		// GLOBAL: TOY2 0x004F0114
		ControlTextEntry* g_keyTextTable[15] = {
			&g_keyTextEntries[0].entry,
			&g_keyTextEntries[1].entry,
			&g_keyTextEntries[2].entry,
			&g_keyTextEntries[3].entry,
			&g_keyTextEntries[4].entry,
			&g_keyTextEntries[5].entry,
			&g_keyTextEntries[6].entry,
			&g_keyTextEntries[7].entry,
			&g_keyTextEntries[8].entry,
			&g_keyTextEntries[9].entry,
			&g_keyTextEntries[10].entry,
			&g_keyTextEntries[11].entry,
			&g_keyTextEntries[12].entry,
			&g_keyAcceptTextEntry,
			(ControlTextEntry*)-1,
		};

		// GLOBAL: TOY2 0x004F0150
		PaddedControlTextEntry g_joyTextEntries[9] = {
			{ { 40, 40, 0, "fire/get/drop", 0 }, 0 },
			{ { 40, 60, 0, "jump/change", 0x100 }, 0 },
			{ { 40, 80, 0, "seed color", 0x200 }, 0 },
			{ { 40, 100, 0, "camera", 0x300 }, 0 },
			{ { 200, 40, 0, "lock camera", 0x400 }, 0 },
			{ { 200, 60, 0, "walk", 0x500 }, 0 },
			{ { 200, 80, 0, "kick", 0x600 }, 0 },
			{ { 80, 180, 0, "keyboard", 0 }, 0 },
			{ { 80, 200, 0, "restore defaults", 0 }, 0 },
		};

		// GLOBAL: TOY2 0x004F0228
		ControlTextEntry g_joyAcceptTextEntry = { 80, 220, 0, "accept", 0 };

		// GLOBAL: TOY2 0x004F023C
		ControlTextEntry* g_joyTextTable[11] = {
			&g_joyTextEntries[0].entry,
			&g_joyTextEntries[1].entry,
			&g_joyTextEntries[2].entry,
			&g_joyTextEntries[3].entry,
			&g_joyTextEntries[4].entry,
			&g_joyTextEntries[5].entry,
			&g_joyTextEntries[6].entry,
			&g_joyTextEntries[7].entry,
			&g_joyTextEntries[8].entry,
			&g_joyAcceptTextEntry,
			(ControlTextEntry*)-1,
		};

		// GLOBAL: TOY2 0x004F4F60
		KeywordEntry g_keywords[] = {
#include "IniKeywords.inc"
		};

		// GLOBAL: TOY2 0x004F5398
		PaddedMessageTextEntry g_messageTextEntries[24] = {
			{ { 20, 200, "select option and press enter" }, 0 },
			{ { 90, 200, "() - to change" }, 0 },
			{ { 20, 220, "press enter to accept changes" }, 0 },
			{ { 50, 240, "press esc to go back" }, 0 },
			{ { 70, 120, "do you want to quit" }, 0 },
			{ { 90, 4, "video options" }, 0 },
			{ { 100, 4, "define keys" }, 0 },
			{ { 130, 4, "save game" }, 0 },
			{ { 10, 200, "select file" }, 0 },
			{ { 10, 220, "press esc to go back" }, 0 },
			{ { 130, 4, "confirm" }, 0 },
			{ { 80, 180, "do you wish to" }, 0 },
			{ { 70, 200, "save over this game" }, 0 },
			{ { 130, 4, "enter name" }, 0 },
			{ { 130, 4, "load game" }, 0 },
			{ { 10, 200, "select game and press enter" }, 0 },
			{ { 10, 220, "press esc to go back" }, 0 },
			{ { 0, 0, &SaveManager::g_emptyString }, 0 },
			{ { 0, 0, &SaveManager::g_emptyString }, 0 },
			{ { 0, 0, &SaveManager::g_emptyString }, 0 },
			{ { 0, 0, &SaveManager::g_emptyString }, 0 },
			{ { 0, 0, &SaveManager::g_emptyString }, 0 },
			{ { 0, 0, &SaveManager::g_emptyString }, 0 },
			{ { 0, 0, &SaveManager::g_emptyString }, 0 },
		};

		// GLOBAL: TOY2 0x004F5518
		MessageTextEntry g_emptyMessageTextEntry = { 0, 0, &SaveManager::g_emptyString };

		// GLOBAL: TOY2 0x004F5524
		MessageTextEntry* g_messageTextTable[25] = {
			&g_messageTextEntries[0].entry,
			&g_messageTextEntries[1].entry,
			&g_messageTextEntries[2].entry,
			&g_messageTextEntries[3].entry,
			&g_messageTextEntries[4].entry,
			&g_messageTextEntries[5].entry,
			&g_messageTextEntries[6].entry,
			&g_messageTextEntries[7].entry,
			&g_messageTextEntries[8].entry,
			&g_messageTextEntries[9].entry,
			&g_messageTextEntries[10].entry,
			&g_messageTextEntries[11].entry,
			&g_messageTextEntries[12].entry,
			&g_messageTextEntries[13].entry,
			&g_messageTextEntries[14].entry,
			&g_messageTextEntries[15].entry,
			&g_messageTextEntries[16].entry,
			&g_messageTextEntries[17].entry,
			&g_messageTextEntries[18].entry,
			&g_messageTextEntries[19].entry,
			&g_messageTextEntries[20].entry,
			&g_messageTextEntries[21].entry,
			&g_messageTextEntries[22].entry,
			&g_messageTextEntries[23].entry,
			&g_emptyMessageTextEntry,
		};

		// GLOBAL: TOY2 0x0053006C
		char g_defTextStorage[64][256];

		// GLOBAL: TOY2 0x0053406C
		int32_t g_defTextCount;

		// GLOBAL: TOY2 0x00534070
		FILE* g_iniFile;

		// GLOBAL: TOY2 0x00534078
		int32_t g_cheatsEnabled;

		// GLOBAL: TOY2 0x0053407C
		char g_iniToken[1024];

		// GLOBAL: TOY2 0x00534480
		int32_t g_language;

		// GLOBAL: TOY2 0x00830C20
		int32_t g_highQualityMpeg;

		// GLOBAL: TOY2 0x00882A20
		char g_iniCdSearchPath[512];

		// GLOBAL: TOY2 0x00882C24
		int32_t g_useAlternateIniPath;

		// GLOBAL: TOY2 0x00882C2C
		char g_iniInstallSearchPath[512];

		STATIC_ASSERT(sizeof(ControlTextEntry) == 0x14);
		STATIC_ASSERT(sizeof(PaddedControlTextEntry) == 0x18);
		STATIC_ASSERT(sizeof(MessageTextEntry) == 0xC);
		STATIC_ASSERT(sizeof(PaddedMessageTextEntry) == 0x10);

		// FUNCTION: TOY2 0x00430BF0 [PROVISIONAL]
		void ParseDefText()
		{
			if (fscanf(g_iniFile, "%s", g_iniToken) == EOF)
				Logger::Log("EOF reached .\n");

			int32_t keyword;
			if (g_iniToken[0] == '#')
			{
				keyword = KEYWORD_COMMENT;
			}
			else
			{
				KeywordEntry* entry = g_keywords;
				while (entry->keyword != KEYWORD_UNKNOWN)
				{
					if (strcmp(entry->name, g_iniToken) == 0)
					{
						keyword = entry->keyword;
						goto keywordFound;
					}
					++entry;
				}
				keyword = KEYWORD_UNKNOWN;
			}

		keywordFound:
			ControlTextEntry** controlTextTable;
			switch (keyword)
			{
				case KEYWORD_KEY:
					controlTextTable = g_keyTextTable;
					break;
				case KEYWORD_JOY:
					controlTextTable = g_joyTextTable;
					break;
				case KEYWORD_MESSAGE:
					break;
				default:
					memset(g_iniToken, 0, sizeof(g_iniToken));
					char* output = g_iniToken;
					int32_t character;
					do
					{
						character = fgetc(g_iniFile);
						*output++ = (char)character;
					} while (character != EOF && character != '\n');

					Logger::Log("UNKNOWN DefText : %s", g_iniToken);
					return;
			}

			if (fscanf(g_iniFile, "%s", g_iniToken) == EOF)
				Logger::Log("EOF reached .\n");
			int32_t entryIndex = atoi(g_iniToken);

			char character;
			do
			{
				character = (char)fgetc(g_iniFile);
			} while (character != '"' && character != EOF);

			char text[256];
			char* output = text;
			do
			{
				character = (char)fgetc(g_iniFile);
				*output++ = character;
			} while (character != '"' && character != EOF);
			output[-1] = '\0';

			if (fscanf(g_iniFile, "%s", g_iniToken) == EOF)
				Logger::Log("EOF reached .\n");
			int32_t x = atoi(g_iniToken);

			if (fscanf(g_iniFile, "%s", g_iniToken) == EOF)
				Logger::Log("EOF reached .\n");
			int32_t y = atoi(g_iniToken);

			if (g_defTextCount < 64)
			{
				strncpy(g_defTextStorage[g_defTextCount], text, 256);
				if (keyword >= KEYWORD_KEY)
				{
					ControlTextEntry* entry;
					if (keyword < KEYWORD_MESSAGE)
					{
						entry = controlTextTable[entryIndex];
						entry->text = g_defTextStorage[g_defTextCount];
						entry->x = x;
						entry->y = y;
					}
					else
					{
						if (keyword != KEYWORD_MESSAGE)
							return;
						MessageTextEntry* message = g_messageTextTable[entryIndex];
						message->text = g_defTextStorage[g_defTextCount];
						message->x = x;
						message->y = y;
					}
					++g_defTextCount;
				}
			}
		}

		inline int32_t FindKeyword(const char* token)
		{
			if (token[0] == '#')
				return KEYWORD_COMMENT;

			KeywordEntry* entry = g_keywords;
			while (entry->keyword != KEYWORD_UNKNOWN)
			{
				if (strcmp(entry->name, token) == 0)
					return entry->keyword;
				++entry;
			}
			return KEYWORD_UNKNOWN;
		}

		inline void ReadToken()
		{
			if (fscanf(g_iniFile, "%s", g_iniToken) == EOF)
				Logger::Log("EOF reached .\n");
		}
	}

	// FUNCTION: TOY2 0x00430E90 [PROVISIONAL]
	void ReadIniFile()
	{
		using namespace Ini;

		char path[1024];
		memset(path, 0, sizeof(path));
		g_defTextCount = 0;

		FILE* rootIni = fopen("c:\\toy2.ini", "rb");
		if (rootIni != NULL)
		{
			fclose(rootIni);
			strcat(path, "c:");
		}
		else
		{
			if (FileUtils::GetFileSize("toy2.ini") == 0)
			{
				Logger::Log("ReadIniFile : No TOY2.INI file found.\n");
				return;
			}

			if (g_useAlternateIniPath != 0)
			{
				strcat(path, g_iniCdSearchPath);
				strcat(path, "cd\\");
			}
			else
			{
				strcat(path, g_iniInstallSearchPath);
				strcat(path, "\\cd\\");
			}
		}
		strcat(path, "toy2.ini");

		g_iniFile = fopen(path, "rt");
		if (g_iniFile == NULL)
		{
			Logger::Log("ReadIniFile : Failed to open TOY2.INI.\n");
			return;
		}

		for (;;)
		{
			ReadToken();
			int32_t keyword = FindKeyword(g_iniToken);

			switch (keyword)
			{
				case KEYWORD_DEFKEY: {
					ReadToken();
					int32_t controlKeyword = FindKeyword(g_iniToken);
					ReadToken();
					strlwr(g_iniToken);
					int16_t inputCode = InputManager::KeyNameToScancode(g_iniToken[0]);
					switch (controlKeyword)
					{
						case KEYWORD_FIRE:
						case KEYWORD_PAD_RL:
							if (inputCode >= 0)
								SaveManager::g_save99Data.saveStructs[CONTROL_MAPPING_FIRE].dInputCode = inputCode;
							break;
						case KEYWORD_JUMP:
						case KEYWORD_PAD_RD:
							if (inputCode >= 0)
								SaveManager::g_save99Data.saveStructs[CONTROL_MAPPING_JUMP].dInputCode = inputCode;
							break;
						case KEYWORD_CAMLEFT:
						case KEYWORD_PAD_FRB:
							if (inputCode >= 0)
								SaveManager::g_save99Data.saveStructs[CONTROL_MAPPING_CAMLEFT].dInputCode = inputCode;
							break;
						case KEYWORD_CAMRIGHT:
						case KEYWORD_PAD_FLB:
							if (inputCode >= 0)
								SaveManager::g_save99Data.saveStructs[CONTROL_MAPPING_CAMRIGHT].dInputCode = inputCode;
							break;
						case KEYWORD_HELMETVIEW:
						case KEYWORD_PAD_FLT:
							if (inputCode >= 0)
								SaveManager::g_save99Data.saveStructs[CONTROL_MAPPING_HELMETVIEW].dInputCode = inputCode;
							break;
						case KEYWORD_PAUSE:
						case KEYWORD_PAD_START:
							if (inputCode >= 0)
								SaveManager::g_save99Data.saveStructs[CONTROL_MAPPING_PAUSE].dInputCode = inputCode;
							break;
						case KEYWORD_SELECT:
						case KEYWORD_PAD_SEL:
							if (inputCode >= 0)
								SaveManager::g_save99Data.saveStructs[CONTROL_MAPPING_SELECT].dInputCode = inputCode;
							break;
						case KEYWORD_DERVISH:
						case KEYWORD_PAD_RR:
							if (inputCode >= 0)
								SaveManager::g_save99Data.saveStructs[CONTROL_MAPPING_DERVISH].dInputCode = inputCode;
							break;
						case KEYWORD_PAD_RU:
							if (inputCode >= 0)
								SaveManager::g_save99Data.saveStructs[CONTROL_MAPPING_PAD_RU].dInputCode = inputCode;
							break;
						case KEYWORD_PAD_FRT:
							if (inputCode >= 0)
								SaveManager::g_save99Data.saveStructs[CONTROL_MAPPING_PAD_FRT].dInputCode = inputCode;
							break;
					}
					break;
				}
				case KEYWORD_COMMENT: {
					memset(g_iniToken, 0, sizeof(g_iniToken));
					char* output = g_iniToken;
					int32_t character;
					do
					{
						character = fgetc(g_iniFile);
						*output++ = (char)character;
					} while (character != EOF && character != '\n');
					break;
				}
				case KEYWORD_LANGUAGE:
					do
					{
						ReadToken();
					} while (strcmp("=", g_iniToken) != 0);
					ReadToken();
					if (g_iniToken[0] == '#' || FindKeyword(g_iniToken) >= 0)
					{
						Logger::Log("ReadIniFile : LANGUAGE set to %s.\n", g_iniToken);
						strncpy(AudioManager::g_sfxSubPath, g_iniToken, 2);
						g_language = FindKeyword(AudioManager::g_sfxSubPath);
					}
					break;
				case KEYWORD_IF:
					ReadToken();
					if (strcmp(g_iniToken, AudioManager::g_sfxSubPath) != 0)
					{
						do
						{
							ReadToken();
						} while (strcmp("ENDIF", g_iniToken) != 0);
					}
					break;
				case KEYWORD_DEFTEXT:
					ParseDefText();
					break;
				case KEYWORD_CHEAT:
					g_cheatsEnabled = g_cheatsEnabled == 0;
					break;
				case KEYWORD_HIMPEG:
					g_highQualityMpeg = g_highQualityMpeg == 0;
					break;
				case KEYWORD_NOMPEG:
					g_mpegPlaybackDisabled = 1;
					Logger::Log("ReadIniFile : Mpeg play OFF.\n");
					break;
				case KEYWORD_QUIET:
					AudioManager::g_quietMode = 1;
					Logger::Log("ReadIniFile : QUIET set TRUE\n");
					break;
				case KEYWORD_LOG:
					Logger::g_logsEnabled = 1;
					break;
				case KEYWORD_FINISHED:
					fclose(g_iniFile);
					return;
			}
		}
	}

}
