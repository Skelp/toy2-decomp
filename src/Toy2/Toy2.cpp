#include "Toy2/Toy2.h"
#include "Toy2/Gadget.h"
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
#include "SaveManager.h"
#include "Random.h"
#include "Toy2/LevelSelect.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Buzz.h"
#include "Toy2/Levels.h"
#include "Toy2/MainMenu.h"
#include "Toy2/Actor.h"
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

void Nullsub10();
void Nullsub11();

namespace AudioManager
{
	extern char g_sfxSubPath[4];
}

namespace Renderer
{
	extern float g_parallaxHorizOffset;
	extern float g_parallaxTexHeightRatio;
	extern float g_parallaxTexWidthRatio;

	namespace Sprite
	{
		void DrawClipped(int16_t xPos, int16_t yPos, int16_t clipLeft, int16_t clipRight, int16_t sheetIndex, int16_t tileIndex);
	}
}

namespace Nu3D
{
	namespace Camera
	{
		int32_t IsActorSpawnVisible(const Vector3I* cameraPosition, const Toy2::Actor::Toy2Actor* actor);
	}
}

namespace Toy2
{
	namespace Actor
	{
		extern int32_t g_periodicHintSoundTimer;
		extern int32_t g_coinQuestHintTimer;
		extern int32_t g_coinTokenAwarded;
		extern int32_t g_rotatingHintSoundTimer;
		extern int32_t g_rotatingHintIndex;
		extern int32_t g_itemReturnHintSoundTimer;
	}
}

namespace Renderer
{
	// GLOBAL: TOY2 0x00559C60
	int32_t g_cinematicBarProgress;

	// FUNCTION: TOY2 0x00440E90 [EFFECTIVE]
	void DrawCinematicBars()
	{
		int32_t frameDelta = g_frameDelta;
		int32_t barProgress;
		Vector2F uvTopLeft = { 0.0f, 0.0f };
		Vector2F uvBottomRight = { 1.0f, 1.0f };

		if (Nu3D::Camera::g_viewHistoryInitialized != 0)
		{
			barProgress = g_cinematicBarProgress + frameDelta;
			if (barProgress > 25)
			{
				g_cinematicBarProgress = 25;
				goto drawBars;
			}
		}
		else
		{
			barProgress = g_cinematicBarProgress - frameDelta;
			if (barProgress < 0)
			{
				g_cinematicBarProgress = 0;
				return;
			}
		}

		g_cinematicBarProgress = barProgress;
		if (barProgress <= 0)
			return;

	drawBars:
		float barHeight = (float)g_cinematicBarProgress * 0.004f;
		RGBA barColor;
		barColor.value = 0xFF000000;
		int32_t renderFlags = RENDER_ALPHA_DEFAULT | RENDER_ZWRITE | RENDER_CULL_NONE;
		Sprite::Queue2DSprite(0.0f, 0.0f, 1.0f, barHeight, &uvTopLeft, &uvBottomRight, 0, barColor, renderFlags);
		Sprite::Queue2DSprite(0.0f, 1.0f - barHeight, 1.0f, barHeight, &uvTopLeft, &uvBottomRight, 0, barColor, renderFlags);
	}
}

namespace Toy2
{
	extern int32_t g_movementInputLockTimer;
	extern int32_t g_mpegPlaybackDisabled;

	namespace Actor
	{
		void UpdateAIMovement(Toy2Actor* actor);
	}

	namespace CreatureBehaviour
	{
		void Zurg3(Actor::Toy2Actor::ActorBehaviourContext* context);
		void TinMan(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Sheep(Actor::Toy2Actor::ActorBehaviourContext* context);
		void RCCar(Actor::Toy2Actor::ActorBehaviourContext* context);
		void LawnMower(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Army(Actor::Toy2Actor::ActorBehaviourContext* context);
		void ZgCar(Actor::Toy2Actor::ActorBehaviourContext* context);
		void ZKite(Actor::Toy2Actor::ActorBehaviourContext* context);
		void LTyke(Actor::Toy2Actor::ActorBehaviourContext* context);
		void ZPod(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Drill(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Mouse(Actor::Toy2Actor::ActorBehaviourContext* context);
		void BPlane(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Box(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Dino(Actor::Toy2Actor::ActorBehaviourContext* context);
		void ZBoat(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Chick(Actor::Toy2Actor::ActorBehaviourContext* context);
		void GunsP(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Clown(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Martian(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Rabid(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Buzzard(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Ducks(Actor::Toy2Actor::ActorBehaviourContext* context);
		void GunsLLevel11(Actor::Toy2Actor::ActorBehaviourContext* context);
		void GunsL(Actor::Toy2Actor::ActorBehaviourContext* context);
		void FatBloke(Actor::Toy2Actor::ActorBehaviourContext* context);
		void BBuggy(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Pilot(Actor::Toy2Actor::ActorBehaviourContext* context);
		void SmithLevel14(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Smith(Actor::Toy2Actor::ActorBehaviourContext* context);
		void Luggage(Actor::Toy2Actor::ActorBehaviourContext* context);
		void ProsPLevel13(Actor::Toy2Actor::ActorBehaviourContext* context);
		void ProsP(Actor::Toy2Actor::ActorBehaviourContext* context);
	}

	namespace ElevatorHop
	{
		void TransformMouseActors();
	}

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

		// FUNCTION: TOY2 0x004A27A0 [PROVISIONAL]
		void ApplyUnlockToGeometry(const UnlockGeometryEntry* entries, int32_t scale)
		{
			while (entries->levelIndex != 0xFF)
			{
				if (entries->levelIndex == g_levelFileIndex)
				{
					Nu3D::Link::SetScaleFromFixedOffsets(entries->lockedLinkId, 0, 0, 0);
					int32_t fixedScale = scale << 10;
					Nu3D::Link::SetScaleFromFixedOffsets(entries->unlockedLinkId, fixedScale, fixedScale, fixedScale);
					Platform::DisableCollision(entries->platformIndex);

					Levels::RecordData* pickupRecords = Levels::g_recordData[63];
					int32_t pickupCount = pickupRecords->recordCount;
					Collectables::PickupRecord* pickup = reinterpret_cast<Collectables::PickupRecord*>(pickupRecords + 1);
					for (int32_t pickupIndex = 0; pickupIndex < pickupCount; pickup++, pickupIndex++)
					{
						if (pickup->objectIndex == entries->unlockedLinkId)
						{
							pickup->facingAngle = 0x1F;
							break;
						}
					}
				}

				entries++;
			}
		}

		// FUNCTION: TOY2 0x004A2080 [PROVISIONAL]
		void InitLevelUnlockGeometry()
		{
			Actor::g_coinTokenAwarded = 0;
			Actor::g_coinQuestHintTimer = 200;
			Actor::g_periodicHintSoundTimer = 250;
			Actor::g_itemReturnHintSoundTimer = 150;
			Actor::g_rotatingHintIndex = -1;
			Actor::g_rotatingHintSoundTimer = 100;

			LevelUnlockInfo* levelUnlock = &g_levelUnlockInfo[g_levelFileIndex - 1];
			if ((g_unlocks & levelUnlock->unlockFlag) == 0)
			{
				g_unlockNodeState = levelUnlock->modelNodeIndex;
				if (g_unlockNodeState != 0)
				{
					HideModelNode(9, g_unlockNodeState);
					if (g_unlockNodeState == 1)
					{
						HideModelNode(9, 0x0B);
						HideModelNode(9, 0x0C);
						HideModelNode(9, 0x0F);
					}
				}
			}
			else
			{
				int32_t linkId;
				if (g_levelFileIndex == 1)
					linkId = 0x46;
				else if (g_levelFileIndex == 4)
					linkId = 0x6D;
				else if (g_levelFileIndex == 7)
					linkId = 0x3F;
				else if (g_levelFileIndex == 10)
					linkId = 0x6F;
				else if (g_levelFileIndex == 13)
					linkId = 0x3F;

				Nu3D::Link::SetScaleFromFixedOffsets(linkId, 0, 0, 0);
				Levels::RecordData* pickupRecords = Levels::g_recordData[63];
				Collectables::PickupRecord* pickup = reinterpret_cast<Collectables::PickupRecord*>(pickupRecords + 1);
				for (int32_t pickupIndex = 0; pickupIndex < pickupRecords->recordCount; pickup++, pickupIndex++)
				{
					if (pickup->objectIndex == linkId)
					{
						pickup->position.y = INT_MIN;
						break;
					}
				}
				g_unlockNodeState = 0;
			}

			Levels::RecordData* pickupRecords = Levels::g_recordData[63];
			UnlockGeometryEntry* entries;
			Collectables::PickupRecord* pickup;
			int32_t pickupIndex;

			if ((g_unlocks & 1) != 0)
			{
				ApplyUnlockToGeometry(g_unlockBit1Geometry, 8);
				pickupRecords = Levels::g_recordData[63];
			}
			else
			{
				entries = g_unlockBit1Geometry;
				while (entries->levelIndex != 0xFF)
				{
					if (entries->levelIndex == g_levelFileIndex)
					{
						pickup = reinterpret_cast<Collectables::PickupRecord*>(pickupRecords + 1);
						for (pickupIndex = 0; pickupIndex < pickupRecords->recordCount; pickup++, pickupIndex++)
						{
							if (pickup->objectIndex == entries->unlockedLinkId)
							{
								Nu3D::Link::SetScaleFromFixedOffsets(entries->unlockedLinkId, 0x1000, 0x1000, 0x1000);
								pickup->facingAngle = 0;
								pickupRecords = Levels::g_recordData[63];
								break;
							}
						}
					}
					entries++;
				}
			}

			if ((g_unlocks & 2) != 0)
			{
				ApplyUnlockToGeometry(g_unlockBit2Geometry, 4);
				pickupRecords = Levels::g_recordData[63];
			}
			else
			{
				entries = g_unlockBit2Geometry;
				while (entries->levelIndex != 0xFF)
				{
					if (entries->levelIndex == g_levelFileIndex)
					{
						pickup = reinterpret_cast<Collectables::PickupRecord*>(pickupRecords + 1);
						for (pickupIndex = 0; pickupIndex < pickupRecords->recordCount; pickup++, pickupIndex++)
						{
							if (pickup->objectIndex == entries->unlockedLinkId)
							{
								Nu3D::Link::SetScaleFromFixedOffsets(entries->unlockedLinkId, 0x1000, 0x1000, 0x1000);
								pickup->facingAngle = 0;
								pickupRecords = Levels::g_recordData[63];
								break;
							}
						}
					}
					entries++;
				}
			}

			if ((g_unlocks & 4) != 0)
			{
				ApplyUnlockToGeometry(g_unlockBit4Geometry, 4);
				pickupRecords = Levels::g_recordData[63];
			}
			else
			{
				entries = g_unlockBit4Geometry;
				while (entries->levelIndex != 0xFF)
				{
					if (entries->levelIndex == g_levelFileIndex)
					{
						pickup = reinterpret_cast<Collectables::PickupRecord*>(pickupRecords + 1);
						for (pickupIndex = 0; pickupIndex < pickupRecords->recordCount; pickup++, pickupIndex++)
						{
							if (pickup->objectIndex == entries->unlockedLinkId)
							{
								Nu3D::Link::SetScaleFromFixedOffsets(entries->unlockedLinkId, 0x1000, 0x1000, 0x1000);
								pickup->facingAngle = 0;
								pickupRecords = Levels::g_recordData[63];
								break;
							}
						}
					}
					entries++;
				}
			}

			if ((g_unlocks & 8) != 0)
			{
				ApplyUnlockToGeometry(g_unlockBit8Geometry, 4);
				pickupRecords = Levels::g_recordData[63];
			}
			else
			{
				entries = g_unlockBit8Geometry;
				while (entries->levelIndex != 0xFF)
				{
					if (entries->levelIndex == g_levelFileIndex)
					{
						pickup = reinterpret_cast<Collectables::PickupRecord*>(pickupRecords + 1);
						for (pickupIndex = 0; pickupIndex < pickupRecords->recordCount; pickup++, pickupIndex++)
						{
							if (pickup->objectIndex == entries->unlockedLinkId)
							{
								Nu3D::Link::SetScaleFromFixedOffsets(entries->unlockedLinkId, 0x1000, 0x1000, 0x1000);
								pickup->facingAngle = 0;
								pickupRecords = Levels::g_recordData[63];
								break;
							}
						}
					}
					entries++;
				}
			}

			if ((g_unlocks & 0x10) != 0)
			{
				ApplyUnlockToGeometry(g_unlockBit16Geometry, 4);
				return;
			}

			entries = g_unlockBit16Geometry;
			while (entries->levelIndex != 0xFF)
			{
				if (entries->levelIndex == g_levelFileIndex)
				{
					pickup = reinterpret_cast<Collectables::PickupRecord*>(pickupRecords + 1);
					for (pickupIndex = 0; pickupIndex < pickupRecords->recordCount; pickup++, pickupIndex++)
					{
						if (pickup->objectIndex == entries->unlockedLinkId)
						{
							Nu3D::Link::SetScaleFromFixedOffsets(entries->unlockedLinkId, 0x0C00, 0x0C00, 0x0C00);
							pickup->facingAngle = 0;
							pickupRecords = Levels::g_recordData[63];
							break;
						}
					}
				}
				entries++;
			}
		}
	}

	namespace Level
	{
		enum RecordType
		{
			RECORD_AMBIENT_EMITTER = 58,
			RECORD_SWING = 60,
			RECORD_POLE = 61,
			RECORD_ZIPLINE = 62
		};

		struct PoleRecord
		{
			Vector3I position;
			int32_t type;
			int32_t height;
			int32_t reserved;
		};

		STATIC_ASSERT(sizeof(PoleRecord) == 0x18);

		// FUNCTION: TOY2 0x00414550 [PROVISIONAL]
		void FixupRecordCoordinates()
		{
			Levels::g_alternateAmbientEmitterStart = 0;
			Levels::g_ambientEmitterScanIndex = 0;

			Levels::RecordData* ambientEmitters = Levels::g_recordData[RECORD_AMBIENT_EMITTER];
			if (ambientEmitters != 0)
			{
				Vector3I* output = ambientEmitters->data;
				Vector3I* source = output;
				int32_t emitterIndex = 0;
				int32_t emitterCount = ambientEmitters->recordCount;
				while (emitterIndex < emitterCount)
				{
					if (output->x == 0 && output->y == 0 && output->z == 0)
					{
						Levels::g_alternateAmbientEmitterStart = emitterIndex;
						++source;
						--Levels::g_recordData[RECORD_AMBIENT_EMITTER]->recordCount;
						++emitterIndex;
					}

					output->x = source->x << 5;
					output->y = source->y << 5;
					output->z = source->z << 5;
					++output;
					++source;
					++emitterIndex;
				}
			}

			Levels::RecordData* coordinateRecords = Levels::g_recordData[RECORD_SWING];
			if (coordinateRecords != 0)
			{
				int32_t* coordinate = &coordinateRecords->data->x;
				for (int32_t i = 0; i < Levels::g_recordData[RECORD_SWING]->recordCount * 3; ++i)
				{
					*coordinate <<= 5;
					++coordinate;
				}
			}

			Levels::RecordData* pairedRecords = Levels::g_recordData[RECORD_POLE];
			if (pairedRecords != 0)
			{
				Vector3I* source = pairedRecords->data;
				PoleRecord* output = reinterpret_cast<PoleRecord*>(source);
				int32_t poleType = 0;
				int32_t outputCount = pairedRecords->recordCount;

				for (int32_t pairIndex = 0; pairIndex < (int32_t)((uint32_t)Levels::g_recordData[RECORD_POLE]->recordCount >> 1); ++pairIndex)
				{
					while (abs(source->x) == abs(source->y) && abs(source->y) == abs(source->z))
					{
						poleType = abs(source->x) / 50;
						++source;
						--outputCount;
					}

					output->position.x = source[0].x << 5;
					output->position.y = source[0].y << 5;
					output->position.z = source[0].z << 5;
					output->type = poleType;
					output->height = output->position.y - (source[1].y << 5);
					source += 2;
					++output;
				}

				Levels::g_recordData[RECORD_POLE]->recordCount = (uint16_t)outputCount;
			}

			coordinateRecords = Levels::g_recordData[RECORD_ZIPLINE];
			if (coordinateRecords != 0)
			{
				int32_t* coordinate = &coordinateRecords->data->x;
				for (int32_t i = 0; i < Levels::g_recordData[RECORD_ZIPLINE]->recordCount * 3; ++i)
				{
					*coordinate <<= 5;
					++coordinate;
				}
			}
		}
	}

	namespace Lighting
	{
		void InitBuzzLight();
		void UpdateBuzzLight();
	}

	extern int32_t g_hudActorAnimationFrame;

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

	namespace Ini
	{
		enum Keyword
		{
			KEYWORD_UNKNOWN = -1,
			KEYWORD_COMMENT = 11,
			KEYWORD_LANGUAGE = 12,
			KEYWORD_DEFTEXT = 13,
			KEYWORD_KEY = 14,
			KEYWORD_JOY = 15,
			KEYWORD_MESSAGE = 16,
			KEYWORD_QUIET = 17,
			KEYWORD_CHEAT = 18,
			KEYWORD_HIMPEG = 19,
			KEYWORD_NOMPEG = 20,
			KEYWORD_IF = 21,
			KEYWORD_LOG = 22,
			KEYWORD_ENDIF = 23,
			KEYWORD_DEFKEY = 24,
			KEYWORD_FIRE = 25,
			KEYWORD_JUMP = 26,
			KEYWORD_CAMLEFT = 27,
			KEYWORD_CAMRIGHT = 28,
			KEYWORD_HELMETVIEW = 29,
			KEYWORD_PAUSE = 30,
			KEYWORD_SELECT = 31,
			KEYWORD_DERVISH = 32,
			KEYWORD_PAD_RL = 37,
			KEYWORD_PAD_RU = 38,
			KEYWORD_PAD_RD = 39,
			KEYWORD_PAD_RR = 40,
			KEYWORD_PAD_FLT = 41,
			KEYWORD_PAD_FLB = 42,
			KEYWORD_PAD_FRT = 43,
			KEYWORD_PAD_FRB = 44,
			KEYWORD_PAD_SEL = 45,
			KEYWORD_PAD_START = 46,
			KEYWORD_FINISHED = 47,
		};

		enum ControlMappingSlot
		{
			CONTROL_MAPPING_PAD_RU = 4,
			CONTROL_MAPPING_JUMP = 5,
			CONTROL_MAPPING_FIRE = 6,
			CONTROL_MAPPING_DERVISH = 7,
			CONTROL_MAPPING_CAMLEFT = 8,
			CONTROL_MAPPING_CAMRIGHT = 9,
			CONTROL_MAPPING_PAD_FRT = 10,
			CONTROL_MAPPING_HELMETVIEW = 11,
			CONTROL_MAPPING_SELECT = 12,
			CONTROL_MAPPING_PAUSE = 13,
		};

		struct KeywordEntry
		{
			const char* name;
			int32_t keyword;
		};

		struct ControlTextEntry
		{
			int32_t x;
			int32_t y;
			int32_t reserved0;
			const char* text;
			int32_t reserved1;
			int32_t reserved2;
		};

		struct MessageTextEntry
		{
			int32_t x;
			int32_t y;
			const char* text;
			int32_t reserved;
		};

		// GLOBAL: TOY2 0x004EFFC8
		ControlTextEntry g_keyTextEntries[14] = {
			{ 60, 40, 0, "forward", 0, 0 },
			{ 60, 60, 0, "back", 0, 0 },
			{ 60, 80, 0, "left", 0, 0 },
			{ 60, 100, 0, "right", 0, 0 },
			{ 60, 120, 0, "fire/get/drop", 0, 0 },
			{ 60, 140, 0, "jump/change", 0, 0 },
			{ 60, 160, 0, "seed color", 0, 0 },
			{ 200, 40, 0, "camera", 0, 0 },
			{ 200, 60, 0, "lock camera", 0, 0 },
			{ 240, 80, 0, "walk", 0, 0 },
			{ 200, 100, 0, "kick", 0, 0 },
			{ 80, 180, 0, "game pad", 0, 0 },
			{ 80, 200, 0, "restore defaults", 0, 0 },
			{ 80, 220, 0, "accept", 0, 0 },
		};

		// GLOBAL: TOY2 0x004F0114
		ControlTextEntry* g_keyTextTable[15] = {
			&g_keyTextEntries[0],
			&g_keyTextEntries[1],
			&g_keyTextEntries[2],
			&g_keyTextEntries[3],
			&g_keyTextEntries[4],
			&g_keyTextEntries[5],
			&g_keyTextEntries[6],
			&g_keyTextEntries[7],
			&g_keyTextEntries[8],
			&g_keyTextEntries[9],
			&g_keyTextEntries[10],
			&g_keyTextEntries[11],
			&g_keyTextEntries[12],
			&g_keyTextEntries[13],
			(ControlTextEntry*)-1,
		};

		// GLOBAL: TOY2 0x004F0150
		ControlTextEntry g_joyTextEntries[10] = {
			{ 40, 40, 0, "fire/get/drop", 0, 0 },
			{ 40, 60, 0, "jump/change", 0x100, 0 },
			{ 40, 80, 0, "seed color", 0x200, 0 },
			{ 40, 100, 0, "camera", 0x300, 0 },
			{ 200, 40, 0, "lock camera", 0x400, 0 },
			{ 200, 60, 0, "walk", 0x500, 0 },
			{ 200, 80, 0, "kick", 0x600, 0 },
			{ 80, 180, 0, "keyboard", 0, 0 },
			{ 80, 200, 0, "restore defaults", 0, 0 },
			{ 80, 220, 0, "accept", 0, 0 },
		};

		// GLOBAL: TOY2 0x004F023C
		ControlTextEntry* g_joyTextTable[11] = {
			&g_joyTextEntries[0],
			&g_joyTextEntries[1],
			&g_joyTextEntries[2],
			&g_joyTextEntries[3],
			&g_joyTextEntries[4],
			&g_joyTextEntries[5],
			&g_joyTextEntries[6],
			&g_joyTextEntries[7],
			&g_joyTextEntries[8],
			&g_joyTextEntries[9],
			(ControlTextEntry*)-1,
		};

		// GLOBAL: TOY2 0x004F4F60
		KeywordEntry g_keywords[] = {
#include "IniKeywords.inc"
		};

		// GLOBAL: TOY2 0x004F5398
		MessageTextEntry g_messageTextEntries[25] = {
			{ 20, 200, "select option and press enter", 0 },
			{ 90, 200, "() - to change", 0 },
			{ 20, 220, "press enter to accept changes", 0 },
			{ 50, 240, "press esc to go back", 0 },
			{ 70, 120, "do you want to quit", 0 },
			{ 90, 4, "video options", 0 },
			{ 100, 4, "define keys", 0 },
			{ 130, 4, "save game", 0 },
			{ 10, 200, "select file", 0 },
			{ 10, 220, "press esc to go back", 0 },
			{ 130, 4, "confirm", 0 },
			{ 80, 180, "do you wish to", 0 },
			{ 70, 200, "save over this game", 0 },
			{ 130, 4, "enter name", 0 },
			{ 130, 4, "load game", 0 },
			{ 10, 200, "select game and press enter", 0 },
			{ 10, 220, "press esc to go back", 0 },
			{ 0, 0, &SaveManager::g_emptyString, 0 },
			{ 0, 0, &SaveManager::g_emptyString, 0 },
			{ 0, 0, &SaveManager::g_emptyString, 0 },
			{ 0, 0, &SaveManager::g_emptyString, 0 },
			{ 0, 0, &SaveManager::g_emptyString, 0 },
			{ 0, 0, &SaveManager::g_emptyString, 0 },
			{ 0, 0, &SaveManager::g_emptyString, 0 },
			{ 0, 0, &SaveManager::g_emptyString, 0 },
		};

		// GLOBAL: TOY2 0x004F5524
		MessageTextEntry* g_messageTextTable[25] = {
			&g_messageTextEntries[0],
			&g_messageTextEntries[1],
			&g_messageTextEntries[2],
			&g_messageTextEntries[3],
			&g_messageTextEntries[4],
			&g_messageTextEntries[5],
			&g_messageTextEntries[6],
			&g_messageTextEntries[7],
			&g_messageTextEntries[8],
			&g_messageTextEntries[9],
			&g_messageTextEntries[10],
			&g_messageTextEntries[11],
			&g_messageTextEntries[12],
			&g_messageTextEntries[13],
			&g_messageTextEntries[14],
			&g_messageTextEntries[15],
			&g_messageTextEntries[16],
			&g_messageTextEntries[17],
			&g_messageTextEntries[18],
			&g_messageTextEntries[19],
			&g_messageTextEntries[20],
			&g_messageTextEntries[21],
			&g_messageTextEntries[22],
			&g_messageTextEntries[23],
			&g_messageTextEntries[24],
		};

		// GLOBAL: TOY2 0x0053006C
		char g_defTextStorage[64][256];

		// GLOBAL: TOY2 0x0053406C
		int32_t g_defTextCount;

		// GLOBAL: TOY2 0x00534070
		FILE* g_iniFile;

		// GLOBAL: TOY2 0x00534078
		int32_t g_cheatsEnabled;

		// GLOBAL: TOY2 0x0053407C
		char g_iniToken[1024];

		// GLOBAL: TOY2 0x00534480
		int32_t g_language;

		// GLOBAL: TOY2 0x00830C20
		int32_t g_highQualityMpeg;

		// GLOBAL: TOY2 0x00882A20
		char g_iniCdSearchPath[512];

		// GLOBAL: TOY2 0x00882C24
		int32_t g_useAlternateIniPath;

		// GLOBAL: TOY2 0x00882C2C
		char g_iniInstallSearchPath[512];

		STATIC_ASSERT(sizeof(ControlTextEntry) == 0x18);
		STATIC_ASSERT(sizeof(MessageTextEntry) == 0x10);

		// FUNCTION: TOY2 0x00430BF0 [PROVISIONAL]
		void ParseDefText()
		{
			if (fscanf(g_iniFile, "%s", g_iniToken) == EOF)
				Logger::Log("EOF reached .\n");

			int32_t keyword;
			if (g_iniToken[0] == '#')
			{
				keyword = KEYWORD_COMMENT;
			}
			else
			{
				KeywordEntry* entry = g_keywords;
				while (entry->keyword != KEYWORD_UNKNOWN)
				{
					if (strcmp(entry->name, g_iniToken) == 0)
					{
						keyword = entry->keyword;
						goto keywordFound;
					}
					++entry;
				}
				keyword = KEYWORD_UNKNOWN;
			}

		keywordFound:
			ControlTextEntry** controlTextTable;
			switch (keyword)
			{
				case KEYWORD_KEY:
					controlTextTable = g_keyTextTable;
					break;
				case KEYWORD_JOY:
					controlTextTable = g_joyTextTable;
					break;
				case KEYWORD_MESSAGE:
					break;
				default:
					memset(g_iniToken, 0, sizeof(g_iniToken));
					char* output = g_iniToken;
					int32_t character;
					do
					{
						character = fgetc(g_iniFile);
						*output++ = (char)character;
					} while (character != EOF && character != '\n');

					Logger::Log("UNKNOWN DefText : %s", g_iniToken);
					return;
			}

			if (fscanf(g_iniFile, "%s", g_iniToken) == EOF)
				Logger::Log("EOF reached .\n");
			int32_t entryIndex = atoi(g_iniToken);

			char character;
			do
			{
				character = (char)fgetc(g_iniFile);
			} while (character != '"' && character != EOF);

			char text[256];
			char* output = text;
			do
			{
				character = (char)fgetc(g_iniFile);
				*output++ = character;
			} while (character != '"' && character != EOF);
			output[-1] = '\0';

			if (fscanf(g_iniFile, "%s", g_iniToken) == EOF)
				Logger::Log("EOF reached .\n");
			int32_t x = atoi(g_iniToken);

			if (fscanf(g_iniFile, "%s", g_iniToken) == EOF)
				Logger::Log("EOF reached .\n");
			int32_t y = atoi(g_iniToken);

			if (g_defTextCount < 64)
			{
				strncpy(g_defTextStorage[g_defTextCount], text, 256);
				if (keyword >= KEYWORD_KEY)
				{
					ControlTextEntry* entry;
					if (keyword < KEYWORD_MESSAGE)
					{
						entry = controlTextTable[entryIndex];
						entry->text = g_defTextStorage[g_defTextCount];
						entry->x = x;
						entry->y = y;
					}
					else
					{
						if (keyword != KEYWORD_MESSAGE)
							return;
						MessageTextEntry* message = g_messageTextTable[entryIndex];
						message->text = g_defTextStorage[g_defTextCount];
						message->x = x;
						message->y = y;
					}
					++g_defTextCount;
				}
			}
		}

		inline int32_t FindKeyword(const char* token)
		{
			if (token[0] == '#')
				return KEYWORD_COMMENT;

			KeywordEntry* entry = g_keywords;
			while (entry->keyword != KEYWORD_UNKNOWN)
			{
				if (strcmp(entry->name, token) == 0)
					return entry->keyword;
				++entry;
			}
			return KEYWORD_UNKNOWN;
		}

		inline void ReadToken()
		{
			if (fscanf(g_iniFile, "%s", g_iniToken) == EOF)
				Logger::Log("EOF reached .\n");
		}
	}

	// FUNCTION: TOY2 0x00430E90 [PROVISIONAL]
	void ReadIniFile()
	{
		using namespace Ini;

		char path[1024];
		memset(path, 0, sizeof(path));
		g_defTextCount = 0;

		FILE* rootIni = fopen("c:\\toy2.ini", "rb");
		if (rootIni != NULL)
		{
			fclose(rootIni);
			strcat(path, "c:");
		}
		else
		{
			if (FileUtils::GetFileSize("toy2.ini") == 0)
			{
				Logger::Log("ReadIniFile : No TOY2.INI file found.\n");
				return;
			}

			if (g_useAlternateIniPath != 0)
			{
				strcat(path, g_iniCdSearchPath);
				strcat(path, "cd\\");
			}
			else
			{
				strcat(path, g_iniInstallSearchPath);
				strcat(path, "\\cd\\");
			}
		}
		strcat(path, "toy2.ini");

		g_iniFile = fopen(path, "rt");
		if (g_iniFile == NULL)
		{
			Logger::Log("ReadIniFile : Failed to open TOY2.INI.\n");
			return;
		}

		for (;;)
		{
			ReadToken();
			int32_t keyword = FindKeyword(g_iniToken);

			switch (keyword)
			{
				case KEYWORD_DEFKEY: {
					ReadToken();
					int32_t controlKeyword = FindKeyword(g_iniToken);
					ReadToken();
					strlwr(g_iniToken);
					int16_t inputCode = InputManager::KeyNameToScancode(g_iniToken[0]);
					switch (controlKeyword)
					{
						case KEYWORD_FIRE:
						case KEYWORD_PAD_RL:
							if (inputCode >= 0)
								SaveManager::g_save99Data.saveStructs[CONTROL_MAPPING_FIRE].dInputCode = inputCode;
							break;
						case KEYWORD_JUMP:
						case KEYWORD_PAD_RD:
							if (inputCode >= 0)
								SaveManager::g_save99Data.saveStructs[CONTROL_MAPPING_JUMP].dInputCode = inputCode;
							break;
						case KEYWORD_CAMLEFT:
						case KEYWORD_PAD_FRB:
							if (inputCode >= 0)
								SaveManager::g_save99Data.saveStructs[CONTROL_MAPPING_CAMLEFT].dInputCode = inputCode;
							break;
						case KEYWORD_CAMRIGHT:
						case KEYWORD_PAD_FLB:
							if (inputCode >= 0)
								SaveManager::g_save99Data.saveStructs[CONTROL_MAPPING_CAMRIGHT].dInputCode = inputCode;
							break;
						case KEYWORD_HELMETVIEW:
						case KEYWORD_PAD_FLT:
							if (inputCode >= 0)
								SaveManager::g_save99Data.saveStructs[CONTROL_MAPPING_HELMETVIEW].dInputCode = inputCode;
							break;
						case KEYWORD_PAUSE:
						case KEYWORD_PAD_START:
							if (inputCode >= 0)
								SaveManager::g_save99Data.saveStructs[CONTROL_MAPPING_PAUSE].dInputCode = inputCode;
							break;
						case KEYWORD_SELECT:
						case KEYWORD_PAD_SEL:
							if (inputCode >= 0)
								SaveManager::g_save99Data.saveStructs[CONTROL_MAPPING_SELECT].dInputCode = inputCode;
							break;
						case KEYWORD_DERVISH:
						case KEYWORD_PAD_RR:
							if (inputCode >= 0)
								SaveManager::g_save99Data.saveStructs[CONTROL_MAPPING_DERVISH].dInputCode = inputCode;
							break;
						case KEYWORD_PAD_RU:
							if (inputCode >= 0)
								SaveManager::g_save99Data.saveStructs[CONTROL_MAPPING_PAD_RU].dInputCode = inputCode;
							break;
						case KEYWORD_PAD_FRT:
							if (inputCode >= 0)
								SaveManager::g_save99Data.saveStructs[CONTROL_MAPPING_PAD_FRT].dInputCode = inputCode;
							break;
					}
					break;
				}
				case KEYWORD_COMMENT: {
					memset(g_iniToken, 0, sizeof(g_iniToken));
					char* output = g_iniToken;
					int32_t character;
					do
					{
						character = fgetc(g_iniFile);
						*output++ = (char)character;
					} while (character != EOF && character != '\n');
					break;
				}
				case KEYWORD_LANGUAGE:
					do
					{
						ReadToken();
					} while (strcmp("=", g_iniToken) != 0);
					ReadToken();
					if (g_iniToken[0] == '#' || FindKeyword(g_iniToken) >= 0)
					{
						Logger::Log("ReadIniFile : LANGUAGE set to %s.\n", g_iniToken);
						strncpy(AudioManager::g_sfxSubPath, g_iniToken, 2);
						g_language = FindKeyword(AudioManager::g_sfxSubPath);
					}
					break;
				case KEYWORD_IF:
					ReadToken();
					if (strcmp(g_iniToken, AudioManager::g_sfxSubPath) != 0)
					{
						do
						{
							ReadToken();
						} while (strcmp("ENDIF", g_iniToken) != 0);
					}
					break;
				case KEYWORD_DEFTEXT:
					ParseDefText();
					break;
				case KEYWORD_CHEAT:
					g_cheatsEnabled = g_cheatsEnabled == 0;
					break;
				case KEYWORD_HIMPEG:
					g_highQualityMpeg = g_highQualityMpeg == 0;
					break;
				case KEYWORD_NOMPEG:
					g_mpegPlaybackDisabled = 1;
					Logger::Log("ReadIniFile : Mpeg play OFF.\n");
					break;
				case KEYWORD_QUIET:
					AudioManager::g_quietMode = 1;
					Logger::Log("ReadIniFile : QUIET set TRUE\n");
					break;
				case KEYWORD_LOG:
					Logger::g_logsEnabled = 1;
					break;
				case KEYWORD_FINISHED:
					fclose(g_iniFile);
					return;
			}
		}
	}

	// GLOBAL: TOY2 0x004F5C9C
	char g_saveGamePrompt[] = "SAVE GAME?";

	// GLOBAL: TOY2 0x004F5CA8
	char g_saveGameSeparator[] = "   /   ";

	// GLOBAL: TOY2 0x004F5CB0
	char g_yesText[] = "YES    ";

	// GLOBAL: TOY2 0x004F5CB8
	char g_noText[] = "    NO ";

	// GLOBAL: TOY2 0x004F5F54
	extern const char g_creditsText[] = {
#include "CreditsText.inc"
	};

	namespace MovieViewer
	{
		struct SpriteTile
		{
			uint8_t sheetIndex;
			uint8_t tileIndex;
		};

		struct MovieDefinition
		{
			SpriteTile thumbnail;
			SpriteTile unusedTile;
		};

		struct MenuItem
		{
			SpriteTile thumbnail;
			uint8_t movieIndex;
			uint8_t terminator;
		};

		// GLOBAL: TOY2 0x004F6860
		char g_selectPrompt[] = "press jump to select";

		// GLOBAL: TOY2 0x004F6E3C
		MovieDefinition g_movieDefinitions[20] = {
			{ { 0x42, 1 }, { 0x41, 3 } },
			{ { 0x43, 4 }, { 0x41, 3 } },
			{ { 0x43, 5 }, { 0x41, 3 } },
			{ { 0x43, 3 }, { 0x41, 3 } },
			{ { 0x43, 2 }, { 0x41, 3 } },
			{ { 0x41, 3 }, { 0x41, 3 } },
			{ { 0x42, 3 }, { 0x41, 3 } },
			{ { 0x42, 0 }, { 0x41, 3 } },
			{ { 0x43, 1 }, { 0x41, 3 } },
			{ { 0x41, 2 }, { 0x41, 3 } },
			{ { 0x41, 1 }, { 0x41, 3 } },
			{ { 0x41, 0 }, { 0x41, 3 } },
			{ { 0x44, 2 }, { 0x41, 3 } },
			{ { 0x44, 0 }, { 0x41, 3 } },
			{ { 0x44, 1 }, { 0x41, 3 } },
			{ { 0x44, 3 }, { 0x41, 3 } },
			{ { 0x43, 0 }, { 0x41, 3 } },
			{ { 0x42, 6 }, { 0x41, 3 } },
			{ { 0x42, 2 }, { 0x41, 3 } },
			{ { 0xFF, 0xFF }, { 0xFF, 0xFF } },
		};

		// GLOBAL: TOY2 0x004F6E8C
		uint8_t g_movieOrder[20] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 16, 12, 13, 14, 15, 17, 18, 0xFF };

		STATIC_ASSERT(sizeof(SpriteTile) == 2);
		STATIC_ASSERT(sizeof(MovieDefinition) == 4);
		STATIC_ASSERT(sizeof(MenuItem) == 4);
	}

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

	// GLOBAL: TOY2 0x0053CA58
	int32_t g_perspectiveTableHalfWidth;

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

	// GLOBAL: TOY2 0x0072E354
	char g_saveSlotDescriptions[8][256];

	// GLOBAL: TOY2 0x0072EF94
	int32_t g_movieTimingRate;

	// GLOBAL: TOY2 0x0072EFB0
	uint32_t g_movieTimingStartMs;

	// GLOBAL: TOY2 0x00731F0C
	uint32_t g_cpuClockHz;

	// GLOBAL: TOY2 0x00726F44
	int32_t g_cpuProfileTotalMs;

	// GLOBAL: TOY2 0x00726F48
	int32_t g_cpuProfileSampleIndex;

	// GLOBAL: TOY2 0x00726F4C
	int32_t g_cpuProfileStartMs;

	// GLOBAL: TOY2 0x00726F50
	int32_t g_cpuProfileWorkIndex;

	// GLOBAL: TOY2 0x00726F54
	int32_t g_cpuProfileSamples[10];

	// GLOBAL: TOY2 0x00726F7C
	float g_cpuProfileWorkValue;

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

	// GLOBAL: TOY2 0x0052F1C0
	FramePulseOutputs g_framePulseOutputs;

	// GLOBAL: TOY2 0x0052AD68
	uint16_t g_framePhase;

	// GLOBAL: TOY2 0x0052AD58
	FramePulsePhases g_framePulsePhases;

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

	// GLOBAL: TOY2 0x0052AD84
	int32_t g_demoExitInput;

	// GLOBAL: TOY2 0x0052AD6A
	int16_t g_unusedLevelState[9];

	// GLOBAL: TOY2 0x0052ADB0
	int16_t g_pauseMenuState;

	enum PauseMenuState
	{
		PAUSE_MENU_MAIN = 0,
		PAUSE_MENU_CAMERA = 1,
		PAUSE_MENU_VOLUME = 2,
		PAUSE_MENU_QUIT = 3,
		PAUSE_MENU_SECRET = 4,
	};

	// GLOBAL: TOY2 0x0052B7E4
	int16_t g_pauseMenuSelection;

	// GLOBAL: TOY2 0x005039BC
	uint8_t g_pauseMenuEntryCounts[5] = { 4, 2, 2, 2, 2 };

	// GLOBAL: TOY2 0x00502750
	char g_pauseSoundVolumeText[16] = "sfx **********";

	// GLOBAL: TOY2 0x00502760
	char g_pauseMusicVolumeText[16] = "bgm **********";

	// GLOBAL: TOY2 0x0050A0B0
	int32_t g_idleVoicePreset;

	// GLOBAL: TOY2 0x0050A0BC
	int32_t g_idleVoiceCooldown;

	// GLOBAL: TOY2 0x00830C90
	int32_t g_gadgetRespawnTimer;

	// GLOBAL: TOY2 0x00830D20
	uint8_t g_environmentTintGreen;

	// GLOBAL: TOY2 0x00830D2C
	uint8_t g_environmentTintRed;

	// GLOBAL: TOY2 0x00830D2D
	uint8_t g_environmentTintBlue;

	// GLOBAL: TOY2 0x00830D38
	int32_t g_cameraIdleTimer;

	// GLOBAL: TOY2 0x00830D44
	int32_t g_levelInteractionTimer;

	// GLOBAL: TOY2 0x00830D4C
	int32_t g_specialPickupCount;

	// GLOBAL: TOY2 0x00830E24
	uint32_t g_savedUnlocks;

	// GLOBAL: TOY2 0x00830E2C
	Nu3D::Particles::ParticleInstance* g_laserAimParticle;

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

	// GLOBAL: TOY2 0x0052EF40
	int32_t g_demoInputRunLength;

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
	{
		// FUNCTION: TOY2 0x0049FC60 [MATCHED]
		int32_t TickSlide(int32_t slideIndex)
		{
			if (g_isPaused == 0)
			{
				int16_t timer = g_slideTimers[slideIndex];

				if (timer != 0 && (g_gameplayStateFlags & GAMEPLAY_STATE_CUTSCENE_ACTIVE) == 0)
				{
					if (g_slideAngles[slideIndex] < 0x400)
					{
						g_slideAngles[slideIndex] += Renderer::g_frameDelta << 5;
						if (g_slideAngles[slideIndex] >= 0x400)
							g_slideAngles[slideIndex] = 0x400;
					}

					if (timer < 1000)
					{
						g_slideTimers[slideIndex] = timer - (int16_t)Renderer::g_frameDelta;
						if (g_slideTimers[slideIndex] <= 0)
							g_slideTimers[slideIndex] = 0;
					}
				}
				else if (g_slideAngles[slideIndex] > 0)
				{
					g_slideAngles[slideIndex] -= Renderer::g_frameDelta << 5;
					if (g_slideAngles[slideIndex] <= 0)
						g_slideAngles[slideIndex] = 0;
				}
			}

			int16_t angle = g_slideAngles[slideIndex];
			if (angle == 0)
				return 1;

			return (Numerics::g_sinCosLUT[angle] >> 8) - 0x40;
		}
	}

	// STUB: TOY2 0x0049FD40
	void RenderHUD() {}

	// FUNCTION: TOY2 0x004A2960 [MATCHED]
	void UpdateFrameTimers()
	{
		uint8_t frameDelta = (uint8_t)Renderer::g_frameDelta;

		g_framePulseOutputs.frameDelta = frameDelta;
		g_framePulseOutputs.twoTickCount = 0;
		g_framePulsePhases.twoTick += frameDelta;
		while (g_framePulsePhases.twoTick >= 2)
		{
			g_framePulsePhases.twoTick -= 2;
			g_framePulseOutputs.twoTickCount++;
		}

		g_framePulseOutputs.threeTick = 0;
		g_framePulsePhases.threeTick += frameDelta;
		while (g_framePulsePhases.threeTick >= 3)
		{
			g_framePulsePhases.threeTick -= 3;
			g_framePulseOutputs.threeTick++;
		}

		g_framePulseOutputs.fourTick = 0;
		g_framePulsePhases.fourTick += frameDelta;
		if (g_framePulsePhases.fourTick >= 4)
		{
			g_framePulsePhases.fourTick -= 4;
			g_framePulseOutputs.fourTick = 1;
		}

		g_framePulseOutputs.fiveTick = 0;
		g_framePulsePhases.fiveTick += frameDelta;
		if (g_framePulsePhases.fiveTick >= 5)
		{
			g_framePulsePhases.fiveTick -= 5;
			g_framePulseOutputs.fiveTick = 1;
		}

		g_framePulseOutputs.sixTick = 0;
		g_framePulsePhases.sixTick += frameDelta;
		if (g_framePulsePhases.sixTick >= 6)
		{
			g_framePulsePhases.sixTick -= 6;
			g_framePulseOutputs.sixTick = 1;
		}

		g_framePulseOutputs.sevenTick = 0;
		g_framePulsePhases.sevenTick += frameDelta;
		if (g_framePulsePhases.sevenTick >= 7)
		{
			g_framePulsePhases.sevenTick -= 7;
			g_framePulseOutputs.sevenTick = 1;
		}

		g_framePulseOutputs.eightTick = 0;
		g_framePulsePhases.eightTick += frameDelta;
		if (g_framePulsePhases.eightTick >= 8)
		{
			g_framePulsePhases.eightTick -= 8;
			g_framePulseOutputs.eightTick = 1;
		}

		g_framePulseOutputs.sixteenTick = 0;
		g_framePulsePhases.sixteenTick += frameDelta;
		if (g_framePulsePhases.sixteenTick >= 16)
		{
			g_framePulsePhases.sixteenTick -= 16;
			g_framePulseOutputs.sixteenTick = 1;
		}

		g_framePulseOutputs.thirtyTwoTick = 0;
		g_framePulsePhases.thirtyTwoTick += frameDelta;
		if (g_framePulsePhases.thirtyTwoTick >= 32)
		{
			g_framePulsePhases.thirtyTwoTick -= 32;
			g_framePulseOutputs.thirtyTwoTick = 1;
		}

		g_framePulseOutputs.sixtyFourTick = 0;
		g_framePulsePhases.sixtyFourTick += frameDelta;
		if (g_framePulsePhases.sixtyFourTick >= 64)
		{
			g_framePulsePhases.sixtyFourTick -= 64;
			g_framePulseOutputs.sixtyFourTick = 1;
		}

		int32_t activeSoundMask = 0;
		if (AudioManager::g_maxLeftVolume > 0)
		{
			AudioManager::g_maxLeftVolume -= (int16_t)Renderer::g_frameDelta;
			activeSoundMask = 1;
		}

		if (AudioManager::g_maxRightVolume > 0)
		{
			AudioManager::g_maxRightVolume -= (int16_t)Renderer::g_frameDelta;
			activeSoundMask |= (uint8_t)AudioManager::g_maxVolume & 0xFE;
		}
		else
		{
			AudioManager::g_maxVolume = 0;
		}

		if (g_demoMode != 1 && (SaveManager::g_save0Data.cameraType & SaveManager::CAMERA_PASSIVE) != 0)
		{
			if (activeSoundMask != 0)
			{
				g_cameraIdleTimer += Renderer::g_frameDelta;
				if (g_cameraIdleTimer > 360)
					g_cameraIdleTimer = 300;
			}
			else
			{
				g_cameraIdleTimer = 0;
			}
		}

		AudioManager::UpdateSoundSequences();

		if (g_randDatBufferPtr > &g_randDatBuffer[1500])
			g_randDatBufferPtr -= 1500;

		if (g_levelTransitionTimer > 0)
		{
			g_levelTransitionTimer -= (int16_t)Renderer::g_frameDelta;
			if (g_levelTransitionTimer < 0)
				g_levelTransitionTimer = 0;
		}

		if (g_levelTransitionTimer < 46 && g_levelTransitionTimer + Renderer::g_frameDelta >= 46 && g_levelTransition > 0)
		{
			Nu3D::Camera::g_targetTintBlue = 0;
			Nu3D::Camera::g_targetTintGreen = 0;
			Nu3D::Camera::g_targetTintRed = 0;
			Nu3D::Camera::g_targetTintFadeSpeed = 6;
			Nu3D::Camera::g_tintBlend = -1;
		}

		Nu3D::Camera::FadeToTargetTint();
	}

	namespace Game
	{
		const uint16_t ACTOR_FLAG_IGNORE_RESPAWN_VISIBILITY = 0x40;

		// FUNCTION: TOY2 0x00406CD0 [PROVISIONAL]
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
			actor->animationFrameSequence = Actor::g_animationFrameSequences[1];
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
				case 9:
				case 16:
				case 21:
				case 29:
				case 30:
				case 34:
				case 35:
				case 36:
				case 37:
				case 39:
				case 42:
				case 49:
				case 54:
				case 55:
				case 57:
				case 62:
					break;
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

		// FUNCTION: TOY2 0x0049F4B0 [PROVISIONAL]
		void MenuLoop()
		{
			int32_t restoreCamera = 0;
			if ((InputManager::g_curButtonsPressed & INPUT_SECRET_MENU) != 0 && (InputManager::g_prevButtonsPressed & INPUT_SECRET_MENU) == 0)
			{
				AudioManager::PlaySoundEffect(0x3D, 0);
				g_pauseMenuState = PAUSE_MENU_SECRET;
				g_pauseMenuSelection = 0;
			}

			if ((InputManager::g_curButtonsPressed & INPUT_DOWN) != 0 && (InputManager::g_prevButtonsPressed & INPUT_DOWN) == 0
				&& g_pauseMenuSelection < g_pauseMenuEntryCounts[g_pauseMenuState] - 1)
			{
				AudioManager::PlaySoundEffect(0x3F, 0);
				g_pauseMenuSelection++;
				g_pauseCheatTimer = 0xEC4;
			}

			if ((InputManager::g_curButtonsPressed & INPUT_UP) != 0 && (InputManager::g_prevButtonsPressed & INPUT_UP) == 0 && g_pauseMenuSelection > 0)
			{
				AudioManager::PlaySoundEffect(0x3F, 0);
				g_pauseMenuSelection--;
				g_pauseCheatTimer = 0xEC4;
			}

			int32_t cancelPressed = (InputManager::g_curButtonsPressed & INPUT_CANCEL) != 0 && (InputManager::g_prevButtonsPressed & INPUT_CANCEL) == 0;
			if (((InputManager::g_curButtonsPressed & INPUT_JUMP) != 0 && (InputManager::g_prevButtonsPressed & INPUT_JUMP) == 0) || cancelPressed)
			{
				switch (g_pauseMenuState)
				{
					case PAUSE_MENU_MAIN:
						if (cancelPressed)
							break;

						if (g_pauseMenuSelection == 0)
						{
							AudioManager::PlaySoundEffect(0x3E, 0);
							g_isPaused = 0;
							AudioManager::PlayMusicLooping((int16_t)AudioManager::g_curTrackIndex);
							if (g_buzzActor.airborneMode == 0)
								g_buzzActor.airborneMode = 5;
							g_movementInputLockTimer = 10;
							restoreCamera = 1;
						}
						if (g_pauseMenuSelection == 1)
						{
							AudioManager::PlaySoundEffect(0x3D, 0);
							g_pauseMenuState = PAUSE_MENU_CAMERA;
							g_pauseMenuSelection = 0;
						}
						else if (g_pauseMenuSelection == 2)
						{
							g_pauseMusicVolume = SaveManager::g_save0Data.musicVolume;
							g_pauseSoundVolume = SaveManager::g_save0Data.soundVolume;
							AudioManager::PlaySoundEffect(0x3D, 0);
							g_pauseMenuState = PAUSE_MENU_VOLUME;
							g_pauseMenuSelection = 0;
						}
						else if (g_pauseMenuSelection == 3)
						{
							AudioManager::PlaySoundEffect(0x3D, 0);
							g_pauseMenuState = PAUSE_MENU_QUIT;
							g_pauseMenuSelection = 0;
						}
						break;

					case PAUSE_MENU_CAMERA:
						if (g_pauseMenuSelection == 0)
						{
							if (! cancelPressed)
								SaveManager::g_save0Data.cameraType &= ~SaveManager::CAMERA_ACTIVE;
							g_pauseMenuState = PAUSE_MENU_MAIN;
						}
						else if (g_pauseMenuSelection == 1)
						{
							if (! cancelPressed)
								SaveManager::g_save0Data.cameraType |= SaveManager::CAMERA_ACTIVE;
							g_pauseMenuState = PAUSE_MENU_MAIN;
							g_pauseMenuSelection = 0;
						}
						AudioManager::PlaySoundEffect(0x3E, 0);
						break;

					case PAUSE_MENU_VOLUME:
						if (g_pauseMenuSelection == 0)
							g_pauseMenuState = PAUSE_MENU_MAIN;
						else if (g_pauseMenuSelection == 1)
						{
							g_pauseMenuState = PAUSE_MENU_MAIN;
							g_pauseMenuSelection = 0;
						}

						if (! cancelPressed)
						{
							SaveManager::g_save0Data.soundVolume = (uint8_t)g_pauseSoundVolume;
							SaveManager::g_save0Data.musicVolume = (uint8_t)g_pauseMusicVolume;
						}
						AudioManager::SetVolumes(AudioManager::g_musicVolTable[SaveManager::g_save0Data.musicVolume] * 2 / 3,
							AudioManager::g_soundVolTable[SaveManager::g_save0Data.soundVolume] * 3 / 2);
						AudioManager::PlaySoundEffect(0x3E, 0);
						break;

					case PAUSE_MENU_QUIT:
						if (cancelPressed)
							g_pauseMenuSelection = 0;

						if (g_pauseMenuSelection == 0)
						{
							g_pauseMenuState = PAUSE_MENU_MAIN;
							AudioManager::PlaySoundEffect(0x3E, 0);
							g_quitToTitleFlag = 0;
						}
						if (g_pauseMenuSelection == 1)
						{
							g_isPaused = 0;
							AudioManager::PlaySoundEffect(0x3D, 0);
							restoreCamera = 1;
							if (g_levelTransition != 1)
								g_levelTransition = 5;
							g_buzzActor.actorFlags |= Buzz::ACTOR_FLAG_LOCK_FACING;
							g_levelTransitionTimer = 0x2E;
							Nu3D::Camera::g_targetTintBlue = 0;
							Nu3D::Camera::g_targetTintGreen = 0;
							Nu3D::Camera::g_targetTintRed = 0;
							Nu3D::Camera::g_targetTintFadeSpeed = 6;
							Nu3D::Camera::g_tintBlend = -1;
						}
						break;

					case PAUSE_MENU_SECRET:
						if (cancelPressed)
							g_pauseMenuSelection = 0;

						if (g_pauseMenuSelection == 0)
						{
							AudioManager::PlaySoundEffect(0x3E, 0);
							g_isPaused = 0;
							AudioManager::PlayMusicLooping((int16_t)AudioManager::g_curTrackIndex);
							if (g_buzzActor.airborneMode == 0)
								g_buzzActor.airborneMode = 5;
							g_movementInputLockTimer = 10;
							restoreCamera = 1;
						}
						if (g_pauseMenuSelection == 1)
						{
							AudioManager::PlaySoundEffect(0x3D, 0);
							g_pauseMenuState = PAUSE_MENU_QUIT;
							g_pauseMenuSelection = 0;
							g_quitToTitleFlag = 1;
						}
						break;
				}
			}

			if (g_pauseMenuState == PAUSE_MENU_VOLUME)
			{
				if (g_pauseSoundVolume > 0)
					memset(g_pauseSoundVolumeText + 4, '*', g_pauseSoundVolume);
				if (g_pauseSoundVolume < 10)
					memset(g_pauseSoundVolumeText + 4 + g_pauseSoundVolume, ' ', 10 - g_pauseSoundVolume);
				if (g_pauseMusicVolume > 0)
					memset(g_pauseMusicVolumeText + 4, '*', g_pauseMusicVolume);
				if (g_pauseMusicVolume < 10)
					memset(g_pauseMusicVolumeText + 4 + g_pauseMusicVolume, ' ', 10 - g_pauseMusicVolume);

				int32_t selectedVolume = g_pauseMenuSelection == 0 ? g_pauseSoundVolume : g_pauseMusicVolume;
				if ((InputManager::g_curButtonsPressed & INPUT_RIGHT) != 0 && (InputManager::g_prevButtonsPressed & INPUT_RIGHT) == 0 && selectedVolume < 10)
				{
					selectedVolume++;
					AudioManager::PlaySoundEffect(0x3D, 0);
				}
				if ((InputManager::g_curButtonsPressed & INPUT_LEFT) != 0 && (InputManager::g_prevButtonsPressed & INPUT_LEFT) == 0 && selectedVolume > 0)
				{
					selectedVolume--;
					AudioManager::PlaySoundEffect(0x3D, 0);
				}

				if (g_pauseMenuSelection == 0)
					g_pauseSoundVolume = selectedVolume;
				else
					g_pauseMusicVolume = selectedVolume;

				AudioManager::SetVolumes(AudioManager::g_musicVolTable[g_pauseMusicVolume] * 2 / 3, AudioManager::g_soundVolTable[g_pauseSoundVolume] * 3 / 2);
			}

			if ((InputManager::g_curButtonsPressed & INPUT_MENU) != 0 && (InputManager::g_prevButtonsPressed & INPUT_MENU) == 0)
			{
				AudioManager::PlaySoundEffect(0x3E, 0);
				g_isPaused = 0;
				AudioManager::PlayMusicLooping((int16_t)AudioManager::g_curTrackIndex);
				if (g_buzzActor.airborneMode == 0)
					g_buzzActor.airborneMode = 5;
				g_movementInputLockTimer = 10;
				restoreCamera = 1;
			}

			if (restoreCamera)
				Camera::g_renderCameraTransform = g_pauseCameraTarget;
		}

		// FUNCTION: TOY2 0x0049E330 [PROVISIONAL]
		void PauseLoop()
		{
			if (g_pauseMenuBlinkTimer < 100)
				g_pauseMenuBlinkTimer = ((uint8_t)Renderer::g_frameDelta + (uint8_t)g_pauseMenuBlinkTimer) & 0x3F;

			if (g_attractModeTimer >= 0)
			{
				if (g_returnedToTitle == 0 && InputManager::g_curButtonsPressed != 0)
					g_attractModeInputTimer = g_attractModeTimer * 2;

				g_attractModeInputTimer -= Renderer::g_frameDelta;
				if (g_attractModeInputTimer <= 0 || (InputManager::g_curButtonsPressed & INPUT_SECRET_MENU) != 0)
				{
					g_isPaused = 0;
					g_levelTransition = 4;
					g_levelTransitionTimer = 0x2E;
					Nu3D::Camera::SetTint(0, 0, 0, 6);
				}
			}

			Vector3I cameraTarget;
			Vector3I movement;
			int32_t yawDelta;
			if (g_pauseCheatTimer <= 0xE10)
			{
				g_pauseCheatTimer -= Renderer::g_frameDelta;

			check_orbit_camera:
				if (g_pauseCheatTimer <= 0 && (g_gameplayStateFlags & 1) == 0 && Camera::g_scriptedCameraState == 0)
				{
					cameraTarget.x = g_buzzActor.posAngles.pos.x;
					cameraTarget.y = g_buzzActor.posAngles.pos.y - 0x4000;
					cameraTarget.z = g_buzzActor.posAngles.pos.z;

					Camera::g_renderCameraTransform.angles.yaw = (Camera::g_renderCameraTransform.angles.yaw + Renderer::g_frameDelta * 8) & 0xFFF;
					int32_t yaw = (int16_t)Camera::g_renderCameraTransform.angles.yaw;
					movement.x = -Numerics::g_sinCosLUT[yaw] * 2;
					movement.y = 0;
					movement.z = -Numerics::g_sinCosLUT[(yaw + 0x400) & 0xFFF] * 2;
					g_pauseCheatTimer = 0;
					Collision::SweepAndSlide(&cameraTarget, &movement, 0x8000, 0, 0x100);
					cameraTarget.x += movement.x;
					cameraTarget.y += movement.y;
					cameraTarget.z += movement.z;
					goto smooth_camera;
				}
			}
			else
			{
				if (InputManager::g_curButtonsPressed == (INPUT_CAMERA_RIGHT | INPUT_VISOR_TOGGLE))
				{
					g_pauseCheatTimer -= Renderer::g_frameDelta;
					if (g_pauseCheatTimer <= 0xE10)
					{
						if (g_buzzActor.coinsCollected == 7)
							g_buzzActor.lives = 9;
						else if (g_buzzActor.coinsCollected == 5)
							g_buzzActor.health = 14;
						g_pauseCheatTimer = 0;
					}
					goto check_orbit_camera;
				}
				else
				{
					g_pauseCheatTimer = 0xEC4;
				}
			}

			cameraTarget = g_pauseCameraTarget.pos;
			Camera::g_renderCameraTransform.angles.yaw &= 0xFFF;
			yawDelta = (Camera::g_renderCameraTransform.angles.yaw - g_pauseCameraTarget.angles.yaw) & 0xFFF;
			if (yawDelta > 0x800)
				yawDelta -= 0x1000;
			Camera::g_renderCameraTransform.angles.yaw -= Renderer::g_frameDelta * yawDelta / 16;

		smooth_camera:
			Camera::g_renderCameraTransform.pos.x -= (Camera::g_renderCameraTransform.pos.x - cameraTarget.x) * Renderer::g_frameDelta / 16;
			Camera::g_renderCameraTransform.pos.y -= (Camera::g_renderCameraTransform.pos.y - cameraTarget.y) * Renderer::g_frameDelta / 16;
			Camera::g_renderCameraTransform.pos.z -= (Camera::g_renderCameraTransform.pos.z - cameraTarget.z) * Renderer::g_frameDelta / 16;

			Nu3D::Camera::ApplyTransformToCamera(&Camera::g_renderCameraTransform);
			RenderHUD();
			Nullsub3();
			RenderGame(1);
			MenuLoop();
		}

		// FUNCTION: TOY2 0x0049DFE0 [PROVISIONAL]
		void MainLoop()
		{
			{
				int16_t input = InputManager::g_curButtonsPressed;
				int32_t demoMode = g_demoMode;

				if (demoMode == 2)
				{
					if (input == InputManager::g_prevButtonsPressed)
					{
						g_demoInputRunLength++;
					}
					else
					{
						g_demoInputBuffer[g_demoPathWriteIdx] = InputManager::g_prevButtonsPressed;
						g_demoInputBuffer[g_demoPathWriteIdx + 1] = (int16_t)g_demoInputRunLength;
						g_demoPathWriteIdx += 2;
						g_demoInputRunLength = 0;
						if (g_demoPathWriteIdx >= 0x800)
							g_demoPathWriteIdx = 0x7FE;
					}
				}

				if (g_attractModeTimer >= 0)
				{
					if (g_returnedToTitle == 0 && input != 0)
						g_attractModeInputTimer = g_attractModeTimer * 2;

					g_attractModeInputTimer -= Renderer::g_frameDelta;
					if (g_attractModeInputTimer <= 0)
					{
						input |= INPUT_SECRET_MENU;
						InputManager::g_curButtonsPressed = input;
					}
				}

				if (demoMode == 1)
				{
					g_demoExitInput = input;
					g_demoInputRunLength--;
					if (g_demoInputRunLength < 0)
					{
						g_demoInputRunLength = g_demoInputBuffer[g_demoPathWriteIdx + 3];
						g_demoPathWriteIdx += 2;
					}
					input = g_demoInputBuffer[g_demoPathWriteIdx];
					InputManager::g_curButtonsPressed = input;
				}

				InputManager::g_directionInputState3Frames = InputManager::g_directionInputState2Frames;
				InputManager::g_directionInputState2Frames = InputManager::g_prevDirectionInputState;
				InputManager::g_prevDirectionInputState = InputManager::g_directionInputState;
				if (demoMode == 1)
				{
					InputManager::g_directionInputState = input;
				}
				else
				{
					InputManager::UpdateDirectionInputState();
				}

				if ((g_buzzActor.actorFlags & Buzz::ACTOR_FLAG_LOCK_FACING) != 0 || (g_levelTransition == 0 && g_levelTransitionTimer > 60))
					InputManager::g_directionInputState &= INPUT_SECRET_MENU | INPUT_MENU | INPUT_CAMERA_LEFT | INPUT_CAMERA_RIGHT;
			}

			Shadow::ResetShadowCount();
			Cutscene::Update();
			Buzz::HandleGameplay(&g_buzzActor);
			Camera::UpdateActiveTransform();
			Nu3D::Camera::ApplyTransformToCamera(&Camera::g_renderCameraTransform);
			UpdateActors();
			Buzz::UpdateRespawnAnchor();
			Collectables::Interactions();
			Buzz::TickGadgets();
			Buzz::CheckParticleCollisions();
			if (g_buzzActor.health >= 0)
				ActorCollisionCheck();
			HandleLevelInteractions(g_levelFileIndex);
			Lighting::UpdateBuzzLight();
			g_buzzActor.unusedFrameState = 0;
			Buzz::UpdateAnimationState();
			AdvanceFramePhase();
			Buzz::UpdateContactEffects();
			Levels::UpdateAmbientEmitters();
			Nu3D::Particles::Update();

			if (g_demoMode == 0 && g_attractModeTimer >= 0 && (InputManager::g_curButtonsPressed & INPUT_SECRET_MENU) != 0 && g_levelTransition == 0)
			{
				g_levelTransition = 4;
				g_levelTransitionTimer = 0x2E;
				Nu3D::Camera::SetTint(0, 0, 0, 6);
			}

			if (g_demoMode != 0 && (InputManager::g_curButtonsPressed & INPUT_MENU) != 0 && g_levelTransition == 0)
			{
				g_levelTransition = 3;
				g_levelTransitionTimer = 0x2E;
				Nu3D::Camera::SetTint(0, 0, 0, 6);
			}

			if (g_demoMode != 0 && g_demoExitInput != 0 && g_levelTransition == 0)
			{
				g_levelTransition = 4;
				g_levelTransitionTimer = 0x2E;
				Nu3D::Camera::SetTint(0, 0, 0, 6);
			}

			if ((InputManager::g_curButtonsPressed & INPUT_MENU) != 0 && (InputManager::g_prevButtonsPressed & INPUT_MENU) == 0 && g_demoMode == 0
				&& Collectables::g_tokenCollectionState != Collectables::TOKEN_COLLECTION_STATE_CUTSCENE)
				goto open_pause_menu;

			if (g_demoMode != 0 || InputManager::g_directionalInputCount != 3)
				goto finish_frame;

		open_pause_menu:
			if (Nu3D::Camera::g_targetTintFadeSpeed == 0 && Nu3D::Camera::g_cameraTintRed != 0)
			{
				AudioManager::StopAndWait();
				AudioManager::FlushSoundVoices();
				g_pauseMenuBlinkTimer = 0;
				g_isPaused = 1;
				g_pauseMenuSelection = 0;
				g_pauseMenuState = 0;
				g_quitToTitleFlag = 0;
				AudioManager::PlaySoundEffect(0x3D, 0);
				g_pauseCameraTarget = Camera::g_renderCameraTransform;
				g_pauseCheatTimer = 0xEC4;
			}

		finish_frame:
			UpdateFrameTimers();
			RenderHUD();
			Actor::PopulateActiveActors();
			Nullsub3();
			RenderGame(1);
		}
	}

	namespace PostGameRecap
	{
		// STUB: TOY2 0x004398B0
		void Tick() {}
	}

	namespace PostGameSaveMenu
	{
		// FUNCTION: TOY2 0x0043A130 [EFFECTIVE]
		int32_t Tick()
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
			SetBackdropByIndex(0);

			int32_t fadeTimer = 4000;
			int32_t saveRequested = 1;
			AudioManager::PlayMusicLooping(19);
			g_screenMusicStarted = 1;
			Nu3D::Camera::SetTint(128, 128, 128, 6);

			while (true)
			{
				Nu3D::Camera::FadeToTargetTint();
				SoftwareRenderer::SetBackdropScrollOverride(0, 0);

				if (fadeTimer > 1000)
				{
					if ((InputManager::g_curButtonsPressed & INPUT_LEFT) != 0 && saveRequested == 0)
					{
						saveRequested = 1;
						AudioManager::PlayOneShotSoundGlobal(1, 0x1200, 0x40, 0x60);
					}
					if ((InputManager::g_curButtonsPressed & INPUT_RIGHT) != 0 && saveRequested == 1)
					{
						saveRequested = 0;
						AudioManager::PlayOneShotSoundGlobal(1, 0x1200, 0x60, 0x40);
					}
				}

				if (saveRequested != 0)
					Renderer::Sprite::DrawTile(0x1C0, 0x70, 0x39, 1);
				else
					Renderer::Sprite::DrawTile(0x20, 0x70, 0x39, 0);

				Nullsub3();
				Renderer::Sprite::DrawWhiteText(LevelSelect::g_jumpToSelectTxt, 200, 256);
				Renderer::Sprite::DrawWhiteText(g_saveGamePrompt, 48, 256);
				Renderer::Sprite::DrawWhiteText(g_saveGameSeparator, 128, 256);
				if (saveRequested != 0)
				{
					Renderer::Sprite::DrawWhiteText(g_yesText, 128, 256);
					Renderer::DrawBitmapText(g_noText, 128, 256, 64, 64, 64, 0);
				}
				else
				{
					Renderer::Sprite::DrawWhiteText(g_noText, 128, 256);
					Renderer::DrawBitmapText(g_yesText, 128, 256, 64, 64, 64, 0);
				}

				Nullsub6();
				MainMenu::RenderMenu();

				if (fadeTimer > 0 && fadeTimer < 1000)
					fadeTimer -= Renderer::g_frameDelta;

				if ((InputManager::g_curButtonsPressed & INPUT_JUMP) != 0 && (InputManager::g_prevButtonsPressed & INPUT_JUMP) == 0 && fadeTimer > 0x17
					&& Nu3D::Camera::g_cameraTintBlue == 128)
				{
					Nu3D::Camera::SetTint(0, 0, 0, 6);
					fadeTimer = 0x35;
					AudioManager::PlayOneShotSoundGlobal(0, 0x1200, 0x60, 0x60);
				}
				else if (fadeTimer <= 0)
				{
					AudioManager::StopAndWait();
					return saveRequested;
				}
			}
		}
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

	namespace Portal
	{
		// GLOBAL: TOY2 0x0054D920
		Vector3I g_previousSectorPosition;

		// GLOBAL: TOY2 0x005551B0
		Vector3I16 g_portalNormal;

		// GLOBAL: TOY2 0x005551E0
		Vector3I g_portalPlaneNormal;

		// GLOBAL: TOY2 0x00555368
		Vector3I g_portalIntersectionPoint;

		// FUNCTION: TOY2 0x0043FEF0 [PROVISIONAL]
		void UpdateActiveSector()
		{
			Levels::PortalRecord** portalRecords = reinterpret_cast<Levels::PortalRecord**>(Levels::g_recordData);
			Levels::PortalEntry* entry = Levels::g_portalZones[Sector::g_currentSectorIndex].entries;
			if (entry->recordIdx == 0xFF)
				return;

			do
			{
				if (entry->categoryIdx != 15 || g_hasBackdrop == 0)
				{
					if ((abs((g_buzzActor.posAngles.pos.x >> 7) - portalRecords[entry->recordIdx]->origin.x) < 0x4000
							&& abs((g_buzzActor.posAngles.pos.y >> 7) - portalRecords[entry->recordIdx]->origin.y) < 0x4000
							&& abs((g_buzzActor.posAngles.pos.z >> 7) - portalRecords[entry->recordIdx]->origin.z) < 0x4000)
						|| (abs((g_buzzActor.posAngles.pos.x >> 7) - portalRecords[entry->recordIdx]->vertices[1].position.x) < 0x4000
							&& abs((g_buzzActor.posAngles.pos.y >> 7) - portalRecords[entry->recordIdx]->vertices[1].position.y) < 0x4000
							&& abs((g_buzzActor.posAngles.pos.z >> 7) - portalRecords[entry->recordIdx]->vertices[1].position.z) < 0x4000))
					{
						g_portalPlaneNormal.x = portalRecords[entry->recordIdx]->normal.x;
						g_portalPlaneNormal.y = portalRecords[entry->recordIdx]->normal.y;
						g_portalPlaneNormal.z = portalRecords[entry->recordIdx]->normal.z;

						int32_t currentDistance = ((g_buzzActor.posAngles.pos.x >> 7) - portalRecords[entry->recordIdx]->origin.x) * g_portalPlaneNormal.x
							+ ((g_buzzActor.posAngles.pos.y >> 7) - portalRecords[entry->recordIdx]->origin.y) * g_portalPlaneNormal.y
							+ ((g_buzzActor.posAngles.pos.z >> 7) - portalRecords[entry->recordIdx]->origin.z) * g_portalPlaneNormal.z;

						if (currentDistance < 0)
						{
							int32_t previousDistance = ((g_previousSectorPosition.x >> 7) - portalRecords[entry->recordIdx]->origin.x) * g_portalPlaneNormal.x
								+ ((g_previousSectorPosition.y >> 7) - portalRecords[entry->recordIdx]->origin.y) * g_portalPlaneNormal.y
								+ ((g_previousSectorPosition.z >> 7) - portalRecords[entry->recordIdx]->origin.z) * g_portalPlaneNormal.z;

							if (previousDistance >= 0)
							{
								g_portalNormal.x = (int16_t)g_portalPlaneNormal.x;
								g_portalNormal.y = (int16_t)g_portalPlaneNormal.y;
								g_portalNormal.z = (int16_t)g_portalPlaneNormal.z;

								int32_t distanceDelta = previousDistance - currentDistance;
								g_portalIntersectionPoint.x =
									(((g_buzzActor.posAngles.pos.x - g_previousSectorPosition.x) * previousDistance / distanceDelta
										 + g_previousSectorPosition.x)
										>> 7)
									- portalRecords[entry->recordIdx]->origin.x;
								g_portalIntersectionPoint.y =
									(((g_buzzActor.posAngles.pos.y - g_previousSectorPosition.y) * previousDistance / distanceDelta
										 + g_previousSectorPosition.y)
										>> 7)
									- portalRecords[entry->recordIdx]->origin.y;
								g_portalIntersectionPoint.z =
									(((g_buzzActor.posAngles.pos.z - g_previousSectorPosition.z) * previousDistance / distanceDelta
										 + g_previousSectorPosition.z)
										>> 7)
									- portalRecords[entry->recordIdx]->origin.z;

								if (Nu3D::Math::PointIntersectsTriangle(g_portalIntersectionPoint.x,
										g_portalIntersectionPoint.y,
										g_portalIntersectionPoint.z,
										portalRecords[entry->recordIdx]->vertices[0].position.x - portalRecords[entry->recordIdx]->origin.x,
										portalRecords[entry->recordIdx]->vertices[0].position.y - portalRecords[entry->recordIdx]->origin.y,
										portalRecords[entry->recordIdx]->vertices[0].position.z - portalRecords[entry->recordIdx]->origin.z,
										portalRecords[entry->recordIdx]->vertices[1].position.x - portalRecords[entry->recordIdx]->origin.x,
										portalRecords[entry->recordIdx]->vertices[1].position.y - portalRecords[entry->recordIdx]->origin.y,
										portalRecords[entry->recordIdx]->vertices[1].position.z - portalRecords[entry->recordIdx]->origin.z,
										&g_portalNormal,
										400)
									|| Nu3D::Math::PointIntersectsTriangle(g_portalIntersectionPoint.x,
										g_portalIntersectionPoint.y,
										g_portalIntersectionPoint.z,
										portalRecords[entry->recordIdx]->vertices[1].position.x - portalRecords[entry->recordIdx]->origin.x,
										portalRecords[entry->recordIdx]->vertices[1].position.y - portalRecords[entry->recordIdx]->origin.y,
										portalRecords[entry->recordIdx]->vertices[1].position.z - portalRecords[entry->recordIdx]->origin.z,
										portalRecords[entry->recordIdx]->vertices[2].position.x - portalRecords[entry->recordIdx]->origin.x,
										portalRecords[entry->recordIdx]->vertices[2].position.y - portalRecords[entry->recordIdx]->origin.y,
										portalRecords[entry->recordIdx]->vertices[2].position.z - portalRecords[entry->recordIdx]->origin.z,
										&g_portalNormal,
										400))
								{
									Sector::g_currentSectorIndex = entry->categoryIdx;
								}
							}
						}
					}
				}

				++entry;
			} while (entry->recordIdx != 0xFF);
		}

		// FUNCTION: TOY2 0x00440260 [MATCHED]
		void UpdateActiveSectorAt(int32_t x, int32_t y, int32_t z)
		{
			int32_t previousX = g_buzzActor.posAngles.pos.x;
			int32_t previousY = g_buzzActor.posAngles.pos.y;
			int32_t previousZ = g_buzzActor.posAngles.pos.z;

			g_buzzActor.posAngles.pos.x = x;
			g_buzzActor.posAngles.pos.y = y;
			g_buzzActor.posAngles.pos.z = z;
			UpdateActiveSector();
			g_buzzActor.posAngles.pos.x = previousX;
			g_buzzActor.posAngles.pos.y = previousY;
			g_buzzActor.posAngles.pos.z = previousZ;
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

	// FUNCTION: TOY2 0x004500A0 [MATCHED]
	void LoadLevelGraphics(int32_t levelFileIndex)
	{
		Levels::FlushRenderer();
		Levels::InitLevelDefaults();
		Levels::g_levelLoadArena = Levels::g_levelDataHeapBase;

		switch (levelFileIndex)
		{
			case -1: {
				char filePath[256];
				RawLoader::LoadPacketData("gfx\\film.raw");
				FileUtils::AppendRegPathToBuffer();
				sprintf(filePath, "%s%s", FileUtils::g_fileNameBuffer, "gfx\\film.raw");
				char* rawExtension = strstr(filePath, ".raw");
				if (rawExtension != NULL)
				{
					strcpy(rawExtension, ".ngn");
					NGNLoader::SetNewImage(filePath);
					NGNLoader::DetectBackdropTextures();
				}
				return;
			}
			case 0: {
				char filePath[256];
				RawLoader::LoadPacketData("gfx\\loading.raw");
				FileUtils::AppendRegPathToBuffer();
				sprintf(filePath, "%s%s", FileUtils::g_fileNameBuffer, "gfx\\loading.raw");
				char* rawExtension = strstr(filePath, ".raw");
				if (rawExtension != NULL)
				{
					strcpy(rawExtension, ".ngn");
					NGNLoader::SetNewImage(filePath);
					NGNLoader::DetectBackdropTextures();
				}
				return;
			}
			case 1: {
				char filePath[256];
				RawLoader::LoadPacketData("gfx\\level1a.raw");
				FileUtils::AppendRegPathToBuffer();
				sprintf(filePath, "%s%s", FileUtils::g_fileNameBuffer, "gfx\\level1a.raw");
				char* rawExtension = strstr(filePath, ".raw");
				if (rawExtension != NULL)
				{
					strcpy(rawExtension, ".ngn");
					NGNLoader::SetNewImage(filePath);
					NGNLoader::DetectBackdropTextures();
				}
				return;
			}
			case 2: {
				char filePath[256];
				RawLoader::LoadPacketData("gfx\\level1b.raw");
				FileUtils::AppendRegPathToBuffer();
				sprintf(filePath, "%s%s", FileUtils::g_fileNameBuffer, "gfx\\level1b.raw");
				char* rawExtension = strstr(filePath, ".raw");
				if (rawExtension != NULL)
				{
					strcpy(rawExtension, ".ngn");
					NGNLoader::SetNewImage(filePath);
					NGNLoader::DetectBackdropTextures();
				}
				return;
			}
			case 3: {
				char filePath[256];
				RawLoader::LoadPacketData("gfx\\level1c.raw");
				FileUtils::AppendRegPathToBuffer();
				sprintf(filePath, "%s%s", FileUtils::g_fileNameBuffer, "gfx\\level1c.raw");
				char* rawExtension = strstr(filePath, ".raw");
				if (rawExtension != NULL)
				{
					strcpy(rawExtension, ".ngn");
					NGNLoader::SetNewImage(filePath);
					NGNLoader::DetectBackdropTextures();
				}
				return;
			}
			case 4: {
				char filePath[256];
				RawLoader::LoadPacketData("gfx\\level2a.raw");
				FileUtils::AppendRegPathToBuffer();
				sprintf(filePath, "%s%s", FileUtils::g_fileNameBuffer, "gfx\\level2a.raw");
				char* rawExtension = strstr(filePath, ".raw");
				if (rawExtension != NULL)
				{
					strcpy(rawExtension, ".ngn");
					NGNLoader::SetNewImage(filePath);
					NGNLoader::DetectBackdropTextures();
				}
				return;
			}
			case 5: {
				char filePath[256];
				RawLoader::LoadPacketData("gfx\\level2b.raw");
				FileUtils::AppendRegPathToBuffer();
				sprintf(filePath, "%s%s", FileUtils::g_fileNameBuffer, "gfx\\level2b.raw");
				char* rawExtension = strstr(filePath, ".raw");
				if (rawExtension != NULL)
				{
					strcpy(rawExtension, ".ngn");
					NGNLoader::SetNewImage(filePath);
					NGNLoader::DetectBackdropTextures();
				}
				return;
			}
			case 6: {
				char filePath[256];
				RawLoader::LoadPacketData("gfx\\level2c.raw");
				FileUtils::AppendRegPathToBuffer();
				sprintf(filePath, "%s%s", FileUtils::g_fileNameBuffer, "gfx\\level2c.raw");
				char* rawExtension = strstr(filePath, ".raw");
				if (rawExtension != NULL)
				{
					strcpy(rawExtension, ".ngn");
					NGNLoader::SetNewImage(filePath);
					NGNLoader::DetectBackdropTextures();
				}
				return;
			}
			case 7: {
				char filePath[256];
				RawLoader::LoadPacketData("gfx\\level3a.raw");
				FileUtils::AppendRegPathToBuffer();
				sprintf(filePath, "%s%s", FileUtils::g_fileNameBuffer, "gfx\\level3a.raw");
				char* rawExtension = strstr(filePath, ".raw");
				if (rawExtension != NULL)
				{
					strcpy(rawExtension, ".ngn");
					NGNLoader::SetNewImage(filePath);
					NGNLoader::DetectBackdropTextures();
				}
				return;
			}
			case 8: {
				char filePath[256];
				RawLoader::LoadPacketData("gfx\\level3b.raw");
				FileUtils::AppendRegPathToBuffer();
				sprintf(filePath, "%s%s", FileUtils::g_fileNameBuffer, "gfx\\level3b.raw");
				char* rawExtension = strstr(filePath, ".raw");
				if (rawExtension != NULL)
				{
					strcpy(rawExtension, ".ngn");
					NGNLoader::SetNewImage(filePath);
					NGNLoader::DetectBackdropTextures();
				}
				return;
			}
			case 9: {
				char filePath[256];
				RawLoader::LoadPacketData("gfx\\level3c.raw");
				FileUtils::AppendRegPathToBuffer();
				sprintf(filePath, "%s%s", FileUtils::g_fileNameBuffer, "gfx\\level3c.raw");
				char* rawExtension = strstr(filePath, ".raw");
				if (rawExtension != NULL)
				{
					strcpy(rawExtension, ".ngn");
					NGNLoader::SetNewImage(filePath);
					NGNLoader::DetectBackdropTextures();
				}
				return;
			}
			case 10:
				RawLoader::LoadRawAndNGN("gfx\\level4a.raw");
				return;
			case 11:
				RawLoader::LoadRawAndNGN("gfx\\level4b.raw");
				return;
			case 12:
				RawLoader::LoadRawAndNGN("gfx\\level4c.raw");
				return;
			case 13:
				RawLoader::LoadRawAndNGN("gfx\\level5a.raw");
				return;
			case 14:
				RawLoader::LoadRawAndNGN("gfx\\level5b.raw");
				return;
			case 15:
				RawLoader::LoadRawAndNGN("gfx\\level5c.raw");
				return;
			case 16:
			case 17:
				RawLoader::LoadRawAndNGN("gfx\\congrats.raw");
				return;
			case 30:
				RawLoader::LoadRawAndNGN("gfx\\bonus.raw");
				return;
			case 31:
				RawLoader::LoadRawAndNGN("gfx\\gamewin.raw");
				return;
			case 35:
			case 38:
			case 41:
			case 44:
			case 47:
				RawLoader::LoadRawAndNGN("gfx\\boss.raw");
				return;
			default:
				RawLoader::LoadRawAndNGN("gfx\\level1a.raw");
				return;
		}
	}

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

	// FUNCTION: TOY2 0x00414720 [PROVISIONAL]
	int32_t EnterLevel(int32_t levelIndex)
	{
		Nu3D::Camera::g_cameraTintBlue = 0;
		Nu3D::Camera::g_cameraTintGreen = 0;
		Nu3D::Camera::g_cameraTintRed = 0;
		Renderer::SetVirtualRatioTo54();

		int32_t demoMode = g_demoMode;
		if (demoMode == 0 || demoMode == 123)
			LoadLevelGraphics(g_levelFileIndex);
		else
			LoadLevelGraphics(0);
		SoftwareRenderer::g_backdropScrollOverride.x = 0;
		SoftwareRenderer::g_backdropScrollOverride.y = 0;

		Nu3D::Camera::SetTint(0x80, 0x80, 0x80, 12);
		int32_t fadeTimer = 28;
		do
		{
			fadeTimer -= Renderer::g_frameDelta;
			if (fadeTimer <= 0)
				fadeTimer = 0;
			Nu3D::Camera::FadeToTargetTint();
			if (demoMode == 0)
				Renderer::Sprite::DrawScaled(0x60, 200, 0x80, 1, 0xFF, 0xFF, 0xFF, 0xFF, 0x800, 0x800);
			Nullsub3();
			MainMenu::RenderMenu();
		} while (fadeTimer != 0);

		Levels::InitLevelPlay(levelIndex);
		int32_t introResult = ShowLevelIntroScreen(g_levelFileIndex, g_demoMode);
		Level::FixupRecordCoordinates();

		g_framePulseOutputs.words[0] = 0;
		g_framePulseOutputs.words[1] = 0;
		g_framePulseOutputs.words[2] = 0;
		g_framePulsePhases.words[0] = 0;

		memset(AudioManager::g_soundSequenceSlots, 0, sizeof(AudioManager::g_soundSequenceSlots));
		g_framePulsePhases.words[1] = 0;

		HUD::g_slideAngles[0] = 0x400;
		HUD::g_slideAngles[1] = 0x400;
		HUD::g_slideAngles[2] = 0x400;
		g_levelTransition = 0;
		g_levelTransitionTimer = 0;
		g_isPaused = 0;
		g_pauseMenuBlinkTimer = 0;
		g_pauseMenuSelection = 0;
		g_pauseMenuState = 0;
		AudioManager::g_maxLeftVolume = 0;
		AudioManager::g_maxRightVolume = 0;
		AudioManager::g_maxVolume = 0;
		Nu3D::Camera::g_targetTintBlue = 0;
		Nu3D::Camera::g_targetTintGreen = 0;
		Nu3D::Camera::g_targetTintRed = 0;
		Nu3D::Camera::g_targetTintFadeSpeed = 0;
		g_randDatBufferPtr = g_randDatBuffer;
		g_hudActorAnimationFrame = 54;
		g_framePulsePhases.words[2] = 0;

		HUD::g_slideAngles[3] = 0;
		HUD::g_slideAngles[4] = 0;
		HUD::g_slideAngles[5] = 0;
		HUD::g_slideAngles[6] = 0;
		HUD::g_slideAngles[7] = 0;
		HUD::g_slideAngles[8] = 0;
		HUD::g_slideAngles[9] = 0;
		HUD::g_slideAngles[10] = 0;

		HUD::g_slideTimers[0] = 180;
		HUD::g_slideTimers[1] = 180;
		HUD::g_slideTimers[2] = 180;
		HUD::g_slideTimers[3] = 0;
		HUD::g_slideTimers[4] = 0;
		HUD::g_slideTimers[5] = 0;
		HUD::g_slideTimers[6] = 0;
		HUD::g_slideTimers[7] = 0;
		HUD::g_slideTimers[8] = 0;
		HUD::g_slideTimers[9] = 0;
		HUD::g_slideTimers[10] = 0;

		Buzz::Init(&g_buzzActor, levelIndex);
		Lighting::InitBuzzLight();
		Camera::InitGameplayCamera(&Camera::g_gameplayCamera, &g_buzzActor);
		Nu3D::Particles::Init();
		ResetGadgets();

		Actor::g_renderActors[0] = 0;
		g_levelObjectiveProgress = 0;
		g_previousLevelObjectiveProgress = 0;
		g_specialPickupCount = -1;
		g_idleVoiceCooldown = 900;
		g_idleVoicePreset = 206;
		g_environmentTintBlue = 0x80;
		g_environmentTintGreen = 0x80;
		g_environmentTintRed = 0x80;
		HUD::g_challengeState = 0;
		AndysHouse::g_raceCheckpointPassCount = 0;

		InitialiseLevelVariables(levelIndex);
		Gadget::InitLevelUnlockGeometry();

		SaveManager::g_curLevelTokenData = g_levelTokenBits[g_levelFileIndex];
		g_savedUnlocks = SaveManager::g_save0Data.unlocks;
		g_framePhase = 0;
		g_unusedLevelState[0] = 0;
		g_unusedLevelState[1] = 0;
		g_unusedLevelState[2] = 0;
		g_unusedLevelState[3] = 0;
		g_unusedLevelState[4] = 0;
		g_unusedLevelState[5] = 0;
		g_unusedLevelState[6] = 0;
		g_unusedLevelState[7] = 0;
		g_unusedLevelState[8] = 0;
		g_levelInteractionTimer = 0;
		g_cameraIdleTimer = 0;
		g_gadgetRespawnTimer = 0;
		g_laserAimParticle = (Nu3D::Particles::ParticleInstance*)-1;

		if (g_levelFileIndex > 0 && g_levelFileIndex < 16 && g_levelIndex < 15 && g_levelFileIndex % 3 != 0)
			AudioManager::PlayMusicLooping(g_levelIndex);

		return introResult;
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

	namespace MovieViewer
	{
		// FUNCTION: TOY2 0x0043A600 [PROVISIONAL]
		int32_t Tick(int32_t movieIndex)
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
			SetBackdropByIndex(0);

			g_movieUnlocked[0] = 1;
			int32_t selectedIndex = 0;
			if (g_movieOrder[0] != movieIndex)
			{
				uint8_t* movieOrder = g_movieOrder;
				do
				{
					if (movieOrder[1] == 0xFF)
						break;
					if (g_movieUnlocked[*movieOrder] != 0)
						selectedIndex++;
					movieOrder++;
				} while (*movieOrder != movieIndex);
			}

			int32_t selectionPosition = (g_movieOrder[selectedIndex] != 0xFF ? selectedIndex : 0) * 0x1000;
			MenuItem menuItems[48];
			int32_t menuItemCount = 0;
			for (int32_t definitionIndex = 0; g_movieDefinitions[definitionIndex].thumbnail.sheetIndex != 0xFF; definitionIndex++)
			{
				uint8_t orderedMovieIndex = g_movieOrder[definitionIndex];
				if (g_movieUnlocked[orderedMovieIndex] != 0)
				{
					menuItems[menuItemCount].thumbnail = g_movieDefinitions[orderedMovieIndex].thumbnail;
					menuItems[menuItemCount].movieIndex = orderedMovieIndex;
					menuItemCount++;
				}
			}
			menuItems[menuItemCount].thumbnail.sheetIndex = 0xFF;
			menuItems[menuItemCount].thumbnail.tileIndex = 0xFF;
			menuItems[menuItemCount].movieIndex = 0xFF;
			menuItems[menuItemCount].terminator = 0xFF;

			int32_t fadeTimer = 4000;
			int32_t scrollVelocity = 0;
			int32_t scrollPosition = selectionPosition;
			int32_t framePhase = 0;
			AudioManager::PlayMusicLooping(19);
			g_screenMusicStarted = 1;
			int32_t result = -1;
			Nu3D::Camera::SetTint(128, 128, 128, 6);
			int32_t rightTarget = selectionPosition + 0x400;
			int32_t leftTarget = selectionPosition - 0x400;

			while (true)
			{
				Nu3D::Camera::FadeToTargetTint();
				framePhase += Renderer::g_frameDelta;

				if (leftTarget > -0x400 && (framePhase & 0x3F) < 0x30)
					Renderer::Sprite::DrawTile(0x10, 0x70, 0x39, 0);
				if (menuItems[(selectionPosition >> 12) + 1].thumbnail.sheetIndex != 0xFF && (framePhase & 0x3F) < 0x30)
					Renderer::Sprite::DrawTile(0x110, 0x70, 0x39, 1);

				Renderer::Sprite::DrawTile(0, 0, 0x3C, 0);
				Renderer::Sprite::DrawTile(0x80, 0, 0x3C, 1);
				Renderer::Sprite::DrawTile(0x100, 0, 0x3D, 0);

				int32_t thumbnailOffset = -((scrollPosition >> 5) & 0x7F);
				Renderer::Sprite::DrawClipped(thumbnailOffset + 0x10C, 0x50, 0xD0, 0x108, 0x40, 0);
				Renderer::Sprite::DrawClipped(thumbnailOffset + 0x72, 0x50, 0x38, 0x70, 0x40, 0);

				int32_t centeredItemIndex = (scrollPosition + 0x800) >> 12;
				thumbnailOffset = -(((scrollPosition - 0x800) >> 5) & 0x7F);
				Renderer::Sprite::DrawClipped(thumbnailOffset + 0x10C,
					0x50,
					0xD0,
					0x108,
					menuItems[centeredItemIndex].thumbnail.sheetIndex,
					menuItems[centeredItemIndex].thumbnail.tileIndex);
				Renderer::Sprite::DrawClipped(thumbnailOffset + 0x72,
					0x50,
					0x38,
					0x70,
					menuItems[centeredItemIndex].thumbnail.sheetIndex,
					menuItems[centeredItemIndex].thumbnail.tileIndex);

				int16_t conveyorPosition = (int16_t)(-(scrollPosition >> 6) % 0x140);
				Renderer::Sprite::DrawTile(conveyorPosition, 0, 0x3E, 0);
				Renderer::Sprite::DrawTile(conveyorPosition + 0x80, 0, 0x3E, 1);
				Renderer::Sprite::DrawTile(conveyorPosition + 0x100, 0, 0x3F, 0);

				conveyorPosition = (int16_t)(-((scrollPosition >> 6) % 0x140));
				Renderer::Sprite::DrawTile(conveyorPosition + 0x140, 0, 0x3E, 0);
				Renderer::Sprite::DrawTile(conveyorPosition + 0x1C0, 0, 0x3E, 1);
				Renderer::Sprite::DrawTile(conveyorPosition + 0x240, 0, 0x3F, 0);

				Nullsub3();
				MainMenu::RenderMenu();

				if (fadeTimer > 1000)
				{
					if ((InputManager::g_curButtonsPressed & INPUT_LEFT) != 0 && (InputManager::g_prevButtonsPressed & INPUT_LEFT) == 0 && leftTarget > -0x400
						&& fadeTimer > 0x17 && abs(scrollPosition - selectionPosition) < 0x800)
					{
						selectionPosition -= 0x1000;
						leftTarget -= 0x1000;
						rightTarget -= 0x1000;
						AudioManager::PlayOneShotSoundGlobal(1, 0x1200, 0x40, 0x60);
					}

					if ((InputManager::g_curButtonsPressed & INPUT_RIGHT) != 0 && (InputManager::g_prevButtonsPressed & INPUT_RIGHT) == 0
						&& menuItems[(selectionPosition >> 12) + 1].thumbnail.sheetIndex != 0xFF && fadeTimer > 0x17
						&& abs(scrollPosition - selectionPosition) < 0x800)
					{
						selectionPosition += 0x1000;
						leftTarget += 0x1000;
						rightTarget += 0x1000;
						AudioManager::PlayOneShotSoundGlobal(1, 0x1200, 0x60, 0x40);
					}

					if (scrollVelocity < 0)
					{
						scrollVelocity += Renderer::g_frameDelta * 8;
						if (scrollVelocity > 0)
							scrollVelocity = 0;
					}
					else if (scrollVelocity > 0)
					{
						scrollVelocity -= Renderer::g_frameDelta * 8;
						if (scrollVelocity < 0)
							scrollVelocity = 0;
					}

					if (scrollPosition < leftTarget)
						scrollVelocity += Renderer::g_frameDelta * 0x10;
					if (scrollPosition > rightTarget)
						scrollVelocity -= Renderer::g_frameDelta * 0x10;

					if (scrollVelocity > 0x80)
						scrollVelocity = 0x80;
					else if (scrollVelocity < -0x80)
						scrollVelocity = -0x80;
					scrollPosition += scrollVelocity;
				}

				Renderer::Sprite::DrawWhiteText(g_selectPrompt, 0xD4, 0xA0);
				Nullsub6();

				if (fadeTimer > 0 && fadeTimer < 1000)
					fadeTimer -= Renderer::g_frameDelta;

				if ((InputManager::g_curButtonsPressed & INPUT_CANCEL) != 0 && (InputManager::g_prevButtonsPressed & INPUT_CANCEL) == 0 && fadeTimer > 0x17
					&& Nu3D::Camera::g_cameraTintBlue == 128)
				{
					fadeTimer = 0x35;
					AudioManager::PlayOneShotSoundGlobal(2, 0x1200, 0x60, 0x60);
					result = -1;
					Nu3D::Camera::SetTint(0, 0, 0, 6);
				}

				if ((InputManager::g_curButtonsPressed & INPUT_JUMP) != 0 && (InputManager::g_prevButtonsPressed & INPUT_JUMP) == 0 && fadeTimer > 0x17
					&& Nu3D::Camera::g_cameraTintBlue == 128)
				{
					fadeTimer = 0x35;
					AudioManager::PlayOneShotSoundGlobal(0, 0x1200, 0x60, 0x60);
					result = menuItems[(scrollPosition + 0x800) >> 12].movieIndex;
					Nu3D::Camera::SetTint(0, 0, 0, 6);
				}
				else if (fadeTimer <= 0)
				{
					AudioManager::StopAndWait();
					return result;
				}
			}
		}
	}

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
			movieIdx = MovieViewer::Tick(movieIdx);

			if (movieIdx < 0)
				break;

			PlayMovieWithTransition(movieIdx + 10, 0);
		}

		g_levelFileIndex = prevLevelFileIdx;
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

		int32_t finished = 0;
		int32_t result = 0;
		int32_t state = SAVE_MENU_OPTIONS;
		int32_t selectedItem = 0;
		int32_t inputDelay = 30;
		int32_t blinkTimer = 20;
		RGBA clearColor;
		clearColor.value = 0;

		Nu3D::Camera::g_cameraTintBlue = 0;
		Nu3D::Camera::g_cameraTintGreen = 0;
		Nu3D::Camera::g_cameraTintRed = 0;
		Nu3D::Camera::SetTint(128, 128, 128, 12);

		if (postGameSave)
			state = SAVE_MENU_SAVE;

		int32_t slotXOffsets[8];
		for (int32_t slot = 0; slot < 8; ++slot)
		{
			char fileName[256];
			sprintf(fileName, "Toy2%02d.sav", slot + 1);

			FILE* file = fopen(fileName, "rb");
			if (file)
			{
				int32_t descriptionLength;
				fread(&descriptionLength, 1, 4, file);
				if (descriptionLength && g_saveSlotDescriptions[slot])
				{
					fread(g_saveSlotDescriptions[slot], 1, descriptionLength, file);
					g_saveSlotDescriptions[slot][descriptionLength] = '\0';
				}
				fclose(file);
			}
			else
			{
				g_saveSlotDescriptions[slot][0] = '\0';
			}

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
				SoftwareRenderer::g_backBufferClearComplete = 0;
				Renderer::g_parallaxHorizOffset = 0.0f;
				Renderer::g_parallaxTexHeightRatio = 1.0f;
				Renderer::g_parallaxTexWidthRatio = 1.0f;
				Renderer::RenderParallaxBackground(0);

				switch (state)
				{
					case SAVE_MENU_OPTIONS: {
						SoftwareRenderer::g_backBufferClearComplete = 0;
						if (NGNLoader::GetTextureDataIndex(g_sectorBackdropTexTable.primary[1]))
							g_nextBackdropId = g_sectorBackdropTexTable.primary[1];
						else if (NGNLoader::GetTextureDataIndex(g_sectorBackdropTexTable.secondary[0]))
							g_nextBackdropId = g_sectorBackdropTexTable.secondary[0];
						g_hasBackdrop = 1;

						Renderer::DrawFormattedText(256, 25, "load / save options");
						if (selectedItem != 0 || blinkTimer >= 10)
							Renderer::DrawFormattedText(256, 75, "load game");
						if (selectedItem != 1 || blinkTimer >= 10)
							Renderer::DrawFormattedText(256, 100, "save game");
						if (selectedItem != 2 || blinkTimer >= 10)
							Renderer::DrawFormattedText(256, 125, "main menu");

						if (inputDelay <= 0)
						{
							if ((InputManager::g_curButtonsPressed & INPUT_UP) && ! (InputManager::g_prevButtonsPressed & INPUT_UP) && selectedItem != 0)
							{
								--selectedItem;
								AudioManager::PlayOneShotSoundGlobal(1, 0x1200, 0x50, 0x50);
							}
							if ((InputManager::g_curButtonsPressed & INPUT_DOWN) && ! (InputManager::g_prevButtonsPressed & INPUT_DOWN) && selectedItem < 2)
							{
								++selectedItem;
								AudioManager::PlayOneShotSoundGlobal(1, 0x1200, 0x50, 0x50);
							}

							if (InputManager::g_curButtonsPressed & INPUT_JUMP)
							{
								if (selectedItem == 0)
									state = SAVE_MENU_LOAD;
								else if (selectedItem == 1)
									state = SAVE_MENU_SAVE;
								else
								{
									AudioManager::PlayOneShotSoundGlobal(2, 0x1200, 0x50, 0x50);
									state = SAVE_MENU_EXIT;
									Nu3D::Camera::SetTint(0, 0, 0, 6);
								}

								if (selectedItem < 2)
								{
									selectedItem = 0;
									inputDelay = 30;
									AudioManager::PlayOneShotSoundGlobal(0, 0x1200, 0x50, 0x50);
								}
							}

							if (InputManager::g_curButtonsPressed & INPUT_CANCEL)
							{
								AudioManager::PlayOneShotSoundGlobal(2, 0x1200, 0x50, 0x50);
								state = SAVE_MENU_EXIT;
								Nu3D::Camera::SetTint(0, 0, 0, 6);
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

						Renderer::DrawFormattedText(256, 3, "select slot to load");
						for (int32_t slot = 0; slot < 8; ++slot)
						{
							if (slot != selectedItem || blinkTimer >= 10)
							{
								if (g_saveSlotDescriptions[slot][0])
									Renderer::DrawFormattedText(slotXOffsets[slot] + 256, slot * 23 + 33, "%s", g_saveSlotDescriptions[slot]);
								else
									Renderer::DrawFormattedText(slotXOffsets[slot] + 256, slot * 23 + 33, "empty slot");
							}
						}
						Renderer::DrawFormattedText(256, 228, "jump:select  cancel:menu");

						if ((InputManager::g_curButtonsPressed & INPUT_UP) && ! (InputManager::g_prevButtonsPressed & INPUT_UP) && selectedItem != 0)
						{
							--selectedItem;
							AudioManager::PlayOneShotSoundGlobal(1, 0x1200, 0x50, 0x50);
						}
						if ((InputManager::g_curButtonsPressed & INPUT_DOWN) && ! (InputManager::g_prevButtonsPressed & INPUT_DOWN) && selectedItem < 7)
						{
							++selectedItem;
							AudioManager::PlayOneShotSoundGlobal(1, 0x1200, 0x50, 0x50);
						}

						if (inputDelay <= 0)
						{
							if ((InputManager::g_curButtonsPressed & INPUT_JUMP) && g_saveSlotDescriptions[selectedItem][0])
							{
								int32_t saveNumber = selectedItem + 1;
								char fileName[256];
								sprintf(fileName, "Toy2%02d.sav", saveNumber);
								FILE* file = fopen(fileName, "rb");
								if (file)
								{
									int32_t descriptionLength;
									char description[256];
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
								Nu3D::Camera::SetTint(0, 0, 0, 6);
								AudioManager::PlayOneShotSoundGlobal(0, 0x1200, 0x50, 0x50);
								result = 1;
							}

							if (InputManager::g_curButtonsPressed & INPUT_CANCEL)
							{
								AudioManager::PlayOneShotSoundGlobal(2, 0x1200, 0x50, 0x50);
								inputDelay = 30;
								state = SAVE_MENU_OPTIONS;
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

						Renderer::DrawFormattedText(256, 3, "select slot to save");
						for (int32_t slot = 0; slot < 8; ++slot)
						{
							if (slot != selectedItem || blinkTimer >= 10)
							{
								if (g_saveSlotDescriptions[slot][0])
									Renderer::DrawFormattedText(slotXOffsets[slot] + 256, slot * 23 + 33, "%s", g_saveSlotDescriptions[slot]);
								else
									Renderer::DrawFormattedText(slotXOffsets[slot] + 256, slot * 23 + 33, "empty slot");
							}
						}
						Renderer::DrawFormattedText(256, 228, postGameSave ? "jump:save  cancel:continue" : "jump:select  cancel:menu");

						if ((InputManager::g_curButtonsPressed & INPUT_UP) && ! (InputManager::g_prevButtonsPressed & INPUT_UP) && selectedItem != 0)
						{
							--selectedItem;
							AudioManager::PlayOneShotSoundGlobal(1, 0x1200, 0x50, 0x50);
						}
						if ((InputManager::g_curButtonsPressed & INPUT_DOWN) && ! (InputManager::g_prevButtonsPressed & INPUT_DOWN) && selectedItem < 7)
						{
							++selectedItem;
							AudioManager::PlayOneShotSoundGlobal(1, 0x1200, 0x50, 0x50);
						}

						if (inputDelay <= 0)
						{
							if (InputManager::g_curButtonsPressed & INPUT_JUMP)
							{
								int32_t progress = ComputeTokenProgress();
								char description[256];
								sprintf(description, "TOK %d LEV %d", (progress >> 16) & 0xff, progress & 0xff);
								SaveManager::SaveToFile(selectedItem + 1, description);
								state = SAVE_MENU_EXIT;
								Nu3D::Camera::SetTint(0, 0, 0, 6);

								for (int32_t slot = 0; slot < 8; ++slot)
								{
									char fileName[256];
									sprintf(fileName, "Toy2%02d.sav", slot + 1);
									FILE* file = fopen(fileName, "rb");
									if (file)
									{
										int32_t descriptionLength;
										fread(&descriptionLength, 1, 4, file);
										if (descriptionLength && g_saveSlotDescriptions[slot])
										{
											fread(g_saveSlotDescriptions[slot], 1, descriptionLength, file);
											g_saveSlotDescriptions[slot][descriptionLength] = '\0';
										}
										fclose(file);
									}
									else
									{
										g_saveSlotDescriptions[slot][0] = '\0';
									}
								}
								AudioManager::PlayOneShotSoundGlobal(0, 0x1200, 0x50, 0x50);
							}

							if (InputManager::g_curButtonsPressed & INPUT_CANCEL)
							{
								AudioManager::PlayOneShotSoundGlobal(2, 0x1200, 0x50, 0x50);
								if (postGameSave)
								{
									state = SAVE_MENU_EXIT;
									Nu3D::Camera::SetTint(0, 0, 0, 6);
								}
								else
								{
									inputDelay = 30;
									state = SAVE_MENU_OPTIONS;
								}
							}
						}
						break;
					}

					case SAVE_MENU_EXIT: {
						if (Nu3D::Camera::g_cameraTintBlue == 0)
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
				blinkTimer += 20;
		}

		return result;
	}

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

#pragma function(abs)
#pragma optimize("", off)
	// FUNCTION: TOY2 0x0047EF80 [MATCHED]
	void ProfileCPU()
	{
		g_cpuProfileTotalMs = 0;
		for (g_cpuProfileSampleIndex = 0; g_cpuProfileSampleIndex < 10; g_cpuProfileSampleIndex++)
		{
			g_cpuProfileStartMs = timeGetTime();
			for (g_cpuProfileWorkIndex = 0; g_cpuProfileWorkIndex < 1000000; g_cpuProfileWorkIndex++)
			{
				g_cpuProfileWorkValue = (float)g_cpuProfileWorkIndex;
				g_cpuProfileWorkValue = g_cpuProfileWorkValue / 50000.0f;
				g_cpuProfileWorkValue = g_cpuProfileWorkValue * 50000.0f;
			}

			g_cpuProfileSamples[g_cpuProfileSampleIndex] = abs((int32_t)timeGetTime() - g_cpuProfileStartMs);
			g_cpuProfileTotalMs += g_cpuProfileSamples[g_cpuProfileSampleIndex];
		}

		g_cpuProfileTotalMs /= 10;
		g_cpuClockHz = 56000 / g_cpuProfileTotalMs;
		g_cpuClockHz *= 1000000;
		Logger::Log("CalculateClockSpeed->time[0]=%i\n", g_cpuProfileSamples[0]);
		Logger::Log("                   ->time[1]=%i\n", g_cpuProfileSamples[1]);
		Logger::Log("                   ->time[2]=%i\n", g_cpuProfileSamples[2]);
		Logger::Log("                   ->time[3]=%i\n", g_cpuProfileSamples[3]);
		Logger::Log("                   ->time[4]=%i\n", g_cpuProfileSamples[4]);
		Logger::Log("                   ->time[5]=%i\n", g_cpuProfileSamples[5]);
		Logger::Log("                   ->time[6]=%i\n", g_cpuProfileSamples[6]);
		Logger::Log("                   ->time[7]=%i\n", g_cpuProfileSamples[7]);
		Logger::Log("                   ->time[8]=%i\n", g_cpuProfileSamples[8]);
		Logger::Log("                   ->time[9]=%i\n", g_cpuProfileSamples[9]);
		Logger::Log("                   ->avgtime=%i\n", g_cpuProfileTotalMs);
		Logger::Log("                   ->CPUSPEED=%u\n", g_cpuClockHz);
		g_cpuProfileWorkValue = (float)g_cpuClockHz;
		g_cpuProfileWorkValue = g_cpuProfileWorkValue / 1000000.0f;
		Logger::Log("INIT : CPU clock speed is %f MHZ\n", g_cpuProfileWorkValue);
	}
#pragma optimize("", on)
#pragma intrinsic(abs)

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

	// FUNCTION: TOY2 0x0047CBA0 [PROVISIONAL]
	void InitSoftWindow(int32_t width, int32_t height)
	{
		Logger::Log("InitSoftWindow(%i,%i)\n", width, height);
		g_softWindowHeight = height;
		g_softWindowWidth = width;
		g_screenClipTop = (g_destRectHeight - height) / 2;
		g_screenClipBottom = g_screenClipTop + height - 1;
		g_softWindowHalfWidth = g_destRectWidth / 2;
		g_screenClipLeft = (g_destRectWidth - width) / 2;
		g_screenClipRight = g_screenClipLeft + width - 1;
		g_screenClipLeftFixed = g_screenClipLeft << 10;
		g_destRectHalfHeight = g_destRectHeight / 2;
		SoftwareRenderer::g_softWindowScaleX = (width << 7) / 320;
		SoftwareRenderer::g_softWindowScaleY = (height * 128) / 240;
		g_screenClipRightFixed = g_screenClipRight * 1024 + 1023;
		g_destRectWidthScaled = (width * 256) / 320;
	}

	// FUNCTION: TOY2 0x0047CC90 [PROVISIONAL]
	void InitSoftwareRenderer()
	{
		Logger::Log("INITSOFTRENDER : Start.\n");
		g_destRectHalfWidth = d3dappi.szClient.cx / 2;
		g_destRectWidth = d3dappi.szClient.cx;
		g_destRectHeight = d3dappi.szClient.cy;
		Logger::Log("SOFTRENDER : Screen set to %dx%d.\n", d3dappi.szClient.cx, d3dappi.szClient.cy);

		if (g_destRectHalfWidth != g_perspectiveTableHalfWidth)
		{
			g_perspectiveTableHalfWidth = g_destRectHalfWidth;
			BuildPerspectiveDivideTable(g_destRectHalfWidth);
		}

		g_softWindowWidth = g_destRectWidth;
		g_screenClipBottom = g_destRectHeight - 1;
		g_screenClipRight = g_destRectWidth - 1;
		g_destRectWidthScaled = (g_destRectWidth << 8) / 320;
		g_screenClipLeft = 0;
		g_screenClipTop = 0;
		g_softWindowHeight = g_destRectHeight;
		g_softWindowHalfWidth = g_destRectWidth / 2;
		g_screenClipLeftFixed = 0;
		g_destRectHalfHeight = g_destRectHeight / 2;
		g_screenClipRightFixed = (g_destRectWidth << 10) - 1;

		SoftwareRenderer::g_unusedSoftwareRendererConfigA = 639;
		SoftwareRenderer::g_displayMaxX = 639;
		SoftwareRenderer::g_unusedSoftwareRendererConfigB = 255;
		SoftwareRenderer::g_unusedSoftwareRendererConfigC = 1023;
		SoftwareRenderer::g_softwareRendererBufferBlockCount = 10240;
		if (SoftwareRenderer::g_softwareRendererBuffer)
			free(SoftwareRenderer::g_softwareRendererBuffer);
		SoftwareRenderer::g_softwareRendererBuffer = malloc(SoftwareRenderer::g_softwareRendererBufferBlockCount << 7);

		DDSURFACEDESC surfaceDesc;
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		HRESULT lockResult;
		do
		{
			lockResult = d3dappi.lpBackBuffer->Lock(NULL, &surfaceDesc, 0, NULL);
		} while (lockResult == DDERR_WASSTILLDRAWING || lockResult == DDERR_SURFACEBUSY);

		if (surfaceDesc.ddpfPixelFormat.dwRBitMask == 0 || surfaceDesc.ddpfPixelFormat.dwGBitMask == 0 || surfaceDesc.ddpfPixelFormat.dwBBitMask == 0)
		{
			SoftwareRenderer::g_redMask = 0;
			SoftwareRenderer::g_greenMask = 0;
			SoftwareRenderer::g_blueMask = 0;
			SoftwareRenderer::g_blueShift = 0;
			SoftwareRenderer::g_redShift = 0;
			SoftwareRenderer::g_greenShift = 0;
			SoftwareRenderer::g_bitsPerPixel = 8;
			SaveManager::SetLightShadowEffects(0);
		}
		else
		{
			SoftwareRenderer::g_redMask = surfaceDesc.ddpfPixelFormat.dwRBitMask;
			SoftwareRenderer::g_greenMask = surfaceDesc.ddpfPixelFormat.dwGBitMask;
			SoftwareRenderer::g_blueMask = surfaceDesc.ddpfPixelFormat.dwBBitMask;

			uint32_t redMask = surfaceDesc.ddpfPixelFormat.dwRBitMask;
			while ((redMask & 1) == 0)
				redMask >>= 1;
			int32_t redBits = 0;
			while (redMask & 1)
			{
				redMask >>= 1;
				++redBits;
			}

			uint32_t greenMask = surfaceDesc.ddpfPixelFormat.dwGBitMask;
			while ((greenMask & 1) == 0)
				greenMask >>= 1;
			int32_t greenBits = 0;
			while (greenMask & 1)
			{
				greenMask >>= 1;
				++greenBits;
			}

			SoftwareRenderer::g_blueShift = 0;
			SoftwareRenderer::g_greenShift = greenBits;
			SoftwareRenderer::g_redShift = redBits + greenBits;
			SoftwareRenderer::g_bitsPerPixel = greenBits == 6 ? 16 : 15;
		}

		int32_t pitchPixels = surfaceDesc.lPitch;
		if (SoftwareRenderer::g_bitsPerPixel != 8)
			pitchPixels /= 2;
		SoftwareRenderer::g_backBufferPitchBytes = pitchPixels * 2;
		SoftwareRenderer::g_backBufferPitchPixels = pitchPixels;
		d3dappi.lpBackBuffer->Unlock(NULL);

		Logger::Log("SOFTRENDER : Bit's per pixel set to %d.\n", SoftwareRenderer::g_bitsPerPixel);
		Logger::Log(
			"SOFTRENDER : RGB shifts - R<<%d, G<<%d, B<<%d.\n", SoftwareRenderer::g_redShift, SoftwareRenderer::g_greenShift, SoftwareRenderer::g_blueShift);
		Logger::Log("SOFTRENDER : RGB masks - R&%x, G&%x, B&%x.\n", SoftwareRenderer::g_redMask, SoftwareRenderer::g_greenMask, SoftwareRenderer::g_blueMask);

		switch (SoftwareRenderer::g_bitsPerPixel)
		{
			case 15:
				SoftwareRenderer::g_softwareRenderDispatch = &SoftwareRenderer::g_softwareRenderDispatch555;
				break;
			case 16:
				SoftwareRenderer::g_softwareRenderDispatch = &SoftwareRenderer::g_softwareRenderDispatch565;
				break;
			default:
				SoftwareRenderer::g_softwareRenderDispatch = &SoftwareRenderer::g_softwareRenderDispatchPalettized;
				SoftwareRenderer::SetNewPalette(SoftwareRenderer::g_defaultSoftwarePalette, 0x11111);
				SoftwareRenderer::SetPaletteOnAPI();
				break;
		}

		SoftwareRenderer::g_softwareRenderItemCount = 0;
		memset(SoftwareRenderer::g_softwareRenderBuckets, 0, sizeof(SoftwareRenderer::g_softwareRenderBucketStorage));
		SoftwareRenderer::g_unusedSoftwareRendererConfigD = 4;

		int32_t softHeight = (g_destRectHeight * 8) / 8;
		int32_t softWidth = (g_destRectWidth * 8) / 8;
		InitSoftWindow(softWidth, softHeight);
		SoftwareRenderer::g_pendingBackBufferClears = 2;
		SoftwareRenderer::BuildColourRampTables();
		Logger::Log("INITSOFTRENDER : End.\n");
		Logger::Log("\n");
	}

	// STUB: TOY2 0x00499950
	void InitDirect3DRenderer()
	{
		// Weird method, a good portion of these variables are never even used in the game
	}
}
