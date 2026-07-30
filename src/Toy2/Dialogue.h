#pragma once

#include "Common.h"

namespace Toy2
{
	namespace Dialogue
	{
		struct CutsceneScript
		{
			int32_t selectRecordCommand;
			int32_t recordType;
			int32_t moveBuzzCommand;
			int32_t buzzActorIndex;
			int32_t buzzPathPoint;
			int32_t moveActorCommand;
			int32_t actorIndex;
			int32_t actorPathPoint;
			int32_t faceBuzzCommand;
			int32_t facingBuzzActorIndex;
			int32_t buzzFacingAngle;
			int32_t faceActorCommand;
			int32_t facingActorIndex;
			int32_t actorFacingAngle;
		};

		struct DialogueRecords
		{
			uint16_t recordCount;
			uint16_t recordType;
			Vector3I buzzPosition;
			Vector3I actorPosition;
		};

		union SubtitleCells
		{
			uint16_t characters[72];
			uint32_t pairs[36];
		};

		extern CutsceneScript g_dialogueCutsceneScript;
		extern SubtitleCells g_subtitleCells;
		extern char g_wrappedSubtitleText[540];
		extern char* g_subtitleTextCursor;
		extern int32_t g_subtitleActive;
		extern int32_t g_subtitleColumn;
		extern int32_t g_subtitleRowStart;
		extern int32_t g_subtitleVisibleStart;
		extern int32_t g_subtitleColour;
		extern int32_t g_subtitleCharacterDelay;
		extern int32_t g_subtitleCharacterStyle;
		extern int32_t g_subtitleBoxScale;
		extern int32_t g_subtitlePageState;

		void WrapSubtitleText(char* subtitle);
		void Begin(int32_t actorIndex, int32_t recordType, char* subtitle, int32_t buzzFacingAngle, int32_t actorFacingAngle, int32_t rewardTokenIndex);

		STATIC_ASSERT(sizeof(DialogueRecords) == 0x1C);
		STATIC_ASSERT(offsetof(DialogueRecords, buzzPosition) == 0x4);
		STATIC_ASSERT(offsetof(DialogueRecords, actorPosition) == 0x10);
	}
}
