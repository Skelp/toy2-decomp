#include "InputManager.h"
#include "Toy2/Win95.h"
#include "SaveManager.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Nu3D.h"
#include "Renderer/TexturedQuad.h"
#include "Toy2/Direct6.h"
#include "Toy2/Ini.h"
#include "Toy2/MainMenu.h"
#include "Toy2/Toy2.h"

#include <DINPUT.H>
#include <cstdio>
#include <cstring>
#include <stddef.h>

namespace InputManager
{
	enum ControlMenuState
	{
		CONTROL_MENU_STATE_BIND_KEYBOARD = 10,
		CONTROL_MENU_STATE_BIND_JOYSTICK = 40,
	};

	struct KeyboardGlyph
	{
		DevDraw::TexturedQuad renderData;
		int32_t width;
		int32_t height;
	};

	STATIC_ASSERT(offsetof(KeyboardGlyph, width) == 0x4C);
	STATIC_ASSERT(sizeof(KeyboardGlyph) == 0x54);

	// GLOBAL: TOY2 0x0088279C
	int16_t g_curButtonsPressed;

	// GLOBAL: TOY2 0x00882794
	int16_t g_prevButtonsPressed;

	// GLOBAL: TOY2 0x00529B00
	int16_t g_buttonsPressed;

	// GLOBAL: TOY2 0x0052AD88
	int16_t g_directionInputState;

	// GLOBAL: TOY2 0x0052F2FE
	int16_t g_prevDirectionInputState;

	// GLOBAL: TOY2 0x0053C630
	int16_t g_directionInputState2Frames;

	// GLOBAL: TOY2 0x0053C632
	int16_t g_directionInputState3Frames;

	// GLOBAL: TOY2 0x00529C98
	int32_t g_inputManagerInit;

	// GLOBAL: TOY2 0x00529D3C
	int32_t g_directInputSuccess;

	// GLOBAL: TOY2 0x00529CA0
	int32_t g_isInputDeviceValid[4];

	// GLOBAL: TOY2 0x00529D28
	int32_t g_unused1;

	// GLOBAL: TOY2 0x00529D30
	int32_t g_unused2;

	// GLOBAL: TOY2 0x00529C90
	int32_t g_dInputDeviceCount;

	// GLOBAL: TOY2 0x00529C94
	LPDIRECTINPUTA g_directInput;

	// GLOBAL: TOY2 0x00529C9C
	LPDIRECTINPUTDEVICEA g_directInputDevice;

	// GLOBAL: TOY2 0x005298D0
	LPDIRECTINPUTDEVICE2A g_directInputDevices[4];

	// GLOBAL: TOY2 0x00529CB4
	LPDIRECTINPUTDEVICE2A g_dInputDeviceCleanupList[4];

	// GLOBAL: TOY2 0x005297D0
	GUID g_dInputGuids[16];

	// GLOBAL: TOY2 0x00529A00
	uint8_t g_inputStates[256];

	// GLOBAL: TOY2 0x005298E0
	uint8_t g_previousInputStates[256];

	// GLOBAL: TOY2 0x00529B02
	int16_t g_joystickUnused1;

	// GLOBAL: TOY2 0x00529D34
	int16_t g_joystickUnused2;

	// GLOBAL: TOY2 0x00529D36
	int16_t g_joystickUnused3;

	// GLOBAL: TOY2 0x00529D38
	int16_t g_joystickUnused4;

	// GLOBAL: TOY2 0x00529D3A
	int16_t g_joystickUnused5;

	// GLOBAL: TOY2 0x00529CD0
	int16_t g_joystickUnused6;

	// GLOBAL: TOY2 0x00731CD8
	int32_t g_analogInputX;

	// GLOBAL: TOY2 0x00731CDC
	int32_t g_analogInputY;

	// GLOBAL: TOY2 0x00529CD8
	DIJOYSTATE g_joystickState;

	// GLOBAL: TOY2 0x00529CC4
	int32_t g_joystickRawX;

	// GLOBAL: TOY2 0x00529CC8
	int32_t g_joystickRawY;

	// GLOBAL: TOY2 0x005299F8
	int32_t g_joystickDeadzoneThreshold1;

	// GLOBAL: TOY2 0x00529CCC
	int32_t g_joystickDeadzoneThreshold2;

	// GLOBAL: TOY2 0x00529D2C
	int32_t g_joystickDirectionLockState;

	// GLOBAL: TOY2 0x00830C34
	int32_t g_directionalInputCount;

	// GLOBAL: TOY2 0x00529CB0
	int16_t g_joystickConnected;

	// GLOBAL: TOY2 0x00529D40
	int32_t g_joystickDirectionFlags;

	// GLOBAL: TOY2 0x0052F3D4
	KeyboardGlyph* g_selectedKeyboardGlyph;

	// GLOBAL: TOY2 0x0052F464
	int32_t g_renderedKeyboardGlyphTextWidth;

	// GLOBAL: TOY2 0x0052F460
	int32_t g_unusedColouredTextState;

	// GLOBAL: TOY2 0x0052F3DC
	int32_t g_controlMenuState;

	// GLOBAL: TOY2 0x0052F468
	int32_t g_selectedControlEntryIndex;

	// GLOBAL: TOY2 0x004EDC58
	int32_t g_keyboardGlyphTextureSlot = 1;

	// clang-format off
	// GLOBAL: TOY2 0x004ED398
	InputMapping g_inputMapping[] =
	{
		{ "esc", 1 },          { "1", 2 },            { "2", 3 },            { "3", 4 },
		{ "4", 5 },            { "5", 6 },            { "6", 7 },            { "7", 8 },
		{ "8", 9 },            { "9", 10 },           { "0", 11 },           { "minus", 12 },
		{ "equals", 13 },      { "back", 14 },        { "tab", 15 },         { "q", 16 },
		{ "w", 17 },           { "e", 18 },           { "r", 19 },           { "t", 20 },
		{ "y", 21 },           { "u", 22 },           { "i", 23 },           { "o", 24 },
		{ "p", 25 },           { "(", 26 },           { ")", 27 },           { "return", 28 },
		{ "lcontrol", 29 },    { "a", 30 },           { "s", 31 },           { "d", 32 },
		{ "f", 33 },           { "g", 34 },           { "h", 35 },           { "j", 36 },
		{ "k", 37 },           { "l", 38 },           { ";", 39 },           { "apostrophe", 40 },
		{ "grave", 41 },       { "lshift", 42 },      { "\\", 43 },          { "z", 44 },
		{ "x", 45 },           { "c", 46 },           { "v", 47 },           { "b", 48 },
		{ "n", 49 },           { "m", 50 },           { ",", 51 },           { ".", 52 },
		{ "/", 53 },           { "rshift", 54 },      { "multiply", 55 },    { "lmenu", 56 },
		{ "space", 57 },       { "capital", 58 },     { "f1", 59 },          { "f2", 60 },
		{ "f3", 61 },          { "f4", 62 },          { "f5", 63 },          { "f6", 64 },
		{ "f7", 65 },          { "f8", 66 },          { "f9", 67 },          { "f10", 68 },
		{ "numlock", 69 },     { "scroll", 70 },      { "pad 7", 71 },       { "pad 8", 72 },
		{ "pad 9", 73 },       { "subtract", 74 },    { "pad 4", 75 },       { "pad 5", 76 },
		{ "pad 6", 77 },       { "add", 78 },         { "pad 1", 79 },       { "pad 2", 80 },
		{ "pad 3", 81 },       { "pad 0", 82 },       { "pad .", 83 },       { "f11", 87 },
		{ "f12", 88 },         { "f13", 100 },        { "f14", 101 },        { "f15", 102 },
		{ "kana", 112 },       { "convert", 121 },    { "noconvert", 123 },  { "yen", 125 },
		{ "numpadequals", 141 }, { "circumflex", 144 }, { "at", 145 },       { "colon", 146 },
		{ "underline", 147 },  { "kanji", 148 },      { "stop", 149 },       { "ax", 150 },
		{ "unlabeled", 151 },  { "numpadenter", 156 }, { "rcontrol", 157 },  { "pad,", 179 },
		{ "divide", 181 },     { "sysrq", 183 },      { "rmenu", 184 },      { "home", 199 },
		{ "A", 200 },          { "prior", 201 },      { "C", 203 },          { "D", 205 },
		{ "end", 207 },        { "B", 208 },          { "next", 209 },       { "ins", 210 },
		{ "del", 211 },        { "lwin", 219 },       { "rwin", 220 },       { "apps", 221 },
		{ "joy 1", 1024 },     { "joy 2", 1025 },     { "joy 3", 1026 },     { "joy 4", 1027 },
		{ "joy 5", 1028 },     { "joy 6", 1029 },     { "joy 7", 1030 },     { "joy 8", 1031 },
		{ "joy 9", 1032 },     { "joy 10", 1033 },    { "joy 11", 1034 },    { "joy 12", 1035 },
		{ "joy 13", 1036 },    { "joy 14", 1037 },    { "joy 15", 1038 },    { "joy 16", 1039 },
		{ "joy 17", 1040 },    { "joy 18", 1041 },    { "joy 19", 1042 },    { "joy 20", 1043 },
		{ "joy 22", 1044 },    { "joy 23", 1045 },    { "joy 24", 1046 },    { "joy 25", 1047 },
		{ "joy 26", 1048 },    { "joy 27", 1049 },    { "joy 28", 1050 },    { "joy 29", 1051 },
		{ "pov up", 1052 },    { "pov right", 1053 }, { "pov down", 1054 },  { "pov left", 1055 },
		{ "direction pad", 2048 },
		{ NULL, -1 }
	};
	// clang-format on

	// GLOBAL: TOY2 0x004EFCE0
	KeyboardGlyphMapping g_keyboardGlyphMappings[] = {
#include "InputManagerKeyGlyphMappings.inc"
	};

	// GLOBAL: TOY2 0x004EDC60
	KeyboardGlyph g_keyboardGlyphs[] = {
#include "InputManagerKeyboardGlyphs.inc"
	};

	// GLOBAL: TOY2 0x00503860
	DirectionInputMapping g_directionInputMappings[] = {
		{ INPUT_FIRE, INPUT_JUMP, INPUT_SPIN },
		{ INPUT_JUMP, INPUT_FIRE, INPUT_CANCEL },
		{ INPUT_JUMP, INPUT_SPIN, INPUT_CANCEL },
		{ INPUT_FIRE, INPUT_JUMP, INPUT_CANCEL },
		{ INPUT_SPIN, INPUT_JUMP, INPUT_CANCEL },
		{ INPUT_JUMP, INPUT_FIRE, INPUT_SPIN },
		{ INPUT_JUMP, INPUT_SPIN, INPUT_FIRE },
		{ INPUT_SPIN, INPUT_JUMP, INPUT_FIRE },
	};

	// FUNCTION: TOY2 0x00414AC0 [MATCHED]
	int32_t GetJoystickX() { return g_joystickRawX >> 8; }

	// FUNCTION: TOY2 0x00414AD0 [MATCHED]
	int32_t GetJoystickY() { return g_joystickRawY >> 8; }

	// FUNCTION: TOY2 0x00414AE0 [MATCHED]
	int32_t GetJoystickButtonState(int32_t buttonIndex) { return g_joystickState.rgbButtons[buttonIndex]; }

	// FUNCTION: TOY2 0x00415100 [MATCHED]
	int32_t HasSecondaryJoystickButtonPressed()
	{
		for (int32_t button = 1; button < 32; button++)
		{
			if (g_joystickState.rgbButtons[button])
				return 1;
		}

		return 0;
	}

	// FUNCTION: TOY2 0x00415120 [MATCHED]
	int32_t GetPressedInput()
	{
		for (int32_t inputCode = 0; inputCode < 256; inputCode++)
		{
			if (g_inputStates[inputCode])
				return inputCode;
		}

		for (int32_t button = 0; button < 32; button++)
		{
			if (g_joystickState.rgbButtons[button])
				return button + TOY_INPUT_JOY1;
		}

		return g_joystickDirectionFlags ? TOY_INPUT_DIRECTIONPAD : TOY_INPUT_UNKNOWN;
	}

	// FUNCTION: TOY2 0x004152A0 [MATCHED]
	char* GetGameControlName(int32_t inputCode)
	{
		for (int32_t i = 0; g_inputMapping[i].name; i++)
		{
			if (g_inputMapping[i].id == inputCode)
				return g_inputMapping[i].name;
		}

		return NULL;
	}

	// FUNCTION: TOY2 0x004154C0 [MATCHED]
	void PollKeyboardState()
	{
		Nu3D::MemSet32Util(g_inputStates, 64, 0);
		g_directInputDevice->Acquire();
		g_directInputDevice->GetDeviceState(256, g_inputStates);
	}

	// FUNCTION: TOY2 0x00415500 [MATCHED]
	int16_t ScancodeToGlyphIndex(uint8_t scanCode)
	{
		for (int32_t i = 0; g_keyboardGlyphMappings[i].scanCode != -1; i++)
		{
			if (g_keyboardGlyphMappings[i].scanCode == scanCode)
				return g_keyboardGlyphMappings[i].glyphIndex;
		}

		return -1;
	}

	// FUNCTION: TOY2 0x00415540 [MATCHED]
	int16_t GlyphIndexToScancode(uint8_t glyphIndex)
	{
		for (int32_t i = 0; g_keyboardGlyphMappings[i].scanCode != -1; i++)
		{
			if (g_keyboardGlyphMappings[i].glyphIndex == glyphIndex)
				return g_keyboardGlyphMappings[i].scanCode;
		}

		return -1;
	}

	// FUNCTION: TOY2 0x00415580 [MATCHED]
	int16_t KeyNameToGlyphIndex(uint8_t keyName)
	{
		for (int32_t i = 0; g_keyboardGlyphMappings[i].scanCode != -1; i++)
		{
			if (g_keyboardGlyphMappings[i].character == keyName)
				return g_keyboardGlyphMappings[i].glyphIndex;
		}

		return -1;
	}

	// FUNCTION: TOY2 0x004155C0 [MATCHED]
	int16_t GlyphIndexToKeyName(uint8_t glyphIndex)
	{
		for (int32_t i = 0; g_keyboardGlyphMappings[i].scanCode != -1; i++)
		{
			if (g_keyboardGlyphMappings[i].glyphIndex == glyphIndex)
				return g_keyboardGlyphMappings[i].character;
		}

		return -1;
	}

	// FUNCTION: TOY2 0x00415600 [MATCHED]
	int16_t ScancodeToKeyName(uint8_t scanCode)
	{
		for (int32_t i = 0; g_keyboardGlyphMappings[i].scanCode != -1; i++)
		{
			if (g_keyboardGlyphMappings[i].scanCode == scanCode)
				return g_keyboardGlyphMappings[i].character;
		}

		return -1;
	}

	// FUNCTION: TOY2 0x00415640 [MATCHED]
	int16_t KeyNameToScancode(uint8_t keyName)
	{
		int32_t i = 0;
		switch (keyName)
		{
			case '[':
				return DIK_LSHIFT;
			case '@':
				return DIK_LCONTROL;
			case '%':
				return DIK_TAB;
			case '#':
				return DIK_SPACE;
			case '+':
				return DIK_RETURN;
			case '<':
				return DIK_LEFT;
			case '>':
				return DIK_RIGHT;
			case '^':
				return DIK_UP;
			case 'v':
				return DIK_DOWN;
		}

		for (; g_keyboardGlyphMappings[i].scanCode != -1; i++)
		{
			if (g_keyboardGlyphMappings[i].character == keyName)
				return g_keyboardGlyphMappings[i].scanCode;
		}

		return -1;
	}

	// FUNCTION: TOY2 0x00415750 [MATCHED]
	int32_t CalculateKeyboardGlyphTextWidth(const char* text)
	{
		int32_t width = 0;
		uint8_t character = *text;

		if (character != '\0')
		{
			do
			{
				if (character == ' ')
				{
					width += 8;
				}
				else
				{
					for (int32_t i = 0; g_keyboardGlyphMappings[i].scanCode != -1; i++)
					{
						if (g_keyboardGlyphMappings[i].character == character)
						{
							int16_t glyphIndex = g_keyboardGlyphMappings[i].glyphIndex;
							if (glyphIndex != -1)
							{
								g_selectedKeyboardGlyph = &g_keyboardGlyphs[glyphIndex];
								width += g_selectedKeyboardGlyph->width;
							}
							break;
						}
					}
				}

				character = *++text;
			} while (character != '\0');
		}

		return width;
	}

	// FUNCTION: TOY2 0x004158A0 [PROVISIONAL]
	void DrawKeyboardGlyphText(int32_t x, int32_t y, const char* text)
	{
		DevDraw::TexturedQuad quad;
		g_renderedKeyboardGlyphTextWidth = 0;
		uint8_t character = *text;

		if (character != '\0')
		{
			do
			{
				if (character == ' ')
				{
					x += 8;
					g_renderedKeyboardGlyphTextWidth += 8;
				}
				else
				{
					for (int32_t i = 0; g_keyboardGlyphMappings[i].scanCode != -1; i++)
					{
						if (g_keyboardGlyphMappings[i].character == character)
						{
							int16_t glyphIndex = g_keyboardGlyphMappings[i].glyphIndex;
							if (glyphIndex != -1)
							{
								int32_t windowHeight = Toy2::g_softWindowHeight;
								g_selectedKeyboardGlyph = &g_keyboardGlyphs[glyphIndex];
								quad = g_selectedKeyboardGlyph->renderData;

								quad.points[0].x = (Toy2::g_softWindowWidth * x) / 320 + Toy2::g_screenClipLeft;
								quad.points[0].y = (windowHeight * y) / 256 + Toy2::g_screenClipTop;
								quad.points[1].x = (Toy2::g_softWindowWidth * g_selectedKeyboardGlyph->width) / 320 + quad.points[0].x;
								quad.points[1].y = quad.points[0].y;
								quad.points[2].x = quad.points[0].x;
								quad.points[2].y = (windowHeight * g_selectedKeyboardGlyph->height) / 256 + quad.points[0].y;
								quad.points[3].x = quad.points[1].x;
								quad.points[3].y = quad.points[2].y;
								quad.depth = 0;
								quad.drawSlot = g_keyboardGlyphTextureSlot;
								g_renderedKeyboardGlyphTextWidth += g_selectedKeyboardGlyph->width;

								if (g_renderMode == RENDERMODE_SOFTWARE)
								{
									quad.texCoords[0].u <<= 16;
									quad.texCoords[0].v <<= 16;
									quad.texCoords[1].u <<= 16;
									quad.texCoords[1].v <<= 16;
									quad.texCoords[2].u <<= 16;
									quad.texCoords[2].v <<= 16;
									quad.texCoords[3].u <<= 16;
									quad.texCoords[3].v <<= 16;
								}

								DevDraw::SubmitTexturedQuad(&quad);
								x += g_selectedKeyboardGlyph->width;
							}
							break;
						}
					}
				}

				character = *++text;
			} while (character != '\0');
		}
	}

	// FUNCTION: TOY2 0x00415AC0 [MATCHED]
	void DrawMessageTextByIndex(int32_t messageIndex)
	{
		Toy2::Ini::MessageTextEntry* message = Toy2::Ini::g_messageTextTable[messageIndex];
		DrawKeyboardGlyphText(message->x, message->y, message->text);
	}

	// FUNCTION: TOY2 0x00415AE0 [PROVISIONAL]
	void DrawColouredKeyboardGlyphText(int32_t x, int32_t y, const char* text, int32_t blue, int32_t green, int32_t red)
	{
		DevDraw::TexturedQuad quad;
		g_unusedColouredTextState = 0;
		g_renderedKeyboardGlyphTextWidth = 0;
		uint8_t character = *text;

		if (character != '\0')
		{
			do
			{
				if (character == ' ')
				{
					x += 8;
					g_renderedKeyboardGlyphTextWidth += 8;
				}
				else
				{
					for (int32_t i = 0; g_keyboardGlyphMappings[i].scanCode != -1; i++)
					{
						if (g_keyboardGlyphMappings[i].character == character)
						{
							int16_t glyphIndex = g_keyboardGlyphMappings[i].glyphIndex;
							if (glyphIndex != -1)
							{
								int32_t windowHeight = Toy2::g_softWindowHeight;
								g_selectedKeyboardGlyph = &g_keyboardGlyphs[glyphIndex];
								quad = g_selectedKeyboardGlyph->renderData;

								quad.points[0].x = (Toy2::g_softWindowWidth * x) / 320 + Toy2::g_screenClipLeft;
								quad.points[0].y = (windowHeight * y) / 256 + Toy2::g_screenClipTop;
								quad.points[1].x = (Toy2::g_softWindowWidth * g_selectedKeyboardGlyph->width) / 320 + quad.points[0].x;
								quad.points[1].y = quad.points[0].y;
								quad.points[2].x = quad.points[0].x;
								quad.points[2].y = (windowHeight * g_selectedKeyboardGlyph->height) / 256 + quad.points[0].y;
								quad.points[3].x = quad.points[1].x;
								quad.points[3].y = quad.points[2].y;
								quad.depth = 0;
								quad.drawSlot = g_keyboardGlyphTextureSlot;
								quad.blue = blue;
								quad.green = green;
								quad.red = red;
								g_renderedKeyboardGlyphTextWidth += g_selectedKeyboardGlyph->width;

								switch (g_renderMode)
								{
									case RENDERMODE_SOFTWARE:
										quad.texCoords[0].u <<= 16;
										quad.texCoords[0].v <<= 16;
										quad.texCoords[1].u <<= 16;
										quad.texCoords[1].v <<= 16;
										quad.texCoords[2].u <<= 16;
										quad.texCoords[2].v <<= 16;
										quad.texCoords[3].u <<= 16;
										quad.texCoords[3].v <<= 16;
										break;
									default:
										break;
								}

								DevDraw::SubmitColouredTexturedQuad(&quad);
								x += g_selectedKeyboardGlyph->width;
							}
							break;
						}
					}
				}

				character = *++text;
			} while (character != '\0');
		}
	}

	// FUNCTION: TOY2 0x00415F50 [MATCHED]
	int32_t GetKeyboardGlyphWidth(int8_t glyphIndex)
	{
		if (glyphIndex != -1)
		{
			g_selectedKeyboardGlyph = &g_keyboardGlyphs[glyphIndex];
			return g_selectedKeyboardGlyph->width;
		}

		return 0;
	}

	// FUNCTION: TOY2 0x00415F80 [MATCHED]
	void DrawKeyboardGlyph(int32_t x, int32_t y, int8_t glyphIndex)
	{
		DevDraw::TexturedQuad quad;

		if (glyphIndex == -1)
			return;

		g_selectedKeyboardGlyph = &g_keyboardGlyphs[glyphIndex];
		quad = g_selectedKeyboardGlyph->renderData;

		quad.points[0].x = (Toy2::g_softWindowWidth * x) / 320 + Toy2::g_screenClipLeft;
		quad.points[0].y = (Toy2::g_softWindowHeight * y) / 256 + Toy2::g_screenClipTop;
		quad.points[1].x = (Toy2::g_softWindowWidth * g_selectedKeyboardGlyph->width) / 320 + quad.points[0].x;
		quad.points[1].y = quad.points[0].y;
		quad.points[2].x = quad.points[0].x;
		quad.points[2].y = (Toy2::g_softWindowHeight * g_selectedKeyboardGlyph->height) / 256 + quad.points[0].y;
		quad.points[3].x = quad.points[1].x;
		quad.points[3].y = quad.points[2].y;

		switch (g_renderMode)
		{
			case RENDERMODE_SOFTWARE:
				quad.texCoords[0].u <<= 16;
				quad.texCoords[0].v <<= 16;
				quad.texCoords[1].u <<= 16;
				quad.texCoords[1].v <<= 16;
				quad.texCoords[2].u <<= 16;
				quad.texCoords[2].v <<= 16;
				quad.texCoords[3].u <<= 16;
				quad.texCoords[3].v <<= 16;
				break;
			default:
				break;
		}

		DevDraw::SubmitTexturedQuad(&quad);
	}

	// FUNCTION: TOY2 0x004160F0 [PROVISIONAL]
	void DrawKeyboardBindings(Toy2::Ini::ControlTextEntry** entries)
	{
		Toy2::Ini::ControlTextEntry** nextEntry = entries;
		Toy2::Ini::ControlTextEntry* entry = *nextEntry;
		int32_t entryIndex = 0;
		nextEntry++;

		if (entry != (Toy2::Ini::ControlTextEntry*)-1)
		{
			do
			{
				int32_t glyphOffset;
				if (entry->glyphIndex != -1)
				{
					g_selectedKeyboardGlyph = &g_keyboardGlyphs[entry->glyphIndex];
					glyphOffset = g_selectedKeyboardGlyph->width;
					if (glyphOffset >= 40)
						glyphOffset += 8;
					else
						glyphOffset = 40;
				}
				else
				{
					glyphOffset = 40;
				}

				DrawKeyboardGlyphText(entry->x, entry->y, entry->text);
				if (entryIndex <= 10)
					DrawKeyboardGlyph(entry->x - glyphOffset, entry->y, entry->glyphIndex);

				if (g_selectedControlEntryIndex >= 0)
				{
					if (g_selectedControlEntryIndex <= 10)
					{
						if (entry == entries[g_selectedControlEntryIndex]
							&& (Renderer::g_frameDelta % 20 < 10 || g_controlMenuState == CONTROL_MENU_STATE_BIND_KEYBOARD))
						{
							DrawKeyboardGlyphText(entry->x + g_selectedKeyboardGlyph->width - glyphOffset, entry->y, "]");
							DrawKeyboardGlyphText(entry->x - glyphOffset - 6, entry->y, "[");
						}
					}
					else if (entry == entries[g_selectedControlEntryIndex]
						&& (Renderer::g_frameDelta % 20 < 10 || g_controlMenuState == CONTROL_MENU_STATE_BIND_KEYBOARD))
					{
						DrawKeyboardGlyphText(entry->x + g_renderedKeyboardGlyphTextWidth, entry->y, "]");
						DrawKeyboardGlyphText(entry->x - 6, entry->y, "[");
					}
				}

				entry = *nextEntry;
				entryIndex++;
				nextEntry++;
			} while (entry != (Toy2::Ini::ControlTextEntry*)-1);
		}
	}

	// FUNCTION: TOY2 0x00416240 [MATCHED]
	void DrawJoystickBindings(Toy2::Ini::ControlTextEntry** entries)
	{
		char buttonText[32];
		Toy2::Ini::ControlTextEntry** nextEntry = entries;
		Toy2::Ini::ControlTextEntry* entry = *nextEntry;
		int32_t entryIndex = 0;
		nextEntry++;

		if (entry != (Toy2::Ini::ControlTextEntry*)-1)
		{
			do
			{
				int32_t bindingOffset;
				DrawKeyboardGlyphText(entry->x, entry->y, entry->text);

				if (entryIndex <= 6)
				{
					memset(buttonText, 0, sizeof(buttonText));
					sprintf(buttonText, "#%d", entry->inputCode + 1);
					bindingOffset = 30;
					DrawKeyboardGlyphText(entry->x - bindingOffset, entry->y, buttonText);
				}

				if (g_selectedControlEntryIndex >= 0)
				{
					if (g_selectedControlEntryIndex <= 6)
					{
						if (entry == entries[g_selectedControlEntryIndex]
							&& (Renderer::g_frameDelta % 20 < 10 || g_controlMenuState == CONTROL_MENU_STATE_BIND_JOYSTICK))
						{
							DrawKeyboardGlyphText(entry->x - bindingOffset + g_renderedKeyboardGlyphTextWidth, entry->y, "]");
							DrawKeyboardGlyphText(entry->x - bindingOffset - 6, entry->y, "[");
						}
					}
					else if (entry == entries[g_selectedControlEntryIndex]
						&& (Renderer::g_frameDelta % 20 < 10 || g_controlMenuState == CONTROL_MENU_STATE_BIND_JOYSTICK))
					{
						DrawKeyboardGlyphText(entry->x + g_renderedKeyboardGlyphTextWidth, entry->y, "]");
						DrawKeyboardGlyphText(entry->x - 6, entry->y, "[");
					}
				}

				entry = *nextEntry;
				entryIndex++;
				nextEntry++;
			} while (entry != (Toy2::Ini::ControlTextEntry*)-1);
		}
	}

	// FUNCTION: TOY2 0x004157D0 [MATCHED]
	void ResetInputHistory(int32_t inputCode) { g_previousInputStates[inputCode] = 0xFF; }
}

namespace InputManager
{
	// FUNCTION: TOY2 0x00414EA0 [MATCHED]
	BOOL WINAPI EnumDevices(LPCDIDEVICEINSTANCEA deviceInstance, LPVOID context)
	{
		GUID* deviceGuids = static_cast<GUID*>(context);

		deviceGuids[g_dInputDeviceCount] = deviceInstance->guidInstance;

		g_dInputDeviceCount++;
		return g_dInputDeviceCount != 4;
	}

	// FUNCTION: TOY2 0x004152E0 [PROVISIONAL]
	void Init()
	{
		LPDIRECTINPUTDEVICE devices[4];

		g_inputManagerInit = 0;
		g_directInputSuccess = 0;

		g_isInputDeviceValid[3] = 0;
		g_isInputDeviceValid[2] = 0;
		g_isInputDeviceValid[1] = 0;
		g_isInputDeviceValid[0] = 0;

		InputManager::g_unused1 = 0;
		InputManager::g_unused2 = 0;

		g_dInputDeviceCount = 0;

		if (! DirectInputCreateA(g_windowData.hInstance, 0x0500, &g_directInput, 0))
		{
			// clang-format off
			if (! g_directInput->CreateDevice(GUID_SysKeyboard, &g_directInputDevice, 0) 
				&& ! g_directInputDevice->SetDataFormat(&c_dfDIKeyboard)
				&& ! g_directInputDevice->SetCooperativeLevel(g_windowData.mainHwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE) && ! g_directInputDevice->Acquire())
			{
				g_directInputSuccess = 1;
			}

			if (! g_directInput->EnumDevices(4, EnumDevices, g_dInputGuids, 1))
			{
				int32_t deviceIdx = 0;

				if (g_dInputDeviceCount > 0)
				{
					LPGUID curGuid = g_dInputGuids;

					do
					{
						if (! g_directInput->CreateDevice(*curGuid, &devices[deviceIdx], 0))
						{
							LPDIRECTINPUTDEVICE2A* curDevice = &g_directInputDevices[deviceIdx];

							if (! devices[deviceIdx]->QueryInterface(IID_IDirectInputDevice2A, (LPVOID*)curDevice)
								&& ! (*curDevice)->SetDataFormat(&c_dfDIJoystick) 
								&& ! (*curDevice)->SetCooperativeLevel(g_windowData.mainHwnd, DISCL_FOREGROUND | DISCL_EXCLUSIVE)
								&& ! (*curDevice)->Acquire())
							{
								g_isInputDeviceValid[deviceIdx] = 1;
							}
						}

						++deviceIdx;
						++curGuid;

					} while (deviceIdx < g_dInputDeviceCount);
				}

				g_inputManagerInit = 1;
			}
			// clang-format on
		}
	}

	// FUNCTION: TOY2 0x004157E0 [MATCHED]
	uint8_t IsKeyPressed(int32_t inputCode)
	{
		uint8_t result = g_inputStates[inputCode] & g_previousInputStates[inputCode];
		g_previousInputStates[inputCode] = ~g_inputStates[inputCode];

		return result;
	}

	// FUNCTION: TOY2 0x0047BAC0 [MATCHED]
	void BeginControlMenuExitOnEscape()
	{
		if (IsKeyPressed(1) && ! Toy2::MainMenu::g_nextScreen)
		{
			Toy2::MainMenu::g_nextScreen = 1;
			Toy2::MainMenu::g_fadeTimer = 44;
			Nu3D::Camera::SetTint(0, 0, 0, 3);
		}
	}

	// FUNCTION: TOY2 0x00415800 [MATCHED]
	int32_t FindKeyPressed()
	{
		for (int32_t inputCode = 1; inputCode < 256; inputCode++)
		{
			uint8_t result = g_inputStates[inputCode] & g_previousInputStates[inputCode];
			g_previousInputStates[inputCode] = ~g_inputStates[inputCode];
			if (result)
				return inputCode;
		}

		return 0;
	}

	// FUNCTION: TOY2 0x00415840 [MATCHED]
	uint8_t IsKeyReleased(int32_t inputCode)
	{
		uint8_t result = g_inputStates[inputCode] & g_previousInputStates[inputCode];
		g_previousInputStates[inputCode] = ~g_inputStates[inputCode];

		return result;
	}

	// FUNCTION: TOY2 0x00415860 [MATCHED]
	int32_t FindKeyReleased()
	{
		for (int32_t inputCode = 1; inputCode < 256; inputCode++)
		{
			uint8_t currentState = g_inputStates[inputCode];
			uint8_t previousState = g_previousInputStates[inputCode];
			previousState &= currentState;
			g_previousInputStates[inputCode] = ~currentState;
			if (previousState)
				return inputCode;
		}

		return 0;
	}

	// FUNCTION: TOY2 0x00498620 [MATCHED]
	int32_t GetCurButtonsPressed() { return g_curButtonsPressed; }

	// FUNCTION: TOY2 0x0049EBA0 [PROVISIONAL]
	void UpdateDirectionInputState()
	{
		uint32_t cameraType = SaveManager::g_save0Data.cameraType & SaveManager::CAMERA_MASK;
		DirectionInputMapping& mapping = g_directionInputMappings[cameraType];

		g_directionInputState = g_curButtonsPressed & 0xFFF;
		if (mapping.fireMask & g_curButtonsPressed)
			g_directionInputState |= INPUT_FIRE;
		if (mapping.jumpMask & g_curButtonsPressed)
			g_directionInputState |= INPUT_JUMP;
		if (mapping.spinMask & g_curButtonsPressed)
			g_directionInputState |= INPUT_SPIN;
	}

	// FUNCTION: TOY2 0x00414AF0 [PROVISIONAL]
	void UpdateInputState()
	{
		int32_t hasDirectionalInput = 0;

		g_buttonsPressed = 0;

		g_joystickUnused1 = 0;
		g_joystickUnused2 = 0;
		g_joystickUnused3 = 0;
		g_joystickUnused4 = 0;
		g_joystickUnused5 = 0;
		g_joystickUnused6 = 0;

		if (g_directInputSuccess)
		{
			Nu3D::MemSet32Util(g_inputStates, 64, 0);

			g_directInputDevice->Acquire();
			g_directInputDevice->GetDeviceState(256, g_inputStates);

			int32_t analogX = 0;
			int32_t analogY = 0;

			if (g_inputStates[DIK_RSHIFT])
				g_inputStates[DIK_LSHIFT] = 1;

			if (g_inputStates[DIK_RCONTROL])
				g_inputStates[DIK_LCONTROL] = 1;

			if (g_inputStates[DIK_RMENU])
				g_inputStates[DIK_LMENU] = 1;

			int16_t curButtonsPressed = g_buttonsPressed;
			SaveManager::SaveControlMapping* saveStructs = SaveManager::g_save99Data.saveStructs;

			do
			{
				int32_t key = saveStructs->dInputCode;

				if (saveStructs->dInputCode >= 0 && key < 1024)
				{
					int32_t keyPressed = g_inputStates[key];

					if (keyPressed)
					{
						curButtonsPressed |= (uint16_t)(saveStructs->gameControlId);
						hasDirectionalInput = 1;
						int32_t tempKeyPressed = keyPressed;
						keyPressed = 0;

						switch (saveStructs->gameControlId)
						{
							case INPUT_UP:
								analogY = 32768;
								break;

							case INPUT_RIGHT:
								analogX = 32768;
								break;

							case INPUT_DOWN:
								analogY = -32768;
								break;

							case INPUT_LEFT:
								analogX = -32768;
								break;

							default:
								keyPressed = tempKeyPressed;
								break;
						}
					}
				}

				++saveStructs;

			} while (saveStructs < SaveManager::g_save99Data.unusedStructs);

			g_buttonsPressed = curButtonsPressed;

			if (hasDirectionalInput)
			{
				g_analogInputX = analogX;
				g_analogInputY = analogY;
			}
		}

		if (g_isInputDeviceValid[0])
		{
			if (g_directInputDevices[0]->Poll())
				g_directInputDevices[0]->Acquire();

			if (g_directInputDevices[0]->GetDeviceState(80, &g_joystickState))
				g_directInputDevices[0]->Acquire();

			int16_t joystickButtons = g_buttonsPressed;

			g_joystickRawX = g_joystickState.lX;
			g_joystickRawY = g_joystickState.lY;

			g_joystickDeadzoneThreshold1 = 11200;
			g_joystickDeadzoneThreshold2 = 11200;

			SaveManager::SaveControlMapping* joystickMapping = SaveManager::g_save99Data.saveStructs;

			do
			{
				int32_t dInputCode = joystickMapping->dInputCode;

				if (joystickMapping->dInputCode >= 0x400 && dInputCode < 0x41F && g_previousInputStates[dInputCode + 40])
					joystickButtons |= (uint16_t)(joystickMapping->gameControlId);

				++joystickMapping;

			} while (joystickMapping < SaveManager::g_save99Data.unusedStructs);

			uint8_t directionLockState = g_joystickDirectionLockState;

			g_buttonsPressed = joystickButtons;
			g_directionalInputCount = 1;
			g_joystickConnected = 1;

			int32_t joystickYAxis;
			int32_t joystickXAxis;

			if ((g_joystickDirectionLockState & JOYSTICK_HORIZONTAL_LOCK_MASK) == JOYSTICK_HORIZONTAL_LOCKED)
			{
				joystickYAxis = (((g_joystickState.lX >> 8) & 0xFF) - 128) << 8;
				if (joystickYAxis <= -11200 || joystickYAxis >= 11200)
				{
					directionLockState = g_joystickDirectionLockState & JOYSTICK_VERTICAL_LOCK_MASK;
					g_joystickDirectionLockState &= JOYSTICK_VERTICAL_LOCK_MASK;
				}
			}
			else
			{
				joystickYAxis = (((g_joystickState.lX >> 8) & 0xFF) - 128) << 8;
			}

			if ((directionLockState & JOYSTICK_VERTICAL_LOCK_MASK) == JOYSTICK_VERTICAL_LOCKED)
			{
				joystickXAxis = (128 - ((g_joystickState.lY >> 8) & 0xFF)) << 8;

				if (joystickXAxis <= -11200 || joystickXAxis >= 11200)
					g_joystickDirectionLockState = directionLockState & JOYSTICK_HORIZONTAL_LOCK_MASK;
			}
			else
			{
				joystickXAxis = (128 - ((g_joystickState.lY >> 8) & 0xFF)) << 8;
			}

			int32_t dirFlags = 0;
			int32_t anyDirectionActive = 0;
			g_joystickDirectionFlags = 0;

			if (joystickYAxis < -11200)
			{
				dirFlags = INPUT_LEFT;
				anyDirectionActive = 1;
				g_joystickDirectionFlags = INPUT_LEFT;
			}

			if (joystickYAxis > 11200)
			{
				dirFlags |= INPUT_RIGHT;
				anyDirectionActive = 1;
				g_joystickDirectionFlags = dirFlags;
			}

			if (joystickXAxis < -11200)
			{
				dirFlags |= INPUT_DOWN;
				anyDirectionActive = 1;
				g_joystickDirectionFlags = dirFlags;
			}

			if (joystickXAxis <= 11200)
			{
				if (! anyDirectionActive)
					return;
			}
			else
			{
				dirFlags |= INPUT_UP;
				g_joystickDirectionFlags = dirFlags;
			}

			g_analogInputX = joystickYAxis;
			g_analogInputY = joystickXAxis;

			g_buttonsPressed = dirFlags | joystickButtons;
		}
	}

	// FUNCTION: TOY2 0x00452180 [MATCHED]
	void UpdateButtonStates()
	{
		g_prevButtonsPressed = g_curButtonsPressed;
		UpdateInputState();
		g_curButtonsPressed = g_buttonsPressed;
	}

	// FUNCTION: TOY2 0x00415460 [PROVISIONAL]
	void Cleanup()
	{
		if (g_directInput)
		{
			if (g_directInputDevice)
			{
				g_directInputDevice->Unacquire();
				g_directInputDevice->Release();
			}
			for (int32_t i = 0; i < 4; i++)
			{
				if (g_dInputDeviceCleanupList[i])
				{
					g_dInputDeviceCleanupList[i]->Unacquire();
					g_dInputDeviceCleanupList[i]->Release();
				}
			}
			g_directInput->Release();
		}
	}
}
