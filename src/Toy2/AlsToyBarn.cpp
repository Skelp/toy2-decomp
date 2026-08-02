#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Actor.h"
#include "Toy2/Camera.h"
#include "Toy2/Collision.h"
#include "Toy2/Collectables.h"
#include "Toy2/Direct6.h"
#include "Toy2/Gadget.h"
#include "Toy2/Levels.h"
#include "Toy2/MainMenu.h"
#include "Toy2/Particles.h"
#include "Toy2/Weather.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Link.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
#include "Random.h"
#include "Numerics.h"
#include "Nullsub.h"

namespace Toy2
{
	extern int32_t g_hudActorAnimationFrame;

	namespace Lighting
	{
		void SpawnLight(int32_t x, int32_t y, int32_t z, int32_t colour, int32_t lifetime, int32_t sourceId);
	}

	namespace Camera
	{
		extern int32_t g_cameraSmoothingDivisor;
	}

	namespace Platform
	{
		void StepMotionScript(int32_t platformIndex, int32_t linkId, int16_t** scriptPosition, int32_t* waitTimer, int32_t* speedScale);
	}

	namespace MoveableObject
	{
		extern State g_objects[10];
	}

	namespace AlsToyBarn
	{
		enum HayBaleRideState
		{
			HAY_BALE_RIDE_STABLE = 0,
			HAY_BALE_RIDE_TIPPING = 1,
			HAY_BALE_RIDE_RECOVERING = 2,
		};

		enum EggChallengeState
		{
			EGG_CHALLENGE_CHICK = 0,
			EGG_CHALLENGE_TOKEN = 1,
			EGG_CHALLENGE_COMPLETE = 2,
		};

		enum DinoEncounterState
		{
			DINO_ENCOUNTER_ACTIVE = 2,
			DINO_ENCOUNTER_DEFEATED = 3,
			DINO_ENCOUNTER_REWARD_GIVEN = 200,
		};

		struct MoveableObjectInitTable
		{
			MoveableObject::InitEntry entries[2];
			int16_t terminator;
		};

		struct TokenDialogueRecord
		{
			int32_t tokenId;
			int32_t dialogueRecordIndex;
			const char* subtitle;
			int32_t facingAngle;
		};

		struct TokenDialogueTable
		{
			TokenDialogueRecord records[1];
			int32_t terminator;
		};

		STATIC_ASSERT(sizeof(TokenDialogueRecord) == 0x10);
		STATIC_ASSERT(sizeof(TokenDialogueTable) == 0x14);
		STATIC_ASSERT(offsetof(TokenDialogueTable, terminator) == 0x10);

		// GLOBAL: TOY2 0x004F2844
		char g_hayBaleRideInstructions[] = {
#include "HayBaleRideInstructions.inc"
		};

		// GLOBAL: TOY2 0x004F294C
		int16_t g_platform7MotionScript[18] = { 1, 0, -0x76C, 0, 8, 3, 0x7F, 0x60, 1, 0, 0, 0, 8, 3, 0x7F, 0x20, 0, 0x10 };
		// GLOBAL: TOY2 0x004F2970
		int16_t g_platform8MotionScript[18] = { 1, 0, -0xA8C, 0, 8, 3, 0x7F, 0x60, 1, 0, 0, 0, 8, 3, 0x7F, 0x20, 0, 0x10 };
		// GLOBAL: TOY2 0x004F2994
		int16_t g_platform9MotionScript[18] = { 1, 0, -0xA8C, 0, 8, 3, 0x7F, 0x60, 1, 0, 0, 0, 8, 3, 0x7F, 0x20, 0, 0x10 };
		// GLOBAL: TOY2 0x004F29B8
		int16_t g_platform13MotionScript[18] = { 1, 0x316, 0, 0, 0xC, 3, 0xFF, 0x20, 1, 0, 0, 0, 0xC, 3, 0xFF, 0x20, 0, 0x10 };
		// GLOBAL: TOY2 0x004F29DC
		int16_t g_platform12MotionScript[18] = { 1, 0x316, 0, 0, 0xC, 3, 0xFF, 0x20, 1, 0, 0, 0, 0xC, 3, 0xFF, 0x20, 0, 0x10 };
		// GLOBAL: TOY2 0x004F2A00
		int16_t g_platform11MotionScript[18] = { 1, 0x316, 0, 0, 0xC, 3, 0xFF, 0x20, 1, 0, 0, 0, 0xC, 3, 0xFF, 0x20, 0, 0x10 };

		// GLOBAL: TOY2 0x004F2A24
		MoveableObjectInitTable g_moveableObjectInitTable = {
			{
				{ 8, 5, 0 },
				{ 9, 6, 1 },
			},
			-1,
		};

		// GLOBAL: TOY2 0x004F2A34
		char* g_rotatingHintSubtitles[] = {
#include "AlsToyBarnHint.inc"
		};

		// GLOBAL: TOY2 0x004F2A48
		int16_t g_tokenLinkIds[] = { 0x31, 0x33, 0x34, 0x32, 0x30, 0 };

		// GLOBAL: TOY2 0x004F2A54
		extern const TokenDialogueTable g_tokenDialogueValues = {
			{
				{ 0x46, 0x14, g_hayBaleRideInstructions, 0x400 },
			},
			-1,
		};

		// GLOBAL: TOY2 0x0052F9BC
		int32_t g_platform10VerticalVelocity;
		// GLOBAL: TOY2 0x0052F9C0
		int32_t g_groundSlamPlatformTimer;
		// GLOBAL: TOY2 0x0052F9C4
		int32_t g_platform12MotionSpeed;
		// GLOBAL: TOY2 0x0052F9C8
		int32_t g_platform11MotionSpeed;
		// GLOBAL: TOY2 0x0052F9CC
		int32_t g_platform9MotionSpeed;
		// GLOBAL: TOY2 0x0052F9D0
		int32_t g_platform13MotionSpeed;
		// GLOBAL: TOY2 0x0052F9D4
		int32_t g_platform7MotionSpeed;
		// GLOBAL: TOY2 0x0052F9D8
		int32_t g_platform8MotionSpeed;
		// GLOBAL: TOY2 0x0052F9DC
		int32_t g_chick10WasActive;
		// GLOBAL: TOY2 0x0052F9E0
		int32_t g_chick11WasActive;
		// GLOBAL: TOY2 0x0052F9E4
		int32_t g_hayBaleRideState;
		// GLOBAL: TOY2 0x0052F9E8
		int32_t g_platform14MotionSpeed;
		// GLOBAL: TOY2 0x0052F9EC
		int32_t g_launchPadBounceTimer;
		// GLOBAL: TOY2 0x0052F9F0
		int16_t* g_platform12MotionCursor;
		// GLOBAL: TOY2 0x0052F9F4
		int16_t* g_platform11MotionCursor;
		// GLOBAL: TOY2 0x0052F9F8
		int16_t* g_platform9MotionCursor;
		// GLOBAL: TOY2 0x0052F9FC
		int16_t* g_platform13MotionCursor;
		// GLOBAL: TOY2 0x0052FA00
		int16_t* g_platform7MotionCursor;
		// GLOBAL: TOY2 0x0052FA04
		int16_t* g_platform8MotionCursor;
		// GLOBAL: TOY2 0x0052FA08
		int32_t g_eggLiftOffset;
		// GLOBAL: TOY2 0x0052FA0C
		int32_t g_ambientParticlePositionIndex;
		// GLOBAL: TOY2 0x0052FA10
		int32_t g_targetEggLiftOffset;

		// GLOBAL: TOY2 0x0052FA14
		int32_t g_dinoEncounterState;
		// GLOBAL: TOY2 0x0052FA18
		int32_t g_unusedState;
		// GLOBAL: TOY2 0x0052FA1C
		int32_t g_eggChallengeState;
		// GLOBAL: TOY2 0x0052FA20
		int32_t g_dinoTintToggle;
		// GLOBAL: TOY2 0x0052FA24
		int32_t g_hayBaleRideTimer;
		// GLOBAL: TOY2 0x0052FA28
		int32_t g_hayBaleRideSpeed;
		// GLOBAL: TOY2 0x0052FA2C
		int32_t g_previousDinoPhase;
		// GLOBAL: TOY2 0x0052FA30
		int32_t g_hayBaleRideAngle;
		// GLOBAL: TOY2 0x0052FA34
		int32_t g_platform12MotionTimer;
		// GLOBAL: TOY2 0x0052FA38
		int32_t g_platform11MotionTimer;
		// GLOBAL: TOY2 0x0052FA3C
		int32_t g_platform9MotionTimer;
		// GLOBAL: TOY2 0x0052FA40
		int32_t g_platform13MotionTimer;
		// GLOBAL: TOY2 0x0052FA44
		int32_t g_platform7MotionTimer;
		// GLOBAL: TOY2 0x0052FA48
		int32_t g_platform8MotionTimer;
		// GLOBAL: TOY2 0x0052FA4C
		int32_t g_platform10ForwardSpeed;
		// GLOBAL: TOY2 0x0052FA50
		int32_t g_dinoTintTimer;

		// FUNCTION: TOY2 0x00420F70 [MATCHED]
		void ResolveChickObjectCollision(int32_t actorIndex, int32_t objectIndex)
		{
			if ((Actor::g_creatureActors[actorIndex].actorFlags & Actor::ACTOR_FLAG_ACTIVE) != 0)
			{
				int32_t actorY = Actor::g_creatureActors[actorIndex].pos.y;
				if (actorY > MoveableObject::g_objects[1].position.y - 0x9000)
				{
					int32_t deltaX = (MoveableObject::g_objects[objectIndex].position.x - Actor::g_creatureActors[actorIndex].pos.x) >> 5;
					int32_t deltaZ = (MoveableObject::g_objects[objectIndex].position.z - Actor::g_creatureActors[actorIndex].pos.z) >> 5;
					if (deltaZ * deltaZ + deltaX * deltaX < 490000)
					{
						int32_t objectY = MoveableObject::g_objects[objectIndex].position.y;
						if (actorY < objectY - 0x8800)
						{
							Actor::g_creatureActors[actorIndex].pos.y = objectY - 0x9000;
							return;
						}

						int32_t angle = Nu3D::Math::CartesianToFixedAngle(deltaX, deltaZ) & 0xFFF;
						Actor::g_creatureActors[actorIndex].pos.x =
							MoveableObject::g_objects[objectIndex].position.x - (Numerics::g_sinCosLUT[angle] * 700 >> 9);
						Actor::g_creatureActors[actorIndex].pos.z =
							MoveableObject::g_objects[objectIndex].position.z - (Numerics::g_sinCosLUT[(angle + 0x400) & 0xFFF] * 700 >> 9);
					}
				}
			}
		}

		// FUNCTION: TOY2 0x00421090 [PROVISIONAL]
		void Init()
		{
			Collectables::Init(g_tokenLinkIds, 0x41);
			Collectables::LoadTokenTable(reinterpret_cast<const Collectables::TokenDialogueValue*>(g_tokenDialogueValues.records));
			MoveableObject::InitTable(g_moveableObjectInitTable.entries);
			Collectables::Activate(3, 1);
			Nullsub7(0x13, 0x12);

			g_hayBaleRideState = 0;
			g_hayBaleRideTimer = 0;
			g_hayBaleRideAngle = 0;
			g_hayBaleRideSpeed = 0;
			Platform::AddFlags(0, 0x100);

			Actor::g_creatureActors[6].actorFlags &= ~(Actor::ACTOR_FLAG_TARGETABLE | Actor::ACTOR_FLAG_COLLIDABLE);
			g_ambientParticlePositionIndex = 0;
			g_dinoEncounterState = 0;
			g_dinoTintToggle = 0;
			g_dinoTintTimer = 0;
			g_previousDinoPhase = Actor::g_creatureActors[0].actorPhase;
			g_chick10WasActive = 0;
			g_chick11WasActive = 0;
			g_unusedState = 0;
			g_groundSlamPlatformTimer = 0;
			Nu3D::Link::SetScaleFromFixedOffsets(0x1E, 0x1000, 0, 0x1000);

			g_launchPadBounceTimer = 0;
			Nu3D::Link::SetScaleFromFixedOffsets(3, 0x1000, 0, 0x1000);
			g_platform14MotionSpeed = 0;
			g_platform10VerticalVelocity = 0;
			g_platform10ForwardSpeed = 0;
			Platform::AddFlags(10, 0x100);
			Platform::AddFlags(14, 0x100);

			g_eggLiftOffset = 0;
			g_eggChallengeState = 0;
			g_targetEggLiftOffset = 0;
			HUD::g_challengeState = 0;
			AndysHouse::g_raceCheckpointPassCount = 0;

			g_platform7MotionCursor = g_platform7MotionScript;
			g_platform7MotionTimer = 0;
			g_platform8MotionCursor = g_platform8MotionScript;
			g_platform8MotionTimer = 0;
			g_platform9MotionCursor = g_platform9MotionScript;
			g_platform9MotionTimer = 0;
			Platform::AddFlags(7, 0x100);
			Platform::AddFlags(8, 0x100);
			Platform::AddFlags(9, 0x100);
			g_platform13MotionCursor = g_platform13MotionScript;
			g_platform13MotionTimer = 0;
			g_platform12MotionCursor = g_platform12MotionScript;
			g_platform12MotionTimer = 0;
			g_platform11MotionCursor = g_platform11MotionScript;
			g_platform11MotionTimer = 0;

			Vector3I platformOrigin;
			Platform::GetOrigin(13, &platformOrigin);
			Platform::SetOrigin(13, platformOrigin.x + 0x62C0, platformOrigin.y, platformOrigin.z);
			Nu3D::Link::SetPositionRawAndCommit(12, (platformOrigin.x + 0x62C0) >> 5, platformOrigin.y >> 5, platformOrigin.z >> 5);

			Platform::GetOrigin(12, &platformOrigin);
			Platform::SetOrigin(12, platformOrigin.x + 0x62C0, platformOrigin.y, platformOrigin.z);
			Nu3D::Link::SetPositionRawAndCommit(13, (platformOrigin.x + 0x62C0) >> 5, platformOrigin.y >> 5, platformOrigin.z >> 5);

			Platform::GetOrigin(11, &platformOrigin);
			Platform::SetOrigin(11, platformOrigin.x + 0x62C0, platformOrigin.y, platformOrigin.z);
			Nu3D::Link::SetPositionRawAndCommit(14, (platformOrigin.x + 0x62C0) >> 5, platformOrigin.y >> 5, platformOrigin.z >> 5);

			MainMenu::g_menuClearColor.r = g_skyColorRed;
			MainMenu::g_menuClearColor.g = g_skyColorGreen;
			MainMenu::g_menuClearColor.b = g_skyColorBlue;
		}

		// FUNCTION: TOY2 0x00421340 [PROVISIONAL]
		void Interactions()
		{
			Vector3I position;
			Vector3I origin;

			if (g_hayBaleRideTimer >= 2 && g_buzzActor.posAngles.pos.z >= -0x11202)
			{
				g_hayBaleRideTimer = 1;
			}
			else if (g_hayBaleRideTimer <= 0)
			{
				if (g_hayBaleRideSpeed > 0)
				{
					g_hayBaleRideSpeed -= Renderer::g_frameDelta;
					if (g_hayBaleRideSpeed < 0)
						g_hayBaleRideSpeed = 0;
				}
				if (g_groundSlamTimer != 0 && Platform::HadBuzzContactThisFrame(15) != 0 && HUD::g_challengeState == 0)
				{
					Platform::SetRotationAngles(15, 0, 0, -0x180);
					Nu3D::Link::SetRotationRelative8bit(33, 0, 0, -0x180);
					g_hayBaleRideTimer = 0xA8C;
					Levels::DeactivateAmbientEmitter(0, 1);
				}
			}

			if (g_hayBaleRideTimer > 0)
			{
				HUD::g_challengeState = 2;
				AndysHouse::g_raceCheckpointPassCount = g_hayBaleRideTimer / 60 + 100;
				g_hayBaleRideSpeed += Renderer::g_frameDelta;
				g_hayBaleRideTimer -= Renderer::g_frameDelta;
				if (g_hayBaleRideSpeed > 0x200)
					g_hayBaleRideSpeed = 0x200;
				if (g_hayBaleRideTimer < 1)
				{
					g_hayBaleRideTimer = 0;
					Platform::SetRotationAngles(15, 0, 0, 0);
					Nu3D::Link::SetRotationRelative8bit(33, 0, 0, 0);
					HUD::g_challengeState = 0;
					AndysHouse::g_raceCheckpointPassCount = 100;
				}
			}

			if (g_hayBaleRideSpeed != 0)
			{
				AudioManager::g_dynamicSoundFrequencies[0] = static_cast<int16_t>(g_hayBaleRideSpeed) * 8 + 0x400;
				Nu3D::Link::GetCurrentPosFixed(33, &position);
				AudioManager::PlaySoundEffect(0x75, &position);
			}
			g_hayBaleRideAngle += g_hayBaleRideSpeed * Renderer::g_frameDelta / 16;

			if (g_hayBaleRideTimer > 0 && (Platform::GetFlags(0) & Platform::PLATFORM_FLAG_BUZZ_CONTACT) != 0)
			{
				g_hayBaleRideState = 1;
				g_groundSlamTimer = 0;
				Buzz::Launch(-0xC00, 2);
				AudioManager::PlaySoundEffect(0x1C, &g_buzzActor.posAngles.pos);
				g_buzzActor.velocity.lateral = Numerics::g_sinCosLUT[0xB90] / 7;
				Camera::g_cameraSmoothingDivisor = 0x40;
				g_buzzActor.velocity.forward = Numerics::g_sinCosLUT[0xF90] / 7;
				g_buzzActor.actorFlags |= Buzz::ACTOR_FLAG_LOCK_FACING | Buzz::ACTOR_FLAG_UNCONTROLLED_MOMENTUM;
				g_buzzActor.posAngles.angles.yaw = 0xB90;
				g_buzzActor.facingAngle = 0xB90;
			}

			Nu3D::Link::GetTargetPosFixed(0, &position);
			Platform::GetOrigin(0, &origin);
			position.y += Numerics::g_sinCosLUT[(g_hayBaleRideAngle * 7 / 6) & 0xFFF] / 2 - origin.y;
			Platform::SetVelocity(0, 0, position.y, 0);
			Platform::GetRotation(0, &position);
			int32_t targetRoll = Numerics::g_sinCosLUT[g_hayBaleRideAngle & 0xFFF] / 32;
			if (g_hayBaleRideState == HAY_BALE_RIDE_TIPPING)
			{
				int32_t rollDelta = -0x708 - position.z;
				Platform::SetAngularVelocity(0, 0, 0, static_cast<int16_t>(rollDelta >> 2));
				if (abs(rollDelta >> 2) < 8)
					g_hayBaleRideState = HAY_BALE_RIDE_RECOVERING;
			}
			else if (g_hayBaleRideState == HAY_BALE_RIDE_RECOVERING)
			{
				int32_t rollDelta = targetRoll - position.z;
				Platform::SetAngularVelocity(0, 0, 0, static_cast<int16_t>(rollDelta >> 3));
				if (abs(rollDelta >> 3) < 8)
					g_hayBaleRideState = HAY_BALE_RIDE_STABLE;
			}
			else
			{
				Platform::SetAngularVelocity(0, 0, 0, static_cast<int16_t>((targetRoll - position.z) >> 2));
			}
			Platform::CommitRotationToLink(0, 0);
			Nu3D::Link::SetPositionRawAndCommit(0, origin.x >> 5, origin.y >> 5, origin.z >> 5);
			Nu3D::Link::SetPositionRawAndCommit(1, origin.x >> 7, origin.y >> 7, origin.z >> 7);
			Nu3D::Link::GetRotation8Bit(0, &position);
			Nu3D::Link::SetRotationRelative8bit(1, position.x, position.y, position.z);

			if (g_eggLiftOffset < g_targetEggLiftOffset)
			{
				g_eggLiftOffset += Renderer::g_frameDelta * 0x200;
				if (g_eggLiftOffset > g_targetEggLiftOffset)
					g_eggLiftOffset = g_targetEggLiftOffset;
			}
			else if (g_eggLiftOffset > g_targetEggLiftOffset)
			{
				g_eggLiftOffset -= Renderer::g_frameDelta * 0x200;
				if (g_eggLiftOffset < g_targetEggLiftOffset)
					g_eggLiftOffset = g_targetEggLiftOffset;
			}
			if (g_eggLiftOffset == 0)
				Collision::MarkPlatformAsMoving(18);
			else
				Platform::DisableCollision(18);
			Nu3D::Link::GetTargetPosFixed(31, &position);
			Nu3D::Link::SetPositionRawAndCommit(31, position.x >> 5, (position.y - g_eggLiftOffset) >> 5, position.z >> 5);

			Platform::SetAngularVelocity(1, 0, 0, 0x24);
			Platform::CommitRotationToLink(1, 7);
			Platform::SetAngularVelocity(2, 0, 0, 0x2C);
			Platform::CommitRotationToLink(2, 6);
			Platform::SetAngularVelocity(3, 0, 0, 0x24);
			Platform::CommitRotationToLink(3, 4);
			Platform::SetAngularVelocity(4, 0, 0, 0x2C);
			Platform::CommitRotationToLink(4, 5);
			int32_t slamPhase = abs(g_groundSlamPlatformTimer);
			Nu3D::Link::SetScaleFromFixedOffsets(30, 0x1000, slamPhase * 0x80, 0x1000);
			Nu3D::Link::SetRotationRelative8bit(2, 0, 0, slamPhase * 0x20);

			bool buzzOnSlamPlatform = g_buzzActor.isOnWalkableFloor != 0 && g_footingType == 8;
			if (buzzOnSlamPlatform && g_groundSlamPlatformTimer == 0 && g_groundSlamTimer != 0)
			{
				Levels::DeactivateAmbientEmitter(1, 1);
				g_groundSlamPlatformTimer = 2;
			}
			if (buzzOnSlamPlatform || g_groundSlamPlatformTimer != 0)
			{
				if (g_groundSlamPlatformTimer < 1)
				{
					g_groundSlamPlatformTimer += Renderer::g_frameDelta;
					if (g_groundSlamPlatformTimer > 0)
						g_groundSlamPlatformTimer = 0;
				}
				else
				{
					int32_t previousTimer = g_groundSlamPlatformTimer;
					g_groundSlamPlatformTimer += Renderer::g_frameDelta;
					if (g_groundSlamPlatformTimer < 0x21)
					{
						if (previousTimer < 7 && g_groundSlamPlatformTimer > 6)
						{
							g_groundSlamTimer = 0;
							Buzz::Launch(-0xC00, 2);
							AudioManager::PlaySoundEffect(0x1C, &g_buzzActor.posAngles.pos);
							g_buzzActor.velocity.lateral = Numerics::g_sinCosLUT[0x81E] >> 3;
							g_buzzActor.velocity.forward = Numerics::g_sinCosLUT[0xC1E] >> 3;
							g_buzzActor.actorFlags |= Buzz::ACTOR_FLAG_LOCK_FACING | Buzz::ACTOR_FLAG_UNCONTROLLED_MOMENTUM;
							g_buzzActor.posAngles.angles.yaw = 0x81E;
							g_buzzActor.facingAngle = 0x81E;
							Camera::g_cameraSmoothingDivisor = 0x80;
						}
					}
					else
					{
						g_groundSlamPlatformTimer = -0x20;
					}
				}
			}

			if (g_buzzActor.isOnWalkableFloor != 0 && g_footingType == 9)
			{
				g_buzzActor.actorFlags &= ~(Buzz::ACTOR_FLAG_LOCK_FACING | Buzz::ACTOR_FLAG_UNCONTROLLED_MOMENTUM);
				g_launchPadBounceTimer = 1;
				int32_t launchVelocity = -0x980;
				if (g_groundSlamTimer != 0)
				{
					g_groundSlamTimer = 0;
					launchVelocity = -0xC00;
				}
				Buzz::Launch(launchVelocity, 2);
				AudioManager::PlaySoundEffect(0x1C, &g_buzzActor.posAngles.pos);
			}
			if (g_launchPadBounceTimer != 0)
			{
				if (g_launchPadBounceTimer < 0x3001)
				{
					int32_t scaleOffset = Numerics::g_sinCosLUT[g_launchPadBounceTimer & 0xFFF] >> (((g_launchPadBounceTimer >> 11) & 0xE) + 3);
					Nu3D::Link::SetScaleFromFixedOffsets(3, 0x1000, scaleOffset, 0x1000);
					g_launchPadBounceTimer += Renderer::g_frameDelta * 0x100;
				}
				else
				{
					g_launchPadBounceTimer = 0;
					Nu3D::Link::SetScaleFromFixedOffsets(3, 0x1000, 0, 0x1000);
				}
			}

			Nu3D::Link::GetCurrentPosFixed(8, &position);
			Nu3D::Link::SetPositionRawAndCommit(21, position.x >> 7, position.y >> 7, position.z >> 7);
			Nu3D::Link::GetCurrentPosFixed(9, &position);
			Nu3D::Link::SetPositionRawAndCommit(22, position.x >> 7, position.y >> 7, position.z >> 7);

			if (g_platform10ForwardSpeed == 0 && g_buzzActor.isOnWalkableFloor != 0 && (Platform::GetFlags(10) & 3) == Platform::PLATFORM_FLAG_BUZZ_CONTACT
				&& Platform::GetContactFaceNormal(10)->y < -0x3000)
			{
				g_platform10ForwardSpeed = 0x20;
				Levels::DeactivateAmbientEmitter(3, 1);
			}
			if (g_platform10ForwardSpeed > 0)
			{
				Platform::GetOrigin(10, &position);
				AudioManager::g_dynamicSoundFrequencies[0] = static_cast<int16_t>(g_platform10ForwardSpeed) + 0xD48;
				AudioManager::PlaySoundEffect(0x7A, &position);
				Nu3D::Link::SetPositionRawAndCommit(11, position.x >> 5, position.y >> 5, position.z >> 5);
				g_platform10ForwardSpeed += Renderer::g_frameDelta * 0x20;
				if (g_platform10ForwardSpeed > 0x6A4)
					g_platform10ForwardSpeed = 0x6A4;
				if (position.z < 0x42C60)
				{
					if (position.y > -0x1E00 - g_platform10VerticalVelocity * Renderer::g_frameDelta && g_platform10VerticalVelocity > 0)
					{
						if (g_platform10VerticalVelocity < 0x4B1)
							g_platform10VerticalVelocity = -0xC0;
						else
							g_platform10VerticalVelocity = g_platform10VerticalVelocity * -3 / 8;
					}
					else
					{
						g_platform10VerticalVelocity += Renderer::g_frameDelta * 0x60;
						if (g_platform10VerticalVelocity > 0x1000)
							g_platform10VerticalVelocity = 0x1000;
					}
				}
				Platform::SetVelocity(10, 0, g_platform10VerticalVelocity * Renderer::g_frameDelta, -g_platform10ForwardSpeed * Renderer::g_frameDelta);
				if (position.z < 0x104C0)
				{
					g_platform10ForwardSpeed = -1;
					Platform::SetVelocity(10, 0, 0, 0);
					if ((Platform::GetFlags(10) & 3) == Platform::PLATFORM_FLAG_BUZZ_CONTACT)
					{
						Buzz::Launch(-0xA00, 2);
						AudioManager::PlaySoundEffect(0x1C, &g_buzzActor.posAngles.pos);
						g_buzzActor.velocity.lateral = Numerics::g_sinCosLUT[0x81E] >> 5;
						g_buzzActor.velocity.forward = Numerics::g_sinCosLUT[0xC1E] >> 5;
						g_buzzActor.actorFlags |= Buzz::ACTOR_FLAG_LOCK_FACING | Buzz::ACTOR_FLAG_UNCONTROLLED_MOMENTUM;
						g_buzzActor.posAngles.angles.yaw = 0x81E;
						g_buzzActor.facingAngle = 0x81E;
					}
				}
			}

			if (g_platform14MotionSpeed == 0 && g_buzzActor.isOnWalkableFloor != 0 && (Platform::GetFlags(14) & 3) == Platform::PLATFORM_FLAG_BUZZ_CONTACT
				&& Platform::GetContactFaceNormal(14)->y < -0x3000)
			{
				g_platform14MotionSpeed = 0x20;
				Levels::DeactivateAmbientEmitter(2, 1);
			}
			if (g_platform14MotionSpeed > 0)
			{
				Platform::GetOrigin(14, &position);
				AudioManager::g_dynamicSoundFrequencies[0] = static_cast<int16_t>(g_platform14MotionSpeed) + 0xD48;
				AudioManager::PlaySoundEffect(0x7A, &position);
				Nu3D::Link::SetPositionRawAndCommit(24, position.x >> 5, position.y >> 5, position.z >> 5);
				Platform::SetVelocity(14, 0, 0, -g_platform14MotionSpeed * Renderer::g_frameDelta);
				g_platform14MotionSpeed += Renderer::g_frameDelta * 0x20;
				if (g_platform14MotionSpeed > 0x708)
					g_platform14MotionSpeed = 0x708;
				if ((position.z >> 5) < 0x2300)
				{
					g_platform14MotionSpeed = g_platform14MotionSpeed * -7 / 8;
					if ((Platform::GetFlags(14) & 3) == Platform::PLATFORM_FLAG_BUZZ_CONTACT)
					{
						Buzz::Launch(-0xA00, 2);
						AudioManager::PlaySoundEffect(0x1C, &g_buzzActor.posAngles.pos);
						g_buzzActor.velocity.lateral = Numerics::g_sinCosLUT[0x81E] >> 4;
						g_buzzActor.velocity.forward = Numerics::g_sinCosLUT[0xC1E] >> 4;
						g_buzzActor.actorFlags |= Buzz::ACTOR_FLAG_LOCK_FACING | Buzz::ACTOR_FLAG_UNCONTROLLED_MOMENTUM;
						g_buzzActor.posAngles.angles.yaw = 0x81E;
						g_buzzActor.facingAngle = 0x81E;
					}
				}
			}
			if (g_platform14MotionSpeed < 0)
			{
				Platform::GetOrigin(14, &position);
				position.x >>= 5;
				position.y >>= 5;
				position.z >>= 5;
				Nu3D::Link::SetPositionRawAndCommit(24, position.x, position.y, position.z);
				if (position.z < 0x3A27 || (g_platform14MotionSpeed += Renderer::g_frameDelta * 0x20) < 0)
				{
					Platform::SetVelocity(14, 0, 0, -g_platform14MotionSpeed);
				}
				else
				{
					Platform::SetVelocity(14, 0, 0, 0);
					g_platform14MotionSpeed = 0;
					Platform::SetVelocity(14, 0, 0, 0);
				}
			}

			Platform::StepMotionScript(7, 15, &g_platform7MotionCursor, &g_platform7MotionTimer, &g_platform7MotionSpeed);
			Platform::StepMotionScript(8, 16, &g_platform8MotionCursor, &g_platform8MotionTimer, &g_platform8MotionSpeed);
			Platform::StepMotionScript(9, 17, &g_platform9MotionCursor, &g_platform9MotionTimer, &g_platform9MotionSpeed);
			Nu3D::Link::GetCurrentPosFixed(15, &position);
			Nu3D::Link::SetPositionRawAndCommit(18, position.x >> 7, position.y >> 7, position.z >> 7);
			Nu3D::Link::GetCurrentPosFixed(16, &position);
			Nu3D::Link::SetPositionRawAndCommit(19, position.x >> 7, position.y >> 7, position.z >> 7);
			Nu3D::Link::GetCurrentPosFixed(17, &position);
			Nu3D::Link::SetPositionRawAndCommit(20, position.x >> 7, position.y >> 7, position.z >> 7);

			int8_t launcherDefenseMode = g_discLauncherShotSlotsAvailable == 6 ? 4 : 5;
			Actor::g_creatureActors[7].creatureRam->defenseMode = launcherDefenseMode;
			Actor::g_creatureActors[8].creatureRam->defenseMode = launcherDefenseMode;
			Actor::g_creatureActors[9].creatureRam->defenseMode = launcherDefenseMode;
			if (Actor::g_creatureActors[7].actorPhase == 0)
				Platform::StepMotionScript(13, 12, &g_platform13MotionCursor, &g_platform13MotionTimer, &g_platform13MotionSpeed);
			if (Actor::g_creatureActors[8].actorPhase == 0)
				Platform::StepMotionScript(12, 13, &g_platform12MotionCursor, &g_platform12MotionTimer, &g_platform12MotionSpeed);
			if (Actor::g_creatureActors[9].actorPhase == 0)
				Platform::StepMotionScript(11, 14, &g_platform11MotionCursor, &g_platform11MotionTimer, &g_platform11MotionSpeed);

			if (g_framePulseOutputs.sixtyFourTick != 0)
			{
				if (Actor::IsInsideBounds(&g_buzzActor.posAngles.pos, -0x5CF57, -0x36A57, 0x6D7D, 0x5247D) != 0)
				{
					g_ambientParticlePositionIndex++;
					Levels::RecordData* particlePositions = Levels::g_recordData[6];
					if (g_ambientParticlePositionIndex >= particlePositions->recordCount)
						g_ambientParticlePositionIndex = 0;
					Vector3I& particlePosition = particlePositions->data[g_ambientParticlePositionIndex];
					Nu3D::Particles::ParticleInstance* particle =
						Nu3D::Particles::SpawnFromPreset(particlePosition.x << 5, particlePosition.y << 5, particlePosition.z << 5, 0x56, 0x19);
					particle->groundHeightY = 0;
					AudioManager::PlaySoundEffect(0x74, &particle->pos);
				}
				if (Actor::IsInsideBounds(&g_buzzActor.posAngles.pos, -0x3BB3A, -0x1B73A, -0xA7DD, 0x48FA3) != 0)
				{
					uint32_t positionIndex = *g_randDatBufferPtr++ & 7;
					if (positionIndex > 5)
						positionIndex -= 6;
					Vector3I& particlePosition = Levels::g_recordData[7]->data[positionIndex];
					Nu3D::Particles::ParticleInstance* particle =
						Nu3D::Particles::SpawnFromPreset(particlePosition.x << 5, particlePosition.y << 5, particlePosition.z << 5, 0x50, 0x18);
					particle->groundAlignRot = (*g_randDatBufferPtr++ & 3) << 10;
				}
			}

			Actor::Toy2Actor& chick10 = Actor::g_creatureActors[10];
			Actor::Toy2Actor& chick11 = Actor::g_creatureActors[11];
			if (static_cast<uint32_t>(chick10.pos.x) < 0xFFF86271)
				chick10.pos.x = -0x79D8F;
			if (static_cast<uint32_t>(chick11.pos.x) < 0xFFF86271)
				chick11.pos.x = -0x79D8F;
			if (chick10.pos.z < -0x38C8)
				chick10.pos.z = -0x38C8;
			if (chick11.pos.z < -0x38C8)
				chick11.pos.z = -0x38C8;
			if (MoveableObject::g_objects[0].position.z < 0xAE00)
			{
				int32_t maximumX = MoveableObject::g_objects[0].position.x - 0x4800;
				if (chick10.pos.x > maximumX)
					chick10.pos.x = maximumX;
				if (chick11.pos.x > maximumX)
					chick11.pos.x = maximumX;
			}
			else
			{
				int32_t maximumZ = MoveableObject::g_objects[0].position.z - 0x9800;
				int32_t maximumX = MoveableObject::g_objects[0].position.x - 0x4800;
				if (chick10.pos.z > maximumZ && chick10.pos.x > maximumX)
				{
					chick10.velForward = -0x900;
					chick10.pos.z = maximumZ;
				}
				if (chick11.pos.z > maximumZ && chick11.pos.x > maximumX)
				{
					chick11.velForward = -0x900;
					chick11.pos.z = maximumZ;
				}
			}

			if ((chick10.actorFlags & Actor::ACTOR_FLAG_TARGETABLE) != 0)
				g_chick10WasActive = 1;
			if ((chick11.actorFlags & Actor::ACTOR_FLAG_TARGETABLE) != 0)
				g_chick11WasActive = 1;
			if (chick10.actorPhase > 0 && g_chick10WasActive != 0 && (chick10.pos.z > 0x47000 || (chick10.actorFlags & Actor::ACTOR_FLAG_TARGETABLE) == 0))
			{
				g_chick10WasActive = 0;
				if ((chick10.actorFlags & Actor::ACTOR_FLAG_ACTIVE) == 0)
					chick10.actorPhase = 0;
				else
					Actor::Kill(&chick10, Actor::KILL_REMOVE_ACTOR);
			}
			if (chick11.actorPhase > 0 && g_chick11WasActive != 0 && (chick11.pos.z > 0x47000 || (chick11.actorFlags & Actor::ACTOR_FLAG_TARGETABLE) == 0))
			{
				g_chick11WasActive = 0;
				if ((chick11.actorFlags & Actor::ACTOR_FLAG_ACTIVE) == 0)
					chick11.actorPhase = 0;
				else
					Actor::Kill(&chick11, Actor::KILL_REMOVE_ACTOR);
			}

			Actor::Toy2Actor& chickQuestActor = Actor::g_creatureActors[12];
			if ((chickQuestActor.actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0)
			{
				chickQuestActor.actorFlags &= ~Actor::ACTOR_FLAG_INTERACTION_REQUESTED;
				if (g_levelObjectiveProgress >= 0)
				{
					AudioManager::PlaySoundEffect(0x76, &chickQuestActor.pos);
					if (g_levelObjectiveProgress == 5)
					{
						Dialogue::Begin(12, 4, "thanks for finding my ^chicks^ buzz! here is a pizza planet ^token^!", -1, 0, 1);
						g_levelObjectiveProgress = -1;
					}
					else
					{
						Dialogue::Begin(12,
							4,
							"hi buzz! if you find my ^five^ missing ^chicks^ and come back and find me, i will give you a pizza planet ^token^.",
							-1,
							0,
							-1);
					}
				}
			}

			Actor::Toy2Actor& eggChallengeActor = Actor::g_creatureActors[13];
			if ((eggChallengeActor.actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0)
			{
				eggChallengeActor.actorFlags &= ~Actor::ACTOR_FLAG_INTERACTION_REQUESTED;
				AudioManager::PlaySoundEffect(0x72, &eggChallengeActor.pos);
				if (HUD::g_challengeState != 0)
				{
					Dialogue::Begin(13, 8, "hurry up, buzz! the ^egg^ is still hatching!", -1, 0, -1);
				}
				else if (g_eggChallengeState != EGG_CHALLENGE_COMPLETE)
				{
					HUD::g_challengeState = 1;
					g_targetEggLiftOffset = 0x4000;
					if (g_eggChallengeState == EGG_CHALLENGE_CHICK)
					{
						AndysHouse::g_raceCheckpointPassCount = 0x96;
						Dialogue::Begin(13,
							8,
							"if you can get to my hatching ^egg^ in time you can keep the ^chick^ that you find in it! come back and see me after you have got "
							"the ^chick^!",
							-1,
							0,
							-1);
					}
					else
					{
						AndysHouse::g_raceCheckpointPassCount = 0x7E;
						Dialogue::Begin(13,
							8,
							"this time the egg will hatch quicker but there is a ^token^ inside it! get to it in time and you can keep the ^token^!",
							-1,
							0,
							2);
					}
				}
			}

			if (HUD::g_challengeState == HUD::CHALLENGE_STATE_WAITING_FOR_CAMERA && Nu3D::Camera::g_viewHistoryInitialized == 0)
				HUD::g_challengeState = HUD::CHALLENGE_STATE_ACTIVE;
			if (HUD::g_challengeState == HUD::CHALLENGE_STATE_ACTIVE && g_hayBaleRideTimer == 0)
			{
				if (g_eggChallengeState == EGG_CHALLENGE_CHICK && Actor::g_creatureActors[6].creatureId == 0)
				{
					g_eggChallengeState = EGG_CHALLENGE_TOKEN;
					HUD::g_challengeState = 0;
					g_targetEggLiftOffset = 0;
					Buzz::Launch(-0x600, 2);
				}
				else if (g_eggChallengeState != EGG_CHALLENGE_CHICK && Collectables::g_tokenStates[2].active == 2)
				{
					g_eggChallengeState = EGG_CHALLENGE_COMPLETE;
					HUD::g_challengeState = 0;
				}
				else
				{
					if (Sector::g_activeSectorIndex == 4)
						AndysHouse::g_raceCheckpointPassCount = 99;
					if (g_framePulseOutputs.sixtyFourTick != 0)
						AndysHouse::g_raceCheckpointPassCount--;
					if (AndysHouse::g_raceCheckpointPassCount < 100)
					{
						AndysHouse::g_raceCheckpointPassCount = 100;
						HUD::g_challengeState = 0;
						if (g_eggChallengeState != EGG_CHALLENGE_CHICK)
							Collectables::Deactivate(2);
						g_targetEggLiftOffset = 0;
					}
				}
			}
			if (HUD::g_challengeState == 0 || g_hayBaleRideTimer != 0)
				Actor::g_creatureActors[6].actorFlags &= ~(Actor::ACTOR_FLAG_TARGETABLE | Actor::ACTOR_FLAG_COLLIDABLE);
			else if (HUD::g_challengeState != HUD::CHALLENGE_STATE_ACTIVE || Nu3D::Camera::g_viewHistoryInitialized == 0)
				Actor::g_creatureActors[6].actorFlags |= Actor::ACTOR_FLAG_TARGETABLE | Actor::ACTOR_FLAG_COLLIDABLE;

			int32_t rocketBootsDialogue = Gadget::g_unlockNodeState < 1 ? 2 : 10;
			Actor::ItemReturnReward(14,
				rocketBootsDialogue,
				"hi buzz! you need ^rocket boots^ to cross over to the shopping cart. find my missing ^arm^, i will let you use the ^rocket boots^!",
				"wow! thanks buzz! thanks for finding my ^arm^ now you can use the ^rocket boots^. they give you great speed for a short amount of time. "
				"they're great for racing around with!",
				"the ^rocket boots^ give you great speed for a short amount of time. they're great for racing around with!",
				0x600,
				0xE00);
			Actor::CollectQuestReward(1, 3, 0xE10, 0x6E0, 0);
			Actor::RotatingHint(31, 11, g_rotatingHintSubtitles);

			if (g_dinoEncounterState == 0 && g_buzzActor.isOnWalkableFloor != 0 && g_buzzActor.posAngles.pos.y >= -0x63B && Sector::g_activeSectorIndex == 4)
			{
				g_dinoEncounterState = 1;
				Dialogue::Begin(0, 5, "ha ha ha ha ... defeat the ^dinosaur^ boss to get a pizza planet ^token^!", -1, 0, -1);
			}
			if (g_dinoEncounterState == 1 && Nu3D::Camera::g_viewHistoryInitialized == 0)
			{
				Actor::g_creatureActors[0].movementData = CreatureBehaviour::g_dinoMovementData + 14;
				g_levelInteractionTimer = 0xB4;
				g_dinoEncounterState = 2;
				Actor::g_creatureActors[0].movementCommandTimer = 0;
				Actor::g_creatureActors[0].creatureRam->initialFacingAngle = 0;
			}
			if (g_dinoEncounterState > 2)
			{
				if (g_dinoEncounterState < 0x78)
					g_dinoEncounterState += Renderer::g_frameDelta;
				else if (g_dinoEncounterState != DINO_ENCOUNTER_REWARD_GIVEN)
				{
					Collectables::Activate(4, 0);
					g_dinoEncounterState = DINO_ENCOUNTER_REWARD_GIVEN;
				}
			}

			ResolveChickObjectCollision(19, 1);
			ResolveChickObjectCollision(20, 1);
			ResolveChickObjectCollision(21, 1);
			Actor::Toy2Actor& buggy = Actor::g_creatureActors[27];
			if (buggy.pos.y < buggy.boundary.y)
				buggy.pos.y = buggy.boundary.y;
			if (g_rocketBootsTimer != 0 && g_buzzActor.posAngles.pos.y < -0xC400 && g_buzzActor.posAngles.pos.y > -0xD000 && g_buzzActor.isOnWalkableFloor != 0)
				Buzz::Launch(-0x780, 2);
			PlayLevelMusic();
		}
	}
}

namespace Toy2
{
	namespace CreatureBehaviour
	{
		// FUNCTION: TOY2 0x00420AF0 [PROVISIONAL]
		void Dino(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			AlsToyBarn::g_dinoTintToggle = (AlsToyBarn::g_dinoTintToggle - 1) & 1;
			if (actor->actorPhase != AlsToyBarn::g_previousDinoPhase)
			{
				AlsToyBarn::g_previousDinoPhase = actor->actorPhase;
				AlsToyBarn::g_dinoTintTimer = 0x3C;
				actor->creatureRam->defenseMode = 4;
				AudioManager::PlaySoundEffect(0x79, &actor->pos);
			}

			if (actor->previousActorPhase != 0)
			{
				if (Sector::g_activeSectorIndex == 4)
				{
					Vector4I particlePosition;
					particlePosition.x = 0x14;
					particlePosition.y = -600;
					particlePosition.z = 0;

					int32_t particleAngle = ((uint16_t)actor->yawAngle + *g_randDatBufferPtr++ - 0x80) & 0xFFF;
					int32_t velocityX = Numerics::g_sinCosLUT[particleAngle] >> 3;
					int32_t velocityZ = Numerics::g_sinCosLUT[(particleAngle + 0x400) & 0xFFF] >> 3;
					Actor::ResolveBoneAttachmentPos(&particlePosition, actor, 1);
					AudioManager::PlaySoundEffect(0x78, &actor->pos);

					int32_t randomValue = *g_randDatBufferPtr;
					g_randDatBufferPtr += 2;
					Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnInstance(
						particlePosition.x, particlePosition.y, particlePosition.z, velocityX, randomValue + 0x200, velocityZ, 0, 0, randomValue - 0x80, 0x55);
					if (actor->previousActorPhase == 0x1E)
					{
						Camera::g_shakeTimer = 0x28;
						particle->renderFlags |= Nu3D::Particles::PARTICLE_INTERACTS_WITH_BUZZ;
					}
				}

				actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
				if (actor->previousActorPhase <= 0)
					actor->previousActorPhase = 0;
			}

			if (AlsToyBarn::g_dinoEncounterState > 1)
			{
				AlsToyBarn::g_dinoTintTimer -= Renderer::g_frameDelta;
				if (AlsToyBarn::g_dinoTintTimer < 0)
				{
					AlsToyBarn::g_dinoTintTimer = 0;
					if (AlsToyBarn::g_dinoEncounterState == AlsToyBarn::DINO_ENCOUNTER_ACTIVE)
						actor->creatureRam->defenseMode = 7;
					actor->useTint = 0;
				}
				else if (AlsToyBarn::g_dinoTintToggle != 0)
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
			}
			else
			{
				actor->useTint = 0;
			}

			if (actor->actorPhase < 10 && AlsToyBarn::g_dinoEncounterState == AlsToyBarn::DINO_ENCOUNTER_ACTIVE)
			{
				actor->movementData = g_dinoMovementData + 58;
				actor->creatureRam->defenseMode = 4;
				actor->actorFlags &= ~Actor::ACTOR_FLAG_DAMAGES_BUZZ;

				int32_t effectX = actor->pos.x + actor->collisionVolumes->offset.x;
				int32_t effectY = actor->pos.y + actor->collisionVolumes->offset.y;
				int32_t effectZ = actor->pos.z + actor->collisionVolumes->offset.z;
				for (int32_t effectCount = 5; effectCount != 0; effectCount--)
				{
					Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(effectX, effectY, effectZ, 0x23, 0xE);
					particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
				}

				Lighting::SpawnLight(effectX, effectY, effectZ, 0xF08000, 0x20, (int32_t)actor);
				AudioManager::PlaySoundEffect(-2, &actor->pos);
				AlsToyBarn::g_dinoEncounterState = AlsToyBarn::DINO_ENCOUNTER_DEFEATED;
				g_hudActorAnimationFrame = 0;
			}

			if (AlsToyBarn::g_dinoEncounterState >= AlsToyBarn::DINO_ENCOUNTER_DEFEATED && Sector::g_activeSectorIndex == 4)
			{
				int32_t yawAngle = actor->yawAngle;
				int32_t sideOffset = Numerics::g_sinCosLUT[(yawAngle - 0x400) & 0xFFF] >> 2;
				int32_t forwardOffset = Numerics::g_sinCosLUT[yawAngle & 0xFFF] >> 2;
				int32_t particleX;
				int32_t particleY;
				int32_t particleZ;
				if (AlsToyBarn::g_dinoTintToggle != 0)
				{
					particleX = actor->pos.x + sideOffset;
					particleY = actor->pos.y - 0x1D00;
					particleZ = actor->pos.z + forwardOffset;
				}
				else
				{
					particleX = actor->pos.x - sideOffset * 3;
					particleY = actor->pos.y - 0x400;
					particleZ = actor->pos.z - forwardOffset * 3;
				}

				if (g_framePulseOutputs.sevenTick != 0)
				{
					Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(particleX, particleY, particleZ, 0x11, 0xA);
					particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
					particle->width = 0x28;
					particle->height = 0x28;
				}

				if ((*g_randDatBufferPtr++ & 3) != 0 && (actor->animationFramePosition & (int32_t)0xFFFF0000) == 0xA0000)
				{
					Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(particleX, particleY, particleZ, 4, 4);
					particle->lifetime = (*g_randDatBufferPtr++ & 0xF) * 2 + 0x18;
				}
			}

			if (AlsToyBarn::g_dinoEncounterState == AlsToyBarn::DINO_ENCOUNTER_ACTIVE)
			{
				g_hudActorAnimationFrame = (actor->actorPhase - 9) * 0x36 / 0x14;
				if (Sector::g_activeSectorIndex == 4)
					HUD::g_slideTimers[HUD::SLIDE_BOSS_STATUS] = 0x5A;
			}
		}

		// FUNCTION: TOY2 0x00420ED0 [MATCHED]
		void Chick(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
			if (actor->previousActorPhase <= 0)
			{
				actor->previousActorPhase = (*g_randDatBufferPtr++ & 0x7F) + 0x3C;
				AudioManager::PlaySoundEffect(0x77, &actor->pos);
			}

			if ((actor->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0 && actor->actorPhase == 0x66)
			{
				g_levelObjectiveProgress++;
				Particles::SpawnCollectSparkle(actor->pos.x, actor->pos.y - 0x2000, actor->pos.z, 0x32);
				AudioManager::PlaySoundEffect(0x77, &actor->pos);
				Actor::Kill(actor, 2);
			}
		}
	}
}
