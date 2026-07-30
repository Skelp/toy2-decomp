#pragma once

#include "Common.h"
#include "Numerics.h"

#include <stddef.h>

namespace Toy2
{
	namespace Levels
	{
		struct RecordData
		{
			uint16_t recordCount;
			uint16_t recordType;
			Vector3I data[];
		};

		struct LevelDataHeader
		{
			int32_t recordCount;
			uint8_t records[];
		};

		struct LevelDataTrailer
		{
			int32_t size;
		};

		struct ModelTypeData_A
		{
			int32_t unk0;
			int32_t unk4;
			int32_t unk8;
			Vector3I16 rotation;
			int16_t scaleX;
			int16_t scaleY;
			int16_t scaleZ;
			int8_t flags;
			uint8_t padding[3];
			int32_t meshPtr;
			int32_t animPtr;
		};

		struct ModelTypeData_B
		{
			int16_t mat_11;
			int16_t mat_12;
			int16_t mat_13;
			int16_t mat_21;
			int16_t mat_22;
			int16_t mat_23;
			Vector3I16 rotation;
			uint8_t flags;
			uint8_t reservedFlags;
			void* modelPtr;
			int32_t animPtr;
			int16_t polyCount;
			int16_t reservedTail;
		};

		struct InstanceSection
		{
			int32_t x;
			int32_t y;
			int32_t z;
			int16_t modelId;
			int8_t flags;
			int8_t category;
			ModelTypeData_B* unk6;
		};

		struct Type64
		{
			Vector3I unkVar0;
			Vector3I unkVar3;
			int32_t unkVar6;
			void* recordPtr;
			void* unkVar8;
		};

		struct ObjectDesc
		{
			int16_t unkVar1;
			int16_t unkVar2;
			int16_t id;
			int16_t unkVar3;
			int32_t unkVar4;
		};

		struct Object
		{
			int32_t x;
			int32_t y;
			int32_t z;
			int16_t facingAngle;
			int16_t modelId;
			ObjectDesc* desc;
		};

		struct ObjectList
		{
			int32_t count;
			Object* entries[];
		};

		struct ObjectDescCache
		{
			int32_t unkInt1;
			int32_t unkInt2;
			int32_t unkInt3;
			int16_t unkInt4;
			int16_t unkInt5;
			int16_t unkInt6;
			int16_t unkInt7;
			int16_t unkInt8;
			int16_t unkInt9;
			int16_t unkInt10;
			int16_t unkInt11;
		};

		struct PortalEntry
		{
			uint8_t recordIdx;
			uint8_t categoryIdx;
		};

		struct PortalZone
		{
			PortalEntry entries[16];
		};

		struct PortalVertex
		{
			Vector3I16 position;
			uint8_t reserved[6];
		};

		struct PortalRecord
		{
			uint16_t recordCount;
			uint16_t recordType;
			Vector3I16 origin;
			Vector3I16 normal;
			PortalVertex vertices[3];
		};

		extern RecordData* g_recordData[96];
		extern PortalZone g_portalZones[20];
		extern ObjectList* g_objectListBase;
		extern InstanceSection* g_instanceSection;
		extern InstanceSection* g_secondInstanceSection;
		extern uint8_t g_levelDataHeapBase[1249280];
		extern uint8_t* g_levelDataHeapBasePtr;
		extern int32_t g_levelLoadConfig;
		extern uint8_t* g_levelLoadArena;
		extern int32_t g_ambientEmitterScanIndex;
		extern int32_t g_alternateAmbientEmitterStart;

		void InitLevelPlay(int32_t levelId);
		void FlushRenderer();
		int32_t InitLevelDefaults();
		void DeactivateAmbientEmitter(int32_t emitterIndex, int32_t useAlternateType);
		void UpdateAmbientEmitters();

		STATIC_ASSERT(sizeof(RecordData) == 0x4);
		STATIC_ASSERT(sizeof(ModelTypeData_A) == 0x24);
		STATIC_ASSERT(sizeof(ModelTypeData_B) == 0x20);
		STATIC_ASSERT(sizeof(InstanceSection) == 0x14);
		STATIC_ASSERT(sizeof(Object) == 0x14);
		STATIC_ASSERT(sizeof(ObjectDesc) == 0xC);
		STATIC_ASSERT(sizeof(ObjectDescCache) == 0x1C);
		STATIC_ASSERT(sizeof(LevelDataHeader) == 0x4);
		STATIC_ASSERT(sizeof(LevelDataTrailer) == 0x4);
		STATIC_ASSERT(sizeof(ObjectList) == 0x4);
		STATIC_ASSERT(sizeof(PortalEntry) == 0x2);
		STATIC_ASSERT(sizeof(PortalZone) == 0x20);
		STATIC_ASSERT(sizeof(PortalVertex) == 0xC);
		STATIC_ASSERT(sizeof(PortalRecord) == 0x34);
		STATIC_ASSERT(offsetof(PortalRecord, origin) == 0x4);
		STATIC_ASSERT(offsetof(PortalRecord, normal) == 0xA);
		STATIC_ASSERT(offsetof(PortalRecord, vertices) == 0x10);
	}
}
