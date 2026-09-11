#include "Toy2/Toy2.h"
#include "Toy2/MoveableObjectInternal.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Actor.h"
#include "Toy2/Buzz.h"
#include "Toy2/Weather.h"
#include "Toy2/Camera.h"
#include "Toy2/Collision.h"
#include "Toy2/Collectables.h"
#include "Toy2/TokenDialogue.h"
#include "Toy2/KiteTail.h"
#include "Toy2/Levels.h"
#include "Toy2/Particles.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Math.h"
#include "Nu3D/Link.h"
#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
#include "Random.h"
#include "Numerics.h"

#include <limits.h>
#include <stdlib.h>

namespace Toy2
{
	extern uint8_t g_environmentTintBlue;
	extern uint8_t g_environmentTintGreen;
	extern uint8_t g_environmentTintRed;
	extern int32_t g_hudActorAnimationFrame;

	namespace Camera
	{
		extern int32_t g_cameraSmoothingDivisor;
	}

	namespace Game
	{
		void InitActor(Actor::Toy2Actor* actor, int32_t fullInit);
	}

	namespace AndysNeighborhood
	{
		// The level props that react to a ground slam, as reported by g_footingType.
		enum StompFooting
		{
			FOOTING_NONE = 0,
			FOOTING_FIRST_TREE = 8,
			FOOTING_SECOND_TREE = 9,
			FOOTING_FALLEN_TREE = 10,
			FOOTING_AIR_PUMP = 11,
		};

		// The mole soldier keeps away from Buzz until enough molehills are destroyed.
		const int16_t ACTOR_FLAG_HIDES_FROM_BUZZ = 0x20;
		const int32_t POOL_SURFACE_Y = 0x4400;
		const int32_t POND_SURFACE_Y = 0x1800;
		const int32_t TROOPS_TO_FIND = 5;
		const int32_t RACE_LAP_COUNT = 3;
		const int32_t RACE_TOKEN_INDEX = 2;
		const int32_t MOLE_HIDDEN_Y = 0x5000;
		const int32_t MOLE_HIDE_TIME = 100;
		const int32_t MOLE_BURST_TIME = 0x50;
		const int32_t MOLEHILL_DUST_Y = -0xBB0;
		const int32_t MOLEHILL_BURST_Y = -0x23B0;
		const int32_t MOLEHILLS_TO_DESTROY = 5;
		const int32_t DUCK_FULL_SCALE = 0x1000;
		const int32_t DUCK_EMPTY_SCALE = 0x400;
		const int32_t TREE_FULL_SCALE = 0x1000;

		enum KiteEncounterState
		{
			KITE_ENCOUNTER_IDLE = 0,
			KITE_ENCOUNTER_INTRO = 1,
			KITE_ENCOUNTER_ACTIVE = 2,
			KITE_ENCOUNTER_DEFEATED = 3,
			KITE_ENCOUNTER_TAIL_DRIFT_END = 120,
			KITE_ENCOUNTER_REWARDED = 200,
		};

		// GLOBAL: TOY2 0x004DFA68
		uint16_t g_zurgKiteMovementScript[] = {
			0x0016,
			0xFFE0,
			0xFFE0,
			0x000C,
			0xFFF3,
			0x0000,
			0x001E,
			0x0004,
			0x000D,
			0x0000,
			0x0002,
			0x0004,
			0xFFFF,
			0x0001,
			0x001E,
			0x0007,
			0x000C,
			0xFFF3,
			0x000C,
			0x0001,
			0x0300,
			0x0004,
			0x001E,
			0x0004,
			0x000C,
			0xFFF7,
			0x0000,
			0x0001,
			0x0100,
			0x0004,
			0xFFFF,
			0x0010,
		};
		// GLOBAL: TOY2 0x004E02F8
		uint16_t* g_zurgKiteMovementData = g_zurgKiteMovementScript;

		// GLOBAL: TOY2 0x004F0FC0
		char g_troopsQuestDialogue[] = {
#include "AndysNeighborhoodTroopsQuestDialogue.inc"
		};
		// GLOBAL: TOY2 0x004F1078
		char g_troopsRewardDialogue[] = "great job locating the ^troops^! here is your pizza planet ^token^.";
		// GLOBAL: TOY2 0x004F10BC
		char g_zurgKiteChallengeDialogue[] = "ha ha ha ha ... defeat the ^zurg kite^ to get a ^token^!";
		// GLOBAL: TOY2 0x004F10F8
		char g_raceChallengeDialogue[] = {
#include "AndysNeighborhoodRaceChallengeDialogue.inc"
		};
		// GLOBAL: TOY2 0x004F11A0
		char g_hammHintDialogue[] = "^hamm^ is on the first branch of the ^tree^. climb the ^swing set^ to get to him.";
		// GLOBAL: TOY2 0x004F11F4
		char g_sargeHintDialogue[] = "^sarge^ has lost his troops. he is on the steps of andy's house. the troops are using ^flares^ to help you find them. ";
		// GLOBAL: TOY2 0x004F126C
		char g_rcCarHintDialogue[] = {
#include "AndysNeighborhoodRcCarHintDialogue.inc"
		};
		// GLOBAL: TOY2 0x004F1358
		char g_rubberDuckHintDialogue[] = "you can inflate the ^rubber duck^ to get to the token over the ^swimming pool^.";
		// GLOBAL: TOY2 0x004F13A8
		char g_kiteBossHintDialogue[] = {
#include "AndysNeighborhoodKiteBossHintDialogue.inc"
		};

		// GLOBAL: TOY2 0x004F1460
		char g_molehillChallengeInstructions[] = "the soldier will surrender if you quickly ^stomp^ on the ^molehills^ that he is near.";

		// GLOBAL: TOY2 0x004F1508
		int16_t g_raceCheckpointQuadrants[] = { 0, 1, 3, 2, 3, 1, 0, 2 };

		// GLOBAL: TOY2 0x004F1518
		MoveableObjectInitTable g_moveableObjectInitTable = {
			{
				{ -2, 2, 4 },
				{ -2, 11, 20 },
			},
			-1,
		};
		// GLOBAL: TOY2 0x004F1528
		char* g_rotatingHintDialogues[] = {
			g_hammHintDialogue,
			g_sargeHintDialogue,
			g_rcCarHintDialogue,
			g_rubberDuckHintDialogue,
			g_kiteBossHintDialogue,
		};
		// GLOBAL: TOY2 0x004F153C
		extern const Collectables::TokenDialogueRecord g_tokenDialogueValues = { 0x46, 0xC, g_molehillChallengeInstructions, 0x800 };
		// GLOBAL: TOY2 0x004F154C
		int16_t g_tokenLinkIds[] = { 0x33, 0x31, 0x32, 0x30, 0x34, 0 };

		// GLOBAL: TOY2 0x0052F5D8
		int32_t g_kiteTintTimer;
		// GLOBAL: TOY2 0x0052F5DC
		int32_t g_moleCurrentHoleIndex;
		// GLOBAL: TOY2 0x0052F5E0
		int32_t g_rcCarRaceStarted;
		// GLOBAL: TOY2 0x0052F5E4
		int32_t g_gateRotationVelocity;
		// GLOBAL: TOY2 0x0052F5E8
		int32_t g_moleTargetHoleIndex;
		// GLOBAL: TOY2 0x0052F5EC
		int32_t g_garageDoorRotationAngle;
		// GLOBAL: TOY2 0x0052F5F0
		int32_t g_gateRotationAngle;
		// GLOBAL: TOY2 0x0052F5F8
		Vector3I g_molehillBurstPosition;
		// GLOBAL: TOY2 0x0052F608
		int32_t g_poleScaleY;
		// GLOBAL: TOY2 0x0052F60C
		int32_t g_launchPadIsRising;
		// GLOBAL: TOY2 0x0052F610
		int32_t g_fallingTreesState;
		// GLOBAL: TOY2 0x0052F614
		int32_t g_fallingTreePitch;
		// GLOBAL: TOY2 0x0052F618
		int32_t g_fallingTreeRoll;
		// GLOBAL: TOY2 0x0052F61C
		int32_t g_moleHitTimer;
		// GLOBAL: TOY2 0x0052F620
		int32_t g_rcCarPathLap;
		// GLOBAL: TOY2 0x0052F628
		Vector3I g_kiteTailAnchor;
		// GLOBAL: TOY2 0x0052F638
		int32_t g_unusedState0;
		// GLOBAL: TOY2 0x0052F63C
		int32_t g_kiteTintToggle;
		// GLOBAL: TOY2 0x0052F640
		int32_t g_garageDoorRotationVelocity;
		// GLOBAL: TOY2 0x0052F648
		Vector3I g_launchPadPosition;
		// GLOBAL: TOY2 0x0052F658
		int32_t g_launchPadSoundTimer;
		// GLOBAL: TOY2 0x0052F65C
		int32_t g_kiteSpinAngle;
		// GLOBAL: TOY2 0x0052F660
		int32_t g_unusedRaceState;
		// GLOBAL: TOY2 0x0052F664
		int32_t g_kiteEncounterState;
		// GLOBAL: TOY2 0x0052F668
		int32_t g_rcCarPathPoint;
		// GLOBAL: TOY2 0x0052F66C
		int32_t g_fallingTreeShakeTimer;
		// GLOBAL: TOY2 0x0052F674
		int32_t g_targetLaunchPadDepression;
		// GLOBAL: TOY2 0x0052F678
		int32_t g_ambientParticlePending;
		// GLOBAL: TOY2 0x0052F680
		Vector3I g_polePosition;
		// GLOBAL: TOY2 0x0052F690
		int32_t g_launchPadDepression;
		// GLOBAL: TOY2 0x0052F6A8
		int32_t g_poleRiseSpeed;
		// GLOBAL: TOY2 0x0052F6AC
		int32_t g_previousKitePhase;
		// GLOBAL: TOY2 0x0052F6B0
		int32_t g_raceCheckpointIndex;
		// GLOBAL: TOY2 0x0052F6B4
		int32_t g_launchPadScaleY;
		// GLOBAL: TOY2 0x0052F6B8
		int32_t g_kiteBobAngle;
		// GLOBAL: TOY2 0x0052F6BC
		int32_t g_launchPadVelocityX;
		// GLOBAL: TOY2 0x0052F6C0
		int32_t g_launchPadVelocityY;
		// GLOBAL: TOY2 0x0052F6C4
		int32_t g_kiteHudPulseAngle;
		// GLOBAL: TOY2 0x0052F6C8
		int32_t g_firstTreeScaleY;
		// GLOBAL: TOY2 0x0052F6CC
		int32_t g_secondTreeScaleY;
		// GLOBAL: TOY2 0x0052F6D0
		int32_t g_destroyedMolehillCount;
		// GLOBAL: TOY2 0x0052F6D4
		int32_t g_molehillParticlePathPoint;
		// GLOBAL: TOY2 0x0052F6D8
		int32_t g_treeSwayAngle;
		// GLOBAL: TOY2 0x0052F6DC
		int32_t g_platform0TiltVelocity;
		// GLOBAL: TOY2 0x0052F6E0
		int32_t g_platform1TiltVelocity;
		// GLOBAL: TOY2 0x0052F6E4
		int32_t g_ambientParticlePathPoint;
		// GLOBAL: TOY2 0x0052F6E8
		int32_t g_unusedState1;
		// GLOBAL: TOY2 0x0052F6EC
		int32_t g_kiteRollAngle;

		void UpdatePickupGroundHeights();

		// FUNCTION: TOY2 0x00418E50 [EFFECTIVE]
		void Init()
		{
			Collectables::Init(g_tokenLinkIds, 0x41);
			Collectables::Activate(3, 1);
			MoveableObject::InitTable(g_moveableObjectInitTable.entries);
			Collectables::LoadTokenTable(reinterpret_cast<const Collectables::TokenDialogueValue*>(&g_tokenDialogueValues));

			g_environmentSurfaceY = 0x4400;
			g_previousBuzzEnvironmentY = 0x4400;
			g_environmentTintRed = 0x50;
			g_environmentTintGreen = 0x60;
			g_environmentTintBlue = 0x80;
			Platform::SetRotationAngles(0, 0, 0x961, 0);
			Platform::SetRotationAngles(1, 0, 0xB4A, 0);

			g_previousKitePhase = Actor::g_creatureActors[26].actorPhase;
			Actor::g_creatureActors[26].actorFlags |= Actor::ACTOR_FLAG_BOSS;
			Actor::g_creatureActors[4].previousActorPhase = 100;
			g_kiteBobAngle = 0;
			g_kiteRollAngle = 0;
			g_kiteSpinAngle = 0;
			g_kiteEncounterState = 0;
			g_kiteTintTimer = 0;
			g_kiteTintToggle = 0;
			g_platform0TiltVelocity = 0;
			g_platform1TiltVelocity = 0;
			HUD::g_challengeState = 0;
			g_rcCarPathPoint = 0;
			g_rcCarPathLap = 0;
			AndysHouse::g_raceCheckpointPassCount = 0;
			g_unusedRaceState = 0;
			g_rcCarRaceStarted = 0;
			g_raceCheckpointIndex = 0;
			g_ambientParticlePathPoint = 0;
			g_ambientParticlePending = 1;
			g_launchPadScaleY = 0xFFF;
			g_targetLaunchPadDepression = 0;
			g_launchPadDepression = 0;
			g_launchPadIsRising = 0;
			g_firstTreeScaleY = 0x1000;
			g_secondTreeScaleY = 0x1000;
			g_fallingTreeRoll = 0;
			g_fallingTreePitch = 0;
			g_treeSwayAngle = 0xC00;
			g_fallingTreeShakeTimer = 0;
			g_fallingTreesState = 0;
			g_gateRotationAngle = 0x2EE;
			g_gateRotationVelocity = 0;
			g_garageDoorRotationAngle = 0;
			g_garageDoorRotationVelocity = 0;
			g_moleCurrentHoleIndex = 0;
			g_molehillParticlePathPoint = 0;
			g_moleHitTimer = 0;
			g_moleTargetHoleIndex = 0;
			g_destroyedMolehillCount = 0;
			g_unusedState0 = 0;
			g_launchPadSoundTimer = 0;
			g_unusedState1 = 0;

			Nu3D::Link::SetRotationRelative8bit(5, 0, 0, 0x2EE);
			Nu3D::Link::GetCurrentPosFixed(6, &g_launchPadPosition);
			Platform::DisableCollision(5);
			Platform::DisableCollision(4);
			Platform::DisableCollision(12);
			Platform::AddFlags(7, 0x100);
			Platform::SetOrigin(7, g_launchPadPosition.x, g_launchPadPosition.y, g_launchPadPosition.z);
			Nu3D::Link::SetScaleFromFixedOffsets(21, 0, 0, 0);
			Nu3D::Link::SetScaleFromFixedOffsets(32, 0, 0, 0);
			Nu3D::Link::SetScaleFromFixedOffsets(33, 0, 0, 0);
			Nu3D::Link::GetCurrentPosFixed(25, &g_polePosition);
			g_poleScaleY = 0x118;
			Nu3D::Link::SetScaleFromFixedOffsets(27, 0x1000, g_poleScaleY, 0x1000);
			Levels::g_recordData[61]->data[0].y = g_polePosition.y;
			g_poleRiseSpeed = 0;
			g_kiteHudPulseAngle = 0;
			KiteTail::Init(&Actor::g_creatureActors[26].pos, 0x10, 0x1000, 0, 0x60);
		}

		// FUNCTION: TOY2 0x004190C0 [PROVISIONAL]
		void Interactions()
		{
			// The two tilting platforms over the pond.
			g_platform0TiltVelocity = Platform::StepTiltPhysics(0, 0, g_platform0TiltVelocity, 0, -0x1C0, 0x1C0, 8);
			g_platform1TiltVelocity = Platform::StepTiltPhysics(1, 1, g_platform1TiltVelocity, 0, -0x1C0, 0xE0, 8);

			// A ground slam only counts while Buzz stands on one of the props that react to it.
			int32_t stompFooting;
			if (g_buzzActor.collisionFlags != 0 && g_groundSlamTimer != 0 && g_footingType >= FOOTING_FIRST_TREE && g_footingType <= FOOTING_AIR_PUMP)
				stompFooting = g_footingType;
			else
				stompFooting = FOOTING_NONE;

			// The swimming pool is higher than the pond.
			if (g_buzzActor.posAngles.pos.x < -0x246FF)
			{
				g_environmentSurfaceY = POOL_SURFACE_Y;
				g_environmentEffectType = 1;
			}
			else
			{
				g_environmentSurfaceY = POND_SURFACE_Y;
				g_environmentEffectType = 2;
			}

			Actor::CollectQuestReward(0xF, 8, 0xC00, 0x400, 0);
			Actor::RotatingHint(0x1B, 0xB, g_rotatingHintDialogues);
			Actor::PlayPeriodicHintSound(6, 0xB2);

			// Sarge asks Buzz to find his lost troops.
			Actor::Toy2Actor* sarge = &Actor::g_creatureActors[6];
			if ((sarge->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0)
			{
				sarge->actorFlags &= ~Actor::ACTOR_FLAG_INTERACTION_REQUESTED;
				if (g_levelObjectiveProgress >= 0)
				{
					if (g_levelObjectiveProgress == TROOPS_TO_FIND)
					{
						AudioManager::Preset::PlayOneShotSound2(0xB4, sarge);
						Dialogue::Begin(6, 7, g_troopsRewardDialogue, 0x700, 0x200, 1);
						g_levelObjectiveProgress = -1;
					}
					else
					{
						AudioManager::Preset::PlayOneShotSound2(0xB3, sarge);
						Dialogue::Begin(6, 7, g_troopsQuestDialogue, 0x700, 0x200, -1);
					}
				}
			}

			// R.C. car challenges Buzz to a race around the garden.
			Actor::Toy2Actor* rcCar = &Actor::g_creatureActors[0x1D];
			if ((rcCar->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0)
			{
				rcCar->actorFlags &= ~Actor::ACTOR_FLAG_INTERACTION_REQUESTED;
				if (Collectables::g_tokenStates[RACE_TOKEN_INDEX].active != 2 && HUD::g_challengeState == HUD::CHALLENGE_STATE_INACTIVE)
				{
					Dialogue::Begin(0x1D, 5, g_raceChallengeDialogue, 0, 0, -1);
					HUD::g_challengeState = HUD::CHALLENGE_STATE_WAITING_FOR_CAMERA;
					g_raceCheckpointIndex = 0;
					g_rcCarPathPoint = 0;
					g_rcCarPathLap = 0;
					AndysHouse::g_raceCheckpointPassCount = 0;
					g_unusedRaceState = 0;
					g_rcCarRaceStarted = 1;
					rcCar->actorPhase = 0xCA;
				}
			}
			if (HUD::g_challengeState == HUD::CHALLENGE_STATE_WAITING_FOR_CAMERA && Nu3D::Camera::g_viewHistoryInitialized == 0)
			{
				rcCar->actorFlags |= Actor::ACTOR_FLAG_TRACKS_TARGET;
				HUD::g_challengeState = HUD::CHALLENGE_STATE_ACTIVE;
			}
			if (HUD::g_challengeState >= HUD::CHALLENGE_STATE_ACTIVE)
			{
				if (HUD::g_challengeState == HUD::CHALLENGE_STATE_ACTIVE)
				{
					// Buzz must pass the quadrants of the garden in the recorded order.
					int32_t buzzX = g_buzzActor.posAngles.pos.x;
					int32_t buzzZ = g_buzzActor.posAngles.pos.z;
					if (g_raceCheckpointIndex < 4)
					{
						int32_t quadrant = 0;
						if (buzzX > 0x25342)
							quadrant = 1;
						if (buzzZ > 0x3CC17)
							quadrant += 2;
						if (quadrant == g_raceCheckpointQuadrants[g_raceCheckpointIndex])
							g_raceCheckpointIndex++;
					}
					else
					{
						if (g_raceCheckpointIndex < 8)
						{
							int32_t quadrant = 0;
							if (buzzX > 0xB9C2)
								quadrant = 1;
							if (buzzZ > -0x42069)
								quadrant += 2;
							if (quadrant == g_raceCheckpointQuadrants[g_raceCheckpointIndex])
								g_raceCheckpointIndex++;
						}
						if (g_raceCheckpointIndex == 8 && buzzZ > -0x24269)
						{
							g_raceCheckpointIndex = 0;
							AndysHouse::g_raceCheckpointPassCount++;
						}
					}
				}

				// Throw up dirt when the car lands.
				if (rcCar->pos.y > 0x2000)
				{
					rcCar->pos.y = 0x2000;
					int32_t dirtOffset = (*g_randDatBufferPtr - 0x80) * 0x20;
					g_randDatBufferPtr += 2;
					Nu3D::Particles::SpawnFromPreset(rcCar->pos.x + dirtOffset, 0x2000, rcCar->pos.z + dirtOffset, 0x35, 4);
				}

				// Steer the car towards the next point of its race path.
				Levels::RecordData* racePath = Levels::g_recordData[1];
				int32_t deltaX = racePath->data[g_rcCarPathPoint].x - (rcCar->pos.x >> 5);
				int32_t deltaZ = racePath->data[g_rcCarPathPoint].z - (rcCar->pos.z >> 5);
				if (deltaX * deltaX + deltaZ * deltaZ < 360000)
				{
					rcCar->boundary.x = racePath->data[g_rcCarPathPoint].x << 5;
					rcCar->boundary.z = racePath->data[g_rcCarPathPoint].z << 5;
					if (g_rcCarPathLap < RACE_LAP_COUNT)
					{
						g_rcCarPathPoint++;
						if (g_rcCarPathPoint > 0x40)
						{
							g_rcCarPathLap++;
							g_rcCarPathPoint = 0;
						}
					}
					else if (rcCar->pos.z > -0x24269)
					{
						if (HUD::g_challengeState == HUD::CHALLENGE_STATE_ACTIVE && AndysHouse::g_raceCheckpointPassCount < RACE_LAP_COUNT)
							HUD::g_challengeState = HUD::CHALLENGE_STATE_COMPLETE;
						rcCar->actorPhase = 1;
					}
				}
				rcCar->motionTargetPos.x = racePath->data[g_rcCarPathPoint].x << 5;
				rcCar->motionTargetPos.z = racePath->data[g_rcCarPathPoint].z << 5;
				if (AndysHouse::g_raceCheckpointPassCount == RACE_LAP_COUNT)
				{
					Collectables::Activate(RACE_TOKEN_INDEX, 0);
					HUD::g_challengeState = HUD::CHALLENGE_STATE_COMPLETE;
				}
			}

			// Put the car back where it started when Buzz leaves the garden.
			if ((g_buzzActor.posAngles.pos.x < -0x27000 || (g_buzzActor.posAngles.pos.x > 0x279B1 && g_buzzActor.posAngles.pos.z < 0)
					|| g_buzzActor.posAngles.pos.y < -0x182D5)
				&& HUD::g_challengeState != HUD::CHALLENGE_STATE_INACTIVE && (rcCar->actorFlags & Actor::ACTOR_FLAG_TARGETABLE) == 0
				&& Collectables::g_tokenStates[RACE_TOKEN_INDEX].active == 0)
			{
				HUD::g_challengeState = HUD::CHALLENGE_STATE_INACTIVE;
				Game::InitActor(rcCar, 0);
			}

			// The mole soldier drops into his molehill when Buzz gets close to it.
			Actor::Toy2Actor* mole = &Actor::g_creatureActors[4];
			if ((mole->actorFlags & Actor::ACTOR_FLAG_ACTIVE) != 0 && (mole->actorFlags & ACTOR_FLAG_HIDES_FROM_BUZZ) != 0)
			{
				int32_t deltaY = (g_buzzActor.posAngles.pos.y - mole->pos.y) >> 5;
				int32_t deltaZ = (g_buzzActor.posAngles.pos.z - mole->pos.z) >> 5;
				int32_t deltaX = (g_buzzActor.posAngles.pos.x - mole->pos.x) >> 5;
				if (deltaY * deltaY + deltaZ * deltaZ + deltaX * deltaX < 0x90000 && mole->motionTargetPos.y != MOLE_HIDDEN_Y)
				{
					mole->motionTargetPos.y = MOLE_HIDDEN_Y;
					g_moleHitTimer = MOLE_HIDE_TIME;
					g_moleTargetHoleIndex = g_moleCurrentHoleIndex;
					AudioManager::PlaySoundEffect(0x31, &mole->pos);
				}
			}

			// Once he is underground he comes up at the next molehill that is neither destroyed nor the one he left.
			if (mole->pos.y > 0x4800)
			{
				Levels::RecordData* molehills = Levels::g_recordData[2];
				g_moleCurrentHoleIndex++;
				if (g_moleCurrentHoleIndex >= molehills->recordCount)
					g_moleCurrentHoleIndex -= molehills->recordCount;
				int32_t searchCount = 0;
				while ((molehills->data[g_moleCurrentHoleIndex].y == INT_MIN || g_moleCurrentHoleIndex == g_moleTargetHoleIndex)
					&& searchCount < molehills->recordCount)
				{
					g_moleCurrentHoleIndex++;
					if (g_moleCurrentHoleIndex >= molehills->recordCount)
						g_moleCurrentHoleIndex = 0;
					searchCount++;
				}
				mole->motionTargetPos.x = molehills->data[g_moleCurrentHoleIndex].x << 5;
				mole->motionTargetPos.y = molehills->data[g_moleCurrentHoleIndex].y << 5;
				mole->boundary.x = mole->motionTargetPos.x;
				mole->motionTargetPos.z = molehills->data[g_moleCurrentHoleIndex].z << 5;
				mole->boundary.y = mole->motionTargetPos.y;
				mole->boundary.z = mole->motionTargetPos.z;
				mole->pos.x = mole->motionTargetPos.x;
				mole->pos.y = 0x3800;
				mole->pos.z = mole->motionTargetPos.z;
				mole->movementCommandValue = 0x1A0;
			}

			// Dust keeps rising from the molehills that are destroyed, one path point per tick.
			for (int32_t dustStep = 0; dustStep < Renderer::g_frameDelta; dustStep++)
			{
				Levels::RecordData* molehills = Levels::g_recordData[2];
				Vector3I* molehill = &molehills->data[g_molehillParticlePathPoint];
				if (molehill->y == INT_MIN)
				{
					int32_t deltaY = (Camera::g_renderCameraTransform.pos.y - MOLEHILL_DUST_Y) >> 8;
					int32_t deltaZ = (Camera::g_renderCameraTransform.pos.z - (molehill->z << 5)) >> 8;
					int32_t deltaX = (Camera::g_renderCameraTransform.pos.x - (molehill->x << 5)) >> 8;
					if (deltaY * deltaY + deltaZ * deltaZ + deltaX * deltaX < 0x40000)
						Nu3D::Particles::SpawnFromPreset(molehill->x << 5, MOLEHILL_DUST_Y, molehill->z << 5, 0x3A, 3);
				}
				g_molehillParticlePathPoint++;
				if (g_molehillParticlePathPoint >= Levels::g_recordData[2]->recordCount)
					g_molehillParticlePathPoint = 0;
			}

			// The molehill the mole hides in bursts open shortly before he comes up again.
			if (g_moleHitTimer > 0)
			{
				g_moleHitTimer -= Renderer::g_frameDelta;
				if (g_moleHitTimer <= 0)
				{
					g_moleHitTimer = 0;
					AudioManager::ClearSequence7Cursor();
				}
				if (g_moleHitTimer < MOLE_BURST_TIME && g_moleHitTimer + Renderer::g_frameDelta >= MOLE_BURST_TIME)
				{
					Vector3I* molehill = &Levels::g_recordData[2]->data[g_moleTargetHoleIndex];
					g_molehillBurstPosition.y = MOLEHILL_BURST_Y;
					g_molehillBurstPosition.x = molehill->x << 5;
					g_molehillBurstPosition.z = molehill->z << 5;
					Nu3D::Particles::SpawnFromPreset(g_molehillBurstPosition.x, MOLEHILL_BURST_Y, g_molehillBurstPosition.z, 0x3B, 2);
					AudioManager::StartSoundSequenceOnActor(-3, &g_molehillBurstPosition);
				}
			}

			// A ground slam on the open molehill destroys it and the mole surrenders once enough are gone.
			if (g_buzzActor.collisionFlags != 0 && g_groundSlamTimer < -0x14 && g_moleCurrentHoleIndex != g_moleTargetHoleIndex && g_moleHitTimer != 0)
			{
				Vector3I* molehill = &Levels::g_recordData[2]->data[g_moleTargetHoleIndex];
				int32_t deltaY = (g_buzzActor.posAngles.pos.y - MOLEHILL_DUST_Y) >> 8;
				int32_t deltaZ = (g_buzzActor.posAngles.pos.z - (molehill->z << 5)) >> 8;
				int32_t deltaX = (g_buzzActor.posAngles.pos.x - (molehill->x << 5)) >> 8;
				if (deltaY * deltaY + deltaZ * deltaZ + deltaX * deltaX < 0x900)
				{
					AudioManager::ClearSequence7Cursor();
					Levels::g_recordData[2]->data[g_moleTargetHoleIndex].y = INT_MIN;
					g_destroyedMolehillCount++;
					g_moleHitTimer = 0;
					if (g_destroyedMolehillCount > MOLEHILLS_TO_DESTROY)
						mole->actorFlags = (mole->actorFlags & ~ACTOR_FLAG_HIDES_FROM_BUZZ) | Actor::ACTOR_FLAG_COLLIDABLE | Actor::ACTOR_FLAG_TRACKS_TARGET;

					// Fade out the burst of the molehill that is now destroyed.
					for (Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::g_particleInstances;
						particle < Nu3D::Particles::g_particleInstances + 64;
						particle++)
					{
						if (particle->typeId == 0x3B && particle->lifetime > 0)
							particle->lifetime = 1;
					}
				}
			}

			// Leaves drift down from the tree path points that pass below Buzz.
			int32_t leafPulseSlot = g_buzzActor.posAngles.pos.y < -0xBE00 ? offsetof(FramePulseOutputs, sixteenTick) : offsetof(FramePulseOutputs, fourTick);
			if (g_framePulseOutputs.bytes[leafPulseSlot] != 0 && (*g_randDatBufferPtr++ & 7) == 0)
				g_ambientParticlePending = 1;
			if (g_ambientParticlePending != 0)
			{
				Vector3I* leafPath = &Levels::g_recordData[0]->data[g_ambientParticlePathPoint];
				int32_t leafY = leafPath->y << 5;
				int32_t heightBelowBuzz = g_buzzActor.posAngles.pos.y - leafY;
				if (heightBelowBuzz > 0x7000 && heightBelowBuzz < 0x50000)
				{
					int32_t leafX = (leafPath->x - 0x200 + *g_randDatBufferPtr++ * 4) << 5;
					int32_t leafZ = (leafPath->z - 0x200 + *g_randDatBufferPtr++ * 4) << 5;
					int32_t deltaZ = (g_buzzActor.posAngles.pos.z - leafZ) >> 8;
					int32_t deltaX = (g_buzzActor.posAngles.pos.x - leafX) >> 8;
					if (deltaZ * deltaZ + deltaX * deltaX < 640000)
					{
						Nu3D::Particles::ParticleInstance* leaf = Nu3D::Particles::SpawnFromPreset(leafX, leafY, leafZ, 0x37, 0x13);
						leaf->pos.y = -0x4000;
						leaf->groundHeightY = Nu3D::Collision::GetGroundHeight(&leaf->groundProbe, 0);
						leaf->pos.y = leafY;
						leaf->rotSpeed = *g_randDatBufferPtr++ - 0x80;
						if (leaf->groundHeightY == INT_MIN)
							leaf->groundHeightY = 0x1460;
						g_ambientParticlePending = 0;
					}
				}
				g_ambientParticlePathPoint++;
				if (g_ambientParticlePathPoint >= Levels::g_recordData[0]->recordCount)
					g_ambientParticlePathPoint = 0;
			}

			// The air pump hisses each time Buzz jumps on it.
			g_launchPadSoundTimer -= Renderer::g_frameDelta;
			if (g_launchPadSoundTimer < 0)
				g_launchPadSoundTimer = 0;
			if (g_footingType == FOOTING_AIR_PUMP && g_buzzActor.specialAirState != 0)
			{
				if (g_launchPadSoundTimer == 0 && g_launchPadDepression == 0)
				{
					Levels::DeactivateAmbientEmitter(3, 1);
					AudioManager::PlaySoundEffect(0x38, &g_buzzActor.posAngles.pos);
					g_launchPadSoundTimer = 0x1E;
					g_launchPadDepression = 0xC00;
				}
			}
			else
				g_launchPadDepression = 0;

			if (g_launchPadScaleY != DUCK_FULL_SCALE)
			{
				if (g_launchPadIsRising != 0)
				{
					// Each stroke of the pump inflates the duck a little more.
					if (g_groundSlamTimer < 0)
						g_launchPadScaleY += Renderer::g_frameDelta * 0x30;
					else
						g_launchPadScaleY += Renderer::g_frameDelta * 0x20;
					if (g_launchPadScaleY >= DUCK_FULL_SCALE)
					{
						g_launchPadScaleY = DUCK_FULL_SCALE;
						g_launchPadVelocityX = 0x352;
						g_launchPadVelocityY = -0x712;
						AudioManager::PlaySoundEffect(0xB, &g_launchPadPosition);
					}
					Nu3D::Link::SetScaleFromFixedOffsets(6, 0x1000, g_launchPadScaleY, 0x1000);
				}
				else if (g_launchPadScaleY > DUCK_EMPTY_SCALE)
				{
					// The duck leaks air again while nobody uses the pump.
					g_launchPadScaleY -= Renderer::g_frameDelta * 10;
					if (g_launchPadScaleY <= DUCK_EMPTY_SCALE)
						g_launchPadScaleY = DUCK_EMPTY_SCALE;
					Nu3D::Link::SetScaleFromFixedOffsets(6, 0x1000, g_launchPadScaleY, 0x1000);
				}
			}
			else
			{
				// The inflated duck floats on the pool and bobs when Buzz slams onto it.
				if (g_groundSlamTimer == -0x28 && (Platform::GetFlags(7) & 3) == 2 && g_buzzActor.posAngles.pos.y < POOL_SURFACE_Y)
					g_launchPadVelocityY = 0x5A8;
				Nu3D::Link::SetPositionRawAndCommit(6, g_launchPadPosition.x >> 5, g_launchPadPosition.y >> 5, g_launchPadPosition.z >> 5);
				if (g_launchPadVelocityX > 0)
				{
					g_launchPadVelocityX -= Renderer::g_frameDelta * 4;
					if (g_launchPadVelocityX < 0)
						g_launchPadVelocityX = 0;
				}
				if (g_launchPadPosition.y < POOL_SURFACE_Y)
				{
					g_launchPadVelocityY += Renderer::g_frameDelta * 0x30;
					if (g_launchPadVelocityY > 0x600)
						g_launchPadVelocityY = 0x600;
				}
				else
					g_launchPadVelocityY -= Renderer::g_frameDelta * 0x30;
				g_launchPadPosition.x += g_launchPadVelocityX * Renderer::g_frameDelta;
				int32_t previousDuckY = g_launchPadPosition.y;
				g_launchPadPosition.y += g_launchPadVelocityY * Renderer::g_frameDelta;
				if (g_launchPadPosition.y >= POOL_SURFACE_Y && previousDuckY < POOL_SURFACE_Y)
				{
					Nu3D::Particles::SpawnFromPreset(g_launchPadPosition.x, POOL_SURFACE_Y, g_launchPadPosition.z, 0x39, 2);
					if (g_groundSlamTimer == 0)
					{
						if (g_launchPadVelocityY > 0x21F)
							g_launchPadVelocityY >>= 1;
						else
							g_launchPadVelocityY = 0x21F;
					}
					AudioManager::PlaySoundEffect(0x39, &g_launchPadPosition);
				}
				Vector3I duckPlatformOrigin;
				Platform::GetOrigin(7, &duckPlatformOrigin);
				Platform::SetVelocity(7,
					g_launchPadPosition.x - duckPlatformOrigin.x,
					g_launchPadPosition.y - duckPlatformOrigin.y - 0x1000,
					g_launchPadPosition.z - duckPlatformOrigin.z);
			}

			// The plunger of the pump goes down under Buzz and rises again.
			if (g_targetLaunchPadDepression != 0)
				Nu3D::Link::SetScaleFromFixedOffsets(7, 0x1000, 0x1000 - g_targetLaunchPadDepression, 0x1000);
			g_launchPadIsRising = 0;
			if (g_targetLaunchPadDepression != g_launchPadDepression)
			{
				g_launchPadIsRising = g_targetLaunchPadDepression - g_launchPadDepression < -0x80;
				g_targetLaunchPadDepression -= (g_targetLaunchPadDepression - g_launchPadDepression) * Renderer::g_frameDelta / 8;
			}

			// A ground slam on a tree stake pulls that stake out of the ground.
			int32_t treeSoundIndex = 0;
			if (g_firstTreeScaleY == TREE_FULL_SCALE && stompFooting == FOOTING_FIRST_TREE)
			{
				g_firstTreeScaleY = TREE_FULL_SCALE - 1;
				treeSoundIndex = 0x38;
				Levels::DeactivateAmbientEmitter(0, 1);
				Nu3D::Link::SetScaleFromFixedOffsets(0x20, 0x1000, 0x1000, 0x1000);
				Nu3D::Link::SetScaleFromFixedOffsets(0x22, 0, 0, 0);
			}
			if (g_secondTreeScaleY == TREE_FULL_SCALE && stompFooting == FOOTING_SECOND_TREE)
			{
				g_secondTreeScaleY = TREE_FULL_SCALE - 1;
				treeSoundIndex = 0x38;
				Levels::DeactivateAmbientEmitter(1, 1);
				Nu3D::Link::SetScaleFromFixedOffsets(0x21, 0x1000, 0x1000, 0x1000);
				Nu3D::Link::SetScaleFromFixedOffsets(0x23, 0, 0, 0);
			}
			int32_t loosenedStakes = 0;
			if (g_firstTreeScaleY != TREE_FULL_SCALE)
			{
				loosenedStakes = 1;
				if (g_firstTreeScaleY > 0)
				{
					g_firstTreeScaleY -= Renderer::g_frameDelta * 0x40;
					if (g_firstTreeScaleY < 0)
						g_firstTreeScaleY = 0;
					Nu3D::Link::SetScaleFromFixedOffsets(2, 0x1000, g_firstTreeScaleY, 0x1000);
				}
			}
			if (g_secondTreeScaleY != TREE_FULL_SCALE)
			{
				loosenedStakes += 2;
				if (g_secondTreeScaleY > 0)
				{
					g_secondTreeScaleY -= Renderer::g_frameDelta * 0x40;
					if (g_secondTreeScaleY < 0)
						g_secondTreeScaleY = 0;
					Nu3D::Link::SetScaleFromFixedOffsets(3, 0x1000, g_secondTreeScaleY, 0x1000);
				}
			}

			// With both stakes out the tree falls; with one stake out it only sways.
			if (loosenedStakes != 0)
			{
				if (loosenedStakes != 3)
				{
					g_fallingTreePitch = (Numerics::g_sinCosLUT[g_treeSwayAngle] >> 9) + 0x20;
					g_treeSwayAngle = (g_treeSwayAngle + Renderer::g_frameDelta * 0x20) & 0xFFF;
					int32_t swayRoll = loosenedStakes == 1 ? -g_fallingTreePitch : g_fallingTreePitch;
					Nu3D::Link::SetRotationRelative8bit(4, swayRoll, 0, g_fallingTreePitch);
					Nu3D::Link::SetRotationRelative8bit(8, swayRoll, 0, g_fallingTreePitch);
					Nu3D::Link::SetRotationRelative8bit(9, swayRoll, 0, g_fallingTreePitch);
					Nu3D::Link::SetRotationRelative8bit(10, swayRoll, 0, g_fallingTreePitch);
					g_fallingTreeRoll = swayRoll;
				}
				else
				{
					if (g_fallingTreesState == 0)
					{
						g_fallingTreesState = 1;
						Collision::MarkPlatformAsMoving(5);
						Platform::DisableCollision(6);
						UpdatePickupGroundHeights();
					}
					bool treeIsMoving = false;
					if (g_fallingTreePitch < 0x80)
					{
						g_fallingTreePitch += Renderer::g_frameDelta * 2;
						if (g_fallingTreePitch > 0x7F)
							g_fallingTreePitch = 0x80;
						treeIsMoving = true;
					}
					if (g_fallingTreeRoll > 0)
					{
						g_fallingTreeRoll -= Renderer::g_frameDelta * 2;
						if (g_fallingTreeRoll < 1)
							g_fallingTreeRoll = 0;
						treeIsMoving = true;
					}
					if (g_fallingTreeRoll < 0)
					{
						g_fallingTreeRoll += Renderer::g_frameDelta * 2;
						if (g_fallingTreeRoll > -1)
							g_fallingTreeRoll = 0;
						treeIsMoving = true;
					}
					if (treeIsMoving)
					{
						Nu3D::Link::SetRotationRelative8bit(4, g_fallingTreeRoll, 0, g_fallingTreePitch);
						Nu3D::Link::SetRotationRelative8bit(8, g_fallingTreeRoll, 0, g_fallingTreePitch);
						Nu3D::Link::SetRotationRelative8bit(9, g_fallingTreeRoll, 0, g_fallingTreePitch);
						Nu3D::Link::SetRotationRelative8bit(10, g_fallingTreeRoll, 0, g_fallingTreePitch);
					}
					else if (g_fallingTreesState == 1)
					{
						// The tree is down: show the fallen tree and hide the standing one.
						g_fallingTreesState = 2;
						Nu3D::Link::SetScaleFromFixedOffsets(0x15, 0x1000, 0x1000, 0x1000);
						Nu3D::Link::SetScaleFromFixedOffsets(4, 0, 0, 0);
						treeSoundIndex = 0x3B;
					}
				}
			}

			if (treeSoundIndex != 0)
			{
				Vector3I treePosition;
				treePosition.x = 0x59280;
				treePosition.y = 0x1000;
				treePosition.z = -0x5C127;
				AudioManager::PlaySoundEffect(treeSoundIndex, &treePosition);
			}

			// A ground slam on the fallen trunk throws Buzz up into the branches.
			if (stompFooting == FOOTING_FALLEN_TREE && g_fallingTreeShakeTimer == 0 && g_fallingTreesState != 0)
				g_fallingTreeShakeTimer = 2;
			if (g_fallingTreeShakeTimer != 0)
			{
				if (g_fallingTreeShakeTimer > 0)
				{
					g_fallingTreeShakeTimer += Renderer::g_frameDelta;
					if (g_fallingTreeShakeTimer > 0x20)
						g_fallingTreeShakeTimer = -0x20;
					else if (g_fallingTreeShakeTimer > 6 && g_fallingTreeShakeTimer - Renderer::g_frameDelta < 7)
					{
						g_groundSlamTimer = 0;
						Levels::DeactivateAmbientEmitter(2, 1);
						AudioManager::PlaySoundEffect(0x1C, &g_buzzActor.posAngles.pos);
						Buzz::Launch(-0x1080, 2);
						g_buzzActor.velocity.forward = Numerics::g_sinCosLUT[0x400];
						g_buzzActor.velocity.lateral = Numerics::g_sinCosLUT[0];
						g_buzzActor.actorFlags |= Buzz::ACTOR_FLAG_LOCK_FACING | Buzz::ACTOR_FLAG_UNCONTROLLED_MOMENTUM;
						g_buzzActor.posAngles.angles.yaw = 0;
						g_buzzActor.facingAngle = 0;
						Camera::g_cameraSmoothingDivisor = 0x40;
					}
				}
				else
				{
					g_fallingTreeShakeTimer += Renderer::g_frameDelta;
					if (g_fallingTreeShakeTimer > 0)
						g_fallingTreeShakeTimer = 0;
				}
				Nu3D::Link::SetRotationRelative8bit(0x15, (-abs(g_fallingTreeShakeTimer) & 0x7F) << 5, 0, 0);
			}

			// The garden gate swings shut and rocks until it comes to rest.
			if (g_gateRotationVelocity != INT_MIN)
			{
				const int32_t GATE_RESTING_ANGLE = 0x299;
				if (g_gateRotationAngle < GATE_RESTING_ANGLE)
					g_gateRotationVelocity += Renderer::g_frameDelta;
				else
					g_gateRotationVelocity = g_forcedFacingActive == 1 ? 8 : 0;
				bool gateWasOpen = g_gateRotationAngle >= GATE_RESTING_ANGLE;
				g_gateRotationAngle -= g_gateRotationVelocity * Renderer::g_frameDelta / 2;
				if (gateWasOpen && g_gateRotationAngle < GATE_RESTING_ANGLE)
				{
					Platform::DisableCollision(2);
					Platform::DisableCollision(3);
					Collision::MarkPlatformAsMoving(4);
					g_forcedFacingActive = 0;
				}
				if (g_gateRotationAngle < 1)
				{
					g_gateRotationVelocity = -(g_gateRotationVelocity >> 2);
					g_gateRotationAngle = 0;
					if (abs(g_gateRotationVelocity) < 4)
						g_gateRotationVelocity = INT_MIN;
					else
					{
						Vector3I gatePosition;
						Nu3D::Link::GetCurrentPosFixed(5, &gatePosition);
						AudioManager::PlaySoundEffect(0x34, &gatePosition);
					}
				}
				Nu3D::Link::SetRotationRelative8bit(5, 0, 0, g_gateRotationAngle);
				Nu3D::Link::SetRotationRelative8bit(0xB, 0, 0, g_gateRotationAngle);
			}

			// The garage door opens once the box below it has been pushed out of the way.
			const int32_t GARAGE_DOOR_OPEN_ANGLE = 0x245;
			if (g_garageDoorRotationAngle == 0)
			{
				if (g_buzzActor.posAngles.pos.y < -0x102F9)
				{
					if (MoveableObject::g_objects[1].pathProgress != 0)
					{
						g_garageDoorRotationAngle = 1;
						Platform::DisableCollision(0xB);
						Collision::MarkPlatformAsMoving(0xC);
						g_buzzActor.velocity.lateral = 0;
						g_buzzActor.velocity.forward = 0;
						g_forcedFacingActive = 0;
					}
				}
				else
					MoveableObject::g_objects[1].pathProgress = 0;
			}
			else
			{
				if (g_forcedFacingActive == 2)
					g_forcedFacingActive = 0;
				if (g_garageDoorRotationAngle < GARAGE_DOOR_OPEN_ANGLE)
				{
					g_garageDoorRotationAngle += g_garageDoorRotationVelocity * Renderer::g_frameDelta;
					if (g_garageDoorRotationAngle >= GARAGE_DOOR_OPEN_ANGLE)
					{
						g_garageDoorRotationAngle = GARAGE_DOOR_OPEN_ANGLE;
						Vector3I doorPosition;
						Nu3D::Link::GetCurrentPosFixed(0x1E, &doorPosition);
						AudioManager::PlaySoundEffect(0x34, &doorPosition);
					}
					Nu3D::Link::SetRotationRelative8bit(0x1E, 0, 0, 0xFFF - g_garageDoorRotationAngle);
					Nu3D::Link::SetRotationRelative8bit(0x1F, 0, 0, 0xFFF - g_garageDoorRotationAngle);
					g_garageDoorRotationVelocity += Renderer::g_frameDelta;
				}
			}

			// Zurg's kite challenges Buzz at the top of the tree.
			Actor::Toy2Actor* kite = &Actor::g_creatureActors[0x1A];
			if (g_kiteEncounterState == KITE_ENCOUNTER_IDLE && g_buzzActor.collisionFlags != 0 && g_buzzActor.posAngles.pos.y < -0x72000
				&& g_buzzActor.posAngles.pos.x > 0x20CC6 && g_buzzActor.posAngles.pos.x < 0x35FC6 && g_buzzActor.posAngles.pos.z > 0x2D174
				&& g_buzzActor.posAngles.pos.z < 0x4C7F4)
			{
				g_kiteEncounterState = KITE_ENCOUNTER_INTRO;
				Dialogue::Begin(0x1A, 9, g_zurgKiteChallengeDialogue, -1, 0, -1);
			}
			if (g_kiteEncounterState == KITE_ENCOUNTER_INTRO && Nu3D::Camera::g_viewHistoryInitialized == 0)
			{
				g_kiteEncounterState = KITE_ENCOUNTER_ACTIVE;
				kite->movementData = g_zurgKiteMovementData + 14;
				kite->movementCommandTimer = 0;
				g_levelInteractionTimer = 0xB4;
			}
			if (g_kiteEncounterState == KITE_ENCOUNTER_ACTIVE)
			{
				// The boss meter follows the damage the kite has taken.
				if (kite->creatureId != 0)
					g_hudActorAnimationFrame = kite->actorPhase * 54 / 10;
				else
				{
					g_kiteEncounterState = KITE_ENCOUNTER_DEFEATED;
					g_hudActorAnimationFrame = 0;
				}
			}
			if (g_kiteEncounterState >= KITE_ENCOUNTER_DEFEATED && g_kiteEncounterState < KITE_ENCOUNTER_TAIL_DRIFT_END)
				g_kiteEncounterState += Renderer::g_frameDelta;
			else if (g_kiteEncounterState >= KITE_ENCOUNTER_TAIL_DRIFT_END && g_kiteEncounterState != KITE_ENCOUNTER_REWARDED)
			{
				Collectables::Activate(4, 0);
				g_kiteEncounterState = KITE_ENCOUNTER_REWARDED;
			}
			if (g_kiteEncounterState == KITE_ENCOUNTER_REWARDED)
				KiteTail::Deactivate(0);
			else
			{
				if (g_kiteEncounterState < KITE_ENCOUNTER_DEFEATED)
				{
					// Hold the tail on the tail bone of the kite.
					Vector4I tailAttachment;
					tailAttachment.x = 0;
					tailAttachment.y = 0x180;
					tailAttachment.z = 0;
					Actor::ResolveBoneAttachmentPos(&tailAttachment, kite, 0);
					g_kiteTailAnchor.x = tailAttachment.x;
					g_kiteTailAnchor.y = tailAttachment.y;
					g_kiteTailAnchor.z = tailAttachment.z;
				}
				else
				{
					// The tail drifts up out of the tree once the kite is defeated.
					int32_t deltaZ = (g_buzzActor.posAngles.pos.z - g_kiteTailAnchor.z) >> 8;
					g_kiteBobAngle = (g_kiteBobAngle + Renderer::g_frameDelta * 0x40) & 0xFFF;
					int32_t deltaX = g_buzzActor.posAngles.pos.x - g_kiteTailAnchor.x;
					int32_t deltaY = g_buzzActor.posAngles.pos.y - g_kiteTailAnchor.y;
					g_kiteTailAnchor.y += 0xC0;
					g_kiteTailAnchor.x += Numerics::g_sinCosLUT[g_kiteBobAngle] >> 5;
					g_kiteTailAnchor.z += Numerics::g_sinCosLUT[g_kiteBobAngle] >> 5;
					deltaX >>= 8;
					deltaY >>= 8;
					if (g_framePulseOutputs.fourTick != 0 && deltaY * deltaY + deltaZ * deltaZ + deltaX * deltaX < 0x40000)
						Nu3D::Particles::SpawnFromPreset(g_kiteTailAnchor.x, g_kiteTailAnchor.y, g_kiteTailAnchor.z, 0x3A, 3);
				}
				if (Nu3D::Math::IsWithinDistance(&Camera::g_renderCameraTransform.pos, &g_kiteTailAnchor, 800) != 0)
				{
					KiteTail::Simulate(&g_kiteTailAnchor, 0);
					KiteTail::Activate(0);
				}
				else
					KiteTail::Deactivate(0);
			}

			// Hamm stays visible from farther away while Buzz is in the corner of the garden.
			Actor::g_creatureActors[0].visibilityDistance =
				Actor::IsInsideBounds(&g_buzzActor.posAngles.pos, -0x7CCDC, -0x2765C, -0x75990, -0x290) != 0 ? 0xED8 : 0x708;

			if (Sector::g_activeSectorIndex == 1)
			{
				// Hide the water plane of the pool while the camera is under it.
				if (g_environmentEffectType == 1)
				{
					if (g_environmentSurfaceY < Camera::g_renderCameraTransform.pos.y)
						Nu3D::Link::SetScaleFromFixedOffsets(0x14, 0, 0, 0);
					else
						Nu3D::Link::SetScaleFromFixedOffsets(0x14, 0x1000, 0x1000, 0x1000);
				}
				// Pulse the kite marker of the HUD.
				Renderer::BlitTextureByIndexOffset(5, 0x80, 0xC0, 0x40, 0x40, 0, (Numerics::g_sinCosLUT[g_kiteHudPulseAngle] >> 9) & 0x3F, 0x40, 0);
				g_kiteHudPulseAngle = (g_kiteHudPulseAngle + Renderer::g_frameDelta * 8) & 0xFFF;
			}

			// The pole rises out of the ground the first time Buzz climbs it.
			// The first point of the record carries the pole height; the second switches its collision on.
			const int32_t POLE_HEIGHT_POINT = 0;
			const int32_t POLE_COLLISION_POINT = 1;
			const int32_t POLE_COLLISION_ON = 2;
			const int32_t POLE_COLLISION_OFF = 0;
			Levels::RecordData* poleRecord = Levels::g_recordData[0x3D];
			if (g_poleRecordOffset == 0 && g_poleRiseSpeed == 0 && g_poleClimbState != 0)
			{
				g_poleRiseSpeed = 2;
				AudioManager::PlaySoundEffect(0x21, &g_buzzActor.posAngles.pos);
				poleRecord->data[POLE_COLLISION_POINT].x = POLE_COLLISION_ON;
			}
			if (g_poleRiseSpeed > 0)
			{
				const int32_t POLE_TOP_Y = -0x2500;
				g_polePosition.y += g_poleRiseSpeed * Renderer::g_frameDelta;
				poleRecord->data[POLE_COLLISION_POINT].y += g_poleRiseSpeed * Renderer::g_frameDelta;
				g_poleRiseSpeed += Renderer::g_frameDelta * 0x40 / 4;
				if (g_poleRiseSpeed > 0x800)
					g_poleRiseSpeed = 0x800;
				if (g_polePosition.y > POLE_TOP_Y)
				{
					g_polePosition.y = POLE_TOP_Y;
					poleRecord->data[POLE_COLLISION_POINT].x = POLE_COLLISION_OFF;
					g_poleRiseSpeed = -1;
				}
				poleRecord->data[POLE_HEIGHT_POINT].y = g_polePosition.y;
				Nu3D::Link::SetPositionRawAndCommit(0x19, g_polePosition.x >> 5, g_polePosition.y >> 5, g_polePosition.z >> 5);
				int32_t poleScaleY = (g_polePosition.y + 0x5826C) * 0x100 / 0x55D2;
				if (poleScaleY > 0x1000)
					poleScaleY = 0x1000;
				else if (poleScaleY < 0)
					poleScaleY = 0;
				Nu3D::Link::SetScaleFromFixedOffsets(0x1B, 0x1000, poleScaleY, 0x1000);
			}
			PlayLevelMusic();
		}

		// FUNCTION: TOY2 0x00448080 [MATCHED]
		void UpdatePickupGroundHeights()
		{
			PosAndAngles groundProbe;
			int32_t pickupIndex = 0;
			Collectables::PickupRecord* pickup = reinterpret_cast<Collectables::PickupRecord*>(Levels::g_recordData[63] + 1);
			if (Levels::g_recordData[63]->recordCount > 0)
			{
				do
				{
					if (pickup->position.y != INT_MIN && pickup->objectIndex < 0x30)
					{
						groundProbe.pos.x = pickup->position.x << 5;
						groundProbe.pos.z = pickup->position.z << 5;
						groundProbe.pos.y = (pickup->position.y - 10) << 5;
						pickup->groundHeight = Nu3D::Collision::GetGroundHeightEx(&groundProbe, 0, 0) >> 5;
						if (pickup->groundHeight == 0x7FFF)
							pickup->groundHeight = 0x7FFE;
						if (pickup->groundHeight - pickup->position.y > 0x1800)
							pickup->groundHeight = 0x7FFF;
					}
					pickup++;
					pickupIndex++;
				} while (pickupIndex < Levels::g_recordData[63]->recordCount);
			}
		}
	}
}

namespace Toy2
{
	namespace CreatureBehaviour
	{
		// FUNCTION: TOY2 0x00418610 [PROVISIONAL]
		void Army(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			if (actor->actorPhase == 0x66)
			{
				if (g_framePulseOutputs.thirtyTwoTick != 0 && (*g_randDatBufferPtr++ & 3) == 0)
				{
					AudioManager::PlaySoundEffect(0x48, &actor->pos);
				}

				if ((actor->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0)
				{
					g_levelObjectiveProgress++;
					Particles::SpawnCollectSparkle(actor->pos.x, actor->pos.y - 0x2000, actor->pos.z, 0x32);
					Actor::Kill(actor, Actor::KILL_REMOVE_ACTOR);
					AudioManager::PlaySoundEffect(0x49, &actor->pos);
				}

				actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
				if (actor->previousActorPhase <= 0)
				{
					actor->previousActorPhase = (*g_randDatBufferPtr++ & 0x7F) + 0x80;
					if ((actor->actorFlags & Actor::ACTOR_FLAG_TARGETABLE) != 0)
					{
						uint8_t* randomData = g_randDatBufferPtr;
						int32_t velocityZ = *randomData++ - 0x80;
						randomData += 2;
						g_randDatBufferPtr = randomData;
						int32_t velocityX = velocityZ * 4;
						Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnInstance(
							actor->pos.x, actor->pos.y - 0x1000, actor->pos.z, velocityX, -0xC00, velocityZ, 0x60, 0, velocityX, 0x79);
						AudioManager::PlaySoundEffect(0x60, &particle->pos);
					}
				}
			}
		}
		// FUNCTION: TOY2 0x004189C0 [PROVISIONAL]
		void ZKite(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			AndysNeighborhood::g_kiteTintToggle = (AndysNeighborhood::g_kiteTintToggle - 1) & 1;
			Actor::Toy2Actor* actor = context->actor;
			AndysNeighborhood::g_kiteTintTimer -= Renderer::g_frameDelta;
			if (AndysNeighborhood::g_kiteTintTimer < 0)
			{
				AndysNeighborhood::g_kiteTintTimer = 0;
				actor->useTint = 0;
			}
			else if (AndysNeighborhood::g_kiteTintToggle != 0)
			{
				actor->useTint = 1;
				actor->actorTint.r = 0x2000;
				actor->actorTint.g = 0x2000;
				actor->actorTint.b = 0x2000;
			}
			else
			{
				actor->useTint = 0;
			}

			AndysNeighborhood::g_kiteRollAngle = (AndysNeighborhood::g_kiteRollAngle + Renderer::g_frameDelta * 0x24) & 0xFFF;
			actor->rollAngle = Numerics::g_sinCosLUT[AndysNeighborhood::g_kiteRollAngle] >> 6;
			AndysNeighborhood::g_kiteBobAngle = (AndysNeighborhood::g_kiteBobAngle + Renderer::g_frameDelta * 0x40) & 0xFFF;
			int32_t buzzX = g_buzzActor.posAngles.pos.x;
			int32_t buzzZ = g_buzzActor.posAngles.pos.z;
			actor->targetYaw = (Nu3D::Math::CartesianToFixedAngle(actor->pos.x - buzzX, actor->pos.z - buzzZ) - 0x800) & 0xFFF;

			if (AndysNeighborhood::g_kiteEncounterState == AndysNeighborhood::KITE_ENCOUNTER_ACTIVE)
			{
				int32_t buzzY = g_buzzActor.posAngles.pos.y;
				int32_t buzzIsAboveKite = buzzY > -0x72000;
				if (buzzIsAboveKite)
				{
					actor->actorFlags = (actor->actorFlags & ~Actor::ACTOR_FLAG_TARGETS_BUZZ) | Actor::ACTOR_FLAG_TRACKS_TARGET;
					actor->motionTargetPos.x = actor->boundary.x;
					actor->motionTargetPos.z = actor->boundary.z;
					actor->creatureRam->defenseMode = 4;
					actor->movementCommandTimer = 0x32;
				}
				else
				{
					HUD::g_slideTimers[HUD::SLIDE_BOSS_STATUS] = 0x5A;
					Camera::g_actorCameraTarget.x = actor->pos.x;
					Camera::g_actorCameraTarget.y = actor->pos.y;
					Camera::g_actorCameraTarget.z = actor->pos.z;
				}

				if (buzzY > -0x721AD)
					buzzY = -0x721AD;

				if ((context->targetFlags & 1) == 0 && ! buzzIsAboveKite)
				{
					actor->previousActorPhase = 2;
					actor->motionTargetPos.x = g_buzzActor.posAngles.pos.x;
					actor->motionTargetPos.y = Numerics::g_sinCosLUT[AndysNeighborhood::g_kiteBobAngle] - 0x3000 + buzzY;
					actor->motionTargetPos.z = g_buzzActor.posAngles.pos.z;
					if (actor->motionTargetPos.y > -0x741AD)
						actor->motionTargetPos.y = -0x741AD;

					int32_t spinSpeed = 0x108 - actor->actorPhase * 0x14;
					AudioManager::g_dynamicSoundFrequencies[0] = (int16_t)spinSpeed * 0x10;
					AudioManager::PlaySoundEffect(0x3C, &actor->pos);
					AndysNeighborhood::g_kiteSpinAngle = (AndysNeighborhood::g_kiteSpinAngle + Renderer::g_frameDelta * spinSpeed) & 0xFFF;
					actor->yawAngle = (int16_t)AndysNeighborhood::g_kiteSpinAngle;
					actor->velX = Numerics::g_sinCosLUT[actor->targetYaw] >> 5;
					actor->velForward = Numerics::g_sinCosLUT[(actor->targetYaw + 0x400) & 0xFFF] >> 5;
				}
				else
				{
					if (AndysNeighborhood::g_previousKitePhase != actor->actorPhase)
					{
						AndysNeighborhood::g_kiteTintTimer = 0x3C;
						AndysNeighborhood::g_previousKitePhase = actor->actorPhase;
						actor->movementCommandTimer = 0;
						if (AndysNeighborhood::g_previousKitePhase > 0)
							AudioManager::PlaySoundEffect(0x98, &actor->pos);
					}

					actor->motionTargetPos.y = Numerics::g_sinCosLUT[AndysNeighborhood::g_kiteBobAngle] - 0xA000 + buzzY;
					AndysNeighborhood::g_kiteSpinAngle = actor->yawAngle;
					actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
					if (actor->previousActorPhase < 0)
					{
						actor->previousActorPhase = 0x78;
						int32_t movementAngle = actor->yawAngle + 0x200;
						if (*g_randDatBufferPtr++ < 0x80)
							movementAngle -= 0x400;
						actor->velX = Numerics::g_sinCosLUT[movementAngle] >> 3;
						actor->velForward = Numerics::g_sinCosLUT[(movementAngle + 0x400) & 0xFFF] >> 3;
					}
				}
			}
			else
			{
				actor->pos.y = (Numerics::g_sinCosLUT[AndysNeighborhood::g_kiteBobAngle] >> 3) - 0x7C000;
			}

			if (actor->pos.y > -0x741AD)
				actor->pos.y = -0x741AD;
		}
		// FUNCTION: TOY2 0x00418CE0 [PROVISIONAL]
		void LawnMower(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			AudioManager::PlaySoundEffect(0x4B, &actor->pos);
			if ((actor->actorFlags & Actor::ACTOR_FLAG_TARGETABLE) != 0)
			{
				if (g_framePulseOutputs.sixteenTick != 0)
				{
					Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(actor->pos.x, actor->pos.y, actor->pos.z, 0x31, 2);
					particle->groundAlignRot = *g_randDatBufferPtr++ << 4;
				}

				int32_t particleX;
				int32_t particleZ;
				particleX = actor->pos.x + Numerics::g_sinCosLUT[(actor->yawAngle - 0x800) & 0xFFF];
				particleZ = actor->pos.z + Numerics::g_sinCosLUT[(actor->yawAngle - 0x400) & 0xFFF];
				for (int32_t particleIndex = 0; particleIndex < g_framePulseOutputs.twoTickCount; particleIndex++)
				{
					int32_t particleAngle = (actor->yawAngle + 0x500 + *g_randDatBufferPtr++ * 6) & 0xFFF;
					int32_t randomValue = *g_randDatBufferPtr;
					g_randDatBufferPtr += 3;
					Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnInstance(particleX,
						actor->pos.y,
						particleZ,
						Numerics::g_sinCosLUT[particleAngle] >> 4,
						randomValue - 0xC00,
						Numerics::g_sinCosLUT[(particleAngle + 0x400) & 0xFFF] >> 4,
						0x100,
						randomValue << 4,
						randomValue * 2 - 0x100,
						0x32);
					particle->colourG = (*g_randDatBufferPtr++ >> 2) + 0x40;
				}
			}
		}

		// FUNCTION: TOY2 0x00418720 [PROVISIONAL]
		void RCCarLevel2(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			int32_t animationIndex = 0;
			Actor::Toy2Actor* actor = context->actor;
			int32_t frameSequenceIndex = 2;
			actor->actorFlags |= Actor::ACTOR_FLAG_RC_CAR;

			if (context->localForwardSpeed > 0)
			{
				AudioManager::g_dynamicSoundFrequencies[2] = context->localForwardSpeed * 4 + 0x800;
				AudioManager::PlaySoundEffect(0x37, &actor->pos);
			}

			if (HUD::g_challengeState >= 2)
			{
				if (context->localForwardSpeed < actor->creatureRam->speedTarget * 8)
				{
					animationIndex = 1;
					frameSequenceIndex = 0xB;
				}

				if (abs(context->localStrafeSpeed) > context->localForwardSpeed)
				{
					animationIndex = context->localStrafeSpeed < 0 ? 2 : 3;
					frameSequenceIndex = 0xC;
				}

				if (animationIndex != actor->primaryAnimIdx
					&& (animationIndex != 0 || actor->primaryAnimIdx != 1 || (actor->animationFramePosition & (int32_t)0xFFFF0000) >= 0xE0000))
				{
					Actor::SetAnimation(actor, (int16_t)animationIndex, frameSequenceIndex);
				}
			}

			if ((actor->actorFlags & Actor::ACTOR_FLAG_TARGETABLE) == 0)
			{
				return;
			}

			Actor::UpdatePrimaryAnimation(actor);
			if (context->localForwardSpeed > 0)
			{
				g_rcCarFrontWheelRotation = (g_rcCarFrontWheelRotation + (context->localForwardSpeed * Renderer::g_frameDelta >> 3)) & 0xFFF;
				SetRCCarNodeAngle(actor, 0, g_rcCarFrontWheelRotation, 0, 0);
				SetRCCarNodeAngle(actor, 1, g_rcCarFrontWheelRotation, 0, 0);

				if (animationIndex == 1 || abs(context->localStrafeSpeed) > 0x200)
				{
					g_rcCarRearWheelRotation = (g_rcCarRearWheelRotation + Renderer::g_frameDelta * 0xA0) & 0xFFF;
				}
				else
				{
					g_rcCarRearWheelRotation = (g_rcCarRearWheelRotation + (context->localForwardSpeed * Renderer::g_frameDelta >> 3)) & 0xFFF;
				}
				SetRCCarNodeAngle(actor, 2, g_rcCarRearWheelRotation, 0, 0);
				SetRCCarNodeAngle(actor, 3, g_rcCarRearWheelRotation, 0, 0);
			}

			if (g_framePulseOutputs.fourTick != 0 && (animationIndex == 1 || abs(context->localStrafeSpeed) > 0x200))
			{
				Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(
					actor->pos.x + (Numerics::g_sinCosLUT[(actor->yawAngle + 0x680) & 0xFFF] >> 1),
					actor->pos.y - 0x800,
					actor->pos.z + (Numerics::g_sinCosLUT[(actor->yawAngle - 0x580) & 0xFFF] >> 1),
					0x2A,
					0xA);
				particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;

				particle = Nu3D::Particles::SpawnFromPreset(actor->pos.x + (Numerics::g_sinCosLUT[(actor->yawAngle - 0x680) & 0xFFF] >> 1),
					actor->pos.y - 0x800,
					actor->pos.z + (Numerics::g_sinCosLUT[(actor->yawAngle - 0x280) & 0xFFF] >> 1),
					0x2A,
					0xA);
				particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
				AudioManager::PlaySoundEffect(0x36, &actor->pos);
			}
		}
	}
}
