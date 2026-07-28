#pragma once

#include "Common.h"
#include "Renderer/Renderer.h"

namespace Toy2
{
	namespace MoveableObject
	{
		void InitTable(int32_t);
	}

	namespace HUD
	{
		enum SlideSlot
		{
			SLIDE_CHALLENGE_STATUS = 6,
			SLIDE_BOSS_STATUS = 7,
		};

		extern int16_t g_slideTimers[12];
		extern int16_t g_slideAngles[12];
		extern int32_t g_challengeState;
	}

	void LoadLevelGraphics(int32_t levelFileIndex);
	void InitialiseLevelVariables(int32_t levelIndex);
	void HandleLevelInteractions(int32_t levelIndex);
	void LoadLevelWithFadeIn(int32_t levelFileIndex, int32_t displayMode);
	int32_t ShowLevelIntroScreen(int32_t backgroundId, int32_t displayMode);
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
	void ShowModelNode(int32_t creatureIndex, int32_t nodeIndex);
	void HideModelNode(int32_t creatureIndex, int32_t nodeIndex);
	void ResetBackdropState();
	void ResetBuzzState();
	void ActivateGravityBoots();
	void RespawnCosmicShield();
	void ResetGadgets();
	void ProcessMiscEvents();
	void ProcessMiscEventsEx();
	void RenderGame(int32_t fullRender);
	void PlayLevelMusic();
	void AdvanceFramePhase();
	int32_t ComputeTokenProgress();
	int32_t* BuildPerspectiveDivideTable(int32_t scale);

	extern ToyCfg g_toyCfgData;
	extern int32_t g_levelFileIndex;
	extern int32_t g_perspectiveScaleFixed;
	extern int32_t g_perspectiveDivideTable[0x8000];
	extern int32_t g_perspectiveHalfScale;
	extern int32_t g_gravityBootsTimer;
	extern int32_t g_gravityBootsHoverHeight;
	extern int32_t g_grappleCharges;
	extern int32_t g_grappleState;
	extern int32_t g_destRectWidth;
	extern int32_t g_screenClipRightFixed;
	extern int32_t g_softWindowWidth;
	extern int32_t g_destRectWidthScaled;
	extern int32_t g_destRectHeight;
	extern int32_t g_screenClipLeftFixed;
	extern int32_t g_screenClipRight;
	extern int32_t g_screenClipBottom;
	extern int32_t g_softWindowHalfWidth;
	extern int32_t g_destRectHalfHeight;
	extern int32_t g_screenClipLeft;
	extern int32_t g_screenClipTop;
	extern int32_t g_softWindowHeight;
	extern DevDraw::DrawBuffer* g_drawBuffer;
	extern DevDraw::TransparentDrawBuffer* g_transparentDrawBuffer;
	extern int16_t g_currentDrawSlot;
	extern int32_t g_destRectHalfWidth;
	extern int32_t g_unusedD3DFrameFlag;
	extern uint32_t g_unusedD3DFrameStartTime;
	extern int32_t g_isElevatorHopLevel;
	extern int32_t g_hasBackdrop;
	extern int32_t g_mainMenuState;
	extern int32_t g_attractModeTimer;
	extern int32_t g_returnedToTitle;
	extern int32_t g_saveLoaded;
	extern int32_t g_showBlackFrames;
	extern int32_t g_demoMode;
	extern uint8_t g_twoTickPulseCount;
	extern uint8_t g_fourTickPulse;
	extern uint8_t g_sixteenTickPulse;
	extern uint16_t g_framePhase;
	extern uint8_t g_sixteenTickPhase;
	extern uint8_t g_thirtyTwoTickPhase;
	extern int32_t g_hasStaticBackdrop;
	extern int32_t g_nextBackdropId;
	extern int16_t g_levelIndex;
	extern int16_t g_unlocks;
	extern int16_t g_levelTransitionTimer;
	extern int16_t g_levelTransition;
	extern int32_t g_levelObjectiveProgress;
	extern uint16_t g_gameplayStateFlags;
	extern int32_t g_levelFileConversion[15];
	extern uint8_t g_levelTokenBits[16];

	STATIC_ASSERT(sizeof(ToyCfg) == 0x18);
}
