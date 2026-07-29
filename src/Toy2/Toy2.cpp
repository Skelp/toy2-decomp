#include "Toy2/Toy2.h"
#include "Toy2/D3DApp.h"
#include "Logger.h"
#include "FileUtils.h"
#include "InputManager.h"
#include "DrawingDevice.h"
#include "ModeSelect.h"
#include "Nullsub.h"
#include "SoftwareRenderer.h"
#include "Renderer/Renderer.h"
#include "Renderer/Sprite.h"
#include "SaveManager.h"
#include "Random.h"
#include "Toy2/LevelSelect.h"
#include "Toy2/Buzz.h"
#include "Toy2/Levels.h"
#include "Toy2/MainMenu.h"
#include "Toy2/Actor.h"
#include "Toy2/Collision.h"
#include "Toy2/Collectables.h"

#include "Nu3D/Font.h"
#include "Nu3D/FMV.h"
#include "Nu3D/Link.h"
#include "Nu3D/Viewport.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
#include "AudioManager/AudioManager.h"
#include "NGNLoader/NGNLoader.h"

#include <WINDOWS.H>
#include <STDIO.H>
#include <STRING.H>
#include <DINPUT.H>
#include <LIMITS.H>
#include <MATH.H>

#include <Numerics.h>

namespace Toy2
{
	namespace MoveableObject
	{
		// GLOBAL: TOY2 0x0053C65C
		int32_t g_objectCount;

		// GLOBAL: TOY2 0x0053C670
		int32_t g_contactSoundCooldown;

		// GLOBAL: TOY2 0x0053C680
		State g_objects[10];

		// FUNCTION: TOY2 0x004334D0 [PROVISIONAL]
		void ComputeSegment(int32_t pathRecordType, State* object)
		{
			Levels::RecordData* path = Levels::g_recordData[pathRecordType];
			int32_t pathPoint = object->currentPathPoint;
			int32_t deltaX = path->data[pathPoint + 1].x - path->data[pathPoint].x;
			int32_t deltaZ = path->data[pathPoint + 1].z - path->data[pathPoint].z;

			object->facingAngle = (int16_t)(Nu3D::Math::CartesianToFixedAngle(deltaX, deltaZ) & 0xFFF);

			Vector3I direction;
			direction.x = deltaX;
			direction.y = 0;
			direction.z = deltaZ;
			Nu3D::Math::NormalizeToFixedPoint(&direction, &direction);

			object->directionX = direction.x;
			object->segmentLength = (int16_t)sqrt((float)(deltaX * deltaX + deltaZ * deltaZ));
			object->directionZ = direction.z;
			object->swingState = 0;

			if (object->targetPathPoint < Levels::g_recordData[object->pathRecordType]->recordCount - 2
				&& path->data[pathPoint + 1].x == path->data[pathPoint + 2].x && path->data[pathPoint + 1].z == path->data[pathPoint + 2].z)
			{
				object->swingState = object->segmentLength / 2;
			}
		}

		// FUNCTION: TOY2 0x004335D0 [PROVISIONAL]
		void InitTable(const InitEntry* initTable)
		{
			memset(g_objects, 0, sizeof(g_objects));
			g_contactSoundCooldown = 0;
			g_objectCount = 0;
			State* object = g_objects;

			if (initTable != 0 && initTable->linkIndex != -1)
			{
				do
				{
					int32_t startPathPoint = g_levelFileIndex == 11 && initTable->pathRecordType == 29 ? 2 : 0;

					object->linkIndex = initTable->linkIndex;
					object->platformIndex = initTable->platformIndex;
					object->pathRecordType = initTable->pathRecordType;

					Levels::RecordData* path = Levels::g_recordData[object->pathRecordType];
					object->position.x = path->data[startPathPoint].x << 5;
					object->position.y = path->data[startPathPoint].y << 5;
					object->position.z = path->data[startPathPoint].z << 5;
					object->currentPathPoint = (int16_t)startPathPoint;
					object->targetPathPoint = 0;

					ComputeSegment(object->pathRecordType, object);
					object->pathProgress = 0;

					if (object->linkIndex >= 0)
					{
						Nu3D::Link::SetPositionRawAndCommit(object->linkIndex, object->position.x >> 5, object->position.y >> 5, object->position.z >> 5);
					}

					Platform::SetOrigin(object->platformIndex, object->position.x, object->position.y, object->position.z);
					++g_objectCount;
					++object;
					++initTable;
				} while (initTable->linkIndex != -1);
			}
		}
	}

	// GLOBAL: TOY2 0x004F5F54
	extern const char g_creditsText[] =
		"Congratulations!~~~~You have completed~~~~Toy Story 2!~~~~~~~~~~Traveller's Tales~~Credits~~~~~~"
		"~Game Design and~~Programming~~~^Jon Burton~~~~~3d engine programming~~~^dave dootson~~~~~pc con"
		"version~~~^steve monks~~~~~software renderer~~~^andy holdroyd~~~~~character animation~~~^jeremy "
		"pardon~~~~~character artwork~~~^neil allen~~^dave burton~~^jeremy pardon~~^will thompson~~~~~bac"
		"kground artwork~~~^neil allen~~^dave burton~~^leon warren~~^jeremy pardon~~^barry thompson~~^jam"
		"es cunliffe~~^bev bush~~~~~terrain design~~~^barry thompson~~~~~utility programming~~~^andy hold"
		"royd~~^dave dootson~~^gary ireland~~~~~qa by~~~^arthur parsons~~~~~directed by~~~^Jon Burton~~~~"
		"~~~~~~ACTIVISION credits~~~~~~Senior Producer~~~^Rob Letts~~~~~Associate Producer~~~^William Oer"
		"tel~~~~~VP European Studios~~~^Julian Lynn-Evans~~~~~Executive VP~~Activision Studios~~~^Mitch L"
		"asky~~~~~QA Manager~~~^Marilena Morini~~~~~Senior Test Lead~~~^Marietta Pashayan~~~~~Project Lea"
		"d~~~^Nadine Theuzillot~~~~~PC Compatibility lead~~~^John Fritts~~~~~Test Team~~~^Richard Kurnadi"
		"~~^Kragen Lum~~^Russell Shirley~~^Daniel Ramirez~~^Christian Biermann~~^David Silverman~~^David "
		"Hakim~~^Keith Harris~~^Brian Ulmer~~^Josh Horowitz~~^Nicole Dodd~~^Eric Zimmerman~~^Jenn Spencie"
		"r~~^Peter Muravez~~^Todd D. Jones~~^Chad Bordwell~~^Hector Garcia~~~~~~~~~~Disney Interactive~~C"
		"redits~~~~~~Senior Producer~~~^Dan Winters~~~~~Producer~~~^Peter Wyse~~~~~Lead Designer~~~^Joel "
		"Goodsell~~~~~Original Character~~Design~~~^Jeff Berting~~^Tom Barlow~~~~~Additional Art~~~^Jeff "
		"Berting~~^Tom Barlow~~~~~Assistant Producer~~~^Renee Johnson~~~~~U.K. Production Lead~~~^Nick Br"
		"idger~~~~~Game Dialogue~~~^Peter Wyse~~~~~Additional Dialogue~~~^Renee Johnson~~~~Senior Lead Te"
		"ster~~~^Carlos Schulte~~~~~Lead Tester~~~^Kevin Cope~~~~~Test Team~~~^Patrick Larkin~~^Andre Agu"
		"ilar~~^Bryan Martinez~~^Amir Firozkar~~~~~~~~~~~Music Credits~~~~~~original music score~~~^swall"
		"ow studios~~~~~sound effects~~~^p.c. music~~~~~tt logo music~~~^aaron szpakowski~~~~~~~~~~Specia"
		"l Thanks~~~^Helen Burton~~^John Lasseter~~^Ash Brannon~~^Helene Plotkin~~^Karen Robert Jackson~~"
		"^Katherine Sarafian~~^Kathleen Handy~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~";

	// GLOBAL: TOY2 0x0055A120
	int32_t g_screenMusicStarted;

	namespace HUD
	{
		// GLOBAL: TOY2 0x0052C824
		int16_t g_slideTimers[12];

		// GLOBAL: TOY2 0x0052F2E0
		int16_t g_slideAngles[12];

		// GLOBAL: TOY2 0x0052F2F8
		int32_t g_challengeState;
	}

	namespace AndysHouse
	{
		// STUB: TOY2 0x004171D0
		void Init() {}

		// STUB: TOY2 0x00417680
		void Interactions() {}
	}

	namespace AndysNeighborhood
	{
		// STUB: TOY2 0x00418E50
		void Init() {}

		// STUB: TOY2 0x004190C0
		void Interactions() {}
	}

	namespace BombsAway
	{
		// STUB: TOY2 0x0041A8A0
		void Init() {}

		// STUB: TOY2 0x0041AA10
		void Interactions() {}
	}

	namespace ConstructionYard
	{
		// STUB: TOY2 0x0041C190
		void Init() {}

		// STUB: TOY2 0x0041C640
		void Interactions() {}
	}

	namespace AlleysAndGullies
	{
		// STUB: TOY2 0x0041E390
		void Init() {}

		// STUB: TOY2 0x0041E880
		void Interactions() {}
	}

	namespace SlimeTime
	{
		// GLOBAL: TOY2 0x0052F98C
		int32_t g_previousPhase;
		// GLOBAL: TOY2 0x0052F990
		int32_t g_phaseCutsceneTimer;
		// GLOBAL: TOY2 0x0052F994
		int32_t g_bobAngle;
		// GLOBAL: TOY2 0x0052F998
		int32_t g_attackSoundTimer;
		// GLOBAL: TOY2 0x0052F99C
		int32_t g_actorOutsideArena;
		// GLOBAL: TOY2 0x0052F9A0
		int32_t g_movementCycleTimer;
		// GLOBAL: TOY2 0x0052F9A4
		int32_t g_encounterState;
		// GLOBAL: TOY2 0x0052F9A8
		int32_t g_tintFlashToggle;
		// GLOBAL: TOY2 0x0052F9AC
		int32_t g_unusedStateLimit;
		// GLOBAL: TOY2 0x0052F9B0
		int32_t g_nearbySoundCooldown;
		// GLOBAL: TOY2 0x0052F9B4
		int32_t g_previousCameraYaw;
		// GLOBAL: TOY2 0x0052F9B8
		int32_t g_horizontalMovementDirection;

		// FUNCTION: TOY2 0x0041FFB0 [MATCHED]
		void Init()
		{
			MoveableObject::InitTable(0);
			Collectables::Init(0, 0);

			Actor::Toy2Actor* boss = &Actor::g_creatureActors[0];
			g_previousPhase = boss->actorPhase;
			boss->actorFlags |= Actor::ACTOR_FLAG_BOSS;
			boss->pos.x += 0x80000;
			g_encounterState = 0;
			g_movementCycleTimer = 300;
			g_horizontalMovementDirection = 0x40000;
			g_actorOutsideArena = 0;
			g_attackSoundTimer = 150;
			g_bobAngle = 0;
			g_unusedStateLimit = 0x7FFFFFFF;
			g_previousCameraYaw = 0;
			g_nearbySoundCooldown = 0;
			g_tintFlashToggle = 0;
			g_phaseCutsceneTimer = 0;

			boss->yawAngle = 0xC00;
			boss->creatureRam->boundHalfX = 0x1000;
			boss->creatureRam->boundHalfZ = 0x1000;
		}

		// STUB: TOY2 0x00420060
		void Interactions() {}
	}

	namespace AlsToyBarn
	{
		// STUB: TOY2 0x00421090
		void Init() {}

		// STUB: TOY2 0x00421340
		void Interactions() {}
	}

	namespace AlsSpaceLand
	{
		// STUB: TOY2 0x00423020
		void Init() {}

		// STUB: TOY2 0x00423200
		void Interactions() {}
	}

	namespace BarnEncounter
	{
		// GLOBAL: TOY2 0x0052FBB8
		int32_t g_previousBossPhase;
		// GLOBAL: TOY2 0x0052FBBC
		int32_t g_cameraTargetActorIndex;
		// GLOBAL: TOY2 0x0052FBC0
		int32_t g_phaseTimer;
		// GLOBAL: TOY2 0x0052FBC4
		int32_t g_secondMinionActorIndex;
		// GLOBAL: TOY2 0x0052FBC8
		int32_t g_firstMinionActorIndex;
		// GLOBAL: TOY2 0x0052FBCC
		int32_t g_orbitHeight;
		// GLOBAL: TOY2 0x0052FBD0
		int32_t g_cameraTargetUpdateTimer;
		// GLOBAL: TOY2 0x0052FBD4
		int32_t g_waveActorIndex;
		// GLOBAL: TOY2 0x0052FBE8
		int32_t g_tintFlashToggle;
		// GLOBAL: TOY2 0x0052FBEC
		int32_t g_orbitPosition;
		// GLOBAL: TOY2 0x0052FBF0
		int32_t g_waitingForMinions;
		// GLOBAL: TOY2 0x0052FBF4
		int32_t g_nextMinionPairIndex;
		// GLOBAL: TOY2 0x0052FBF8
		int32_t g_introSoundTimer;
		// GLOBAL: TOY2 0x0052FBFC
		int32_t g_damageSoundCooldown;
		// GLOBAL: TOY2 0x0052FC00
		int32_t g_laserCooldown;
		// GLOBAL: TOY2 0x0052FC04
		int32_t g_encounterState;
		// GLOBAL: TOY2 0x0052FC08
		int32_t g_heightBobAngle;
		// GLOBAL: TOY2 0x0052FC0C
		int32_t g_orbitAngle;

		// FUNCTION: TOY2 0x00424390 [MATCHED]
		void Init()
		{
			MoveableObject::InitTable(0);
			Collectables::Init(0, 0);

			g_previousBossPhase = Actor::g_creatureActors[0].actorPhase;
			g_orbitAngle = 0;
			g_orbitPosition = 0;
			g_heightBobAngle = 0;
			g_orbitHeight = -0x58000;
			g_encounterState = 0;
			g_phaseTimer = 0;
			g_tintFlashToggle = 0;
			g_waveActorIndex = 1;
			g_waitingForMinions = 0;
			g_laserCooldown = 200;
			g_firstMinionActorIndex = 7;
			g_secondMinionActorIndex = 7;
			g_nextMinionPairIndex = 0;
			g_cameraTargetActorIndex = 0;
			g_cameraTargetUpdateTimer = 0;
			g_introSoundTimer = 120;
			g_damageSoundCooldown = 0;
			Actor::g_creatureActors[0].creatureRam->speedTarget = 0;

			for (int32_t actorIndex = 7; actorIndex < 12; actorIndex++)
			{
				Actor::g_creatureActors[actorIndex].respawnDelay = 10000;
				Actor::g_creatureActors[actorIndex].actorPhase = 0;
				Actor::g_creatureActors[actorIndex].scalePivotHeight = 0;
				Actor::g_creatureActors[actorIndex].creatureRam->boundHalfX = Actor::g_creatureActors[6].creatureRam->boundHalfX;
				Actor::g_creatureActors[actorIndex].creatureRam->boundHalfZ = Actor::g_creatureActors[6].creatureRam->boundHalfZ;
			}

			for (int32_t bossActorIndex = 0; bossActorIndex < 12; bossActorIndex++)
			{
				Actor::g_creatureActors[bossActorIndex].actorFlags |= Actor::ACTOR_FLAG_BOSS;
			}
		}

		// STUB: TOY2 0x00424490
		void Interactions() {}
	}

	namespace ElevatorHop
	{
		// STUB: TOY2 0x00425B60
		void Init() {}

		// STUB: TOY2 0x00425F60
		void Interactions() {}
	}

	namespace AlsPenthouse
	{
		// STUB: TOY2 0x00429D70
		void Init() {}

		// STUB: TOY2 0x0042A130
		void Interactions() {}
	}

	namespace EvilEmperorZurg
	{
		// STUB: TOY2 0x0042B3A0
		void Interactions() {}
	}

	namespace Path
	{
		// FUNCTION: TOY2 0x0042C200 [PROVISIONAL]
		void SamplePoint(int32_t pathRecordType, int32_t pathPosition, Vector4I* position)
		{
			int32_t pointIndex = pathPosition / 0x1000;
			int32_t fraction = pathPosition & 0xFFF;

			position->x =
				(Levels::g_recordData[pathRecordType]->data[pointIndex + 1].x - Levels::g_recordData[pathRecordType]->data[pointIndex].x) * fraction / 0x1000
				+ Levels::g_recordData[pathRecordType]->data[pointIndex].x;
			position->y =
				(Levels::g_recordData[pathRecordType]->data[pointIndex + 1].y - Levels::g_recordData[pathRecordType]->data[pointIndex].y) * fraction / 0x1000
				+ Levels::g_recordData[pathRecordType]->data[pointIndex].y;
			position->z =
				(Levels::g_recordData[pathRecordType]->data[pointIndex + 1].z - Levels::g_recordData[pathRecordType]->data[pointIndex].z) * fraction / 0x1000
				+ Levels::g_recordData[pathRecordType]->data[pointIndex].z;
		}
	}

	namespace Platform
	{
		struct PathPlatformState
		{
			int32_t pathPosition;
			int32_t speed;
			int32_t previousX;
			int32_t previousZ;
			int32_t platformIndex;
			int32_t pathRecordType;
			int16_t facingAngle;
			int16_t emitParticles;
			int16_t primaryLinkIndex;
			int16_t secondaryLinkIndex;
		};

		// GLOBAL: TOY2 0x0052FE48
		PathPlatformState g_pathPlatforms[5];

		STATIC_ASSERT(sizeof(PathPlatformState) == 0x20);
		STATIC_ASSERT(offsetof(PathPlatformState, platformIndex) == 0x10);
		STATIC_ASSERT(offsetof(PathPlatformState, facingAngle) == 0x18);
		STATIC_ASSERT(offsetof(PathPlatformState, primaryLinkIndex) == 0x1C);

		// FUNCTION: TOY2 0x0042C2B0 [MATCHED]
		void InitPathPlatform(int32_t pathIndex,
			int32_t platformIndex,
			int32_t pathRecordType,
			int32_t primaryLinkIndex,
			int32_t secondaryLinkIndex,
			int32_t speedLimit,
			int32_t pathPosition,
			int32_t facingAngle)
		{
			g_pathPlatforms[pathIndex].pathPosition = pathPosition;
			g_pathPlatforms[pathIndex].speed = 0;
			g_pathPlatforms[pathIndex].platformIndex = platformIndex;
			g_pathPlatforms[pathIndex].pathRecordType = pathRecordType;
			g_pathPlatforms[pathIndex].primaryLinkIndex = (int16_t)primaryLinkIndex;
			g_pathPlatforms[pathIndex].secondaryLinkIndex = (int16_t)secondaryLinkIndex;
			g_pathPlatforms[pathIndex].facingAngle = (int16_t)facingAngle;
			g_pathPlatforms[pathIndex].emitParticles = 1;
			g_pathPlatforms[pathIndex].previousX = INT_MAX;
			g_pathPlatforms[pathIndex].previousZ = INT_MAX;

			Vector4I position;
			Path::SamplePoint(g_pathPlatforms[pathIndex].pathRecordType, g_pathPlatforms[pathIndex].pathPosition, &position);
			SetOrigin(g_pathPlatforms[pathIndex].platformIndex, position.x << 5, position.y << 5, position.z << 5);
			if (g_pathPlatforms[pathIndex].secondaryLinkIndex != 0)
			{
				Nu3D::Link::SetPositionRawAndCommit(g_pathPlatforms[pathIndex].secondaryLinkIndex, position.x, position.y, position.z);
			}
			Nu3D::Link::SetPositionRawAndCommit(g_pathPlatforms[pathIndex].primaryLinkIndex, position.x, position.y, position.z);
			SetRotationAngles(platformIndex, 0, (int16_t)facingAngle, 0);
			Nu3D::Link::SetRotationRelative8bit(primaryLinkIndex, 0, facingAngle, 0);
			if (secondaryLinkIndex != 0)
			{
				Nu3D::Link::SetRotationRelative8bit(secondaryLinkIndex, 0, facingAngle, 0);
			}
		}
	}

	namespace AirportInfiltration
	{
		// GLOBAL: TOY2 0x004F4668
		Vector3I g_fanParticleVelocities[5] = {
			{ 0, 0, 0x500 },
			{ 0x500, 0, 0 },
			{ 0, 0, -0x500 },
			{ -0x500, 0, 0 },
			{ 0, 0, -0x500 },
		};

		// GLOBAL: TOY2 0x004F46A4
		MoveableObject::InitEntry g_moveableObjectInitTable[] = {
			{ 8, 11, 0 },
			{ -1, 0, 0 },
		};

		// GLOBAL: TOY2 0x004F46C0
		int16_t g_tokenLinkIds[5] = { 49, 50, 52, 48, 51 };

		// GLOBAL: TOY2 0x0052FE38
		int32_t g_slammedPlatformRotation;
		// GLOBAL: TOY2 0x0052FE40
		int32_t g_hiddenCollectiblesVisible;
		// GLOBAL: TOY2 0x0052FEE8
		int32_t g_prospectorState;

		struct HiddenCollectibleState
		{
			int32_t* verticalPosition;
			int32_t savedVerticalPosition;
		};

		struct State
		{
			HiddenCollectibleState hiddenCollectibles[5];
			int32_t prospectorTurnAngle;
			int32_t fanBlend;
			int32_t pilotDialogueState;
			int32_t platform3Rotation;
			int32_t platform4Rotation;
			int32_t fanPhase;
			int32_t prospectorTimer;
			int32_t prospectorActionTimer;
			int32_t oddFanRotation;
			int32_t evenFanRotation;
			int32_t previousPilotPhase;
			int32_t prospectorTargetAngle;
			int32_t prospectorCooldown;
		};

		// GLOBAL: TOY2 0x0052FEEC
		State g_state;

		STATIC_ASSERT(sizeof(HiddenCollectibleState) == 0x8);
		STATIC_ASSERT(sizeof(State) == 0x5C);
		STATIC_ASSERT(offsetof(State, prospectorTurnAngle) == 0x28);
		STATIC_ASSERT(offsetof(State, pilotDialogueState) == 0x30);
		STATIC_ASSERT(offsetof(State, previousPilotPhase) == 0x50);
		STATIC_ASSERT(offsetof(State, prospectorCooldown) == 0x58);

		// FUNCTION: TOY2 0x0042C810 [PROVISIONAL]
		void SpawnFanParticle(const Vector3I* position, int32_t fanIndex)
		{
			if ((fanIndex == 4 ? g_sevenTickPulse : g_sixteenTickPulse) != 0)
			{
				Nu3D::Particles::ParticleInstance* particle =
					Nu3D::Particles::SpawnFromPreset(position->x, position->y, position->z, fanIndex == 4 ? 0x4F : 0x4E, 2);
				particle->velX = g_fanParticleVelocities[fanIndex].x;
				particle->velY = g_fanParticleVelocities[fanIndex].y;
				particle->velZ = g_fanParticleVelocities[fanIndex].z;
				particle->rotSpeed = -0x100;
				particle->groundAlignRot = 0xFFF - g_thirtyTwoTickPhase * 0x40;
			}
		}

		// FUNCTION: TOY2 0x0042C8A0 [MATCHED]
		void InitHiddenCollectibles()
		{
			Levels::RecordData* pickupRecords = Levels::g_recordData[63];
			Collectables::PickupRecord* pickup = reinterpret_cast<Collectables::PickupRecord*>(pickupRecords + 1);
			for (int32_t pickupIndex = 0; pickupIndex < pickupRecords->recordCount; pickup++, pickupIndex++)
			{
				if (pickup->objectIndex == 56)
					g_state.hiddenCollectibles[0].verticalPosition = &pickup->position.y;
				if (pickup->objectIndex == 57)
					g_state.hiddenCollectibles[1].verticalPosition = &pickup->position.y;
				if (pickup->objectIndex == 58)
					g_state.hiddenCollectibles[2].verticalPosition = &pickup->position.y;
				if (pickup->objectIndex == 59)
					g_state.hiddenCollectibles[3].verticalPosition = &pickup->position.y;
				if (pickup->objectIndex == 60)
					g_state.hiddenCollectibles[4].verticalPosition = &pickup->position.y;
			}

			for (int32_t hiddenIndex = 0; hiddenIndex < 5; hiddenIndex++)
			{
				g_state.hiddenCollectibles[hiddenIndex].savedVerticalPosition = *g_state.hiddenCollectibles[hiddenIndex].verticalPosition;
				*g_state.hiddenCollectibles[hiddenIndex].verticalPosition = INT_MIN;
				Nu3D::Link::SetScaleFromFixedOffsets(hiddenIndex + 56, 0, 0, 0);
			}
		}

		// FUNCTION: TOY2 0x0042C930 [MATCHED]
		void Init()
		{
			MoveableObject::InitTable(g_moveableObjectInitTable);
			Collectables::Init(g_tokenLinkIds, 0x40);
			Collectables::Activate(3, 1);
			InitHiddenCollectibles();

			g_state.platform4Rotation = 0;
			g_state.platform3Rotation = 0;
			Platform::SetRotationAngles(4, 0, -0x400, 0);
			g_state.oddFanRotation = 0;
			g_state.evenFanRotation = 0;
			g_state.prospectorTargetAngle = -0x200;
			g_state.prospectorTurnAngle = 0x200;
			g_slammedPlatformRotation = 0;
			g_state.prospectorActionTimer = 200;

			Platform::InitPathPlatform(0, 8, 2, 21, 20, 125, 0, 0x400);
			Platform::InitPathPlatform(1, 10, 4, 25, 24, 125, 0, 0x400);
			Platform::InitPathPlatform(2, 2, 6, 13, 12, 125, 0x8000, -0x400);
			Platform::InitPathPlatform(3, 5, 7, 15, 14, 125, 0, -0xC00);
			Platform::InitPathPlatform(4, 1, 5, 11, 10, 125, 0, -0x638);

			int32_t previousPilotPhase = Actor::g_creatureActors[32].actorPhase;
			RawLoader::CreatureListRam* pilotRam = Actor::g_creatureActors[32].creatureRam;
			g_hiddenCollectiblesVisible = 1;
			g_state.pilotDialogueState = 0;
			g_state.prospectorCooldown = 0;
			g_state.prospectorTimer = 0;
			g_prospectorState = 0;
			g_state.previousPilotPhase = previousPilotPhase;
			pilotRam->boundHalfX = 90;
		}

		// STUB: TOY2 0x0042CA60
		void Interactions() {}
	}

	namespace TarmacTrouble
	{
		// STUB: TOY2 0x0042E600
		void Init() {}

		// STUB: TOY2 0x0042E790
		void Interactions() {}
	}

	namespace FinalShowdown
	{
		// STUB: TOY2 0x0042FAA0
		void Init() {}

		// STUB: TOY2 0x0042FC50
		void Interactions() {}
	}

	// FUNCTION: TOY2 0x00430930 [MATCHED]
	void InitialiseLevel16() {}

	// FUNCTION: TOY2 0x00430950 [MATCHED]
	void InitialiseLevel17() {}

	// FUNCTION: TOY2 0x00430940 [MATCHED]
	void HandleLevel16Interactions() {}

	// FUNCTION: TOY2 0x00430960 [MATCHED]
	void HandleLevel17Interactions() {}

	// FUNCTION: TOY2 0x004A1A50 [MATCHED]
	void InitialiseLevelVariables(int32_t levelIndex)
	{
		switch (levelIndex)
		{
			case 1:
				AndysHouse::Init();
				break;
			case 2:
				AndysNeighborhood::Init();
				break;
			case 3:
				BombsAway::Init();
				break;
			case 4:
				ConstructionYard::Init();
				break;
			case 5:
				AlleysAndGullies::Init();
				break;
			case 6:
				SlimeTime::Init();
				break;
			case 7:
				AlsToyBarn::Init();
				break;
			case 8:
				AlsSpaceLand::Init();
				break;
			case 9:
				BarnEncounter::Init();
				break;
			case 10:
				ElevatorHop::Init();
				break;
			case 11:
				AlsPenthouse::Init();
				break;
			case 12:
				EvilEmperorZurg::Init();
				break;
			case 13:
				AirportInfiltration::Init();
				break;
			case 14:
				TarmacTrouble::Init();
				break;
			case 15:
				FinalShowdown::Init();
				break;
			case 16:
				InitialiseLevel16();
				break;
			case 17:
				InitialiseLevel17();
				break;
		}
	}

	// FUNCTION: TOY2 0x004A1B00 [MATCHED]
	void HandleLevelInteractions(int32_t levelIndex)
	{
		switch (levelIndex)
		{
			case 1:
				AndysHouse::Interactions();
				break;
			case 2:
				AndysNeighborhood::Interactions();
				break;
			case 3:
				BombsAway::Interactions();
				break;
			case 4:
				ConstructionYard::Interactions();
				break;
			case 5:
				AlleysAndGullies::Interactions();
				break;
			case 6:
				SlimeTime::Interactions();
				break;
			case 7:
				AlsToyBarn::Interactions();
				break;
			case 8:
				AlsSpaceLand::Interactions();
				break;
			case 9:
				BarnEncounter::Interactions();
				break;
			case 10:
				ElevatorHop::Interactions();
				break;
			case 11:
				AlsPenthouse::Interactions();
				break;
			case 12:
				EvilEmperorZurg::Interactions();
				break;
			case 13:
				AirportInfiltration::Interactions();
				break;
			case 14:
				TarmacTrouble::Interactions();
				break;
			case 15:
				FinalShowdown::Interactions();
				break;
			case 16:
				HandleLevel16Interactions();
				break;
			case 17:
				HandleLevel17Interactions();
				break;
		}
	}

	// FUNCTION: TOY2 0x0044F840 [MATCHED]
	void ShowModelNode(int32_t creatureIndex, int32_t nodeIndex) { Nu3D::Creature::SetNodeVisibleByIndex(creatureIndex, nodeIndex, 1); }

	// FUNCTION: TOY2 0x0044F860 [MATCHED]
	void HideModelNode(int32_t creatureIndex, int32_t nodeIndex) { Nu3D::Creature::SetNodeVisibleByIndex(creatureIndex, nodeIndex, 0); }

	// FUNCTION: TOY2 0x0049F490 [MATCHED]
	void AdvanceFramePhase() { g_framePhase = (g_framePhase + 1) & 0xF; }

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

	// FUNCTION: TOY2 0x0047D4E0 [PROVISIONAL]
	int32_t* BuildPerspectiveDivideTable(int32_t scale)
	{
		g_perspectiveHalfScale = scale >> 1;
		int32_t fixedScale = scale << 12;
		g_perspectiveScaleFixed = fixedScale;

		int32_t divisor = 0x7fff;
		do
		{
			g_perspectiveDivideTable[divisor] = fixedScale / divisor;
		} while (--divisor != 0);

		return g_perspectiveDivideTable;
	}

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
	DevDraw::DrawBuffer* drawb;

	// GLOBAL: TOY2 0x00500A14
	DevDraw::TransparentDrawBuffer* drawtranb;

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

	// GLOBAL: TOY2 0x0072E34C
	int32_t g_mpegPlaybackDisabled;

	// GLOBAL: TOY2 0x0072EF94
	int32_t g_movieTimingRate;

	// GLOBAL: TOY2 0x0072EFB0
	uint32_t g_movieTimingStartMs;

	// GLOBAL: TOY2 0x00731F0C
	uint32_t g_cpuClockHz;

	// GLOBAL: TOY2 0x0052AD9C
	int32_t g_returnedToTitle;

	// GLOBAL: TOY2 0x0052ADA0
	int32_t g_attractModeTimer;

	// GLOBAL: TOY2 0x00A4C454
	int32_t g_isElevatorHopLevel;

	// GLOBAL: TOY2 0x005D2A90
	int32_t g_hasBackdrop;

	// GLOBAL: TOY2 0x004F7280
	int32_t g_showBlackFrames = 1;

	// GLOBAL: TOY2 0x004FCDB4
	int32_t g_cdBaseTrack = 2;

	// GLOBAL: TOY2 0x005281C8
	int32_t g_modeSelectFinished;

	// GLOBAL: TOY2 0x0052ADA4
	int32_t g_unused1;

	// GLOBAL: TOY2 0x0052ADA8
	int32_t g_unused2;

	// GLOBAL: TOY2 0x0072EFD8
	int32_t g_pastInitialBoot;

	// GLOBAL: TOY2 0x0052ADB4
	int32_t g_attractModeInputTimer;

	// GLOBAL: TOY2 0x0052C83C
	int32_t g_curDemoLevel;

	// GLOBAL: TOY2 0x0052B7DC
	int16_t g_levelTransition;

	// GLOBAL: TOY2 0x0052B7D8
	int32_t g_levelObjectiveProgress;

	// GLOBAL: TOY2 0x0052B816
	uint16_t g_gameplayStateFlags;

	// GLOBAL: TOY2 0x0052AD94
	int32_t g_demoMode;

	// GLOBAL: TOY2 0x0052F1C4
	uint8_t g_fourTickPulse;
	// GLOBAL: TOY2 0x0052F1C7
	uint8_t g_sevenTickPulse;

	// GLOBAL: TOY2 0x0052F1C9
	uint8_t g_sixteenTickPulse;

	// GLOBAL: TOY2 0x0052F1C2
	uint8_t g_twoTickPulseCount;

	// GLOBAL: TOY2 0x0052AD68
	uint16_t g_framePhase;

	// GLOBAL: TOY2 0x0052AD61
	uint8_t g_sixteenTickPhase;

	// GLOBAL: TOY2 0x0052AD62
	uint8_t g_thirtyTwoTickPhase;

	// GLOBAL: TOY2 0x0055A0E0
	int32_t g_hasStaticBackdrop;

	// GLOBAL: TOY2 0x00500A50
	int32_t g_nextBackdropId = 36;

	// GLOBAL: TOY2 0x00830C88
	int32_t g_mainMenuState;

	// GLOBAL: TOY2 0x00830D58
	int32_t g_saveLoaded;

	// GLOBAL: TOY2 0x00503840
	int16_t g_levelTokenTarget[16];

	// GLOBAL: TOY2 0x00503AD4
	char g_pathBinName[16] = "PAD\\PATH00.BIN";

	// GLOBAL: TOY2 0x0052AD8A
	int16_t g_levelIndex;

	// GLOBAL: TOY2 0x0050268C
	int32_t g_levelFileConversion[15] = { 1, 2, 6, 4, 5, 3, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

	// GLOBAL: TOY2 0x0052AD7C
	int32_t g_demoPathWriteIdx;

	// GLOBAL: TOY2 0x00882920
	int32_t g_gravityBootsTimer;

	// GLOBAL: TOY2 0x00882928
	int32_t g_gravityBootsHoverHeight;

	// GLOBAL: TOY2 0x00882938
	int32_t g_grappleCharges;

	// GLOBAL: TOY2 0x00882950
	int32_t g_grappleState;

	// GLOBAL: TOY2 0x0052EF40
	int32_t g_demoInputRunLength;

	// GLOBAL: TOY2 0x0052B818
	int16_t g_isPaused;

	// GLOBAL: TOY2 0x0052B820
	int16_t g_demoInputBuffer[2048];

	// GLOBAL: TOY2 0x00529E48
	int16_t g_pauseMenuBlinkTimer;

	// GLOBAL: TOY2 0x0052F0D7
	uint8_t g_levelTokenBits[16];

	// GLOBAL: TOY2 0x0052F0E7
	uint8_t g_movieUnlocked[19];

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
	int32_t g_unused0;

	// GLOBAL: TOY2 0x00731F18
	int32_t g_saveMenuState;

	int32_t TickSaveMenuMachine(int32_t param);
	int32_t MovieViewerTick(int32_t movieIdx);
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

	namespace Game
	{
		// STUB: TOY2 0x00406CD0
		void InitActor(Actor::Toy2Actor* actor, int32_t param) {}

		// STUB: TOY2 0x0049E330
		void PauseLoop() {}

		// STUB: TOY2 0x0049DFE0
		void MainLoop() {}
	}

	namespace PostGameRecap
	{
		// STUB: TOY2 0x004398B0
		void Tick() {}
	}

	namespace PostGameSaveMenu
	{
		// STUB: TOY2 0x0043A130
		int32_t Tick() { return 0; }
	}

	namespace GameOver
	{
		// FUNCTION: TOY2 0x00437B20 [MATCHED]
		void Tick()
		{
			InputManager::g_curButtonsPressed = 0;
			InputManager::g_prevButtonsPressed = 0;
			MainMenu::g_fadeTimer = 0;
			MainMenu::g_nextScreen = 0;
			Nu3D::Camera::g_cameraTintBlue = 0;
			Nu3D::Camera::g_cameraTintGreen = 0;
			Nu3D::Camera::g_cameraTintRed = 0;
			Nu3D::Camera::SetTint(128, 128, 128, 12);
			SoftwareRenderer::SetBackdropScrollOverride(0, 0);
			Renderer::g_frameDelta = 1;
			SetBackdropByIndex(1);

			int32_t exitTimer = 0x4b0;
			AudioManager::PlayMusicOneShot(0x11);

			while (true)
			{
				Nu3D::Camera::FadeToTargetTint();
				MainMenu::RenderMenu();

				if (exitTimer > 0)
				{
					exitTimer -= Renderer::g_frameDelta;
					if (exitTimer <= 0)
						exitTimer = 0;
				}

				if (AudioManager::IsStreamActive() == 0)
				{
					if (exitTimer > 0x17)
						exitTimer = 0x17;
				}
				else if (exitTimer > 0x17)
				{
					goto skip_tint;
				}
				if (Renderer::g_frameDelta + exitTimer > 0x17)
				{
					Nu3D::Camera::SetTint(0, 0, 0, 12);
				}
			skip_tint:
				if (g_attractModeTimer >= 0 && (InputManager::g_curButtonsPressed & 1))
				{
					InputManager::g_curButtonsPressed |= 0x4000;
				}
				if ((InputManager::g_curButtonsPressed & 0xf000) == 0 || (InputManager::g_prevButtonsPressed & 0xf000) != 0 || exitTimer >= 0x474
					|| exitTimer <= 0x17)
				{
					if (exitTimer == 0)
					{
						AudioManager::StopAndWait();
						return;
					}
				}
				else
				{
					exitTimer = 0x18;
				}
			}
		}
	}

	// STUB: TOY2 0x00440F70
	void RenderGame(int32_t fullRender) {}

	// FUNCTION: TOY2 0x00453CA0 [MATCHED]
	void ResetBackdropState()
	{
		g_hasStaticBackdrop = 0;
		Renderer::g_virtualScreenWidth = 512.0f;
		Renderer::g_virtualScreenHeight = 256.0f;
		g_nextBackdropId = 36;
	}

	// FUNCTION: TOY2 0x00454020 [MATCHED]
	void ShowPostGameSaveMenu()
	{
		int32_t prevLevelFileIdx = g_levelFileIndex;

		g_levelFileIndex = 16;

		if (g_saveMenuState)
		{
			Levels::g_levelLoadConfig = 0xf8;
			g_hasStaticBackdrop = 0;
			Renderer::g_virtualScreenWidth = 512.0;
			Renderer::g_virtualScreenHeight = 256.0;
			g_nextBackdropId = 36;
			Levels::InitLevelPlay(16);

			int32_t result = PostGameSaveMenu::Tick();
			g_saveMenuState = result;

			if (result)
			{
				g_saveMenuState = 1;
				SaveManager::TransferProgressData(&SaveManager::g_save0Data);
				TickSaveMenuMachine(1);
				SaveManager::LoadProgressData(&SaveManager::g_save0Data);
				g_saveMenuState = 0;
			}
		}

		g_levelFileIndex = prevLevelFileIdx;
	}

	// STUB: TOY2 0x004500A0
	void LoadLevelGraphics(int32_t levelFileIndex) {}

	// FUNCTION: TOY2 0x00414270 [MATCHED]
	void LoadLevelWithFadeIn(int32_t levelFileIndex, int32_t displayMode)
	{
		if (displayMode != 0 && displayMode != 123)
		{
			LoadLevelGraphics(0);
		}
		else
		{
			LoadLevelGraphics(levelFileIndex);
		}

		SoftwareRenderer::g_backdropScrollOverride.x = 0;
		SoftwareRenderer::g_backdropScrollOverride.y = 0;
		Nu3D::Camera::SetTint(128, 128, 128, 12);

		int32_t fadeTimer = 28;
		do
		{
			fadeTimer -= Renderer::g_frameDelta;
			if (fadeTimer <= 0)
			{
				fadeTimer = 0;
			}
			Nu3D::Camera::FadeToTargetTint();
			if (displayMode == 0)
			{
				Renderer::Sprite::DrawScaled(96, 200, 128, 1, 255, 255, 255, 255, 2048, 2048);
			}
			Nullsub3();
			MainMenu::RenderMenu();
		} while (fadeTimer != 0);
	}

	// FUNCTION: TOY2 0x00414320 [MATCHED]
	int32_t ShowLevelIntroScreen(int32_t backgroundId, int32_t displayMode)
	{
		int32_t result = 0;
		int32_t autoAdvanceTimer;
		int32_t fadeTimer;
		int32_t isFadingOut;
		int32_t slidePhase;
		int32_t blinkTimer;

		if (displayMode != 0 && displayMode != 123)
		{
			Nu3D::Camera::SetTint(0, 0, 0, 12);
			Nu3D::Camera::g_cameraTintBlue = Nu3D::Camera::g_targetTintBlue;
			Nu3D::Camera::g_cameraTintGreen = Nu3D::Camera::g_targetTintGreen;
			Nu3D::Camera::g_cameraTintRed = Nu3D::Camera::g_targetTintRed;
			Nu3D::Camera::g_targetTintFadeSpeed = 0;
			goto cleanup;
		}

		if (g_attractModeTimer >= 0)
		{
			autoAdvanceTimer = g_attractModeTimer * 2;
		}
		else
		{
			autoAdvanceTimer = -1;
		}

		if (displayMode != 0 && displayMode != 123)
		{
			Nu3D::Camera::SetTint(0, 0, 0, 12);
			slidePhase = 0x400;
			isFadingOut = 1;
		}
		else
		{
			isFadingOut = 0;
			slidePhase = 0;
		}

		blinkTimer = 0;
		SoftwareRenderer::g_backdropScrollOverride.x = 0;
		SoftwareRenderer::g_backdropScrollOverride.y = 0;
		fadeTimer = 28;

		do
		{
			if (isFadingOut)
			{
				fadeTimer -= Renderer::g_frameDelta;
				if (fadeTimer <= 0)
				{
					fadeTimer = 0;
				}
			}

			if (autoAdvanceTimer > 0)
			{
				autoAdvanceTimer -= Renderer::g_frameDelta;
				if (autoAdvanceTimer <= 0)
				{
					InputManager::g_curButtonsPressed |= INPUT_SECRET_MENU;
					autoAdvanceTimer = 0;
				}
			}

			if ((InputManager::g_curButtonsPressed & INPUT_JUMP) != 0 && (InputManager::g_prevButtonsPressed & INPUT_JUMP) == 0 && ! isFadingOut)
			{
				Nu3D::Camera::SetTint(0, 0, 0, 12);
				isFadingOut = 1;
				AudioManager::PlaySoundEffect(7, 0);
			}

			if ((InputManager::g_curButtonsPressed & INPUT_SECRET_MENU) != 0 && ! isFadingOut && g_attractModeTimer >= 0)
			{
				Nu3D::Camera::SetTint(0, 0, 0, 12);
				isFadingOut = 1;
				result = 1;
			}

			Nu3D::Camera::FadeToTargetTint();
			blinkTimer = (blinkTimer + Renderer::g_frameDelta) & 0x3f;
			if (blinkTimer > 30)
			{
				Renderer::Sprite::DrawScaled(96, 200, 128, 0, 255, 255, 255, 255, 2048, 2048);
			}

			if (slidePhase < 0x400 && displayMode != 123)
			{
				slidePhase += Renderer::g_frameDelta * 32;
				Renderer::Sprite::DrawScaled(96, 264 - (Numerics::g_sinCosLUT[slidePhase + 0x400] >> 8), 128, 1, 255, 255, 255, 255, 2048, 2048);
			}

			Nullsub3();
			MainMenu::RenderMenu();
		} while (fadeTimer != 0);

	cleanup:
		ResetBackdropState();
		SoftwareRenderer::g_backdropScrollOverride.x = -32768;
		SoftwareRenderer::g_backdropScrollOverride.y = -32768;
		return result;
	}

	// FUNCTION: TOY2 0x00453D90 [MATCHED]
	void ShowActClearScreen()
	{
		int32_t previousLevelFileIndex = g_levelFileIndex;
		g_levelFileIndex += 32;
		Renderer::g_virtualScreenWidth = 320.0f;
		Renderer::g_virtualScreenHeight = 256.0f;
		LoadLevelGraphics(g_levelFileIndex);
		SoftwareRenderer::g_backdropScrollOverride.x = 0;
		SoftwareRenderer::g_backdropScrollOverride.y = 0;
		Nu3D::Camera::SetTint(128, 128, 128, 12);
		int32_t fadeTimer = 28;
		AudioManager::PlayMusicOneShot(21);

		do
		{
			fadeTimer -= Renderer::g_frameDelta;
			if (fadeTimer <= 0)
				fadeTimer = 0;
			Nu3D::Camera::FadeToTargetTint();
			MainMenu::RenderMenu();
		} while (fadeTimer != 0);

		int32_t isFadingOut = 0;
		int32_t blinkTimer = 0;
		SoftwareRenderer::g_backdropScrollOverride.x = 0;
		SoftwareRenderer::g_backdropScrollOverride.y = 0;
		fadeTimer = 28;
		do
		{
			Nu3D::Camera::FadeToTargetTint();
			if (isFadingOut)
			{
				fadeTimer -= Renderer::g_frameDelta;
				if (fadeTimer <= 0)
					fadeTimer = 0;
			}

			if ((InputManager::g_curButtonsPressed & INPUT_JUMP) != 0 && (InputManager::g_prevButtonsPressed & INPUT_JUMP) == 0 && ! isFadingOut)
			{
				Nu3D::Camera::SetTint(0, 0, 0, 12);
				isFadingOut = 1;
				AudioManager::PlaySoundEffect(7, 0);
			}

			blinkTimer = (blinkTimer + Renderer::g_frameDelta) & 0x3f;
			if (blinkTimer > 30)
				Renderer::Sprite::DrawScaled(96, 200, 128, 0, 255, 255, 255, 255, 2048, 2048);
			MainMenu::RenderMenu();
		} while (fadeTimer != 0);

		AudioManager::StopAndWait();
		g_hasStaticBackdrop = 0;
		SoftwareRenderer::g_backdropScrollOverride.x = -32768;
		SoftwareRenderer::g_backdropScrollOverride.y = -32768;
		Renderer::g_virtualScreenWidth = 512.0f;
		Renderer::g_virtualScreenHeight = 256.0f;
		g_nextBackdropId = 36;
		g_saveMenuState = 1;
		g_levelFileIndex = previousLevelFileIndex;
	}

	// FUNCTION: TOY2 0x0049EB50 [PROVISIONAL]
	int32_t ComputeTokenProgress()
	{
		int32_t collected = 0;
		int32_t levelCount = 0;
		for (int32_t i = 0; i < 15; i++)
		{
			int32_t bits = g_levelTokenBits[g_levelFileConversion[i]];
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

	// STUB: TOY2 0x00414720
	int32_t EnterLevel(int32_t levelIndex) { return 0; }

	// FUNCTION: TOY2 0x004A3770 [MATCHED]
	void LoadPathBin()
	{
		int32_t levelFile = g_levelFileIndex;
		int32_t tens = levelFile / 10;
		g_pathBinName[8] = (char)(tens + '0');
		g_pathBinName[9] = (char)(levelFile - tens * 10 + '0');
		FileUtils::LoadFile(g_pathBinName, g_demoInputBuffer);
	}

	// FUNCTION: TOY2 0x00453CF0 [MATCHED]
	int32_t ShowLevelSelect()
	{
		int32_t prevLevelFileIdx = Toy2::g_levelFileIndex;

		g_levelFileIndex = 16;
		Levels::g_levelLoadConfig = 56;

		g_hasStaticBackdrop = 0;
		Renderer::g_virtualScreenWidth = 512.0;
		Renderer::g_virtualScreenHeight = 256.0;
		g_nextBackdropId = 36;
		Levels::InitLevelPlay(16);

		Renderer::g_frameDelta = 2;
		g_hasBackdrop = 0;
		MainMenu::g_menuClearColor.b = 140;
		MainMenu::g_menuClearColor.g = 140;
		MainMenu::g_menuClearColor.r = 140;

		int32_t newState = LevelSelect::Tick();

		g_levelFileIndex = prevLevelFileIdx;

		MainMenu::g_menuClearColor.b = 32;
		MainMenu::g_menuClearColor.g = 32;
		MainMenu::g_menuClearColor.r = 32;

		return newState;
	}

	// STUB: TOY2 0x0043A600
	int32_t MovieViewerTick(int32_t movieIdx) { return -1; }

	// FUNCTION: TOY2 0x00453FA0 [MATCHED]
	void ShowMovieViewer()
	{
		int32_t prevLevelFileIdx = g_levelFileIndex;

		g_levelFileIndex = 16;

		int32_t movieIdx = 0;
		while (true)
		{
			Levels::g_levelLoadConfig = 0xb8;
			Renderer::g_virtualScreenWidth = 320.0;
			Renderer::g_virtualScreenHeight = 256.0;
			Levels::InitLevelPlay(g_levelFileIndex);

			MainMenu::g_menuClearColor.b = 0;
			MainMenu::g_menuClearColor.g = 0;
			MainMenu::g_menuClearColor.r = 0;
			movieIdx = MovieViewerTick(movieIdx);

			if (movieIdx < 0)
				break;

			PlayMovieWithTransition(movieIdx + 10, 0);
		}

		g_levelFileIndex = prevLevelFileIdx;
	}

	// STUB: TOY2 0x0049B9E0
	int32_t TickSaveMenuMachine(int32_t param) { return 0; }

	// FUNCTION: TOY2 0x00453F20 [MATCHED]
	int32_t ShowSaveScreen()
	{
		int32_t prevLevelFileIdx = g_levelFileIndex;

		g_levelFileIndex = 16;
		Levels::g_levelLoadConfig = 0xf8;
		g_hasStaticBackdrop = 0;
		Renderer::g_virtualScreenWidth = 512.0;
		Renderer::g_virtualScreenHeight = 256.0;
		g_nextBackdropId = 36;
		Levels::InitLevelPlay(16);

		g_saveMenuState = 0;
		SaveManager::TransferProgressData(&SaveManager::g_save0Data);
		int32_t result = TickSaveMenuMachine(0);
		SaveManager::LoadProgressData(&SaveManager::g_save0Data);

		g_levelFileIndex = prevLevelFileIdx;
		return result;
	}

	// FUNCTION: TOY2 0x0049EB20 [MATCHED]
	void UnlockAndPlayMovie(int32_t movieId, int32_t backgroundId, int32_t forcePlay)
	{
		if (! g_movieUnlocked[movieId] || forcePlay)
		{
			g_movieUnlocked[movieId] = 1;
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
		int32_t sectorIdx = index + 1;
		Renderer::g_parallaxCurHorizScroll = 0.0;

		if (index + 1 >= 0 && sectorIdx < 9)
		{
			if (NGNLoader::GetTextureDataIndex(g_sectorBackdropTexTable.primary[sectorIdx]))
			{
				g_nextBackdropId = g_sectorBackdropTexTable.primary[sectorIdx];
				g_hasBackdrop = 1;
			}
			else
			{
				uint32_t l_textureId = g_sectorBackdropTexTable.secondary[sectorIdx - 1];
				int32_t* l_id = &g_sectorBackdropTexTable.secondary[sectorIdx - 1];

				if (NGNLoader::GetTextureDataIndex(l_textureId))
					g_nextBackdropId = *l_id;

				g_hasBackdrop = 1;
			}
		}
	}

	// FUNCTION: TOY2 0x00438520 [MATCHED]
	int32_t ShowStaticScreen(int32_t backdropIndex)
	{
		InputManager::g_curButtonsPressed = 0;
		InputManager::g_prevButtonsPressed = 0;
		MainMenu::g_fadeTimer = 0;
		MainMenu::g_nextScreen = 0;
		Nu3D::Camera::g_cameraTintBlue = 0;
		Nu3D::Camera::g_cameraTintGreen = 0;
		Nu3D::Camera::g_cameraTintRed = 0;
		Nu3D::Camera::SetTint(128, 128, 128, 12);
		SoftwareRenderer::SetBackdropScrollOverride(0, 0);
		Renderer::g_frameDelta = 1;
		SetBackdropByIndex(backdropIndex);

		int32_t result = 0;
		int32_t fadeFrames = 600;
		int32_t threshold = ((int16_t)g_pastInitialBoot != 0) ? 600 : 300;

		while (true)
		{
			Nu3D::Camera::FadeToTargetTint();
			SoftwareRenderer::SetBackdropScrollOverride(0, 0);
			Nullsub3();
			MainMenu::RenderMenu();

			if (fadeFrames > 0)
			{
				fadeFrames -= Renderer::g_frameDelta;
				if (fadeFrames <= 0)
					fadeFrames = 0;
			}

			if (fadeFrames <= 0x17)
			{
				if (Renderer::g_frameDelta + fadeFrames > 0x17)
					Nu3D::Camera::SetTint(0, 0, 0, 12);
			}

			if (g_attractModeTimer >= 0 && (InputManager::g_curButtonsPressed & 1) && fadeFrames > 0x17)
			{
				fadeFrames = 0x18;
				result = 1;
			}

			if ((InputManager::g_curButtonsPressed & 0xf000) != 0 && (InputManager::g_prevButtonsPressed & 0xf000) == 0 && fadeFrames < threshold
				&& fadeFrames > 0x17)
			{
				fadeFrames = 0x18;
			}
			else if (fadeFrames <= 0)
			{
				return result;
			}
		}
	}

	// FUNCTION: TOY2 0x0043A380 [PROVISIONAL]
	int32_t ShowCredits()
	{
		const int32_t lineCount = 40;
		const int32_t lineLength = 64;
		char lines[lineCount][lineLength];
		const char* creditsCursor = g_creditsText;
		int32_t scrollPosition = 0;
		int32_t loadedLineCount = 0;

		for (int32_t line = 0; line < lineCount; ++line)
			lines[line][0] = '\0';

		InputManager::g_curButtonsPressed = 0;
		InputManager::g_prevButtonsPressed = 0;
		MainMenu::g_fadeTimer = 0;
		MainMenu::g_nextScreen = 0;
		Nu3D::Camera::g_cameraTintBlue = 0;
		Nu3D::Camera::g_cameraTintGreen = 0;
		Nu3D::Camera::g_cameraTintRed = 0;
		Nu3D::Camera::SetTint(128, 128, 128, 12);
		SoftwareRenderer::SetBackdropScrollOverride(0, 0);
		Renderer::g_frameDelta = 1;
		SetBackdropByIndex(0);
		MainMenu::g_menuClearColor.b = 0;
		MainMenu::g_menuClearColor.g = 0;
		MainMenu::g_menuClearColor.r = 0;

		int32_t creditsTimer = 4000;
		AudioManager::PlayMusicLooping(AudioManager::MUSIC_TRACK_CREDITS);
		g_screenMusicStarted = 1;
		Nu3D::Camera::SetTint(128, 128, 128, 6);

		int32_t backdropFramesRemaining = 600;
		int32_t backdropIndex = 0;

		do
		{
			Nu3D::Camera::FadeToTargetTint();

			int32_t visibleLineCount = scrollPosition / 128;
			if (visibleLineCount > loadedLineCount)
			{
				int32_t linesToLoad = visibleLineCount - loadedLineCount;
				int32_t ringIndex = loadedLineCount + 2;
				loadedLineCount += linesToLoad;

				do
				{
					int32_t characterIndex = 0;
					while (*creditsCursor != '~' && *creditsCursor != '\0')
					{
						lines[ringIndex % lineCount][characterIndex++] = *creditsCursor++;
					}

					if (*creditsCursor == '\0')
					{
						lines[ringIndex % lineCount][characterIndex] = '\0';
					}
					else
					{
						++creditsCursor;
						lines[ringIndex % lineCount][characterIndex] = '\0';
					}
					++ringIndex;
				} while (--linesToLoad != 0);
			}

			SoftwareRenderer::SetBackdropScrollOverride(0, 0);
			int32_t scrollPhase = (scrollPosition / 16) % 320;
			char* line = lines[0];
			for (int32_t lineY = 320; lineY < 640; lineY += 8)
			{
				Renderer::Sprite::DrawWhiteText(line, ((lineY - scrollPhase) % 320) - 33, 160);
				line += lineLength;
			}

			Renderer::Sprite::DrawBackdropTransition(&backdropFramesRemaining, &backdropIndex, 600);
			Nullsub6();
			scrollPosition += Renderer::g_frameDelta * 8;

			if (creditsTimer > 0 && creditsTimer < 1000)
				creditsTimer -= Renderer::g_frameDelta;

			if (((InputManager::g_curButtonsPressed & 0xf000) != 0 && (InputManager::g_prevButtonsPressed & 0xf000) == 0 || *creditsCursor == '\0')
				&& creditsTimer > 53 && Nu3D::Camera::g_cameraTintBlue == 128)
			{
				creditsTimer = 53;
				Nu3D::Camera::SetTint(0, 0, 0, 6);
			}

			MainMenu::RenderMenu();
		} while (creditsTimer > 0);

		AudioManager::StopAndWait();
		MainMenu::g_menuClearColor.b = 32;
		MainMenu::g_menuClearColor.g = 32;
		MainMenu::g_menuClearColor.r = 32;
		return 32;
	}

	// FUNCTION: TOY2 0x004381F0 [PROVISIONAL]
	int32_t ScreenDispatcher(int32_t index)
	{
		int32_t defaultFadeFramesRemaining;

		switch (index)
		{
			case 1: {
				int32_t caseOneFadeFrames = 160;
				InputManager::g_curButtonsPressed = 0;
				InputManager::g_prevButtonsPressed = 0;

				MainMenu::g_fadeTimer = 0;
				MainMenu::g_nextScreen = 0;

				Nu3D::Camera::g_cameraTintBlue = 0;
				Nu3D::Camera::g_cameraTintGreen = 0;
				Nu3D::Camera::g_cameraTintRed = 0;

				Nu3D::Camera::SetTint(128, 128, 128, 12);
				SoftwareRenderer::SetBackdropScrollOverride(0, 0);
				Renderer::g_frameDelta = 1;

				SetBackdropByIndex(1);

				while (true)
				{
					Nu3D::Camera::FadeToTargetTint();
					caseOneFadeFrames -= Renderer::g_frameDelta;

					if (caseOneFadeFrames <= 0)
						break;

					if (caseOneFadeFrames <= 23)
					{
						if ((caseOneFadeFrames + Renderer::g_frameDelta) > 23)
							Nu3D::Camera::SetTint(0, 0, 0, 12);
					}

				LBL_CHECK_FADE_COMPLETE:

					if (! caseOneFadeFrames)
						return 1;
				}

				caseOneFadeFrames = 0;

				if ((caseOneFadeFrames + Renderer::g_frameDelta) > 23)
					Nu3D::Camera::SetTint(0, 0, 0, 12);

				goto LBL_CHECK_FADE_COMPLETE;
			}

			case 2:
				g_mainMenuState = MainMenu::Tick();
				return 1;

			case 4:
				PostGameRecap::Tick();
				return 1;

			case 5:
				GameOver::Tick();
				return 1;

			case 6: {
				if (! g_returnedToTitle && g_attractModeTimer >= 0)
				{
					int32_t caseSixFadeFrames = 2 * g_attractModeTimer;
					InputManager::g_curButtonsPressed = 0;
					InputManager::g_prevButtonsPressed = 0;

					MainMenu::g_fadeTimer = 0;
					MainMenu::g_nextScreen = 0;

					Nu3D::Camera::g_cameraTintBlue = 0;
					Nu3D::Camera::g_cameraTintGreen = 0;
					Nu3D::Camera::g_cameraTintRed = 0;

					Nu3D::Camera::SetTint(128, 128, 128, 12);
					SoftwareRenderer::SetBackdropScrollOverride(0, 0);
					Renderer::g_frameDelta = 1;
					SetBackdropByIndex(0);

					int32_t skipInputThreshold = caseSixFadeFrames - 120;

					if (! caseSixFadeFrames)
						return 1;

					while (true)
					{
						Nu3D::Camera::FadeToTargetTint();

						if (caseSixFadeFrames > 0)
						{
							caseSixFadeFrames -= Renderer::g_frameDelta;

							if (caseSixFadeFrames <= 0)
								break;
						}

						if (caseSixFadeFrames)
						{
							if ((caseSixFadeFrames + Renderer::g_frameDelta) > 23)
								Nu3D::Camera::SetTint(0, 0, 0, 12);
						}

					LBL_CHECK_OR_COMPLETE:

						if ((InputManager::g_curButtonsPressed & (INPUT_CANCEL | INPUT_SPIN | INPUT_JUMP | INPUT_FIRE)) != 0
							&& caseSixFadeFrames < skipInputThreshold && caseSixFadeFrames > 23)
						{
							caseSixFadeFrames = 24;
						}
						else if (! caseSixFadeFrames)
						{
							return 1;
						}
					}

					caseSixFadeFrames = 0;

					if ((caseSixFadeFrames + Renderer::g_frameDelta) > 23)
						Nu3D::Camera::SetTint(0, 0, 0, 12);

					goto LBL_CHECK_OR_COMPLETE;
				}

				defaultFadeFramesRemaining = 600;

				InputManager::g_curButtonsPressed = 0;
				InputManager::g_prevButtonsPressed = 0;

				MainMenu::g_fadeTimer = 0;
				MainMenu::g_nextScreen = 0;

				Nu3D::Camera::g_cameraTintBlue = 0;
				Nu3D::Camera::g_cameraTintGreen = 0;
				Nu3D::Camera::g_cameraTintRed = 0;

				Nu3D::Camera::SetTint(128, 128, 128, 12);
				SoftwareRenderer::SetBackdropScrollOverride(0, 0);
				Renderer::g_frameDelta = 1;

				SetBackdropByIndex(0);
				break;
			}

			case 8:
				g_mainMenuState = MainMenu::g_nextScreen - 1;
				return 1;

			case 9:
				MainMenu::ShowSettings();
				return 1;

			case 10:
				ShowStaticScreen(2);
				ShowStaticScreen(3);
				return 1;

			case 11:
				ShowCredits();
				return 1;

			default:
				return 1;
		}

		do
		{
			Nu3D::Camera::FadeToTargetTint();

			defaultFadeFramesRemaining -= Renderer::g_frameDelta;

			if (defaultFadeFramesRemaining > 0)
			{
				if (defaultFadeFramesRemaining > 23)
					continue;
			}
			else
			{
				defaultFadeFramesRemaining = 0;
			}

			if ((defaultFadeFramesRemaining + Renderer::g_frameDelta) > 23)
				Nu3D::Camera::SetTint(0, 0, 0, 12);

		} while (defaultFadeFramesRemaining);

		return 1;
	}

	// STUB: TOY2 0x0048E730
	void OneInit()
	{
		g_randDatBufferPtr = g_randDatBuffer;
		SaveManager::Init();
	}

	// FUNCTION: TOY2 0x00490730 [MATCHED]
	void CheckForQuit()
	{
		if (D3DApp::g_windowData.wndIsExiting != 0)
		{
			Logger::Log("CheckForQuit : Starting shutdown now...\n");
			DestroyWindow(D3DApp::g_windowData.mainHwnd);

			switch (D3DApp::g_renderMode)
			{
				case RENDERMODE_SOFTWARE:
					SoftwareRenderer::Destroy();
					break;
				case RENDERMODE_D3D:
					Logger::Log("QUIT : Destroying Direct3D renderer.\n");
					break;
			}

			CleanupManagers();
			D3DApp::PostQuitMessage();
			CoUninitialize();

			D3DApp::g_windowData.mainHwnd = 0;

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

			exit(D3DApp::g_windowData.wndEventMsg.wParam);
		}
	}

	// FUNCTION: TOY2 0x004CE760 [MATCHED]
	void InitCfg()
	{
		memset(&g_toyCfgData, 0, sizeof(g_toyCfgData));

		g_toyCfgData.driverIndex = -1;
		g_toyCfgData.detail = 1;
		g_toyCfgData.flags |= 7;
		g_toyCfgData.gammaCorrection = 2.0;
	}

	// FUNCTION: TOY2 0x004CE810 [MATCHED]
	int32_t ReadCfg()
	{
		InitCfg();

		FILE* fileHandle = fopen("toy2.cfg", "rb");

		if (fileHandle)
		{
			fread(&g_toyCfgData, 1, sizeof(ToyCfg), fileHandle);
			fclose(fileHandle);
			return 1;
		}

		return 0;
	}

	// FUNCTION: TOY2 0x00412B50 [PROVISIONAL]
	void RunModeSelect()
	{
		if (! g_modeSelectFinished)
		{
			atexit(DrawingDevice::Quit);
			ModeSelect::SetForceFullscreen_T(0);

			if (ModeSelect::EnumerateDrivers_T(ModeSelect::DeviceFilterCallback) < 0)
				Logger::GetErrorHandler("C:\\projects\\toy2\\direct6.cpp", 103)("Unable to enumerate a suitable device");

			ModeSelect::Show();

			DrawingDevice::DDAppDevice* primaryDevice;
			DrawingDevice::DDAppDevice::App* ddApp;

			if (DrawingDevice::GetChosenDevice_T(&ddApp, &primaryDevice))
				Logger::GetErrorHandler("C:\\projects\\toy2\\direct6.cpp", 111)("Unable to create D3D device\r\n try a lower resolution or screen depth");

			int32_t canDoWindowed = primaryDevice->canRenderWindowedOnPrimary;
			int32_t fullscreenExclusive = (ModeSelect::g_unusedFlag1 != 0 ? 2 : 0) | (canDoWindowed == 0) | (ModeSelect::g_unusedFlag2 != 0 ? 4 : 0);

			if (! canDoWindowed)
				Logger::g_showMsgBoxOnThrow = 1;

			if (primaryDevice->isHardwareAccelerated)
			{
				Renderer::SetIsSoftwareRendering(0);
			}
			else
			{
				Renderer::SetIsSoftwareRendering(1);
				while (Toy2::Graphics::RemoveDetailLevel()) {};
			}

			if (! primaryDevice->isHardwareAccelerated && primaryDevice->canRenderWindowedOnPrimary)
			{
				RECT adjustedRect;
				adjustedRect.top = 0;
				adjustedRect.left = 0;
				adjustedRect.right = 320;
				adjustedRect.bottom = 240;

				AdjustWindowRect(&adjustedRect, 0, 0);
				SetWindowPos(D3DApp::g_windowData.mainHwnd, 0, 0, 0, adjustedRect.right, adjustedRect.bottom, 2);
			}

			ShowWindow(D3DApp::g_windowData.mainHwnd, SW_SHOWMAXIMIZED);

			if (DrawingDevice::CD3DFramework::Build(
					D3DApp::g_windowData.mainHwnd, &ddApp->guid, primaryDevice, primaryDevice->primaryDisplayMode, fullscreenExclusive)
				>= 0)
				g_modeSelectFinished = 1;
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

	// FUNCTION: TOY2 0x0049D910 [PROVISIONAL]
	int32_t Run(int32_t argCount, char** argList)
	{
		g_returnedToTitle = 0;

		g_unused1 = 2;
		g_unused2 = 0;

		g_attractModeTimer = -25;
		g_attractModeInputTimer = -50;

		SoftwareRenderer::SwapRenderBuffer();

		g_pastInitialBoot = 0;
		g_curDemoLevel = 0;
		g_levelTransition = 0;

		SaveManager::g_save0Data.unkData1 = 1;
		SaveManager::g_save0Data.unkData2 = 23;
		SaveManager::g_save0Data.cameraType = SaveManager::CAMERA_PASSIVE | SaveManager::CAMERA_ACTIVE;
		SaveManager::g_save0Data.musicVolume = 8;
		SaveManager::g_save0Data.soundVolume = 8;

		AudioManager::SetVolumesProcessed(8, 8);

		int32_t enteredLevelIdx;
		int32_t levelIdxCache = g_levelFileIndex;

	LBL_RESTART_GAME:

		g_demoMode = 0;
		g_mainMenuState = 0;
		g_levelFileIndex = 0;

		Levels::g_levelLoadConfig = 1084;

		Renderer::SetVirtualRatioTo54();
		Levels::InitLevelPlay(0);
		ScreenDispatcher(10);

		g_levelFileIndex = levelIdxCache;

		if (! PlayMovieWithTransition(2, 0) && ! PlayMovieWithTransition(0, 0) && ! PlayMovieWithTransition(1, 0))
			UnlockAndPlayMovie(0, 0, 1);

		g_pastInitialBoot = 1;

	LBL_RESTART_MENU_STATE:

		SaveManager::InitProgressData(&SaveManager::g_save0Data);
		SaveManager::LoadProgressData(&SaveManager::g_save0Data);

		g_mainMenuState = 0;
		g_saveLoaded = 0;

		LevelSelect::ResetCursor();

	LBL_REDO_MENU_LOOP:

		while (true)
		{
			int32_t savedLevelFileIndex = g_levelFileIndex;
			g_levelFileIndex = 0;
			Levels::g_levelLoadConfig = 1084;

			Renderer::SetVirtualRatioTo54();
			Levels::InitLevelPlay(0);
			ScreenDispatcher(2);

			g_levelFileIndex = savedLevelFileIndex;

			switch (g_mainMenuState)
			{
				case 0:
					g_demoMode = 1;
					goto LBL_DEMO_MODE;

				case 1:
					g_demoMode = 0;
					goto LBL_DEMO_MODE;

				case 2: // Options Screen
					g_levelFileIndex = 0;
					Levels::g_levelLoadConfig = 1212;

					Renderer::SetVirtualRatioTo54();
					Levels::InitLevelPlay(0);
					ScreenDispatcher(9);

					g_levelFileIndex = enteredLevelIdx;
					g_mainMenuState = -1;
					continue;

				case 3: // Save Screen
					if (ShowSaveScreen())
						g_saveLoaded = 1;

					g_mainMenuState = -1;
					continue;

				case 4: // Movie Viewer
					ShowMovieViewer();
					g_mainMenuState = -1;
					continue;

				case 8:
					g_levelFileIndex = 0;
					Levels::g_levelLoadConfig = 1084;

					Renderer::SetVirtualRatioTo54();
					Levels::InitLevelPlay(0);
					ScreenDispatcher(8);

					g_levelFileIndex = enteredLevelIdx;
					g_mainMenuState = 0;
					goto LBL_RESTART_MENU_STATE;

				case 9:
					Nullsub5();
					return 0;

				default:

				LBL_DEMO_MODE:
					g_mainMenuState = 0;

					if (g_demoMode == 1)
					{
						switch (g_curDemoLevel)
						{
							case 0:
								g_levelIndex = 0;
								break;
							case 1:
								g_levelIndex = 3;
								break;
							case 2:
								g_levelIndex = 7;
								break;
							case 3:
								g_levelIndex = 10;
								break;
							case 4:
								g_levelIndex = 13;
								break;
							default:
								break;
						}

						if (++g_curDemoLevel > 4)
							g_curDemoLevel = 0;

						g_levelFileIndex = g_levelFileConversion[g_levelIndex];

						LoadPathBin();

						int32_t levelIdxCache = g_levelIndex;

						g_demoPathWriteIdx = -2;
						g_demoInputRunLength = 0;

						SaveManager::InitProgressData(&SaveManager::g_save0Data);
						SaveManager::LoadProgressData(&SaveManager::g_save0Data);

						g_levelIndex = levelIdxCache;
					}

					if (! g_demoMode && g_attractModeTimer < 0)
						goto LBL_SHOW_LEVEL_SELECT;

					break;
			}

			break;
		}

		while (true)
		{
			g_levelFileIndex = g_levelFileConversion[g_levelIndex];

			if (EnterLevel(g_levelFileIndex))
				break;

			while (true)
			{
				g_attractModeInputTimer = 2 * g_attractModeTimer;

				InputManager::g_curButtonsPressed = 0;
				InputManager::g_prevButtonsPressed = 0;

				g_isPaused = 0;
				g_pauseMenuBlinkTimer = 0;

				InputManager::g_directionInputState = 0;
				InputManager::g_prevDirectionInputState = 0;
				InputManager::g_directionInputState2Frames = 0;
				InputManager::g_directionInputState3Frames = 0;

				Renderer::g_frameDelta = 1;
				g_levelTransition = 0;
				g_levelTransitionTimer = 90;

				Nu3D::Camera::SetTint(128, 128, 128, 6);

				while (! g_levelTransition || g_levelTransitionTimer)
				{
					if (g_demoMode)
						Renderer::g_frameDelta = 2; // Half frame rate in demo mode

					if (g_isPaused)
						Game::PauseLoop();
					else
						Game::MainLoop();
				}

				AudioManager::FlushSoundVoices();

				if (g_levelTransition != 2)
					AudioManager::StopAndWait();

				if (g_quitToTitleFlag)
				{
					if (g_levelTransition == 2)
						AudioManager::StopAndWait();

					goto LBL_RESTART_MENU_STATE;
				}

				if (g_levelTransition != 2)
					break;

				if (! g_buzzActor.lives)
				{
					AudioManager::StopAndWait();
					goto LBL_GAME_OVER;
				}

				Buzz::Respawn();
			}

			if (g_levelTransition == 3 || g_levelTransition == 4)
			{
				if (g_attractModeTimer >= 0)
					break;

				if (g_levelTransition != 3)
					goto LBL_RESTART_MENU_STATE;

				levelIdxCache = g_levelFileIndex;

				goto LBL_RESTART_GAME;
			}

			enteredLevelIdx = g_levelFileIndex;

			if (g_levelTransition != 5)
			{
				if (g_levelTransition != 1)
				{
					if (g_attractModeTimer >= 0)
						break;

					goto LBL_SHOW_LEVEL_SELECT;
				}

				goto LBL_SHOW_LEVEL_RESULTS;
			}

			if (g_buzzActor.health < 0)
			{
				if (! g_buzzActor.lives)
				{
				LBL_GAME_OVER:

					levelIdxCache = g_levelFileIndex;

					g_levelFileIndex = 0;
					Levels::g_levelLoadConfig = 1148;

					Renderer::SetVirtualRatioTo54();

					Levels::InitLevelPlay(0);
					ScreenDispatcher(5);

					g_levelFileIndex = levelIdxCache;

					if (g_attractModeTimer >= 0)
						break;

					goto LBL_RESTART_GAME;
				}

				--g_buzzActor.lives;
			}

			if (g_attractModeTimer >= 0)
				break;

			if (g_levelFileIndex % 3)
			{
				g_levelTransition = 1;

			LBL_SHOW_LEVEL_RESULTS:

				if (g_levelFileIndex % 3)
				{
					g_levelFileIndex = 0;
					Levels::g_levelLoadConfig = 1148;

					Renderer::SetVirtualRatioTo54();
					Levels::InitLevelPlay(0);
					ScreenDispatcher(4);
					g_levelFileIndex = enteredLevelIdx;

					if (g_attractModeTimer >= 0)
						break;

					int32_t movie17Status = SaveManager::g_save0Data.moviesUnlocked[17];

					if (SaveManager::g_curLevelTokenData != SaveManager::g_save0Data.tokens[g_levelFileConversion[g_levelIndex]]
						&& (ComputeTokenProgress() & 0xFF0000) == 0x320000)
					{
						SaveManager::g_save0Data.moviesUnlocked[17] = 1;
					}

					ShowPostGameSaveMenu();

					if (! movie17Status && SaveManager::g_save0Data.moviesUnlocked[17])
						UnlockAndPlayMovie(17, 16, 1);
				}
				else
				{
					ShowActClearScreen();

					int32_t levelMovieStatus = SaveManager::g_save0Data.moviesUnlocked[g_levelIndex + 1];

					SaveManager::g_save0Data.moviesUnlocked[g_levelIndex + 1] = 1;

					if (g_levelFileIndex == 15)
						SaveManager::g_save0Data.moviesUnlocked[18] = 1;

					ShowPostGameSaveMenu();

					if (! levelMovieStatus)
						UnlockAndPlayMovie(g_levelIndex + 1, 30, 1);

					if (g_levelFileIndex == 15)
					{
						UnlockAndPlayMovie(18, 31, 1);
						levelIdxCache = g_levelFileIndex;

						g_levelFileIndex = 0;
						Levels::g_levelLoadConfig = 1276;

						Renderer::SetVirtualRatioTo54();
						Levels::InitLevelPlay(0);
						ScreenDispatcher(11); // Show credits

						goto LBL_RESTART_GAME;
					}
				}
			}

			if (g_attractModeTimer >= 0)
				break;

		LBL_SHOW_LEVEL_SELECT:

			g_mainMenuState = 0;
			g_saveLoaded = 1;

			if (ShowLevelSelect())
			{
				g_mainMenuState = -1;
				goto LBL_REDO_MENU_LOOP;
			}

			if ((g_levelIndex + 1) % 3)
			{
				UnlockAndPlayMovie(g_levelIndex + 1, 0, 0);
			}
			else if (g_levelIndex == 11)
			{
				UnlockAndPlayMovie(16, 0, 0);
			}
		}

		Nullsub5();

		return 0;
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

	// Updates the Direct3D render-target state from the current destination
	// rectangle. Computes the dest width and height, derives several scaled
	// half-width / fixed-point variants used by the rasterizer, resets a few
	// frame-local flags, points SoftwareRenderer::g_softwareRenderBuckets at its
	// shared bucket storage, empties the shared vertex pool, and records the
	// frame start time. Called on the hardware (D3D) render path (g_renderMode == 2); the
	// software path uses InitSoftwareRenderer.
	//
	// The halfWidth/halfHeight locals and the store interleaving (width store
	// before the height field-loads; width-1 before height-1) reproduce
	// retail's register assignment: width/2 in EDI (callee-saved) and the
	// frame zero in EBX. The only residual is instruction scheduling of the
	// SHL/SAR/DEC block against the E4/halfWidth stores, which reccmp treats
	// as a behavior-neutral effective match.
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

		D3DApp::g_windowData.wndIsExiting = g_wndIsExitingUnused;

		if (g_wndIsExitingUnused)
		{
			Logger::Log("CheckForQuit : Starting shutdown now...\n");

			DestroyWindow(D3DApp::g_windowData.mainHwnd);

			if (D3DApp::g_renderMode == RENDERMODE_SOFTWARE)
			{
				SoftwareRenderer::Destroy();
			}
			else if (D3DApp::g_renderMode == RENDERMODE_D3D)
			{
				Logger::Log("QUIT : Destroying Direct3D renderer.\n");
			}

			CleanupManagers();
			D3DApp::PostQuitMessage();

			CoUninitialize();

			D3DApp::g_windowData.mainHwnd = 0;

			Logger::Log("CheckForQuit : Code shutdown.\n");

			g_clearScreenSaveResult = SystemParametersInfoA(SPI_SCREENSAVERRUNNING, 0, &g_setScreenSaveRunning, 0);

			if (g_clearScreenSaveResult)
				Logger::Log("Managed to clear SCREENSAVERRUNNING\n");
			else
				Logger::Log("Failed to clear SCREENSAVERRUNNING\n");

			exit(D3DApp::g_windowData.wndEventMsg.wParam);
		}

		D3DApp::ProcessWndEvents();

		if (D3DApp::g_windowData.wndIsExiting)
		{
			DrawingDevice::g_drawingDevice->RestoreToGDISurface(1);
			Logger::GetErrorHandler("C:\\projects\\toy2\\toy2.cpp", 4670)("");
		}

		if (InputManager::IsKeyPressed(DIK_F3))
		{
			SoftwareRenderer::ZoomOut();
			g_extraControlsUnused = 15;
		}

		if (InputManager::IsKeyPressed(DIK_F4))
		{
			SoftwareRenderer::ZoomIn();
			g_extraControlsUnused = 15;
		}

		if (InputManager::IsKeyPressed(DIK_F5))
		{
			Graphics::RemoveDetailLevel();
			g_extraControlsUnused = 15;
		}

		if (InputManager::IsKeyPressed(DIK_F6))
		{
			Graphics::AddDetailLevel();
			g_extraControlsUnused = 15;
		}

		InputManager::UpdateButtonStates();

		if (g_inputSuppressFrames)
		{
			InputManager::g_curButtonsPressed = 0;
			--g_inputSuppressFrames;
		}

		UpdateAudioChannels();

		SoftwareRenderer::g_backBufferClearComplete = 0;

		if (D3DApp::g_renderMode == RENDERMODE_SOFTWARE)
		{
			SoftwareRenderer::UnkFunc2();
			SoftwareRenderer::UpdateBackdropScroll();
		}
		else if (D3DApp::g_renderMode == RENDERMODE_D3D)
		{
			UpdateD3DState();
		}
	}

	// FUNCTION: TOY2 0x00498550 [MATCHED]
	void ProcessMiscEvents()
	{
		D3DApp::ProcessWndEvents();
		if (D3DApp::g_windowData.wndIsExiting)
		{
			DrawingDevice::RestoreToGDISurface(1);
			Logger::GetErrorHandler("C:\\projects\\toy2\\toy2.cpp", 0x123E)(&SaveManager::g_emptyString);
		}

		if (InputManager::IsKeyPressed(DIK_F3))
		{
			SoftwareRenderer::ZoomOut();
			g_extraControlsUnused = 15;
		}
		if (InputManager::IsKeyPressed(DIK_F4))
		{
			SoftwareRenderer::ZoomIn();
			g_extraControlsUnused = 15;
		}
		if (InputManager::IsKeyPressed(DIK_F5))
		{
			Graphics::RemoveDetailLevel();
			g_extraControlsUnused = 15;
		}
		if (InputManager::IsKeyPressed(DIK_F6))
		{
			Graphics::AddDetailLevel();
			g_extraControlsUnused = 15;
		}

		InputManager::UpdateButtonStates();
		if (g_inputSuppressFrames != 0)
		{
			--g_inputSuppressFrames;
			InputManager::g_curButtonsPressed = 0;
		}
		UpdateAudioChannels();
		SoftwareRenderer::g_backBufferClearComplete = 0;
	}

	// STUB: TOY2 0x0047CC90
	void InitSoftwareRenderer()
	{
		// Some back story on this, the game has two rendering modes. Hardware accelerated (DirectX) or full software based rendering.
		// This is normal for the time, considering it was uncommon for people to have dedicated graphics hardware as it was expensive.
		//
		// The software renderer will be decompiled last, as it is extremely math heavy and optimized, making it difficult to produce
		// code that can be read by human eyes.
		return;
	}

	// STUB: TOY2 0x00499950
	void InitDirect3DRenderer()
	{
		// Weird method, a good portion of these variables are never even used in the game
	}
}

// $FUNC DEBUG
void AllocateConsole()
{
	AllocConsole();

	FILE* fp;

	// redirect STDOUT
	fp = freopen("CONOUT$", "w", stdout);
	fp = freopen("CONOUT$", "w", stderr);

	// redirect STDIN
	fp = freopen("CONIN$", "r", stdin);

	printf("[Debug Console Allocated!]\n");
}

// FUNCTION: TOY2 0x004316C0 [PROVISIONAL]
int32_t WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, char* cmdLine, int32_t cmdShow)
{
#ifdef APPLY_FIXES
	AllocateConsole();
#endif

	Toy2::g_unused0 = 0;

	memset(&D3DApp::d3dappi, 0, sizeof(D3DApp::d3dappi));

	D3DApp::g_no32bitColors = 1;

	FileUtils::ValidateInstall();

	Toy2::g_levelFileIndex = 1;

	memset(&D3DApp::g_windowData, 0, sizeof(D3DApp::g_windowData));

	D3DApp::g_windowData.hInstance = hInstance;
	D3DApp::g_windowData.hPrev = hPrev;
	D3DApp::g_windowData.lpCmdLine = cmdLine;
	D3DApp::g_windowData.nShowCmd = cmdShow;

	D3DApp::g_windowData.unkInt8 = 0;
	D3DApp::g_windowData.unkInt3 = 1;
	D3DApp::g_windowData.unkInt4 = 1;
	D3DApp::g_windowData.unkInt5 = 1;
	D3DApp::g_windowData.wndIsExiting = 0;

	Toy2::OneInit();

	Toy2::g_returnedToTitle = 0;
	Toy2::g_attractModeTimer = -1;

	Toy2::g_unused1 = 2;
	Toy2::g_unused2 = 0;

	Toy2::ReadCfg();
	D3DApp::BuildProfileMachine();
	D3DApp::BuildWindow();
	Toy2::ShowModeSelect();

	switch (D3DApp::g_renderMode)
	{
		case RENDERMODE_SOFTWARE:
			Toy2::InitSoftwareRenderer();
			break;
		case RENDERMODE_D3D:
			Toy2::InitDirect3DRenderer();
			break;
	}

	int32_t tokenCount = 0;
	char* currentToken;
	char* tokenEntries[8];

	currentToken = strtok(cmdLine, " ");

	if (currentToken)
	{
		tokenCount = 1;
		char* nextToken = strtok(0, " ");

		if (nextToken)
		{
			char** tokenArrayPtr = tokenEntries;

			do
			{
				*tokenArrayPtr = nextToken;
				++tokenCount;
				++tokenArrayPtr;

				nextToken = strtok(0, " ");
			} while (nextToken);
		}
	}

	Toy2::Run(tokenCount, &currentToken);

	D3DApp::g_windowData.wndIsExiting = 1;

	Toy2::CheckForQuit();

	return 0;
}
