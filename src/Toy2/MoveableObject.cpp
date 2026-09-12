#include "Toy2/Toy2.h"
#include "Toy2/Buzz.h"
#include "Toy2/Levels.h"
#include "Toy2/Collision.h"
#include "InputManager.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "AudioManager/AudioManager.h"
#include "Random.h"

#include <LIMITS.H>
#include <MATH.H>

namespace Toy2
{
	// Defined in Toy2.cpp and AlsPenthouse.cpp; this unit only reads them.
	extern uint32_t g_actionStateFlags;
	extern int32_t g_forcedFacingAngle;
	extern int32_t g_movementLockTimer;
	extern int32_t g_riderPlatformOffsetX;
	extern int32_t g_riderPlatformOffsetZ;

	namespace AlsPenthouse
	{
		extern int32_t g_trainCollisionTimer;
	}

	namespace MoveableObject
	{
		const uint32_t RIDER_CONTROL_BLOCKING_ACTIONS = 0xFFDFE;

		// GLOBAL: TOY2 0x0053C65C
		int32_t g_objectCount;

		// GLOBAL: TOY2 0x0053C670
		int32_t g_contactSoundCooldown;

		// GLOBAL: TOY2 0x0053C680
		State g_objects[10];

		// FUNCTION: TOY2 0x004334D0 [PROVISIONAL]
		void ComputeSegment(int32_t pathRecordType, State* object)
		{
			int32_t deltaX;
			int32_t deltaZ;
			{
				Levels::RecordData* path = Levels::g_recordData[pathRecordType];
				int32_t pathPoint = object->currentPathPoint;
				deltaX = path->data[pathPoint + 1].x - path->data[pathPoint].x;
				deltaZ = path->data[pathPoint + 1].z - path->data[pathPoint].z;
			}

			object->facingAngle = (int16_t)(Nu3D::Math::CartesianToFixedAngle(deltaX, deltaZ) & 0xFFF);

			Vector3I direction;
			direction.x = deltaX;
			direction.y = 0;
			direction.z = deltaZ;
			Nu3D::Math::NormalizeToFixedPoint(&direction, &direction);

			int32_t squaredLength = deltaZ * deltaZ + deltaX * deltaX;
			object->directionX = direction.x;
			object->segmentLength = (int16_t)sqrt((float)squaredLength);
			object->directionZ = direction.z;
			object->swingState = 0;

			if (object->targetPathPoint < Levels::g_recordData[object->pathRecordType]->recordCount - 2)
			{
				Levels::RecordData* path = Levels::g_recordData[pathRecordType];
				int32_t pathPoint = object->currentPathPoint;
				if (path->data[pathPoint + 1].x == path->data[pathPoint + 2].x && path->data[pathPoint + 1].z == path->data[pathPoint + 2].z)
				{
					object->swingState = object->segmentLength / 2;
				}
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

		// FUNCTION: TOY2 0x00433700 [PROVISIONAL]
		void Update(Buzz::Toy2BuzzActor* buzz)
		{
			g_contactSoundCooldown -= Renderer::g_frameDelta;
			if (g_contactSoundCooldown < 0)
			{
				g_contactSoundCooldown = 0;
			}

			if ((InputManager::g_directionInputState & (INPUT_UP | INPUT_RIGHT | INPUT_DOWN | INPUT_LEFT)) == 0
				|| (g_actionStateFlags & RIDER_CONTROL_BLOCKING_ACTIONS) != 0)
			{
				g_forcedFacingActive = 0;
			}
			else if (g_forcedFacingActive == 0 && buzz->collisionFlags != 0)
			{
				for (int32_t objectIndex = 0; objectIndex < g_objectCount; ++objectIndex)
				{
					State* object = &g_objects[objectIndex];
					if (object->verticalVelocity == 0 && object->swingState >= 0 && (Platform::GetFlags(object->platformIndex) & 3) == 1)
					{
						Vector3I16* normal = Platform::GetContactFaceNormal(object->platformIndex);
						int32_t facingAngle = (Nu3D::Math::CartesianToFixedAngle(normal->x, normal->z) - 0x800) & 0xFFF;
						if (((facingAngle - buzz->posAngles.angles.yaw + 0x180) & 0xFFF) < 0x300)
						{
							g_forcedFacingActive = objectIndex + 1;
							g_riderPlatformOffsetX = (object->position.x - buzz->posAngles.pos.x) >> 5;
							g_riderPlatformOffsetZ = (object->position.z - buzz->posAngles.pos.z) >> 5;
							g_forcedFacingAngle = facingAngle;
							if (g_contactSoundCooldown == 0)
							{
								AudioManager::Preset::PlayOneShotSound2(0x2D, buzz);
								g_contactSoundCooldown = 0x28;
							}
							break;
						}
					}
				}
			}

			for (int32_t objectIndex = 0; objectIndex < g_objectCount; ++objectIndex)
			{
				State* object = &g_objects[objectIndex];
				if (object->verticalVelocity != 0)
				{
					object->verticalVelocity += (int16_t)(Renderer::g_frameDelta * 0x100 / 4);
					if (object->verticalVelocity > 0x800)
					{
						object->verticalVelocity = 0x800;
					}

					object->position.y += (object->verticalVelocity >> 5) * Renderer::g_frameDelta * 0x20;
					int32_t targetY = Levels::g_recordData[object->pathRecordType]->data[object->currentPathPoint].y * 0x20;
					if (object->position.y >= targetY)
					{
						object->position.y = targetY;
						object->verticalVelocity = 0;
						object->targetPathPoint = object->currentPathPoint;
						ComputeSegment(object->pathRecordType, object);
						AudioManager::PlaySoundEffect(0x2F, &object->position);
					}

					if (object->linkIndex >= 0)
					{
						Nu3D::Link::SetPositionRawAndCommit(object->linkIndex, object->position.x >> 5, object->position.y >> 5, object->position.z >> 5);
					}
					Platform::SetOrigin(object->platformIndex, object->position.x, object->position.y, object->position.z);
				}

				if (object->swingState < 0)
				{
					object->pathProgress += (int16_t)(Renderer::g_frameDelta * 0x18);
					if (object->pathProgress > object->segmentLength)
					{
						object->currentPathPoint += 2;
						object->verticalVelocity = 2;
						object->facingAngle = 0;
						object->segmentLength = 0;
						object->directionX = 0;
						object->directionZ = 0;
						object->swingState = 0;
						object->pathProgress = 0;
					}

					Levels::RecordData* path = Levels::g_recordData[object->pathRecordType];
					object->position.x = ((object->directionX * object->pathProgress >> 7) & ~0x1F) + path->data[object->currentPathPoint].x * 0x20;
					object->position.z = ((object->directionZ * object->pathProgress >> 7) & ~0x1F) + path->data[object->currentPathPoint].z * 0x20;
					if (object->linkIndex >= 0)
					{
						Nu3D::Link::SetPositionRawAndCommit(object->linkIndex, object->position.x >> 5, object->position.y >> 5, object->position.z >> 5);
					}
					Platform::SetOrigin(object->platformIndex, object->position.x, object->position.y, object->position.z);
				}
			}

			if (g_forcedFacingActive == 0)
			{
				return;
			}

			int32_t riderIndex = g_forcedFacingActive - 1;
			State* object = &g_objects[riderIndex];
			if (object->verticalVelocity != 0 || object->swingState < 0)
			{
				return;
			}

			int32_t playMovementSound = 0;
			int32_t moving = 0;
			if (object->targetPathPoint < Levels::g_recordData[object->pathRecordType]->recordCount - 1)
			{
				if (((g_forcedFacingAngle - buzz->posAngles.angles.yaw + 8) & 0xFFF) < 0x10)
				{
					if (Levels::g_recordData[0x3A] != 0 && Levels::g_recordData[0x3A]->data[riderIndex].x != INT_MIN)
					{
						Levels::DeactivateAmbientEmitter(riderIndex, 0);
					}

					if (g_levelFileIndex != 11 || g_forcedFacingActive != 1 || AlsPenthouse::g_trainCollisionTimer <= 0)
					{
						playMovementSound = 1;
						object->pathProgress += (int16_t)(Renderer::g_frameDelta * 0xC);
						moving = 1;
						if (object->swingState > 0 && object->swingState < object->pathProgress)
						{
							object->swingState = -1;
							AudioManager::PlaySoundEffect(0x31, &object->position);
							g_forcedFacingActive = 0;
							buzz->velocity.lateral = 0;
							buzz->velocity.forward = 0;
							g_movementLockTimer = 10;
						}
					}
				}

				if (((g_forcedFacingAngle - buzz->posAngles.angles.yaw - 0x7F8) & 0xFFF) < 0x10)
				{
					playMovementSound = 1;
					moving = 1;
					object->pathProgress += (int16_t)(Renderer::g_frameDelta * -0xC);
				}

				if (object->pathProgress == 0 && object->targetPathPoint < object->currentPathPoint)
				{
					--object->currentPathPoint;
					ComputeSegment(object->pathRecordType, object);
					object->pathProgress = object->segmentLength;
				}
				else if (object->pathProgress >= object->segmentLength)
				{
					if (object->pathProgress == object->segmentLength)
					{
						playMovementSound = moving;
					}
					if (object->currentPathPoint < Levels::g_recordData[object->pathRecordType]->recordCount - 2)
					{
						++object->currentPathPoint;
						ComputeSegment(object->pathRecordType, object);
						playMovementSound = 0;
						object->pathProgress = 0;
					}
					else if (object->pathProgress > object->segmentLength)
					{
						object->pathProgress = object->segmentLength;
					}
				}

				if (object->pathProgress < 0)
				{
					if (object->targetPathPoint < object->currentPathPoint)
					{
						--object->currentPathPoint;
						ComputeSegment(object->pathRecordType, object);
						object->pathProgress = object->segmentLength;
					}
					else
					{
						object->pathProgress = 0;
					}
					playMovementSound = 0;
				}
			}

			Levels::RecordData* path = Levels::g_recordData[object->pathRecordType];
			object->position.x = ((object->directionX * object->pathProgress >> 7) & ~0x1F) + path->data[object->currentPathPoint].x * 0x20;
			object->position.z = ((object->directionZ * object->pathProgress >> 7) & ~0x1F) + path->data[object->currentPathPoint].z * 0x20;
			if (object->linkIndex >= 0)
			{
				Nu3D::Link::SetPositionRawAndCommit(object->linkIndex, object->position.x >> 5, object->position.y >> 5, object->position.z >> 5);
			}
			Platform::SetOrigin(object->platformIndex & 0x7FFF, object->position.x, object->position.y, object->position.z);

			buzz->velocity.lateral = object->position.x - g_riderPlatformOffsetX * 0x20 - buzz->posAngles.pos.x;
			buzz->velocity.forward = object->position.z - g_riderPlatformOffsetZ * 0x20 - buzz->posAngles.pos.z;
			if (playMovementSound)
			{
				AudioManager::PlaySoundEffect(0x2E, &object->position);
				if (g_framePulseOutputs.fourTick != 0)
				{
					int32_t randomOffset = *g_randDatBufferPtr++ - 0x80;
					Nu3D::Particles::SpawnFromPreset(buzz->posAngles.pos.x + buzz->velocity.lateral * 6 + (buzz->velocity.forward * randomOffset >> 4),
						buzz->posAngles.pos.y,
						buzz->posAngles.pos.z + buzz->velocity.forward * 6 + (buzz->velocity.lateral * randomOffset >> 4),
						2,
						1);
				}
			}
		}
	}
}
