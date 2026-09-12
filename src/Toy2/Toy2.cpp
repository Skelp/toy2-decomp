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

namespace Toy2
{
	namespace Cutscene
	{
		// STUB: TOY2 0x00402A10
		void Update() {}
	}

	namespace Gadget
	{
		// GLOBAL: TOY2 0x00503A24
		LevelUnlockInfo g_levelUnlockInfo[16] = {
			{ 8, 1 },
			{ 0, 0 },
			{ 0, 0 },
			{ 9, 4 },
			{ 0, 0 },
			{ 0, 0 },
			{ 1, 2 },
			{ 0, 0 },
			{ 0, 0 },
			{ 4, 0x10 },
			{ 0, 0 },
			{ 0, 0 },
			{ 5, 8 },
			{ 0, 0 },
			{ 0, 0 },
			{ 0, 0 },
		};

		// GLOBAL: TOY2 0x00503A44
		UnlockGeometryEntry g_unlockBit1Geometry[] = {
			{ 1, 0x19, 0x31, 0x0A },
			{ 8, 1, 0x30, 2 },
			{ 0x0B, 0x52, 0x6A, 0x18 },
			{ 0xFF, 0, 0, 0 },
		};

		// GLOBAL: TOY2 0x00503A54
		UnlockGeometryEntry g_unlockBit2Geometry[] = {
			{ 2, 0x11, 0x3B, 8 },
			{ 2, 0x12, 0x3C, 9 },
			{ 2, 0x13, 0x3D, 0x0A },
			{ 5, 0x25, 0x50, 0x14 },
			{ 5, 0x30, 0x66, 0x15 },
			{ 7, 0x20, 0x3C, 0x10 },
			{ 0x0E, 0x49, 0x63, 5 },
			{ 0x0E, 0x4B, 0x62, 0x0A },
			{ 0x0E, 0x4A, 0x61, 7 },
			{ 0x0E, 0x47, 0x60, 0x0B },
			{ 0x0E, 0x41, 0x71, 0x0C },
			{ 0x0E, 0x40, 0x70, 9 },
			{ 0x0E, 0x3F, 0x6E, 3 },
			{ 0x0E, 0x3E, 0x6D, 6 },
			{ 0x0E, 0x3D, 0x68, 8 },
			{ 0x0E, 0x3C, 0x67, 2 },
			{ 0x0E, 0x3B, 0x66, 1 },
			{ 0x0E, 0x3A, 0x65, 4 },
			{ 0x0E, 0x35, 0x64, 0x0D },
			{ 0xFF, 0, 0, 0 },
		};

		// GLOBAL: TOY2 0x00503AA4
		UnlockGeometryEntry g_unlockBit4Geometry[] = {
			{ 4, 0x46, 0x6B, 0x0D },
			{ 4, 0x45, 0x6C, 0x19 },
			{ 5, 0x2B, 0x5A, 0x18 },
			{ 7, 0x23, 0x40, 0x13 },
			{ 0xFF, 0, 0, 0 },
		};

		// GLOBAL: TOY2 0x00503AB8
		UnlockGeometryEntry g_unlockBit8Geometry[] = {
			{ 7, 0x19, 0x3E, 0x11 },
			{ 0x0D, 0x1A, 0x3D, 0x0D },
			{ 0x0D, 0x1B, 0x3E, 0x0C },
			{ 0xFF, 0, 0, 0 },
		};

		// GLOBAL: TOY2 0x00503AC8
		UnlockGeometryEntry g_unlockBit16Geometry[] = {
			{ 5, 0x26, 0x51, 0x19 },
			{ 0x0A, 0x2C, 0x60, 0x12 },
			{ 0xFF, 0, 0, 0 },
		};

		// GLOBAL: TOY2 0x00830D48
		int32_t g_unlockNodeState;

	}

	namespace Lighting
	{
		void InitBuzzLight();
		void UpdateBuzzLight();
	}

	extern int32_t g_hudActorAnimationFrame;

	namespace HUD
	{
		// GLOBAL: TOY2 0x0052C824
		int16_t g_slideTimers[12];

		// GLOBAL: TOY2 0x0052F2E0
		int16_t g_slideAngles[12];

		// GLOBAL: TOY2 0x0052F2F8
		int32_t g_challengeState;
	}

	// FUNCTION: TOY2 0x00430930 [MATCHED]
	void InitialiseLevel16() {}

	// FUNCTION: TOY2 0x00430950 [MATCHED]
	void InitialiseLevel17() {}

	// FUNCTION: TOY2 0x00430940 [MATCHED]
	void HandleLevel16Interactions() {}

	// FUNCTION: TOY2 0x00430960 [MATCHED]
	void HandleLevel17Interactions() {}

	// FUNCTION: TOY2 0x0044F840 [MATCHED]
	void ShowModelNode(int32_t creatureIndex, int32_t nodeIndex) { Nu3D::Creature::SetNodeVisibleByIndex(creatureIndex, nodeIndex, 1); }

	// FUNCTION: TOY2 0x0044F860 [MATCHED]
	void HideModelNode(int32_t creatureIndex, int32_t nodeIndex) { Nu3D::Creature::SetNodeVisibleByIndex(creatureIndex, nodeIndex, 0); }

	// FUNCTION: TOY2 0x0049EAC0 [MATCHED]
	void PlayLevelMusic()
	{
		if (HUD::g_slideTimers[HUD::SLIDE_BOSS_STATUS] != 0)
		{
			if (AudioManager::g_loopingMusicTrackIndex != AudioManager::MUSIC_TRACK_BOSS)
			{
				AudioManager::PlayMusicLooping(AudioManager::MUSIC_TRACK_BOSS);
			}
		}
		else if (HUD::g_slideTimers[HUD::SLIDE_CHALLENGE_STATUS] != 0 && HUD::g_challengeState > 1)
		{
			if (AudioManager::g_loopingMusicTrackIndex != AudioManager::MUSIC_TRACK_CHALLENGE)
			{
				AudioManager::PlayMusicLooping(AudioManager::MUSIC_TRACK_CHALLENGE);
			}
		}
		else if (AudioManager::g_loopingMusicTrackIndex != g_levelIndex)
		{
			AudioManager::PlayMusicLooping(g_levelIndex);
		}
	}

	// GLOBAL: TOY2 0x00508D70
	ToyCfg g_toyCfgData = {
		7, /* flags */
		1, /* detail */
		2.0, /* gamma correction */
		-1, /* driver index */
		-1, /* device index */
		-1, /* display mode index */
	};

	// GLOBAL: TOY2 0x0088278C
	int32_t g_levelFileIndex;

	// GLOBAL: TOY2 0x00704E6C
	int32_t g_perspectiveScaleFixed;

	// GLOBAL: TOY2 0x00704E74
	int32_t g_perspectiveDivideTable[0x8000];

	// GLOBAL: TOY2 0x00724E74
	int32_t g_perspectiveHalfScale;

	// GLOBAL: TOY2 0x00882768
	int32_t g_destRectWidth;

	// The rasterizer's screen-space clip rectangle. InitSoftWindow (0x0047CBA0,
	// named by its own "InitSoftWindow(%i,%i)" log string) centers a window of
	// the requested size in the destination rectangle and writes these bounds:
	// left = (destWidth - windowWidth) / 2, right = left + windowWidth - 1, and
	// the same for top and bottom. UpdateD3DState writes the full-window case,
	// where left and top are 0.
	//
	// The helper at 0x00490D10 proves the roles: it clamps a point's x field up
	// to g_screenClipLeft and down to g_screenClipRight, and its y field up to
	// g_screenClipTop and down to g_screenClipBottom.
	//
	// The *Fixed pair holds the same left and right edges in 1/1024 units.
	// InitSoftWindow computes right * 1024 + 1023, which for a zero left edge
	// equals the width * 1024 - 1 that UpdateD3DState stores.

	// GLOBAL: TOY2 0x00882784
	int32_t g_screenClipRightFixed;

	// The size InitSoftWindow was last asked for, retained for the callers that
	// re-derive the window without repeating the centering arithmetic.

	// GLOBAL: TOY2 0x00882798
	int32_t g_softWindowWidth;

	// GLOBAL: TOY2 0x008828A0
	int32_t g_destRectWidthScaled;

	// GLOBAL: TOY2 0x008828AC
	int32_t g_destRectHeight;

	// GLOBAL: TOY2 0x008828BC
	int32_t g_screenClipLeftFixed;

	// GLOBAL: TOY2 0x008828C4
	int32_t g_screenClipRight;

	// GLOBAL: TOY2 0x008828CC
	int32_t g_screenClipBottom;

	// GLOBAL: TOY2 0x008828D8
	int32_t g_softWindowHalfWidth;

	// GLOBAL: TOY2 0x008828DC
	int32_t g_destRectHalfHeight;

	// GLOBAL: TOY2 0x008828E0
	int32_t g_screenClipLeft;

	// GLOBAL: TOY2 0x008828E4
	int32_t g_screenClipTop;

	// GLOBAL: TOY2 0x008828E8
	int32_t g_softWindowHeight;

	// GLOBAL: TOY2 0x00500A10
	DevDraw::DrawBuffer* drawb = &DevDraw::g_drawBufferStorage;

	// GLOBAL: TOY2 0x00500A14
	DevDraw::TransparentDrawBuffer* drawtranb = &DevDraw::g_transparentDrawBufferStorage;

	// GLOBAL: TOY2 0x00500A28
	int16_t g_currentDrawSlot;

	// GLOBAL: TOY2 0x00500A24
	int32_t g_destRectHalfWidth;

	// Retail writes this flag during UpdateD3DState but never reads it.
	// GLOBAL: TOY2 0x0072E340
	int32_t g_unusedD3DFrameFlag;

	// Retail stores the frame start time but never reads it.
	// GLOBAL: TOY2 0x00731CBC
	uint32_t g_unusedD3DFrameStartTime;

	// GLOBAL: TOY2 0x00731C00
	int16_t g_unusedD3DRendererValue;

	// GLOBAL: TOY2 0x00731F28
	int32_t g_unusedD3DRendererBuffer[0x420];

	// GLOBAL: TOY2 0x0072E34C
	int32_t g_mpegPlaybackDisabled;

	// GLOBAL: TOY2 0x0072E354
	char g_saveSlotDescriptions[8][256];

	// GLOBAL: TOY2 0x0072EF94
	int32_t g_movieTimingRate;

	// GLOBAL: TOY2 0x0072EFB0
	uint32_t g_movieTimingStartMs;

	// GLOBAL: TOY2 0x0052AD9C
	int32_t g_returnedToTitle;

	// GLOBAL: TOY2 0x0052ADA0
	int32_t g_attractModeTimer;

	// GLOBAL: TOY2 0x00A4C454
	int32_t g_isElevatorHopLevel;

	// GLOBAL: TOY2 0x005D2A90
	int32_t g_hasBackdrop;

	// GLOBAL: TOY2 0x004FCDB4
	int32_t g_cdBaseTrack = 2;

	// GLOBAL: TOY2 0x005281C8
	int32_t g_modeSelectFinished;

	// GLOBAL: TOY2 0x0072EFD8
	int16_t g_pastInitialBoot;

	// GLOBAL: TOY2 0x0052B7DC
	int16_t g_levelTransition;

	// GLOBAL: TOY2 0x0052B7D8
	int32_t g_levelObjectiveProgress;

	// GLOBAL: TOY2 0x0052B816
	uint16_t g_gameplayStateFlags;

	// GLOBAL: TOY2 0x0052AD94
	int32_t g_demoMode;

	// GLOBAL: TOY2 0x0052F1C0
	FramePulseOutputs g_framePulseOutputs;

	// GLOBAL: TOY2 0x0052AD68
	uint16_t g_framePhase;

	// GLOBAL: TOY2 0x0052AD58
	FramePulsePhases g_framePulsePhases;

	// GLOBAL: TOY2 0x0055A0E0
	int32_t g_hasStaticBackdrop;

	// GLOBAL: TOY2 0x0054E04E
	int16_t g_pickupSpriteFrame;

	// GLOBAL: TOY2 0x005D2A94
	uint8_t g_nearbyEffectRecordIndices[256];

	// GLOBAL: TOY2 0x00500A50
	int32_t g_nextBackdropId = 36;

	// GLOBAL: TOY2 0x00830C88
	int32_t g_mainMenuState;

	// GLOBAL: TOY2 0x00503840
	int16_t g_levelTokenTarget[16] = { 0, 0, 3, 0, 0, 10, 0, 0, 18, 0, 0, 28, 0, 0, 40, 0 };

	// GLOBAL: TOY2 0x00503AD4
	char g_pathBinName[16] = "PAD\\PATH00.BIN";

	// GLOBAL: TOY2 0x0052AD8A
	int16_t g_levelIndex;

	// GLOBAL: TOY2 0x0050268C
	int32_t g_levelFileConversion[15] = { 1, 2, 6, 4, 5, 3, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

	// GLOBAL: TOY2 0x0052ADB0
	int16_t g_pauseMenuState;

	// GLOBAL: TOY2 0x0052B7E4
	int16_t g_pauseMenuSelection;

	// GLOBAL: TOY2 0x005039BC
	uint8_t g_pauseMenuEntryCounts[5] = { 4, 2, 2, 2, 2 };

	// GLOBAL: TOY2 0x00830D38
	int32_t g_cameraIdleTimer;

	// GLOBAL: TOY2 0x00830D4C
	int32_t g_specialPickupCount;

	// GLOBAL: TOY2 0x00830E38
	int32_t g_previousLevelObjectiveProgress;

	// GLOBAL: TOY2 0x00882920
	int32_t g_gravityBootsTimer;

	// GLOBAL: TOY2 0x00882928
	int32_t g_gravityBootsHoverHeight;

	// GLOBAL: TOY2 0x00882938
	int32_t g_grappleCharges;

	// GLOBAL: TOY2 0x00882950
	int32_t g_grappleState;

	// GLOBAL: TOY2 0x0052B818
	int16_t g_isPaused;

	// GLOBAL: TOY2 0x0052B820
	int16_t g_demoInputBuffer[2048];

	// GLOBAL: TOY2 0x00529E48
	int16_t g_pauseMenuBlinkTimer;

	// GLOBAL: TOY2 0x00830CB0
	Nu3D::Camera::ActiveCameraTransform g_pauseCameraTarget;

	// GLOBAL: TOY2 0x00830CC8
	int32_t g_pauseCheatTimer;

	// GLOBAL: TOY2 0x00830E20
	int32_t g_pauseMusicVolume;

	// GLOBAL: TOY2 0x00830E30
	int32_t g_pauseSoundVolume;

	// GLOBAL: TOY2 0x0052F2D8
	int16_t g_unlocks;

	// GLOBAL: TOY2 0x0052F2DC
	int16_t g_levelTransitionTimer;

	// GLOBAL: TOY2 0x00830D50
	int32_t g_quitToTitleFlag;

	// GLOBAL: TOY2 0x0052F300
	Buzz::Toy2BuzzActor g_buzzActor;

	// GLOBAL: TOY2 0x00529388
	int32_t g_wndIsExitingUnused;

	// GLOBAL: TOY2 0x0072EFC8
	int32_t g_clearScreenSaveResult;

	// GLOBAL: TOY2 0x00500AA0
	int32_t g_setScreenSaveRunning = 1;

	// GLOBAL: TOY2 0x00830C64
	int32_t g_extraControlsUnused;

	// GLOBAL: TOY2 0x00830C1C
	int32_t g_inputSuppressFrames;

	// GLOBAL: TOY2 0x00500A58
	SectorBackdropTexIdTable g_sectorBackdropTexTable = {
		{ 36, 40, 41, 42, 43, 44, 45, 46, 47, 32 },
		{ 88, 89, 90, 91, 92, 93, 94, 95 },
	};

	// GLOBAL: TOY2 0x00534550
	D3DAppInfo* g_d3dAppInfo;

	// GLOBAL: TOY2 0x00534560
	int32_t g_demoVersion;

	// GLOBAL: TOY2 0x00731F18
	int32_t g_saveMenuState;

	int32_t TickSaveMenuMachine(int32_t param);
	int16_t UpdateD3DState();
	void UpdateAudioChannels();
	namespace MovieViewer
	{
		int32_t Tick(int32_t movieIndex);
	}
	int32_t PlayMovie(int32_t movieId);
	int32_t PlayMovieWithTransition(int32_t movieId, int32_t backgroundId);
	int32_t CleanupManagers();
}

namespace Toy2
{
	namespace Graphics
	{
		// FUNCTION: TOY2 0x004CDD90 [MATCHED]
		int32_t AddDetailLevel()
		{
			int32_t detail = g_toyCfgData.detail + 1;

			if ((g_toyCfgData.detail + 1) >= 2)
				detail = 2;

			g_toyCfgData.detail = detail;

			return detail;
		}

		// FUNCTION: TOY2 0x004CDDB0 [MATCHED]
		int32_t RemoveDetailLevel()
		{
			int32_t detail = (g_toyCfgData.detail - 1) <= 0 ? 0 : g_toyCfgData.detail - 1;
			g_toyCfgData.detail = detail;

			return detail;
		}
	}

	namespace HUD
	{}

	namespace Game
	{
		const uint16_t ACTOR_FLAG_IGNORE_RESPAWN_VISIBILITY = 0x40;

		// FUNCTION: TOY2 0x00406CD0 [MATCHED]
		void InitActor(Actor::Toy2Actor* actor, int32_t fullInit)
		{
			RawLoader::CreatureListRam* creature = actor->creatureRam;
			actor->creatureId = creature->creatureId;
			actor->pos.x = creature->pos.x << 5;
			actor->pos.y = creature->pos.y << 5;
			actor->pos.z = creature->pos.z << 5;
			actor->movementData = Actor::g_movementDataByControl[creature->movCtrl];
			actor->actorPhase = creature->entCtrl.actorPhase;

			int16_t actorFlags;
			if (fullInit == 0)
			{
				actorFlags = actor->actorFlags & Actor::ACTOR_FLAG_BOSS;
				if (actorFlags != 0)
					actorFlags = creature->entCtrl.actorFlags + Actor::ACTOR_FLAG_BOSS;
				else
					actorFlags = creature->entCtrl.actorFlags;
			}
			else
			{
				actorFlags = creature->entCtrl.actorFlags;
			}

			int32_t actorX = actor->pos.x;
			int32_t actorZ = actor->pos.z;
			actor->actorFlags = actorFlags;

			actor->respawnDelay = creature->entCtrl.respawnDelay;
			int32_t actorY = actor->pos.y;
			actor->boundary.x = actorX;
			actor->motionTargetPos.x = actorX;
			actor->boundary.y = actorY;
			actor->boundary.z = actorZ;
			actor->motionTargetPos.y = actorY;
			actor->motionTargetPos.z = actorZ;
			if (actor->respawnDelay == 100)
				actor->respawnDelay = 0x708;

			actor->pitchAngle = 0;
			actor->yawAngle = creature->initialFacingAngle << 4;
			actor->rollAngle = 0;
			actor->targetYaw = actor->yawAngle;
			actor->velX = 0;
			actor->gravityVel = 0;
			actor->velForward = 0;
			actor->animationFramePosition = 0;
			actor->primaryAnimIdx = 0;
			actor->secondaryAnimIdx = -1;
			actor->animationFrameSequence = Actor::g_animationFrameSequences[1 - 1];
			actor->previousActorPhase = 0;
			actor->unkWord15 = 0;
			actor->movementCommandTimer = 0;
			actor->damageCooldownTimer = 0;
			actor->movementCommandValue = INT_MIN;
			actor->lastValidYPosition = actor->pos.y;

			if (fullInit == 0)
				return;

			actor->visibilityDistance = 0x500;
			switch (creature->creatureId)
			{
				case 1:
					actor->visibilityDistance = 0x708;
					return;
				case 9:
					actor->visibilityDistance = 0x708;
					return;
				case 16:
					actor->visibilityDistance = 0x708;
					return;
				case 21:
					actor->visibilityDistance = 0x708;
					return;
				case 29:
					actor->visibilityDistance = 0x708;
					return;
				case 30:
					actor->visibilityDistance = 0x708;
					return;
				case 34:
					actor->visibilityDistance = 0x708;
					return;
				case 35:
					actor->visibilityDistance = 0x708;
					return;
				case 36:
					actor->visibilityDistance = 0x708;
					return;
				case 37:
					actor->visibilityDistance = 0x708;
					return;
				case 39:
					actor->visibilityDistance = 0x708;
					return;
				case 42:
					actor->visibilityDistance = 0x708;
					return;
				case 49:
					actor->visibilityDistance = 0x708;
					return;
				case 54:
					actor->visibilityDistance = 0x708;
					return;
				case 55:
					actor->visibilityDistance = 0x708;
					return;
				case 57:
					actor->visibilityDistance = 0x708;
					return;
				case 62:
					actor->visibilityDistance = 0x708;
					return;
				case 4:
					actor->actorBehaviour = CreatureBehaviour::Zurg3;
					return;
				case 5:
					actor->actorBehaviour = CreatureBehaviour::TinMan;
					break;
				case 6:
					actor->actorBehaviour = CreatureBehaviour::Sheep;
					break;
				case 8:
					actor->actorBehaviour = CreatureBehaviour::RCCar;
					break;
				case 12:
					actor->actorBehaviour = CreatureBehaviour::LawnMower;
					break;
				case 13:
					actor->actorBehaviour = CreatureBehaviour::Army;
					return;
				case 14:
					actor->actorBehaviour = CreatureBehaviour::ZgCar;
					break;
				case 15:
					actor->actorBehaviour = CreatureBehaviour::ZKite;
					return;
				case 19:
					actor->actorBehaviour = CreatureBehaviour::LTyke;
					return;
				case 20:
					actor->actorBehaviour = CreatureBehaviour::ZPod;
					return;
				case 22:
					actor->actorBehaviour = CreatureBehaviour::Drill;
					break;
				case 23:
					actor->actorBehaviour = CreatureBehaviour::Mouse;
					return;
				case 24:
					actor->actorBehaviour = CreatureBehaviour::BPlane;
					actor->actorPhase = 0;
					actor->respawnDelay = 10000;
					return;
				case 25:
					actor->actorBehaviour = CreatureBehaviour::Box;
					return;
				case 26:
					actor->actorBehaviour = CreatureBehaviour::Dino;
					break;
				case 27:
					actor->actorBehaviour = CreatureBehaviour::ZBoat;
					break;
				case 28:
					actor->actorBehaviour = CreatureBehaviour::Chick;
					return;
				case 31:
					actor->actorBehaviour = CreatureBehaviour::GunsP;
					break;
				case 32:
					actor->actorBehaviour = CreatureBehaviour::Clown;
					break;
				case 38:
					actor->actorBehaviour = CreatureBehaviour::Martian;
					return;
				case 40:
					actor->actorBehaviour = CreatureBehaviour::Rabid;
					return;
				case 41:
					actor->actorBehaviour = CreatureBehaviour::Buzzard;
					return;
				case 43:
					actor->actorBehaviour = CreatureBehaviour::Ducks;
					return;
				case 45:
					if (g_levelFileIndex == 11)
						actor->actorBehaviour = CreatureBehaviour::GunsLLevel11;
					else
						actor->actorBehaviour = CreatureBehaviour::GunsL;
					break;
				case 46:
					actor->actorBehaviour = CreatureBehaviour::FatBloke;
					return;
				case 47:
					actor->actorBehaviour = CreatureBehaviour::BBuggy;
					break;
				case 48:
					actor->visibilityDistance = 0xED8;
					return;
				case 50:
				case 51:
				case 52:
				case 53:
					actor->actorBehaviour = CreatureBehaviour::Pilot;
					return;
				case 58:
					if (g_levelFileIndex == 14)
						actor->actorBehaviour = CreatureBehaviour::SmithLevel14;
					else
						actor->actorBehaviour = CreatureBehaviour::Smith;
					return;
				case 59:
					actor->actorBehaviour = CreatureBehaviour::Luggage;
					return;
				case 61:
					if (g_levelFileIndex == 13)
						actor->actorBehaviour = CreatureBehaviour::ProsPLevel13;
					else
						actor->actorBehaviour = CreatureBehaviour::ProsP;
					break;
				default:
					return;
			}

			actor->visibilityDistance = 0x708;
		}

		// FUNCTION: TOY2 0x00407440 [PROVISIONAL]
		void ActorCollisionCheck()
		{
			int32_t attackType = Actor::DAMAGE_NONE;
			int32_t buzzRadius = 150;
			if (g_spinCooldownTimer > 20 || g_spinHoverTimer <= -120)
			{
				attackType = Actor::DAMAGE_SPIN;
				buzzRadius = 400;
			}

			if (g_groundSlamTimer > 0)
				attackType = Actor::DAMAGE_GROUND_SLAM;
			if (g_groundSlamTimer < -30)
			{
				attackType = Actor::DAMAGE_GROUND_SLAM;
				buzzRadius = 400;
			}

			int32_t buzzX = g_buzzActor.posAngles.pos.x;
			int32_t buzzY = g_buzzActor.posAngles.pos.y - 0x1CC0;
			int32_t buzzZ = g_buzzActor.posAngles.pos.z;

			Actor::Toy2Actor** actorSlot = Actor::g_activeActors;
			Actor::Toy2Actor* actor = *actorSlot;
			while (actor != 0)
			{
				int32_t deltaX = (actor->boundingOffset.x - buzzX + actor->pos.x) >> 5;
				int32_t deltaY = (actor->boundingOffset.y - buzzY + actor->pos.y) >> 5;
				int32_t deltaZ = (actor->boundingOffset.z - buzzZ + actor->pos.z) >> 5;
				int32_t combinedRadius = actor->boundingSphereRadius + buzzRadius;

				if (deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ < combinedRadius * combinedRadius && actor->damageCooldownTimer >= 0)
				{
					int32_t yawSin = Numerics::g_sinCosLUT[(actor->yawAngle - 0x800) & 0xFFF] >> 2;
					int32_t yawCos = Numerics::g_sinCosLUT[(actor->yawAngle - 0x400) & 0xFFF] >> 2;
					Actor::ActorCollisionVolume* volume = &actor->collisionVolumes[actor->primaryAnimIdx];

					int32_t collisionX = ((volume->offset.x * yawCos + volume->offset.z * yawSin) >> 12) - buzzX + actor->pos.x;
					int32_t collisionZ = ((volume->offset.z * yawCos - volume->offset.x * yawSin) >> 12) - buzzZ + actor->pos.z;
					int32_t scaledX = (((yawSin * collisionZ + yawCos * collisionX) >> 12) * volume->scale.z) >> 13;
					int32_t scaledZ = (((yawCos * collisionZ - yawSin * collisionX) >> 12) * volume->scale.x) >> 13;
					int32_t scaledY = ((volume->offset.y - buzzY + actor->pos.y) * volume->scale.y) >> 13;
					combinedRadius = volume->radius + buzzRadius;

					if (scaledX * scaledX + scaledY * scaledY + scaledZ * scaledZ < combinedRadius * combinedRadius
						&& (actor->actorFlags & Actor::ACTOR_FLAG_COLLIDABLE) != 0)
					{
						uint32_t attackAngle =
							Nu3D::Math::CartesianToFixedAngle(g_buzzActor.posAngles.pos.x - actor->pos.x, g_buzzActor.posAngles.pos.z - actor->pos.z);
						actor->actorFlags |= Actor::ACTOR_FLAG_INTERACTION_REQUESTED;

						int32_t actorDamageType = attackType;
						if (attackType == Actor::DAMAGE_NONE || (actor->creatureRam->defenseMode & RawLoader::CREATURE_DEFENSE_SPECIAL_ATTACK_DAMAGE) == 0
							|| (attackType == Actor::DAMAGE_GROUND_SLAM && scaledY < 0))
						{
							actorDamageType = Actor::DAMAGE_NONE;
						}

						uint32_t buzzDamageFlags;
						if ((actor->actorFlags & Actor::ACTOR_FLAG_DAMAGES_BUZZ) != 0 && attackType == Actor::DAMAGE_NONE)
							buzzDamageFlags = Buzz::DAMAGE_KNOCKBACK | Buzz::DAMAGE_NORMAL;
						else if (actor->actorPhase == 0x66 || actorDamageType != Actor::DAMAGE_NONE)
							buzzDamageFlags = 0;
						else
							buzzDamageFlags = Buzz::DAMAGE_KNOCKBACK;

						Actor::HandleDamage(actor, attackAngle + 0x800, actorDamageType);
						Buzz::HandleDamage(attackAngle & 0xFFF, buzzDamageFlags);
					}
				}

				actor = *++actorSlot;
			}
		}

		// FUNCTION: TOY2 0x004086F0 [PROVISIONAL]
		void UpdateActors()
		{
			int32_t activeActorCount = 0;
			int32_t cameraDistanceSquared[64];
			for (int32_t actorIndex = 0; actorIndex < 64; actorIndex++)
			{
				Actor::Toy2Actor* actor = &Actor::g_creatureActors[actorIndex];
				uint16_t actorFlags = actor->actorFlags;
				actor->actorFlags = actorFlags & ~Actor::ACTOR_FLAG_ACTIVE;
				if (actor->creatureId <= 0)
					continue;

				if (actor->actorPhase > 0)
				{
					int32_t activationDistance = (actor->visibilityDistance * 362 >> 9) + (actor->boundingSphereRadius >> 3);
					int32_t deltaX = (Camera::g_renderCameraTransform.pos.x - actor->boundingOffset.x - actor->pos.x) >> 8;
					int32_t deltaY = (Camera::g_renderCameraTransform.pos.y - actor->boundingOffset.y - actor->pos.y) >> 8;
					int32_t deltaZ = (Camera::g_renderCameraTransform.pos.z - actor->boundingOffset.z - actor->pos.z) >> 8;
					int32_t distanceSquared;
					if (g_levelFileIndex == 6 || actor->actorPhase == 0xCA)
						distanceSquared = 10;
					else
						distanceSquared = deltaZ * deltaZ + deltaY * deltaY + deltaX * deltaX;

					if (distanceSquared < activationDistance * activationDistance)
					{
						Actor::g_activeActors[activeActorCount] = actor;
						cameraDistanceSquared[activeActorCount] = distanceSquared;
						activeActorCount++;
					}
				}
				else
				{
					if (actor->respawnDelay == 0)
					{
						if ((actorFlags & ACTOR_FLAG_IGNORE_RESPAWN_VISIBILITY) != 0
							|| Nu3D::Camera::IsActorSpawnVisible(&Camera::g_renderCameraTransform.pos, actor) == 0)
						{
							InitActor(actor, 0);
						}
					}
					else if (actor->respawnDelay < 5000 && Actor::g_lastKilledActor != actor)
					{
						actor->respawnDelay -= (int16_t)Renderer::g_frameDelta;
						if (actor->respawnDelay < 0)
							actor->respawnDelay = 0;
					}
				}
			}

			Actor::g_activeActors[activeActorCount] = 0;
			cameraDistanceSquared[activeActorCount] = -1;
			Camera::CullActors(&Camera::g_renderCameraTransform.pos);
			if (g_levelFileIndex == 10)
				ElevatorHop::TransformMouseActors();

			Actor::Toy2Actor* const excludedActor = (Actor::Toy2Actor*)-1;
			for (int32_t activeIndex = 0; Actor::g_activeActors[activeIndex] != 0; activeIndex++)
			{
				Actor::Toy2Actor* actor = Actor::g_activeActors[activeIndex];
				int32_t activeDistance = (actor->boundingSphereRadius >> 3) + 400;
				if (((actor->actorFlags & Actor::ACTOR_FLAG_TARGETABLE) == 0 && cameraDistanceSquared[activeIndex] >= activeDistance * activeDistance)
					|| (actor->actorFlags & Actor::ACTOR_FLAG_CULLED) != 0)
				{
					Actor::g_activeActors[activeIndex] = excludedActor;
				}
				else
				{
					actor->actorFlags |= Actor::ACTOR_FLAG_ACTIVE;
				}
			}

			activeActorCount = 0;
			for (Actor::Toy2Actor** compactSlot = Actor::g_activeActors; *compactSlot != 0; compactSlot++)
			{
				if (*compactSlot != excludedActor)
					Actor::g_activeActors[activeActorCount++] = *compactSlot;
			}
			Actor::g_activeActors[activeActorCount] = 0;

			Actor::Toy2Actor** actorSlot = Actor::g_activeActors;
			while (*actorSlot != 0)
			{
				Actor::Toy2Actor* actor = *actorSlot;
				Actor::UpdateAIMovement(actor);
				if (actor->hitpoints > 0)
				{
					actor->hitpoints -= (int16_t)Renderer::g_frameDelta;
					if (actor->hitpoints <= 0)
					{
						actor->hitpoints = 0;
						Actor::Kill(actor, Actor::KILL_EFFECTS);
					}
				}

				if (actor->hitpoints < 0)
				{
					if (g_framePulseOutputs.fourTick != 0 && actor->hitpoints < -1)
					{
						Actor::ActorCollisionVolume* volume = actor->collisionVolumes;
						int32_t effectX = (*g_randDatBufferPtr++ - 0x80) * 0x20 + volume->offset.x + actor->pos.x;
						int32_t effectY = (*g_randDatBufferPtr++ - 0x80) * 0x20 + volume->offset.y + actor->pos.y;
						int32_t effectZ = (*g_randDatBufferPtr++ - 0x80) * 0x20 + volume->offset.z + actor->pos.z;
						Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(effectX, effectY, effectZ, 0x25, 1);
						particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
					}

					actor->hitpoints += (int16_t)Renderer::g_frameDelta;
					if (actor->hitpoints >= 0)
					{
						actor->hitpoints = 0;
						Actor::Kill(actor, Actor::KILL_REMOVE_ACTOR);
						actorSlot--;
					}
				}
				actorSlot++;
			}

			if (g_levelFileIndex == 10)
				ElevatorHop::TransformMouseActors();
		}

	}
	namespace PostGameRecap
	{}

	namespace PostGameSaveMenu
	{}

	namespace GameOver
	{}

	// FUNCTION: TOY2 0x00453CA0 [MATCHED]
	void ResetBackdropState()
	{
		g_hasStaticBackdrop = 0;
		Renderer::g_virtualScreenWidth = 512.0f;
		Renderer::g_virtualScreenHeight = 256.0f;
		g_nextBackdropId = 36;
	}

	// FUNCTION: TOY2 0x0049EB50 [PROVISIONAL]
	int32_t ComputeTokenProgress()
	{
		int32_t collected = 0;
		int32_t levelCount = 0;
		for (int32_t i = 0; i < 15; i++)
		{
			int32_t bits = SaveManager::g_save0Data.tokens[g_levelFileConversion[i]];
			if (! bits)
				break;
			for (int32_t j = 0; j < 5; j++)
			{
				if (bits & 1)
					collected++;
				bits >>= 1;
			}
			levelCount++;
		}
		return (g_levelTokenTarget[levelCount] + collected * 0x100) * 0x100 + levelCount;
	}

	// FUNCTION: TOY2 0x004A3770 [MATCHED]
	void LoadPathBin()
	{
		int32_t levelFile = g_levelFileIndex;
		int32_t tens = levelFile / 10;
		g_pathBinName[8] = (char)(tens + '0');
		g_pathBinName[9] = (char)(levelFile - tens * 10 + '0');
		FileUtils::LoadFile(g_pathBinName, g_demoInputBuffer);
	}

	namespace MovieViewer
	{}

	// Applies the debug view keys: F3 and F4 zoom the software renderer out and in,
	// F5 and F6 remove and add a detail level. Three frame loops state this block.
#define PROCESS_DEBUG_VIEW_KEYS()           \
	if (InputManager::IsKeyPressed(DIK_F3)) \
	{                                       \
		SoftwareRenderer::ZoomOut();        \
		g_extraControlsUnused = 15;         \
	}                                       \
	if (InputManager::IsKeyPressed(DIK_F4)) \
	{                                       \
		SoftwareRenderer::ZoomIn();         \
		g_extraControlsUnused = 15;         \
	}                                       \
	if (InputManager::IsKeyPressed(DIK_F5)) \
	{                                       \
		Graphics::RemoveDetailLevel();      \
		g_extraControlsUnused = 15;         \
	}                                       \
	if (InputManager::IsKeyPressed(DIK_F6)) \
	{                                       \
		Graphics::AddDetailLevel();         \
		g_extraControlsUnused = 15;         \
	}
	// FUNCTION: TOY2 0x0049B9E0 [PROVISIONAL]
	int32_t TickSaveMenuMachine(int32_t postGameSave)
	{
		enum SaveMenuState
		{
			SAVE_MENU_OPTIONS,
			SAVE_MENU_LOAD,
			SAVE_MENU_SAVE,
			SAVE_MENU_EXIT
		};

		enum SaveMenuOption
		{
			SAVE_OPTION_LOAD,
			SAVE_OPTION_SAVE,
			SAVE_OPTION_MAIN_MENU
		};

		const int32_t slotCount = 8;
		const int32_t inputRepeatDelay = 30;
		const int32_t blinkPeriod = 20;
		// A selected item is hidden while blinkTimer is at or above blinkOnPoint.
		const int32_t blinkOnPoint = 10;
		const int32_t slotRowPitch = 23;
		const int32_t slotRowTop = 33;
		const int32_t promptRowPitch = 25;
		const int32_t promptRowTop = 28;
		const int32_t textCenterX = 256;
		const int32_t titleRowTop = 25;
		const int32_t slotTitleRowTop = 3;
		const int32_t loadOptionRowY = 75;
		const int32_t saveOptionRowY = 100;
		const int32_t mainMenuOptionRowY = 125;
		const int32_t fileNameSize = 256;
		const int32_t descriptionSize = 256;
		// The menu starts at the neutral tint and fades to black before it exits.
		const int32_t neutralTintLevel = 128;
		const int32_t tintFadeInRate = 12;
		const int32_t tintFadeOutRate = 6;
		const int32_t menuSoundPitch = 0x1200;
		const int32_t menuSoundVolume = 0x50;
		const int32_t progressFieldMask = 0xff;

		int32_t finished = 0;
		int32_t state = SAVE_MENU_OPTIONS;
		int32_t selectedOption = SAVE_OPTION_LOAD;
		int32_t result = 0;
		RGBA clearColor;
		clearColor.value = 0;

		Nu3D::Camera::g_cameraTintRed = 0;
		Nu3D::Camera::g_cameraTintGreen = 0;
		Nu3D::Camera::g_cameraTintBlue = 0;
		Nu3D::Camera::SetTint(neutralTintLevel, neutralTintLevel, neutralTintLevel, tintFadeInRate);

		int32_t inputDelay = inputRepeatDelay;
		int32_t blinkTimer = blinkPeriod;
		int32_t selectedSlot = 0;

		if (postGameSave)
			state = SAVE_MENU_SAVE;

		int32_t slotXOffsets[slotCount];
		for (int32_t slot = 0; slot < slotCount; ++slot)
		{
			char* slotDescription = g_saveSlotDescriptions[slot];
			char fileName[fileNameSize];
			sprintf(fileName, "Toy2%02d.sav", slot + 1);

			FILE* file = fopen(fileName, "rb");
			if (file)
			{
				int32_t descriptionLength;
				fread(&descriptionLength, 1, 4, file);
				if (descriptionLength && slotDescription)
				{
					fread(slotDescription, 1, descriptionLength, file);
					slotDescription[descriptionLength] = '\0';
				}
				fclose(file);
			}
			else if (slotDescription)
			{
				slotDescription[0] = '\0';
			}

			// The slots sit on a cosine arc, so each row has its own horizontal offset.
			slotXOffsets[slot] = (int32_t)(cos(slot * 0.642699062824249f - 1.0f) * 80.0);
		}

		while (! finished)
		{
			ProcessWndEvents();
			if (g_windowData.wndIsExiting)
			{
				DrawingDevice::RestoreToGDISurface(1);
				Logger::GetErrorHandler("C:\\projects\\toy2\\toy2.cpp", 0x123E)(&SaveManager::g_emptyString);
			}

			PROCESS_DEBUG_VIEW_KEYS();

			InputManager::UpdateButtonStates();
			if (g_inputSuppressFrames)
			{
				--g_inputSuppressFrames;
				InputManager::g_curButtonsPressed = 0;
			}
			UpdateAudioChannels();
			SoftwareRenderer::g_backBufferClearComplete = 0;

			Renderer::DoFrameDelay(0);
			Nu3D::Camera::FadeToTargetTint();
			Renderer::ClearScreen(clearColor, 2);

			if (Renderer::BeginScene())
			{
				Renderer::g_parallaxCurHorizScroll = 0.0f;
				Renderer::g_parallaxHorizOffset = 0.0f;
				Renderer::g_parallaxTexHeightRatio = 1.0f;
				Renderer::g_parallaxTexWidthRatio = 1.0f;
				Renderer::RenderParallaxBackground(0);

				switch (state)
				{
					case SAVE_MENU_OPTIONS: {
						Renderer::g_parallaxCurHorizScroll = 0.0f;
						if (NGNLoader::GetTextureDataIndex(g_sectorBackdropTexTable.primary[1]))
							g_nextBackdropId = g_sectorBackdropTexTable.primary[1];
						else if (NGNLoader::GetTextureDataIndex(g_sectorBackdropTexTable.secondary[0]))
							g_nextBackdropId = g_sectorBackdropTexTable.secondary[0];
						g_hasBackdrop = 1;

						Renderer::DrawFormattedText(textCenterX, titleRowTop, "load / save options");
						if (selectedOption != SAVE_OPTION_LOAD || blinkTimer < blinkOnPoint)
							Renderer::DrawFormattedText(textCenterX, loadOptionRowY, "load game");
						if (selectedOption != SAVE_OPTION_SAVE || blinkTimer < blinkOnPoint)
							Renderer::DrawFormattedText(textCenterX, saveOptionRowY, "save game");
						if (selectedOption != SAVE_OPTION_MAIN_MENU || blinkTimer < blinkOnPoint)
							Renderer::DrawFormattedText(textCenterX, mainMenuOptionRowY, "main menu");

						if (inputDelay <= 0)
						{
							if (selectedOption != SAVE_OPTION_LOAD && (InputManager::g_curButtonsPressed & INPUT_UP) != 0
								&& ((InputManager::g_prevButtonsPressed & INPUT_UP) ^ InputManager::g_curButtonsPressed) != 0)
							{
								--selectedOption;
								AudioManager::PlayOneShotSoundGlobal(1, menuSoundPitch, menuSoundVolume, menuSoundVolume);
							}
							if (selectedOption < SAVE_OPTION_MAIN_MENU && (InputManager::g_curButtonsPressed & INPUT_DOWN) != 0
								&& ((InputManager::g_prevButtonsPressed & INPUT_DOWN) ^ InputManager::g_curButtonsPressed) != 0)
							{
								++selectedOption;
								AudioManager::PlayOneShotSoundGlobal(1, menuSoundPitch, menuSoundVolume, menuSoundVolume);
							}

							if (InputManager::g_curButtonsPressed & INPUT_JUMP)
							{
								switch (selectedOption)
								{
									case SAVE_OPTION_LOAD:
										state = SAVE_MENU_LOAD;
										selectedSlot = 0;
										inputDelay = inputRepeatDelay;
										AudioManager::PlayOneShotSoundGlobal(0, menuSoundPitch, menuSoundVolume, menuSoundVolume);
										break;
									case SAVE_OPTION_SAVE:
										state = SAVE_MENU_SAVE;
										selectedSlot = 0;
										inputDelay = inputRepeatDelay;
										AudioManager::PlayOneShotSoundGlobal(0, menuSoundPitch, menuSoundVolume, menuSoundVolume);
										break;
									default:
										AudioManager::PlayOneShotSoundGlobal(2, menuSoundPitch, menuSoundVolume, menuSoundVolume);
										state = SAVE_MENU_EXIT;
										Nu3D::Camera::SetTint(0, 0, 0, tintFadeOutRate);
										break;
								}
							}

							if (InputManager::g_curButtonsPressed & INPUT_CANCEL)
							{
								AudioManager::PlayOneShotSoundGlobal(2, menuSoundPitch, menuSoundVolume, menuSoundVolume);
								state = SAVE_MENU_EXIT;
								Nu3D::Camera::SetTint(0, 0, 0, tintFadeOutRate);
							}
						}
						break;
					}

					case SAVE_MENU_LOAD: {
						Renderer::g_parallaxCurHorizScroll = 0.0f;
						if (NGNLoader::GetTextureDataIndex(g_sectorBackdropTexTable.primary[1]))
							g_nextBackdropId = g_sectorBackdropTexTable.primary[1];
						else if (NGNLoader::GetTextureDataIndex(g_sectorBackdropTexTable.secondary[0]))
							g_nextBackdropId = g_sectorBackdropTexTable.secondary[0];
						g_hasBackdrop = 1;

						Renderer::DrawFormattedText(textCenterX, slotTitleRowTop, "select slot to load");
						int32_t slot;
						for (slot = 0; slot < slotCount; ++slot)
						{
							if (slot != selectedSlot || blinkTimer < blinkOnPoint)
							{
								if (g_saveSlotDescriptions[slot][0])
									Renderer::DrawFormattedText(
										slotXOffsets[slot] + textCenterX, slot * slotRowPitch + slotRowTop, "%s", g_saveSlotDescriptions[slot]);
								else
									Renderer::DrawFormattedText(slotXOffsets[slot] + textCenterX, slot * slotRowPitch + slotRowTop, "empty slot");
							}
						}
						Renderer::DrawFormattedText(textCenterX, slot * promptRowPitch + promptRowTop, "jump:select  cancel:menu");

						if ((InputManager::g_curButtonsPressed & INPUT_UP) != 0
							&& ((InputManager::g_prevButtonsPressed & INPUT_UP) ^ InputManager::g_curButtonsPressed) != 0 && selectedSlot != 0)
						{
							--selectedSlot;
							AudioManager::PlayOneShotSoundGlobal(1, menuSoundPitch, menuSoundVolume, menuSoundVolume);
						}
						if ((InputManager::g_curButtonsPressed & INPUT_DOWN) != 0
							&& ((InputManager::g_prevButtonsPressed & INPUT_DOWN) ^ InputManager::g_curButtonsPressed) != 0 && selectedSlot < slotCount - 1)
						{
							++selectedSlot;
							AudioManager::PlayOneShotSoundGlobal(1, menuSoundPitch, menuSoundVolume, menuSoundVolume);
						}

						if (inputDelay <= 0)
						{
							if ((InputManager::g_curButtonsPressed & INPUT_JUMP) && g_saveSlotDescriptions[selectedSlot][0])
							{
								int32_t saveNumber = selectedSlot + 1;
								char fileName[fileNameSize];
								sprintf(fileName, "Toy2%02d.sav", saveNumber);
								FILE* file = fopen(fileName, "rb");
								if (file)
								{
									int32_t descriptionLength;
									char description[descriptionSize];
									fread(&descriptionLength, 1, 4, file);
									if (descriptionLength)
									{
										fread(description, 1, descriptionLength, file);
										description[descriptionLength] = '\0';
									}
									if (saveNumber == 99)
										fread(&SaveManager::g_save99Data, 1, sizeof(SaveManager::g_save99Data), file);
									else
										fread(&SaveManager::g_save0Data, 1, sizeof(SaveManager::g_save0Data), file);
									fclose(file);
								}

								state = SAVE_MENU_EXIT;
								Nu3D::Camera::SetTint(0, 0, 0, tintFadeOutRate);
								AudioManager::PlayOneShotSoundGlobal(0, menuSoundPitch, menuSoundVolume, menuSoundVolume);
								result = 1;
							}

							if (InputManager::g_curButtonsPressed & INPUT_CANCEL)
							{
								inputDelay = inputRepeatDelay;
								state = SAVE_MENU_OPTIONS;
								AudioManager::PlayOneShotSoundGlobal(2, menuSoundPitch, menuSoundVolume, menuSoundVolume);
							}
						}
						break;
					}

					case SAVE_MENU_SAVE: {
						Renderer::g_parallaxCurHorizScroll = 0.0f;
						if (NGNLoader::GetTextureDataIndex(g_sectorBackdropTexTable.primary[1]))
							g_nextBackdropId = g_sectorBackdropTexTable.primary[1];
						else if (NGNLoader::GetTextureDataIndex(g_sectorBackdropTexTable.secondary[0]))
							g_nextBackdropId = g_sectorBackdropTexTable.secondary[0];
						g_hasBackdrop = 1;

						Renderer::DrawFormattedText(textCenterX, slotTitleRowTop, "select slot to save");
						int32_t slot;
						for (slot = 0; slot < slotCount; ++slot)
						{
							if (slot != selectedSlot || blinkTimer < blinkOnPoint)
							{
								if (g_saveSlotDescriptions[slot][0])
									Renderer::DrawFormattedText(
										slotXOffsets[slot] + textCenterX, slot * slotRowPitch + slotRowTop, "%s", g_saveSlotDescriptions[slot]);
								else
									Renderer::DrawFormattedText(slotXOffsets[slot] + textCenterX, slot * slotRowPitch + slotRowTop, "empty slot");
							}
						}
						if (postGameSave)
							Renderer::DrawFormattedText(textCenterX, slot * promptRowPitch + promptRowTop, "jump:save  cancel:continue");
						else
							Renderer::DrawFormattedText(textCenterX, slot * promptRowPitch + promptRowTop, "jump:select  cancel:menu");

						if ((InputManager::g_curButtonsPressed & INPUT_UP) != 0
							&& ((InputManager::g_prevButtonsPressed & INPUT_UP) ^ InputManager::g_curButtonsPressed) != 0 && selectedSlot != 0)
						{
							--selectedSlot;
							AudioManager::PlayOneShotSoundGlobal(1, menuSoundPitch, menuSoundVolume, menuSoundVolume);
						}
						if ((InputManager::g_curButtonsPressed & INPUT_DOWN) != 0
							&& ((InputManager::g_prevButtonsPressed & INPUT_DOWN) ^ InputManager::g_curButtonsPressed) != 0 && selectedSlot < slotCount - 1)
						{
							++selectedSlot;
							AudioManager::PlayOneShotSoundGlobal(1, menuSoundPitch, menuSoundVolume, menuSoundVolume);
						}

						if (inputDelay <= 0)
						{
							if (InputManager::g_curButtonsPressed & INPUT_JUMP)
							{
								int32_t progress = ComputeTokenProgress();
								char description[descriptionSize];
								sprintf(description, "TOK %d LEV %d", (progress >> 16) & progressFieldMask, progress & progressFieldMask);
								SaveManager::SaveToFile(selectedSlot + 1, description);
								state = SAVE_MENU_EXIT;
								Nu3D::Camera::SetTint(0, 0, 0, tintFadeOutRate);

								for (int32_t slot = 0; slot < slotCount; ++slot)
								{
									char* slotDescription = g_saveSlotDescriptions[slot];
									char fileName[fileNameSize];
									sprintf(fileName, "Toy2%02d.sav", slot + 1);
									FILE* file = fopen(fileName, "rb");
									if (file)
									{
										int32_t descriptionLength;
										fread(&descriptionLength, 1, 4, file);
										if (descriptionLength && slotDescription)
										{
											fread(slotDescription, 1, descriptionLength, file);
											slotDescription[descriptionLength] = '\0';
										}
										fclose(file);
									}
									else if (slotDescription)
									{
										slotDescription[0] = '\0';
									}
								}
								AudioManager::PlayOneShotSoundGlobal(0, menuSoundPitch, menuSoundVolume, menuSoundVolume);
							}

							if (InputManager::g_curButtonsPressed & INPUT_CANCEL)
							{
								if (postGameSave)
								{
									AudioManager::PlayOneShotSoundGlobal(2, menuSoundPitch, menuSoundVolume, menuSoundVolume);
									state = SAVE_MENU_EXIT;
									Nu3D::Camera::SetTint(0, 0, 0, tintFadeOutRate);
								}
								else
								{
									inputDelay = inputRepeatDelay;
									state = SAVE_MENU_OPTIONS;
									AudioManager::PlayOneShotSoundGlobal(2, menuSoundPitch, menuSoundVolume, menuSoundVolume);
								}
							}
						}
						break;
					}

					case SAVE_MENU_EXIT: {
						if (Nu3D::Camera::g_cameraTintRed == 0)
							finished = 1;
						break;
					}
				}

				Renderer::DrawTintOverlay();
				Renderer::Sprite::DrawQueuedSprite();
				Renderer::EndScene(1);
			}

			if (inputDelay > 0)
				inputDelay -= Renderer::g_frameDelta;
			blinkTimer -= Renderer::g_frameDelta;
			if (blinkTimer < 0)
				blinkTimer += blinkPeriod;
		}

		return result;
	}

	// FUNCTION: TOY2 0x0049EB20 [MATCHED]
	void UnlockAndPlayMovie(int32_t movieId, int32_t backgroundId, int32_t forcePlay)
	{
		if (! SaveManager::g_save0Data.moviesUnlocked[movieId] || forcePlay)
		{
			SaveManager::g_save0Data.moviesUnlocked[movieId] = 1;
			PlayMovieWithTransition(movieId + 10, backgroundId);
		}
	}

	// FUNCTION: TOY2 0x0049A930 [MATCHED]
	int32_t PlayMovie(int32_t movieId)
	{
		char moviePath[256];
		FileUtils::AppendCDPath(moviePath);
		strcat(moviePath, "rtlibs\\");

		switch (movieId)
		{
			case 1:
				strcat(moviePath, "tt");
				break;
			case 0:
				strcat(moviePath, "dlogo");
				break;
			case 2:
				strcat(moviePath, "acti");
				break;
			case 10:
				strcat(moviePath, "1st trailer");
				break;
			case 11:
				strcat(moviePath, "l 01 in");
				break;
			case 12:
				strcat(moviePath, "l 02 in");
				break;
			case 13:
				strcat(moviePath, "l 03 bo");
				break;
			case 14:
				strcat(moviePath, "l 04 in");
				break;
			case 15:
				strcat(moviePath, "l 05 in");
				break;
			case 16:
				strcat(moviePath, "l 06 bo");
				break;
			case 17:
				strcat(moviePath, "l 07 in");
				break;
			case 18:
				strcat(moviePath, "l 08 in");
				break;
			case 19:
				strcat(moviePath, "l 09 bo");
				break;
			case 20:
				strcat(moviePath, "l 10 in");
				break;
			case 21:
				strcat(moviePath, "l 11 in");
				break;
			case 22:
				strcat(moviePath, "l 12 bo");
				break;
			case 23:
				strcat(moviePath, "l 13 in");
				break;
			case 24:
				strcat(moviePath, "l 14 in");
				break;
			case 25:
				strcat(moviePath, "l 15 bo 1");
				break;
			case 26:
				strcat(moviePath, "l 12 in");
				break;
			case 27:
				strcat(moviePath, "l 15 bo 2");
				break;
			case 28:
				strcat(moviePath, "end 01");
				break;
			default:
				return 0;
		}

		strcat(moviePath, ".dll");
		AudioManager::ReleaseBuffers();
		int32_t interrupted = Nu3D_FMV_PlayMovie(moviePath);
		AudioManager::Init();
		return interrupted;
	}

	// FUNCTION: TOY2 0x0049AB90 [MATCHED]
	int32_t PlayMovieWithTransition(int32_t movieId, int32_t backgroundId)
	{
		int32_t result = 0;
		Renderer::SetVirtualRatioTo54();
		if (backgroundId != 0)
		{
			LoadLevelWithFadeIn(backgroundId, result);
		}

		int32_t samplesRemaining = 60;
		do
		{
			g_movieTimingStartMs = timeGetTime();
			int32_t elapsedMs = timeGetTime() - g_movieTimingStartMs;
			g_movieTimingRate = g_cpuClockHz / (abs(elapsedMs) + 1);

			do
			{
				elapsedMs = timeGetTime() - g_movieTimingStartMs;
				g_movieTimingRate = 1000 / (abs(elapsedMs) + 1);
			} while (g_movieTimingRate > 60);
		} while (--samplesRemaining != 0);

		if (backgroundId != 0)
		{
			ShowLevelIntroScreen(backgroundId, result);
		}

		if (! g_mpegPlaybackDisabled)
		{
			result = PlayMovie(movieId);
		}
		return result;
	}

	// FUNCTION: TOY2 0x0048F1B0 [PROVISIONAL]
	void SetBackdropByIndex(int32_t index)
	{
		index++;
		Renderer::g_parallaxCurHorizScroll = 0.0;

		if (index < 0 || index >= 9)
			return;

		if (NGNLoader::GetTextureDataIndex(g_sectorBackdropTexTable.primary[index]))
		{
			g_nextBackdropId = g_sectorBackdropTexTable.primary[index];
			g_hasBackdrop = 1;
		}
		else
		{
			if (NGNLoader::GetTextureDataIndex(g_sectorBackdropTexTable.secondary[index - 1]))
				g_nextBackdropId = g_sectorBackdropTexTable.secondary[index - 1];

			g_hasBackdrop = 1;
		}
	}

	struct ScreenTextSurfaceState
	{
		char text[256];
		int16_t screenX;
		int16_t screenY;
		int32_t status;
		int32_t drawResult;
		int32_t textLength;
		HRESULT releaseResult;
		va_list arguments;
		LPDIRECTDRAWSURFACE3 surface;
		DDSURFACEDESC surfaceDesc;
		DDCOLORKEY colorKey;
		int32_t width;
		int32_t height;
		HDC deviceContext;
		RECT sourceRect;
	};

	STATIC_ASSERT(sizeof(ScreenTextSurfaceState) == 0x1AC);
	STATIC_ASSERT(offsetof(ScreenTextSurfaceState, surface) == 0x118);
	STATIC_ASSERT(offsetof(ScreenTextSurfaceState, surfaceDesc) == 0x11C);
	STATIC_ASSERT(offsetof(ScreenTextSurfaceState, colorKey) == 0x188);
	STATIC_ASSERT(offsetof(ScreenTextSurfaceState, deviceContext) == 0x198);
	STATIC_ASSERT(offsetof(ScreenTextSurfaceState, sourceRect) == 0x19C);

	struct ScreenTextQueue
	{
		ScreenTextSurfaceState* entries[32];
		int32_t count;
		int32_t current;
	};

	STATIC_ASSERT(sizeof(ScreenTextQueue) == 0x88);
	STATIC_ASSERT(offsetof(ScreenTextQueue, count) == 0x80);
	STATIC_ASSERT(offsetof(ScreenTextQueue, current) == 0x84);

	// GLOBAL: TOY2 0x004FE75C
	int16_t g_screenTextLineY = 8;

	// GLOBAL: TOY2 0x0072E2B0
	ScreenTextQueue g_screenTextQueue;

	// FUNCTION: TOY2 0x0048E2B0 [EFFECTIVE]
	ScreenTextSurfaceState* __fastcall InitScreenTextSurface(ScreenTextSurfaceState* screenText)
	{
		screenText->status = 0;
		screenText->drawResult = 0;
		screenText->width = g_screenClipRight;
		screenText->height = 4 - g_d3dAppLogFont.lfHeight;

		memset(screenText->text, 0, sizeof(screenText->text));

		screenText->surfaceDesc.dwSize = sizeof(screenText->surfaceDesc);
		screenText->surfaceDesc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
		screenText->surfaceDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
		screenText->surfaceDesc.dwWidth = screenText->width;
		screenText->surfaceDesc.dwHeight = screenText->height;

		HRESULT result = D3DAppICreateSurface(&screenText->surfaceDesc, &screenText->surface);
		if (result < 0)
			Logger::LogDDError("D3DAppCreateSurface(&ddsd, &Surface)", result);

		DDCOLORKEY* colorKey = &screenText->colorKey;
		memset(colorKey, 0, sizeof(*colorKey));
		LPDIRECTDRAWSURFACE3 surface = screenText->surface;
		surface->SetColorKey(DDCKEY_SRCBLT, colorKey);
		++screenText->status;

		return screenText;
	}

	// FUNCTION: TOY2 0x0048E470 [MATCHED]
	void QueueScreenText(ScreenTextSurfaceState* screenText, int16_t screenX, int32_t unused, const char* format, ...)
	{
		if (screenText->status != 1)
			return;

		va_start(screenText->arguments, format);
		screenText->textLength = vsprintf(screenText->text, format, screenText->arguments);
		va_end(screenText->arguments);

		screenText->screenX = screenX;
		screenText->screenY = g_screenTextLineY;
		g_screenTextLineY += 16;

		SetRect(&screenText->sourceRect, 0, 0, screenText->width, screenText->height);
		screenText->drawResult = screenText->surface->GetDC(&screenText->deviceContext);
		if (screenText->drawResult != DD_OK)
			return;

		SelectObject(screenText->deviceContext, g_d3dAppFont);
		SetTextColor(screenText->deviceContext, 0xFFFF);
		SetBkColor(screenText->deviceContext, 0);
		SetBkMode(screenText->deviceContext, OPAQUE);
		SetRect(&screenText->sourceRect, 0, 0, screenText->width, screenText->height);
		screenText->drawResult =
			ExtTextOutA(screenText->deviceContext, 0, 0, ETO_OPAQUE, &screenText->sourceRect, screenText->text, screenText->textLength, NULL);
		screenText->releaseResult = screenText->surface->ReleaseDC(screenText->deviceContext);

		if (g_screenTextQueue.count < 32)
		{
			g_screenTextQueue.entries[g_screenTextQueue.count] = screenText;
			g_screenTextQueue.count = g_screenTextQueue.count + 1;
		}

		++screenText->status;
	}

	// FUNCTION: TOY2 0x0048E680 [MATCHED]
	void __fastcall DrawQueuedScreenText(ScreenTextQueue* queue)
	{
		queue->current = 0;
		while (queue->current < queue->count)
		{
			ScreenTextSurfaceState* screenText = queue->entries[queue->current];
			if (screenText->status == 2)
			{
				screenText->status = 1;
				g_screenTextLineY = 8;
				d3dappi.lpBackBuffer->BltFast(
					screenText->screenX, screenText->screenY, screenText->surface, &screenText->sourceRect, DDBLTFAST_SRCCOLORKEY | DDBLTFAST_WAIT);
			}

			queue->entries[queue->current] = NULL;
			++queue->current;
		}

		queue->count = 0;
	}

	// FUNCTION: TOY2 0x0048E730 [PROVISIONAL]
	void OneInit()
	{
		Logger::g_logsEnabled = 0;
		g_mpegPlaybackDisabled = 0;
		Ini::g_cheatsEnabled = 0;
		Ini::g_highQualityMpeg = 0;

		char* command = strtok(g_windowData.lpCmdLine, " -");
		while (command != NULL)
		{
			if (lstrcmpA(command, "cheat") == 0)
				Ini::g_cheatsEnabled = 1;
			if (lstrcmpA(command, "log") == 0)
				Logger::g_logsEnabled = 1;
			if (lstrcmpA(command, "mpeg") == 0)
			{
				g_mpegPlaybackDisabled = 1;
				Logger::Log("ONEINIT : Mpeg play OFF.\n");
			}
			if (lstrcmpA(command, "high") == 0)
			{
				Ini::g_highQualityMpeg = 1;
				Logger::Log("ONEINIT : Hi quality mpeg playing ON.\n");
			}
			if (lstrcmpA(command, "demo") == 0)
			{
				g_demoVersion = g_demoVersion == 0;
				Logger::Log("ONEINIT : Switched to %s version.\n", g_demoVersion == 1 ? "demo" : "full");
			}

			command = strtok(NULL, " -");
		}

		SaveManager::Init();
		Nullsub10();
		SaveManager::g_save0Data.musicVolume = 0x80;
		SaveManager::g_save0Data.soundVolume = 0xFF;
		AudioManager::SetVolumesProcessed(0x80, 0xFF);

		int32_t saveNameLength;
		char secondBuffer[256];
		char firstBuffer[256];

		sprintf(firstBuffer, "Toy2%02d.sav", 0);
		FILE* saveFile = fopen(firstBuffer, "rb");
		if (saveFile != NULL)
		{
			fread(&saveNameLength, 1, sizeof(saveNameLength), saveFile);
			if (saveNameLength != 0)
			{
				fread(secondBuffer, 1, saveNameLength, saveFile);
				secondBuffer[saveNameLength] = '\0';
			}
			fread(&SaveManager::g_save0Data, 1, sizeof(SaveManager::g_save0Data), saveFile);
			fclose(saveFile);
		}

		sprintf(secondBuffer, "Toy2%02d.sav", 99);
		saveFile = fopen(secondBuffer, "rb");
		if (saveFile != NULL)
		{
			fread(&saveNameLength, 1, sizeof(saveNameLength), saveFile);
			if (saveNameLength != 0)
			{
				fread(firstBuffer, 1, saveNameLength, saveFile);
				firstBuffer[saveNameLength] = '\0';
			}
			fread(&SaveManager::g_save99Data, 1, sizeof(SaveManager::g_save99Data), saveFile);
			fclose(saveFile);
		}

		SaveManager::InitProgressData(&SaveManager::g_save0Data);
		SaveManager::LoadProgressData(&SaveManager::g_save0Data);
		g_levelIndex = 0;
		g_randDatBufferPtr = g_randDatBuffer;
		ProfileCPU();
		Nullsub11();
		ReadIniFile();
	}

	// FUNCTION: TOY2 0x00490730 [MATCHED]
	void CheckForQuit()
	{
		if (g_windowData.wndIsExiting != 0)
		{
			Logger::Log("CheckForQuit : Starting shutdown now...\n");
			DestroyWindow(g_windowData.mainHwnd);

			switch (g_renderMode)
			{
				case RENDERMODE_SOFTWARE:
					SoftwareRenderer::Destroy();
					break;
				case RENDERMODE_D3D:
					Logger::Log("QUIT : Destroying Direct3D renderer.\n");
					break;
			}

			CleanupManagers();
			PostQuitMessage();
			CoUninitialize();

			g_windowData.mainHwnd = 0;

			Logger::Log("CheckForQuit : Code shutdown.\n");

			g_clearScreenSaveResult = SystemParametersInfoA(SPI_SCREENSAVERRUNNING, 0, &g_setScreenSaveRunning, 0);

			if (g_clearScreenSaveResult == 0)
			{
				Logger::Log("Failed to clear SCREENSAVERRUNNING\n");
			}
			else
			{
				Logger::Log("Managed to clear SCREENSAVERRUNNING\n");
			}

			exit(g_windowData.wndEventMsg.wParam);
		}
	}

	// FUNCTION: TOY2 0x0047D8D0 [MATCHED]
	void UnusedInit()
	{
		// GLOBAL: TOY2 0x00725F20
		static int32_t g_unusedInit;

		g_unusedInit = 2;
	}

	// FUNCTION: TOY2 0x00412D70 [PROVISIONAL]
	int32_t ShowModeSelect()
	{
		char cdFileName[8];
		char cdTrackBuffer[1024];

		strcpy(cdFileName, "cd.txt");
		memset(cdTrackBuffer, 0, sizeof(cdTrackBuffer));

		int32_t baseCDTrack;

		if (FileUtils::LoadFile(cdFileName, cdTrackBuffer))
			baseCDTrack = atoi(cdTrackBuffer);
		else
			baseCDTrack = 2;

		g_cdBaseTrack = baseCDTrack;
		Logger::Log("CONFIG : Base CD track is %d.\n", baseCDTrack);

		AudioManager::Init();
		UnusedInit();
		Numerics::InitTrigLut();

		RunModeSelect();

		InputManager::Init();
		Renderer::Init();
		Nu3D::Viewport::Init();

		RECT* destRect = DrawingDevice::GetDestRect();

		Nu3D::Font::Init();
		Nu3D::Font* font = Nu3D::Font::Build("ariel", 20, 0);

		if (font)
		{
			Nu3D::Font::SetFont(font);
			Nu3D::Font::SetTextColor(0xFFFFFFFF);
			Nu3D::Font::SetTextClipRect(destRect->left, destRect->top, destRect->right - 1, destRect->bottom - 1);
		}

		while (ShowCursor(FALSE) >= 0) {};

		return 1;
	}

	// FUNCTION: TOY2 0x00412E80 [MATCHED]
	int32_t CleanupManagers()
	{
		InputManager::Cleanup();
		AudioManager::StopAndFlush();
		AudioManager::ReleaseBuffers();
		Renderer::Cleanup();
		DrawingDevice::Quit();
		return 1;
	}

	// FUNCTION: TOY2 0x0047D7C0 [MATCHED]
	void UpdateAudioChannels() { AudioManager::UpdateChannels(); }

	// FUNCTION: TOY2 0x00490BF0 [EFFECTIVE]
	int16_t UpdateD3DState()
	{
		g_unusedD3DFrameFlag = 0;

		RECT* destRect = DrawingDevice::GetDestRect();
		int32_t width = destRect->right - destRect->left;
		g_destRectWidth = width;
		int32_t height = destRect->bottom - destRect->top;
		int32_t halfWidth = width / 2;
		int32_t halfHeight = height / 2;
		g_softWindowWidth = width;
		g_screenClipRight = width - 1;
		g_screenClipBottom = height - 1;
		g_destRectHeight = height;
		g_destRectWidthScaled = (width * 256) / 320;
		g_screenClipLeft = 0;
		g_screenClipTop = 0;
		g_destRectHalfWidth = halfWidth;
		g_screenClipRightFixed = width * 1024 - 1;
		g_softWindowHalfWidth = halfWidth;
		g_destRectHalfHeight = halfHeight;
		g_softWindowHeight = height;
		g_screenClipLeftFixed = 0;

		SoftwareRenderer::g_softwareRenderBuckets = SoftwareRenderer::g_softwareRenderBucketStorage;
		SoftwareRenderer::g_displayMaxX = 0x3ff;

		drawb->VerticePoolCount = 0;

		g_unusedD3DFrameStartTime = timeGetTime();

		return 1;
	}

	// FUNCTION: TOY2 0x004909E0 [PROVISIONAL]
	void ProcessMiscEventsEx()
	{
		Nu3D::Font::SetTextCursor(0, (int32_t)Nu3D::g_scaledFontAscent);
		DevDraw::g_vertexCount = 0;

		g_windowData.wndIsExiting = g_wndIsExitingUnused;

		if (g_wndIsExitingUnused)
		{
			Logger::Log("CheckForQuit : Starting shutdown now...\n");

			DestroyWindow(g_windowData.mainHwnd);

			if (g_renderMode == RENDERMODE_SOFTWARE)
			{
				SoftwareRenderer::Destroy();
			}
			else if (g_renderMode == RENDERMODE_D3D)
			{
				Logger::Log("QUIT : Destroying Direct3D renderer.\n");
			}

			CleanupManagers();
			PostQuitMessage();

			CoUninitialize();

			g_windowData.mainHwnd = 0;

			Logger::Log("CheckForQuit : Code shutdown.\n");

			g_clearScreenSaveResult = SystemParametersInfoA(SPI_SCREENSAVERRUNNING, 0, &g_setScreenSaveRunning, 0);

			if (g_clearScreenSaveResult)
				Logger::Log("Managed to clear SCREENSAVERRUNNING\n");
			else
				Logger::Log("Failed to clear SCREENSAVERRUNNING\n");

			exit(g_windowData.wndEventMsg.wParam);
		}

		ProcessWndEvents();

		if (g_windowData.wndIsExiting)
		{
			DrawingDevice::g_drawingDevice->RestoreToGDISurface(1);
			Logger::GetErrorHandler("C:\\projects\\toy2\\toy2.cpp", 4670)("");
		}

		PROCESS_DEBUG_VIEW_KEYS();

		InputManager::UpdateButtonStates();

		if (g_inputSuppressFrames)
		{
			InputManager::g_curButtonsPressed = 0;
			--g_inputSuppressFrames;
		}

		UpdateAudioChannels();

		SoftwareRenderer::g_backBufferClearComplete = 0;

		if (g_renderMode == RENDERMODE_SOFTWARE)
		{
			SoftwareRenderer::RenderBackdropColourBands();
			SoftwareRenderer::UpdateBackdropScroll();
		}
		else if (g_renderMode == RENDERMODE_D3D)
		{
			UpdateD3DState();
		}
	}

	// FUNCTION: TOY2 0x00498550 [MATCHED]
	void ProcessMiscEvents()
	{
		ProcessWndEvents();
		if (g_windowData.wndIsExiting)
		{
			DrawingDevice::RestoreToGDISurface(1);
			Logger::GetErrorHandler("C:\\projects\\toy2\\toy2.cpp", 0x123E)(&SaveManager::g_emptyString);
		}

		PROCESS_DEBUG_VIEW_KEYS();

		InputManager::UpdateButtonStates();
		if (g_inputSuppressFrames != 0)
		{
			--g_inputSuppressFrames;
			InputManager::g_curButtonsPressed = 0;
		}
		UpdateAudioChannels();
		SoftwareRenderer::g_backBufferClearComplete = 0;
	}

#undef PROCESS_DEBUG_VIEW_KEYS

	// FUNCTION: TOY2 0x00498B80 [PROVISIONAL]
	void CopyTextureSlot(int32_t sourceIndex, int32_t destinationIndex)
	{
		sourceIndex += 12;
		destinationIndex += 6;

		void** sourceTexture = &g_textureData[sourceIndex];
		if (*sourceTexture == NULL)
			return;

		int32_t* destinationType = &d3dappi.TextureType[destinationIndex];
		if (g_textureDimensions[destinationIndex].width != 256)
		{
			*destinationType = D3DTEXTURE_STATUS_DESTROY;
			D3DAppIReleaseAllTextures();
		}

		d3dappi.TextureStatus[destinationIndex] = d3dappi.TextureStatus[sourceIndex];
		g_textureDimensions[destinationIndex].width = g_textureDimensions[sourceIndex].width;
		g_textureData[destinationIndex] = *sourceTexture;
		g_textureDimensions[destinationIndex].height = g_textureDimensions[sourceIndex].height;
		SoftwareRenderer::g_softwareTextureData[destinationIndex] = *sourceTexture;
		*destinationType = D3DTEXTURE_STATUS_LOADED;
		D3DAppIReleaseAllTextures();
	}

	// FUNCTION: TOY2 0x004998A0 [MATCHED]
	void LogTextureStatuses()
	{
		int32_t textureIndex = 0;
		int32_t* textureType = d3dappi.TextureType;
		int32_t textureCount = 64;
		do
		{
			Logger::Log("INFO Texture page %d - ", textureIndex);
			switch (*textureType)
			{
				case D3DTEXTURE_STATUS_NULL:
					Logger::Log("Status is D3DTEXTURE_STATUS_NULL");
					break;
				case D3DTEXTURE_STATUS_LOADED:
					Logger::Log("Status is D3DTEXTURE_STATUS_LOADED");
					break;
				case D3DTEXTURE_STATUS_GETHANDLE:
					Logger::Log("Status is D3DTEXTURE_STATUS_GETHANDLE");
					break;
				case D3DTEXTURE_STATUS_READY:
					Logger::Log("Status is D3DTEXTURE_STATUS_READY");
					break;
				case D3DTEXTURE_STATUS_RELOAD:
					Logger::Log("Status is D3DTEXTURE_STATUS_RELOAD");
					break;
				case D3DTEXTURE_STATUS_DESTROY:
					Logger::Log("Status is D3DTEXTURE_STATUS_DESTROY");
					break;
				case D3DTEXTURE_STATUS_DESTROYED:
					Logger::Log("Status is D3DTEXTURE_STATUS_DESTROYED");
					break;
				case D3DTEXTURE_STATUS_FAILEDLOAD:
					Logger::Log("Status is D3DTEXTURE_STATUS_FAILEDLOAD");
					break;
			}
			Logger::Log("\n");
			textureIndex++;
			textureType++;
		} while (--textureCount != 0);
	}

	// FUNCTION: TOY2 0x00499950 [PROVISIONAL]
	void InitDirect3DRenderer()
	{
		Logger::Log("INIT : Starting Direct3D renderer.\n");

		RECT* destRect = DrawingDevice::GetDestRect();
		g_destRectWidth = destRect->right - destRect->left;
		g_destRectHeight = destRect->bottom - destRect->top;

		if (g_destRectHalfWidth != g_perspectiveTableHalfWidth)
		{
			g_perspectiveTableHalfWidth = g_destRectHalfWidth;
			BuildPerspectiveDivideTable(g_destRectHalfWidth);
		}

		g_softWindowWidth = g_destRectWidth;
		g_screenClipRight = g_destRectWidth - 1;
		g_screenClipBottom = g_destRectHeight - 1;
		g_destRectWidthScaled = (g_destRectWidth << 8) / 320;
		g_screenClipLeft = 0;
		g_destRectHalfWidth = g_destRectWidth / 2;
		g_destRectHalfHeight = g_destRectHeight / 2;
		g_screenClipRightFixed = (g_destRectWidth << 10) - 1;
		g_softWindowHalfWidth = g_destRectHalfWidth;
		g_softWindowHeight = g_destRectHeight;

		memset(g_unusedD3DRendererBuffer, 0, sizeof(g_unusedD3DRendererBuffer));
		g_screenClipTop = 0;
		g_screenClipLeftFixed = 0;
		g_unusedD3DRendererValue = 0;
	}

	// FUNCTION: TOY2 0x00499EB0 [MATCHED]
	void MarkTextureForDestroy(uint32_t textureSlot)
	{
		textureSlot &= 0xffff;
		int32_t* textureType = &d3dappi.TextureType[textureSlot];
		if (*textureType != D3DTEXTURE_STATUS_NULL)
		{
			*textureType = D3DTEXTURE_STATUS_DESTROY;
			D3DAppIReleaseAllTextures();
		}
	}

	namespace Portal
	{}
}
