#pragma once

#include "Common.h"
#include "InputTypes.h"

namespace InputManager
{
	struct InputMapping
	{
		char* name;
		int32_t id;
	};

	struct DirectionInputMapping
	{
		uint16_t fireMask;
		uint16_t jumpMask;
		uint16_t spinMask;
	};

	extern int16_t g_curButtonsPressed;
	extern int16_t g_prevButtonsPressed;
	extern int16_t g_directionInputState;
	extern int16_t g_prevDirectionInputState;
	extern int16_t g_directionInputState2Frames;
	extern int16_t g_directionInputState3Frames;
	extern uint8_t g_previousInputStates[256];

	void Init();
	int32_t GetPressedInput();
	char* GetGameControlName(int32_t inputCode);
	uint8_t IsKeyPressed(int32_t inputCode);
	int32_t FindKeyPressed();
	int32_t FindKeyReleased();
	int32_t GetCurButtonsPressed();
	void UpdateDirectionInputState();
	void UpdateButtonStates();
	void Cleanup();

	STATIC_ASSERT(sizeof(InputMapping) == 0x8);
	STATIC_ASSERT(sizeof(DirectionInputMapping) == 0x6);
}
