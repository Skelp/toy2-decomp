#pragma once

#include "Common.h"

namespace Toy2
{
	struct ToyCfg
	{
		uint32_t flags;
		int32_t detail;
		float gammaCorrection;
		int32_t driverIndex;
		int32_t deviceIndex;
		int32_t displayModeIndex;
	};

	struct SectorBackdropTexIdTable
	{
		int32_t primary[10];
		int32_t secondary[8];
	};

	void SetBackdropByIndex(int32_t index);
	void ProcessMiscEventsEx();
	void RenderGame(int32_t fullRender);
	int32_t ComputeTokenProgress();

	extern ToyCfg g_toyCfgData;
	extern int32_t g_levelFileIndex;
	extern int32_t g_destRectWidth;
	extern int32_t g_destRectWidthTimes1024Minus1;
	extern int32_t g_destRectWidthCopy;
	extern int32_t g_destRectWidthScaled;
	extern int32_t g_destRectHeight;
	extern int32_t g_unk8828BC;
	extern int32_t g_destRectWidthMinus1;
	extern int32_t g_destRectHeightMinus1;
	extern int32_t g_destRectHalfWidthCopy;
	extern int32_t g_destRectHalfHeight;
	extern int32_t g_unk8828E0;
	extern int32_t g_unk8828E4;
	extern int32_t g_destRectHeightCopy;
	extern void* g_unk500A10;
	extern void* g_unk500A14;
	extern int16_t g_currentDrawSlot;
	extern int32_t g_destRectHalfWidth;
	extern int32_t g_unk72E340;
	extern uint32_t g_unk731CBC;
	extern int32_t g_isElevatorHopLevel;
	extern int32_t g_hasBackdrop;
	extern int32_t g_mainMenuState;
	extern int32_t g_attractModeTimer;
	extern int32_t g_returnedToTitle;
	extern int32_t g_saveLoaded;
	extern int32_t g_showBlackFrames;
	extern int32_t g_demoMode;
	extern int32_t g_hasStaticBackdrop;
	extern int32_t g_nextBackdropId;
	extern int16_t g_levelIndex;
	extern int16_t g_unlocks;
	extern int32_t g_levelFileConversion[15];

	STATIC_ASSERT(sizeof(ToyCfg) == 0x18);
}
