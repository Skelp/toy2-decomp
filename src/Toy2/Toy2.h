#pragma once

#include "Common.h"
#include "Renderer/Renderer.h"

struct D3DAppInfo;

namespace Toy2
{
	enum GameplayStateFlags
	{
		GAMEPLAY_STATE_CUTSCENE_ACTIVE = 0x1,
		GAMEPLAY_STATE_LOCK_FACING_DURING_TRANSITION = 0x2,
	};

	namespace Graphics
	{
		int32_t RemoveDetailLevel();
	}

	namespace Portal
	{
		void UpdateActiveSector();
		void UpdateActiveSectorAt(int32_t x, int32_t y, int32_t z);
	}

	namespace MoveableObject
	{
		struct State
		{
			Vector3I position;
			int32_t directionX;
			int32_t directionZ;
			int16_t verticalVelocity;
			int16_t swingState;
			int16_t pathProgress;
			int16_t segmentLength;
			int16_t facingAngle;
			int16_t currentPathPoint;
			int16_t targetPathPoint;
			int16_t pathRecordType;
			int16_t platformIndex;
			int16_t linkIndex;
		};

		struct InitEntry
		{
			int16_t linkIndex;
			int16_t platformIndex;
			int16_t pathRecordType;
		};

		void ComputeSegment(int32_t pathRecordType, State* object);
		void InitTable(const InitEntry* initTable);

		STATIC_ASSERT(sizeof(State) == 0x28);
		STATIC_ASSERT(sizeof(InitEntry) == 0x6);
	}

	namespace HUD
	{
		enum ChallengeState
		{
			CHALLENGE_STATE_INACTIVE = 0,
			CHALLENGE_STATE_WAITING_FOR_CAMERA = 1,
			CHALLENGE_STATE_ACTIVE = 2,
			CHALLENGE_STATE_COMPLETE = 3,
		};

		enum SlideSlot
		{
			SLIDE_COINS = 2,
			SLIDE_CHALLENGE_STATUS = 6,
			SLIDE_BOSS_STATUS = 7,
		};

		extern int16_t g_slideTimers[12];
		extern int16_t g_slideAngles[12];
		extern int32_t g_challengeState;
	}

	union FramePulseOutputs
	{
		struct
		{
			uint8_t twoTickPhase;
			uint8_t frameDelta;
			uint8_t twoTickCount;
			uint8_t threeTick;
			uint8_t fourTick;
			uint8_t fiveTick;
			uint8_t sixTick;
			uint8_t sevenTick;
			uint8_t eightTick;
			uint8_t sixteenTick;
			uint8_t thirtyTwoTick;
			uint8_t sixtyFourTick;
		};
		uint32_t words[3];
	};

	union FramePulsePhases
	{
		struct
		{
			uint8_t unusedTwoTick;
			uint8_t unusedThreeTick;
			uint8_t twoTick;
			uint8_t threeTick;
			uint8_t fourTick;
			uint8_t fiveTick;
			uint8_t sixTick;
			uint8_t sevenTick;
			uint8_t eightTick;
			uint8_t sixteenTick;
			uint8_t thirtyTwoTick;
			uint8_t sixtyFourTick;
		};
		uint32_t words[3];
	};

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
	void RunModeSelect();
	void OneInit();
	void CheckForQuit();
	int32_t ReadCfg();
	int32_t ShowModeSelect();
	void InitSoftwareRenderer();
	void InitDirect3DRenderer();
	int32_t Run(int32_t argc, char** argv);
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
	extern DevDraw::DrawBuffer* drawb;
	extern DevDraw::TransparentDrawBuffer* drawtranb;
	extern int16_t g_currentDrawSlot;
	extern int32_t g_destRectHalfWidth;
	extern int32_t g_unusedD3DFrameFlag;
	extern uint32_t g_unusedD3DFrameStartTime;
	extern int32_t g_isElevatorHopLevel;
	extern int32_t g_hasBackdrop;
	extern int32_t g_mainMenuState;
	extern int32_t g_attractModeTimer;
	extern int32_t g_returnedToTitle;
	extern D3DAppInfo* g_d3dAppInfo;
	extern int32_t g_unused1;
	extern int32_t g_unused2;
	extern int32_t g_modeSelectFinished;
	extern int32_t g_saveLoaded;
	extern int32_t g_showBlackFrames;
	extern int32_t g_demoMode;
	extern FramePulseOutputs g_framePulseOutputs;
	extern FramePulsePhases g_framePulsePhases;
	extern uint16_t g_framePhase;
	extern int32_t g_hasStaticBackdrop;
	extern int32_t g_nextBackdropId;
	extern int16_t g_levelIndex;
	extern int16_t g_unlocks;
	extern int16_t g_levelTransitionTimer;
	extern int16_t g_levelTransition;
	extern int32_t g_levelObjectiveProgress;
	extern int32_t g_levelInteractionTimer;
	extern int32_t g_specialPickupCount;
	extern uint16_t g_gameplayStateFlags;
	extern int32_t g_idleVoicePreset;
	extern int32_t g_idleVoiceCooldown;
	extern int32_t g_levelFileConversion[15];
	extern uint8_t g_levelTokenBits[16];

	STATIC_ASSERT(sizeof(ToyCfg) == 0x18);
	STATIC_ASSERT(sizeof(FramePulseOutputs) == 0xC);
	STATIC_ASSERT(sizeof(FramePulsePhases) == 0xC);
}
