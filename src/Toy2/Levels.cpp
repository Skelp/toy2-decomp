#include "Toy2/Levels.h"

#include "SoftwareRenderer.h"
#include "FileUtils.h"
#include "Random.h"
#include "Logger.h"
#include "CharacterLoader.h"
#include "RawLoader.h"
#include "Collision.h"

#include "NGNLoader/NGNLoader.h"
#include "Renderer/Glue.h"
#include "Toy2/Toy2.h"
#include "Toy2/Collectables.h"
#include "Toy2/Actor.h"
#include "Toy2/Animation.h"
#include "Toy2/Camera.h"
#include "AudioManager/AudioManager.h"
#include "Renderer/Renderer.h"
#include "Renderer/Shadows.h"
#include "Renderer/Sprite.h"
#include "Renderer/SpriteSheets.h"
#include "Nu3D/Particles.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Light.h"
#include "SaveManager.h"
#include "Toy2/Direct6.h"
#include "Toy2/Weather.h"

namespace Nu3D
{
	namespace Camera
	{
		extern Vector3I g_sectorViewPosition;
	}
}

namespace Toy2
{
	int32_t WriteCfg();

	namespace Levels
	{
		extern int32_t g_type64Count;
		extern int32_t g_currentType64Region;
		extern Type64 g_type64Structs[24];

		STATIC_ASSERT(sizeof(Type64) == 0x24);
		STATIC_ASSERT(offsetof(Type64, boundsSizeSquared) == 0x18);
		STATIC_ASSERT(offsetof(Type64, recordPtr) == 0x1C);
		STATIC_ASSERT(offsetof(Type64, regionData) == 0x20);
	}

	namespace Level
	{
		struct PrimitiveCommandBytes
		{
			uint8_t flags;
			uint8_t upperByte;
		};

		union PrimitiveCommandWord
		{
			uint16_t value;
			PrimitiveCommandBytes bytes;
		};

		enum ModelPrimitiveInfo
		{
			MODEL_PRIMITIVE_FLAGS_MASK = 0x7,
		};

		STATIC_ASSERT(sizeof(PrimitiveCommandWord) == 2);

		// GLOBAL: TOY2 0x0054DD94
		uint8_t g_activeZoneVisTable[256];

		// GLOBAL: TOY2 0x0054E054
		int32_t g_visibilityRegionUpdateState;

		// GLOBAL: TOY2 0x00550C20
		uint8_t* g_activeRegionData;

		// FUNCTION: TOY2 0x0043E070 [PROVISIONAL]
		void UpdateVisibilityRegion()
		{
			int32_t previousRegion = Levels::g_currentType64Region;
			int32_t smallestBoundsSize = 0x7FFFFFFF;

			for (int32_t regionIndex = 0; regionIndex < Levels::g_type64Count; regionIndex++)
			{
				Levels::Type64& region = Levels::g_type64Structs[regionIndex];
				if (region.boundsSizeSquared < smallestBoundsSize && Nu3D::Camera::g_sectorViewPosition.x >= region.boundsMin.x
					&& Nu3D::Camera::g_sectorViewPosition.y >= region.boundsMin.y && Nu3D::Camera::g_sectorViewPosition.z >= region.boundsMin.z
					&& Nu3D::Camera::g_sectorViewPosition.x <= region.boundsMax.x && Nu3D::Camera::g_sectorViewPosition.y <= region.boundsMax.y
					&& Nu3D::Camera::g_sectorViewPosition.z <= region.boundsMax.z)
				{
					Levels::g_currentType64Region = regionIndex;
					smallestBoundsSize = region.boundsSizeSquared;
				}
			}

			if (Levels::g_currentType64Region == -1)
				Levels::g_currentType64Region = 0;

			if (Levels::g_currentType64Region != previousRegion)
			{
				uint8_t* regionData = Levels::g_type64Structs[Levels::g_currentType64Region].regionData;
				memcpy(g_activeZoneVisTable, regionData + 4, sizeof(g_activeZoneVisTable));
				g_visibilityRegionUpdateState = 2;
				g_activeRegionData = regionData + 0x104;
			}
		}

		// FUNCTION: TOY2 0x0043E2D0 [PROVISIONAL]
		int32_t CountCommandStreamPolygons(void* streamData, uint32_t* formatValue)
		{
			*formatValue = 0;

			int32_t* modelData = static_cast<int32_t*>(streamData);
			int32_t vertexCount = *modelData;
			PrimitiveCommandWord* command;
			if (vertexCount < 0)
			{
				vertexCount = -vertexCount;
				command = reinterpret_cast<PrimitiveCommandWord*>(modelData + vertexCount * 3 + 2);
				*modelData = -vertexCount;
			}
			else
			{
				command = reinterpret_cast<PrimitiveCommandWord*>(modelData + vertexCount * 2 + 1);
			}

			int32_t polygonCount = 0;
			uint16_t commandType = command->value;
			int32_t primitiveCount;
			PrimitiveCommandWord* nextCommand;
			while (commandType != 0xFFFF)
			{
				nextCommand = command;
				switch (commandType & 0x1F)
				{
					case 0:
					case 1:
					case 2:
					case 3:
					case 4:
					case 6:
						primitiveCount = static_cast<int16_t>(command[1].value);
						nextCommand = command + 2;
						if (primitiveCount > 0)
						{
							nextCommand += primitiveCount * 6;
							polygonCount += primitiveCount;
						}
						break;

					case 8:
					case 9:
					case 10:
					case 11:
					case 12:
					case 14:
						primitiveCount = static_cast<int16_t>(command[1].value);
						nextCommand = command + 2;
						if (primitiveCount > 0)
						{
							polygonCount += primitiveCount * 2;
							nextCommand += primitiveCount * 6;
						}
						break;

					case 16:
					case 18:
					case 20:
					case 22:
					case 24:
					case 26:
					case 28:
					case 30:
						primitiveCount = static_cast<int16_t>(command[1].value);
						command->bytes.flags &= 0xF7;
						nextCommand = command + 2;
						if (primitiveCount > 0)
						{
							nextCommand += primitiveCount * 2;
							polygonCount += primitiveCount;
						}
						break;

					case 17:
					case 19:
					case 25:
					case 27:
						primitiveCount = static_cast<int16_t>(command[1].value);
						command->bytes.flags &= 0xF7;
						nextCommand = command + 2;
						if (primitiveCount > 0)
						{
							nextCommand += primitiveCount * 2;
							polygonCount += primitiveCount;
						}
						break;
				}

				command = nextCommand;
				commandType = command->value;
			}
			return polygonCount;
		}

		// FUNCTION: TOY2 0x0043E430 [PROVISIONAL]
		int32_t CalculatePolyCount(Levels::InstanceSection* instance)
		{
			Levels::ModelTypeData_B* renderData = instance->unk6;
			void* modelData = renderData->modelPtr;
			uint32_t formatValue;

			switch (static_cast<uint16_t>(static_cast<uint8_t>(instance->flags)) & 0xFF6F)
			{
				case 1:
				case 4:
				case 0x41:
				case 0x44: {
					Levels::ModelTypeData_B* model = static_cast<Levels::ModelTypeData_B*>(modelData);
					int32_t polygonCount = CountCommandStreamPolygons(model->modelPtr, &formatValue);
					formatValue = model->reservedFlags >> 3;
					if (formatValue > 0x12)
					{
						formatValue = 0x12;
					}
					model->reservedFlags = (formatValue << 3) + (model->reservedFlags & MODEL_PRIMITIVE_FLAGS_MASK);
					return polygonCount;
				}

				case 9:
				case 12:
				case 0x49:
				case 0x4C: {
					Levels::ModelTypeData_A* model = static_cast<Levels::ModelTypeData_A*>(modelData);
					int32_t polygonCount = CountCommandStreamPolygons(reinterpret_cast<void*>(model->meshPtr), &formatValue);
					formatValue = model->padding[0] >> 3;
					if (formatValue > 0x12)
					{
						formatValue = 0x12;
					}
					model->padding[0] = (formatValue << 3) + (model->padding[0] & MODEL_PRIMITIVE_FLAGS_MASK);
					return polygonCount;
				}

				case 2:
				case 3:
				case 0x42:
				case 0x43: {
					int32_t* polygonData = static_cast<int32_t*>(static_cast<Levels::ModelTypeData_B*>(modelData)->modelPtr);
					int32_t polygonCount = 0;
					int32_t batchCount = *polygonData;
					int32_t* batch = polygonData + batchCount * 3 + 1;
					if (batchCount > 0)
					{
						do
						{
							int32_t batchPolygonCount = *batch++;
							if (batchPolygonCount > 0)
							{
								polygonCount += batchPolygonCount;
								batch += batchPolygonCount * 11;
							}
							batchCount--;
						} while (batchCount != 0);
					}
					return polygonCount;
				}

				case 10:
				case 11:
				case 0x4A:
				case 0x4B: {
					int32_t* polygonData = reinterpret_cast<int32_t*>(static_cast<Levels::ModelTypeData_A*>(modelData)->meshPtr);
					int32_t polygonCount = 0;
					int32_t batchCount = *polygonData;
					int32_t* batch = polygonData + batchCount * 3 + 1;
					if (batchCount > 0)
					{
						do
						{
							int32_t batchPolygonCount = *batch++;
							if (batchPolygonCount > 0)
							{
								polygonCount += batchPolygonCount;
								batch += batchPolygonCount * 11;
							}
							batchCount--;
						} while (batchCount != 0);
					}
					return polygonCount;
				}
			}

			return 0;
		}
	}

	// GLOBAL: TOY2 0x00547ED8
	int32_t g_zonedInstanceCount;
	// GLOBAL: TOY2 0x0054D940
	ZoneRenderData g_zoneRenderData[20];
	// GLOBAL: TOY2 0x005574FC
	int32_t g_zoneCount;

	// FUNCTION: TOY2 0x0043E5A0 [PROVISIONAL]
	void InitZoneData()
	{
		int16_t instanceCount = sizeof(Levels::InstanceSection);
		ZoneRenderData* zoneData = g_zoneRenderData;
		do
		{
			zoneData->visibilityDepth = 0;
			zoneData->isProcessed = 0;
			zoneData->secondaryInstanceBytes = 0;
			zoneData->primaryInstanceBytes = 0;
			zoneData++;
		} while (zoneData < g_zoneRenderData + 20);

		int32_t highestZoneIndex;
		int32_t zoneIndex = -1;
		if (Levels::g_instanceSection->flags != 0)
		{
			Levels::InstanceSection* instance = Levels::g_instanceSection;
			do
			{
				if ((uint8_t)instance->category != zoneIndex)
				{
					if (zoneIndex != -1)
						g_zoneRenderData[zoneIndex].primaryInstanceBytes = instanceCount * sizeof(Levels::InstanceSection);

					zoneIndex = (uint8_t)instance->category;
					instanceCount = 0;
				}

				instanceCount++;
				instance++;
			} while (instance->flags != 0);
		}

		if (zoneIndex != -1)
		{
			g_zoneRenderData[zoneIndex].primaryInstanceBytes = instanceCount * sizeof(Levels::InstanceSection);
			highestZoneIndex = zoneIndex;
			g_zoneCount = highestZoneIndex;
		}
		else
			highestZoneIndex = g_zoneCount;

		zoneIndex = -1;
		if (Levels::g_secondInstanceSection->flags != 0)
		{
			Levels::InstanceSection* instance = Levels::g_secondInstanceSection;
			do
			{
				if ((uint8_t)instance->category != zoneIndex)
				{
					if (zoneIndex != -1)
						g_zoneRenderData[zoneIndex].secondaryInstanceBytes = instanceCount * sizeof(Levels::InstanceSection);

					zoneIndex = (uint8_t)instance->category;
					instanceCount = 0;
				}

				instanceCount++;
				instance++;
			} while (instance->flags != 0);

			if (zoneIndex != -1)
				g_zoneRenderData[zoneIndex].secondaryInstanceBytes = instanceCount * sizeof(Levels::InstanceSection);
		}

		if (highestZoneIndex < zoneIndex)
			highestZoneIndex = zoneIndex;

		g_zonedInstanceCount = 0;
		for (int32_t i = 0; i < 20; i++)
		{
			g_zonedInstanceCount += (uint32_t)g_zoneRenderData[i].primaryInstanceBytes / sizeof(Levels::InstanceSection);
			g_zonedInstanceCount += (uint32_t)g_zoneRenderData[i].secondaryInstanceBytes / sizeof(Levels::InstanceSection);
		}

		g_zoneCount = highestZoneIndex + 1;
	}

	namespace Levels
	{
		// GLOBAL: TOY2 0x00559C70
		RecordData* g_recordData[96];

		// GLOBAL: TOY2 0x0054F39C
		PortalZone g_portalZones[20];

		// GLOBAL: TOY2 0x0055A114
		int32_t g_levelLoadConfig;

		// GLOBAL: TOY2 0x0055A12C
		int32_t g_initialLevelLoadConfig;

		// GLOBAL: TOY2 0x00729128
		void* g_cachedAllBuffer;

		// GLOBAL: TOY2 0x005D2BE8
		uint8_t g_levelDataHeapBase[1249280];

		// GLOBAL: TOY2 0x00703C00
		uint8_t* g_levelDataHeapBasePtr;

		// GLOBAL: TOY2 0x00559DF4
		uint8_t* g_levelLoadArena;

		// GLOBAL: TOY2 0x00830C9C
		int32_t g_ambientEmitterScanIndex;

		// GLOBAL: TOY2 0x00830E28
		int32_t g_alternateAmbientEmitterStart;

		// GLOBAL: TOY2 0x0054DE98
		void* g_levelDataBase;

		// GLOBAL: TOY2 0x00556FB8
		int32_t g_unused1;

		// GLOBAL: TOY2 0x005571D0
		int32_t g_unused2;

		// GLOBAL: TOY2 0x00556FB4
		int32_t g_unused3;

		// GLOBAL: TOY2 0x005571CC
		int32_t g_unused4;

		// GLOBAL: TOY2 0x0054F090
		int32_t g_unused5;

		// GLOBAL: TOY2 0x00557AA8
		int32_t g_unused6;

		// GLOBAL: TOY2 0x00547EE8
		int32_t g_unused7;

		// GLOBAL: TOY2 0x00557AA0
		int32_t g_unused8;

		// GLOBAL: TOY2 0x0054DEB8
		int32_t g_unused9;

		// GLOBAL: TOY2 0x0054E07C
		int32_t g_unused10;

		// GLOBAL: TOY2 0x00556FA4
		int32_t g_unused11;

		// GLOBAL: TOY2 0x0054F61C
		int32_t g_unused12;

		// GLOBAL: TOY2 0x0054DD58
		int32_t g_hasZoneData;

		// GLOBAL: TOY2 0x0054BEF0
		int32_t g_type63CullDistance;

		// GLOBAL: TOY2 0x00557C20
		int16_t g_layerScaleTable[4095];

		// GLOBAL: TOY2 0x00557AAC
		int32_t g_type64Count;

		// GLOBAL: TOY2 0x00557A98
		int32_t g_currentType64Region;

		// GLOBAL: TOY2 0x00554040
		Type64 g_type64Structs[24];

		// GLOBAL: TOY2 0x00556FA8
		int32_t g_instanceRecordCount;

		// GLOBAL: TOY2 0x0054E048
		InstanceSection* g_instanceSection;

		// GLOBAL: TOY2 0x0054F628
		ObjectList* g_objectListBase;

		// GLOBAL: TOY2 0x0054DD80
		InstanceSection* g_secondInstanceSection;

		// GLOBAL: TOY2 0x00559C4C
		ObjectDescCache* g_objectDescCache;

		// FUNCTION: TOY2 0x0049FAB0 [PROVISIONAL]
		void DeactivateAmbientEmitter(int32_t emitterIndex, int32_t useAlternateType)
		{
			if (useAlternateType != 0)
				emitterIndex += g_alternateAmbientEmitterStart;

			Vector3I& emitterPosition = g_recordData[58]->data[emitterIndex];
			if (emitterPosition.y != (int32_t)0x80000000)
			{
				const int32_t particleType = useAlternateType * 2 + 0x71;
				for (int32_t particleIndex = 0; particleIndex < 64; particleIndex++)
				{
					Nu3D::Particles::ParticleInstance& particle = Nu3D::Particles::g_particleInstances[particleIndex];
					if (particle.pos.y == emitterPosition.y && particle.lifetime > 0 && particle.pos.x == emitterPosition.x
						&& particle.pos.z == emitterPosition.z && particle.typeId == particleType)
					{
						particle.lifetime = 1;
						break;
					}
				}

				emitterPosition.y = (int32_t)0x80000000;
			}
		}

		// FUNCTION: TOY2 0x0049FB40 [PROVISIONAL]
		void UpdateAmbientEmitters()
		{
			if (g_framePulseOutputs.sixteenTick != 0 && g_recordData[58] != 0)
			{
				int32_t emitterIndex = g_ambientEmitterScanIndex;
				if (emitterIndex < g_recordData[58]->recordCount)
				{
					Vector3I* emitterPosition = &g_recordData[58]->data[emitterIndex];
					do
					{
						if (emitterPosition->y != (int32_t)0x80000000)
						{
							const int32_t distanceY = (Camera::g_renderCameraTransform.pos.y - emitterPosition->y) >> 8;
							const int32_t distanceX = (Camera::g_renderCameraTransform.pos.x - emitterPosition->x) >> 8;
							const int32_t distanceZ = (Camera::g_renderCameraTransform.pos.z - emitterPosition->z) >> 8;
							const int32_t distanceSquared = distanceZ * distanceZ + distanceY * distanceY + distanceX * distanceX;
							if (distanceSquared < 360000 && distanceSquared + 1 != 0)
							{
								if (emitterIndex < g_alternateAmbientEmitterStart)
									Nu3D::Particles::SpawnFromPreset(emitterPosition->x, emitterPosition->y, emitterPosition->z, 0x71, 2);
								else
									Nu3D::Particles::SpawnFromPreset(emitterPosition->x, emitterPosition->y, emitterPosition->z, 0x73, 2);
							}
						}

						emitterIndex += 4;
						emitterPosition += 4;
					} while (emitterIndex < g_recordData[58]->recordCount);
				}

				g_ambientEmitterScanIndex++;
				if (g_ambientEmitterScanIndex >= g_recordData[58]->recordCount || g_ambientEmitterScanIndex >= 4)
					g_ambientEmitterScanIndex = 0;
			}
		}

		// FUNCTION: TOY2 0x004CEA20 [MATCHED]
		void FlushRenderer()
		{
			Renderer::Glue::ReleaseBackdrop();
			Nu3D::Light::Cleanup();
			if (NGNLoader::g_ngnImage)
			{
				NGNLoader::NGNImage::Destroy(NGNLoader::g_ngnImage);
			}
			NGNLoader::g_ngnImage = 0;
			Renderer::Cleanup();
		}

		// FUNCTION: TOY2 0x004CE8B0 [MATCHED]
		int32_t InitLevelDefaults()
		{
			Renderer::Init();
			Renderer::BuildGammaCorrectionLUT(g_toyCfgData.gammaCorrection);

			Nu3D::SetUseAsDiffuseModulation(0);
			Nu3D::SetDefaultPrimFlags(32);

			Nu3D::Light::BuildGlobalLights();
			Nu3D::Camera::ApplyTransformToCamera(0);

			Toy2::Animation::ResetNodeAngles();

			SoftwareRenderer::InitialisePrimarySurface_T();

			Renderer::GetBlendShadeCaps(&Renderer::g_deviceBlendShadeCapsCpy);

			SaveManager::SaveToFile(0, "default.cfg");

			return WriteCfg();
		}

		// FUNCTION: TOY2 0x0043E6E0 [PROVISIONAL]
		int32_t LoadDAT(int32_t levelId, int32_t fileSize)
		{
			// FUNCTION IS WIP, very big and confusing
			// the commented out bits are what we don't need for
			// getting into level select thankfully

			int32_t l_cullBandWidth; // eax
			uint32_t l_scaleTableZeroCount; // esi
			int16_t* l_scaleTableCursor; // edx
			int16_t* l_scaleFillPtr; // edi
			int32_t l_oddTailCount; // ecx
			int16_t* l_region0600; // edi
			int16_t* l_region0400; // edx
			int16_t* l_region0512; // edx
			int32_t l_type64Count; // ebp
			int16_t l_type65Idx; // bx
			int16_t l_recordType; // ax
			int32_t l_type64Idx; // eax
			int32_t l_zoneAnchorIdx; // ecx
			int32_t* l_zoneAnchor; // eax
			int32_t l_portalVertIdx; // ecx
			int32_t l_portalVert; // eax
			int32_t l_coordRaw; // edx
			int32_t l_portalByteOffset; // eax
			int16_t l_portalCounts_; // dx
			int32_t l_termOffset; // ecx
			int32_t l_secondIdx; // eax
			InstanceSection* l_secondSection; // eax
			ModelTypeData_B* l_sectionStorage; // esi
			InstanceSection* l_secondSection_; // ebp
			int8_t l_secondSection14Val; // cl
			int8_t* l_secondSection14; // eax
			int16_t l_modelId; // cx
			ModelTypeData_B* l_typeData2; // edi
			ModelTypeData_A* l_typeData; // edi
			int32_t l_mat_21; // ecx
			int32_t l_mat_12; // ecx
			int32_t l_mat_22; // edx
			int32_t l_secRotY; // ecx
			int32_t l_mat_13; // edx
			int32_t l_secRotZ; // edx
			int32_t l_secRotZValue; // eax
			int32_t l_secondSectionCount; // ecx
			int32_t l_secondInstance; // ebp
			int16_t l_modelIdRaw; // ax
			ModelTypeData_B* l_modelB2; // edi
			ModelTypeData_A* l_modelA2; // edi
			int32_t l_secMat21; // ecx
			int32_t l_secMat12; // ecx
			int32_t l_secMat22; // edx
			int32_t l_secRotY2; // ecx
			int32_t l_secMat13; // edx
			int32_t l_secRotZ2; // edx
			int32_t l_secRotZ2Value; // eax
			int8_t l_nextFlags; // al
			int32_t l_lodTrailerSize; // ecx
			Type64* l_record; // ebp
			int32_t* l_unkVar0Y; // edi
			int32_t* l_unkVar0Z; // esi
			int32_t l_recordOffset; // edx
			int32_t l_extentZ; // edx
			int32_t l_extentY; // esi
			int32_t l_extentX; // edi
			ObjectDescCache* l_descCache; // eax
			ObjectList* l_objectList; // ecx
			int32_t l_objIdx; // edi
			int32_t l_cacheByteOffset; // eax
			int32_t l_objEntryOffset; // edx
			uint32_t l_meshPtr; // esi
			int32_t l_objDesc; // ecx
			int32_t l_totalRecordCount; // [esp+10h] [ebp-30h]
			int16_t* l_newLvlBase; // [esp+10h] [ebp-30h]
			int32_t l_type64Count_; // [esp+14h] [ebp-2Ch]
			int16_t l_portalCounts[20]; // [esp+18h] [ebp-28h] BYREF
			int32_t l_bboxVert; // [esp+48h] [ebp+8h]
			int32_t l_bboxVert2; // [esp+48h] [ebp+8h]
			char* l_bboxVert3; // [esp+48h] [ebp+8h]
			char* l_bboxVert4; // [esp+48h] [ebp+8h]

			Levels::g_unused1 = 256;
			Levels::g_unused2 = 256;

			g_hasZoneData = 0;
			Weather::g_spawnAccumulator = 0;
			Weather::g_precipitationParticles = 0;

			Levels::g_unused3 = 512;
			Levels::g_unused4 = 512;

			if (g_renderMode == RENDERMODE_SOFTWARE || g_renderMode == RENDERMODE_D3D)
			{
				l_cullBandWidth = 1664;
				g_type63CullDistance = 1664;
			}
			else
			{
				l_cullBandWidth = g_type63CullDistance;
			}

			l_scaleTableZeroCount = l_cullBandWidth - 512;

			Levels::g_unused5 = l_cullBandWidth;
			Levels::g_unused6 = l_cullBandWidth - 512;
			Levels::g_unused7 = 3072;

			l_scaleTableCursor = g_layerScaleTable;
			Levels::g_unused8 = 170;

			if (l_cullBandWidth - 512 > 0)
			{
				l_scaleTableCursor = &g_layerScaleTable[l_scaleTableZeroCount];

				memset(g_layerScaleTable, 0, 4 * (l_scaleTableZeroCount >> 1));
				l_scaleFillPtr = &g_layerScaleTable[2 * (l_scaleTableZeroCount >> 1)];

				for (l_oddTailCount = l_cullBandWidth & 1; l_oddTailCount; --l_oddTailCount)
					*l_scaleFillPtr++ = 0;
			}

			l_region0600 = l_scaleTableCursor;
			l_region0400 = l_scaleTableCursor + 170;

			Nu3D::MemSet32Util(l_region0600, 85, 0x6000600);
			Nu3D::MemSet32Util(l_region0400, 85, 0x4000400);

			for (int16_t* p = l_region0400 + 170; p != g_layerScaleTable + (sizeof(g_layerScaleTable) / sizeof(g_layerScaleTable[0])); ++p)
				*p = 512;

			LevelDataHeader* l_levelDataHeader = reinterpret_cast<LevelDataHeader*>(g_levelDataBase);
			l_type64Count = 0;
			Levels::g_unused9 = 0;

			g_type64Count = 0;

			Type64* end = g_type64Structs + (sizeof(g_type64Structs) / sizeof(g_type64Structs[0]));
			Type64* l_type64Init = g_type64Structs;

			do
			{
				(l_type64Init++)->recordPtr = 0;
			} while (l_type64Init < end);

			uint8_t* l_recordPtr = l_levelDataHeader->records;
			memset(l_portalCounts, 0, sizeof(l_portalCounts));

			l_type65Idx = 65;

			if (l_levelDataHeader->recordCount > 0)
			{
				l_totalRecordCount = l_levelDataHeader->recordCount;

				printf("Total Record Count -> %d\n", l_totalRecordCount);

				do
				{
					RecordData* l_record = reinterpret_cast<RecordData*>(l_recordPtr);
					l_recordType = static_cast<int16_t>(l_record->recordType);

					if (l_recordType >= 0)
					{
						if (l_recordType == 64)
						{
							l_type64Idx = l_type64Count++;

							g_type64Count = l_type64Count;
							g_type64Structs[l_type64Idx].recordPtr = l_recordPtr;

							l_recordPtr += 12 * static_cast<int16_t>(l_record->recordCount) + 4;
						}
						else
						{
							if (l_recordType <= 64)
							{
								g_recordData[l_recordType] = reinterpret_cast<RecordData*>(l_recordPtr);
							}
							else
							{
								// g_hasZoneData = 1;
								// g_recordData[l_type65Idx] = reinterpret_cast<RecordData*>(l_recordPtr);

								// l_zoneAnchorIdx = *(l_recordPtr + 12 * *l_recordPtr - 8);
								// l_zoneAnchor = reinterpret_cast<int32_t*>(l_recordPtr + 12 * *l_recordPtr - 8);

								// *(&g_portalEntries[l_zoneAnchorIdx].entries[0].recordIdx + l_portalCounts[l_zoneAnchorIdx]) = l_type65Idx;
								// *(&g_portalEntries[*l_zoneAnchor].entries[0].categoryIdx + l_portalCounts[*l_zoneAnchor]) = *(l_zoneAnchor + 4);

								// l_portalVertIdx = 0;
								// l_portalCounts[*l_zoneAnchor] += 2;

								// l_portalVert = l_recordPtr + 4;

								// if (*l_recordPtr > 0)
								// {
								// 	do
								// 	{
								// 		l_coordRaw = *l_portalVert;
								// 		l_portalVert += 12;

								// 		*(l_portalVert - 12) = l_coordRaw >> 2;
								// 		*(l_portalVert - 10) = *(l_portalVert - 8) >> 2;
								// 		*(l_portalVert - 8) = *(l_portalVert - 4) >> 2;

								// 		++l_portalVertIdx;
								// 	} while (l_portalVertIdx < *l_recordPtr);
								// }

								// l_type64Count = g_type64Count;
								// ++l_type65Idx;
							}

							if (static_cast<int16_t>(l_record->recordType) == 63)
								l_recordPtr += 16 * static_cast<int16_t>(l_record->recordCount) + 4;
							else
								l_recordPtr += 12 * static_cast<int16_t>(l_record->recordCount) + 4;
						}
					}
					else
					{
						g_recordData[-l_recordType] = reinterpret_cast<RecordData*>(l_recordPtr);
						l_recordPtr += 4 * ((3 * static_cast<int16_t>(l_record->recordCount) + 1) / 2) + 16;
					}

					--l_totalRecordCount;
				} while (l_totalRecordCount);
			}

			/*
			l_portalByteOffset = 0;
			*&l_portalCounts_ = l_portalCounts;
			do
			{
				l_termOffset = l_portalByteOffset + **&l_portalCounts_;
				l_portalByteOffset += 32;
				*&l_portalCounts_ += 2;

				*(&g_portalEntries[0].entries[0].recordIdx + l_termOffset) = -1;
				*(&g_portalEntries[0].entries[0].categoryIdx + l_termOffset) = -1;

			} while (l_portalByteOffset < 640);*/

			l_secondIdx = *l_recordPtr << 7;
			g_instanceRecordCount = 0;

			l_secondSection = (InstanceSection*)(l_recordPtr + 4 * (l_secondIdx / 4) + 4);
			l_sectionStorage = (ModelTypeData_B*)((uint8_t*)g_levelDataBase + fileSize);

			printf("Second Idx %d\n", l_secondIdx);

			g_instanceSection = l_secondSection;

			l_secondSection_ = l_secondSection;
			l_secondSection14Val = l_secondSection->flags;
			l_secondSection14 = &l_secondSection->flags;

			printf("l_secondSection14 %d\n", l_secondSection14Val);

			/*
			if (l_secondSection14Val)
			{
				do
				{
					++g_instanceRecordCount;
					l_modelId = l_secondSection_->modelId;

					if (l_modelId < 0)
						l_secondSection_->modelId = -l_modelId;

					if (*l_secondSection14 < 0)
						*l_secondSection14 = (*l_secondSection14 | 0x50) - 16;

					if ((*l_secondSection14 & 8) != 0)
					{
						l_typeData = (g_levelDataBase + l_secondSection_->unk6);
						l_secondSection_->unk6 = l_typeData;
						l_typeData->meshPtr += g_levelDataBase;

						if ((*l_secondSection14 & 0x20) != 0)
						{
							*l_secondSection14 |= 0x40u;
							l_typeData->animPtr += g_levelDataBase;
						}

						Nu3D::Math::SetRotationXYZ(&l_typeData->rotation, l_sectionStorage);

						l_mat_21 = l_sectionStorage->mat_21;
						l_sectionStorage->mat_11 = l_typeData->scaleX * l_sectionStorage->mat_11 / 4096;
						l_sectionStorage->mat_21 = l_mat_21 * l_typeData->scaleX / 4096;
						l_mat_12 = l_sectionStorage->mat_12;
						l_sectionStorage->rotation.x = l_typeData->scaleX * l_sectionStorage->rotation.x / 4096;
						l_mat_22 = l_sectionStorage->mat_22;

						l_sectionStorage->mat_12 = l_mat_12 * l_typeData->scaleY / 4096;
						l_secRotY = l_sectionStorage->rotation.y;
						l_sectionStorage->mat_22 = l_mat_22 * l_typeData->scaleY / 4096;
						l_mat_13 = l_sectionStorage->mat_13;
						l_sectionStorage->rotation.y = l_secRotY * l_typeData->scaleY / 4096;
						l_sectionStorage->mat_13 = l_mat_13 * l_typeData->scaleZ / 4096;
						l_secRotZ = l_sectionStorage->rotation.z;
						l_sectionStorage->mat_23 = l_typeData->scaleZ * l_sectionStorage->mat_23 / 4096;
						l_secRotZValue = l_secRotZ * l_typeData->scaleZ;

						l_sectionStorage->modelPtr = l_typeData;
						l_sectionStorage->animPtr = 0;
						l_sectionStorage->rotation.z = l_secRotZValue / 4096;

						l_secondSection_->unk6 = l_sectionStorage;
						l_sectionStorage->polyCount = Toy2::Level::CalculatePolyCount(l_secondSection_);
						l_typeData->flags &= 0xFu;
					}
					else
					{
						l_typeData2 = (g_levelDataBase + l_secondSection_->unk6);
						l_secondSection_->unk6 = l_typeData2;
						l_typeData2->modelPtr = l_typeData2->modelPtr + g_levelDataBase;

						if ((*l_secondSection14 & 0x20) != 0)
						{
							*l_secondSection14 |= 0x40u;
							l_typeData2->animPtr += g_levelDataBase;
						}

						Nu3D::Math::SetRotationXYZ(&l_typeData2->rotation, l_sectionStorage);

						l_sectionStorage->modelPtr = l_typeData2;
						l_sectionStorage->animPtr = 0;
						l_secondSection_->unk6 = l_sectionStorage;

						l_sectionStorage->polyCount = Toy2::Level::CalculatePolyCount(l_secondSection_);
						l_typeData2->flags &= 0xFu;
					}

					++l_secondSection_;
					++l_sectionStorage;

					l_secondSection14 = &l_secondSection_->flags;
				} while (l_secondSection_->flags);
			}*/

			g_objectListBase = (ObjectList*)((uint8_t*)l_secondSection_ + sizeof(InstanceSection));
			l_secondSectionCount = g_objectListBase->count;

			g_secondInstanceSection = reinterpret_cast<InstanceSection*>(&g_objectListBase->entries[l_secondSectionCount + 1]);

			printf("Second section count -> %d\n", l_secondSectionCount);
			printf("Second Instance Flags -> %d\n", g_secondInstanceSection->flags);

			/*
			l_secondInstance = ((uint8_t*)l_secondSection_ + 20 + 4 * l_secondSectionCount + 22);

			if (*(g_secondInstanceSection + 14))
			{
				do
				{
					++g_instanceRecordCount;
					l_modelIdRaw = *(l_secondInstance - 2);

					if (l_modelIdRaw < 0)
						*(l_secondInstance - 2) = -l_modelIdRaw;

					if (*l_secondInstance < 0)
						*l_secondInstance = (*l_secondInstance | 0x50) - 16;

					if ((*l_secondInstance & 8) != 0)
					{
						l_modelA2 = (g_levelDataBase + *(l_secondInstance + 2));
						*(l_secondInstance + 2) = l_modelA2;
						l_modelA2->meshPtr += g_levelDataBase;

						if ((*l_secondInstance & 0x20) != 0)
						{
							*l_secondInstance |= 0x40u;
							l_modelA2->animPtr += g_levelDataBase;
						}

						Nu3D::Math::SetRotationXYZ(&l_modelA2->rotation, l_sectionStorage);
						l_secMat21 = l_sectionStorage->mat_21;
						l_sectionStorage->mat_11 = l_modelA2->scaleX * l_sectionStorage->mat_11 / 4096;
						l_sectionStorage->mat_21 = l_secMat21 * l_modelA2->scaleX / 4096;
						l_secMat12 = l_sectionStorage->mat_12;
						l_sectionStorage->rotation.x = l_modelA2->scaleX * l_sectionStorage->rotation.x / 4096;
						l_secMat22 = l_sectionStorage->mat_22;
						l_sectionStorage->mat_12 = l_secMat12 * l_modelA2->scaleY / 4096;
						l_secRotY2 = l_sectionStorage->rotation.y;
						l_sectionStorage->mat_22 = l_secMat22 * l_modelA2->scaleY / 4096;
						l_secMat13 = l_sectionStorage->mat_13;
						l_sectionStorage->rotation.y = l_secRotY2 * l_modelA2->scaleY / 4096;
						l_sectionStorage->mat_13 = l_secMat13 * l_modelA2->scaleZ / 4096;
						l_secRotZ2 = l_sectionStorage->rotation.z;
						l_sectionStorage->mat_23 = l_modelA2->scaleZ * l_sectionStorage->mat_23 / 4096;
						l_secRotZ2Value = l_secRotZ2 * l_modelA2->scaleZ;
						l_sectionStorage->modelPtr = l_modelA2;
						l_sectionStorage->animPtr = 0;
						l_sectionStorage->rotation.z = l_secRotZ2Value / 4096;

						*(l_secondInstance + 2) = l_sectionStorage;
						l_sectionStorage->polyCount = Toy2::Level::CalculatePolyCount((l_secondInstance - 14));
						l_modelA2->flags &= 0xFu;
					}
					else
					{
						l_modelB2 = (g_levelDataBase + *(l_secondInstance + 2));
						*(l_secondInstance + 2) = l_modelB2;
						l_modelB2->modelPtr = l_modelB2->modelPtr + g_levelDataBase;

						if ((*l_secondInstance & 0x20) != 0)
						{
							*l_secondInstance |= 0x40u;
							l_modelB2->animPtr += g_levelDataBase;
						}

						Nu3D::Math::SetRotationXYZ(&l_modelB2->rotation, l_sectionStorage);
						l_sectionStorage->modelPtr = l_modelB2;
						l_sectionStorage->animPtr = 0;
						*(l_secondInstance + 2) = l_sectionStorage;
						l_sectionStorage->polyCount = Toy2::Level::CalculatePolyCount((l_secondInstance - 14));

						l_modelB2->flags &= 0xFu;
					}

					l_nextFlags = *(l_secondInstance + 20);
					l_secondInstance += 20;
					++l_sectionStorage;
				} while (l_nextFlags);
			}*/

			LevelDataTrailer* l_levelDataTrailer = reinterpret_cast<LevelDataTrailer*>(reinterpret_cast<uint8_t*>(g_levelDataBase) + fileSize) - 1;
			l_lodTrailerSize = l_levelDataTrailer->size;
			printf("Load trailer size -> %d\n", l_lodTrailerSize);
			printf("Object List Base Count -> %d\n", g_objectListBase->count);

			/*
			if (l_lodTrailerSize == -1)
			{
				Levels::g_unused10 = -1;
				memset(g_activeZoneVisTable, 255u, 256u);
			}
			else
			{
				l_newLvlBase = (g_levelDataBase + p_fileSize - l_lodTrailerSize);

				if (g_type64Count > 0)
				{
					l_type64Records = &g_type64Structs[0].recordPtr;
					l_type64Count_ = g_type64Count;

					do
					{
						l_record = ADJ(l_type64Records);

						l_unkVar0Y = &ADJ(l_type64Records)->boundsMin.y;
						l_unkVar0Z = &ADJ(l_type64Records)->boundsMin.z;

						ADJ(l_type64Records)->boundsMin.x = 0x7FFFFFFF;
						ADJ(l_type64Records)->boundsMin.y = 0x7FFFFFFF;
						ADJ(l_type64Records)->boundsMin.z = 0x7FFFFFFF;

						ADJ(l_type64Records)->boundsMax.x = 0x80000000;
						ADJ(l_type64Records)->boundsMax.y = 0x80000000;
						ADJ(l_type64Records)->boundsMax.z = 0x80000000;

						for (l_recordOffset = 0; l_recordOffset < 24; l_recordOffset += 12)
						{
							l_bboxVert = ADJ(l_type64Records)->recordPtr + l_recordOffset;

							if (*(l_bboxVert + 4) < l_record->boundsMin.x)
								l_record->boundsMin.x = *(l_bboxVert + 4);

							l_bboxVert2 = ADJ(l_type64Records)->recordPtr + l_recordOffset;

							if (*(l_bboxVert2 + 8) < *l_unkVar0Y)
								*l_unkVar0Y = *(l_bboxVert2 + 8);

							if (*(ADJ(l_type64Records)->recordPtr + l_recordOffset + 12) < *l_unkVar0Z)
								*l_unkVar0Z = *(ADJ(l_type64Records)->recordPtr + l_recordOffset + 12);

							l_bboxVert3 = ADJ(l_type64Records)->recordPtr + l_recordOffset;

							if (*(l_bboxVert3 + 1) > ADJ(l_type64Records)->boundsMax.x)
								ADJ(l_type64Records)->boundsMax.x = *(l_bboxVert3 + 1);

							l_bboxVert4 = ADJ(l_type64Records)->recordPtr + l_recordOffset;

							if (*(l_bboxVert4 + 2) > ADJ(l_type64Records)->boundsMax.y)
								ADJ(l_type64Records)->boundsMax.y = *(l_bboxVert4 + 2);

							if (*(ADJ(l_type64Records)->recordPtr + l_recordOffset + 12) > ADJ(l_type64Records)->boundsMax.z)
								ADJ(l_type64Records)->boundsMax.z = *(ADJ(l_type64Records)->recordPtr + l_recordOffset + 12);
						}

						l_extentZ = ADJ(l_type64Records)->boundsMax.z - *l_unkVar0Z;
						l_extentY = ADJ(l_type64Records)->boundsMax.y - *l_unkVar0Y;
						l_extentX = ADJ(l_type64Records++)->boundsMax.x - l_record->boundsMin.x;

						// Confirm this field type.
						l_type64Records[-2].regionData =
							((l_extentZ >> 5) * (l_extentZ >> 5) + (l_extentY >> 5) * (l_extentY >> 5) + (l_extentX >> 5) * (l_extentX >> 5));

						l_type64Records[-1].boundsMin.y = l_newLvlBase;

						l_newLvlBase = (l_newLvlBase + *l_newLvlBase);

						--l_type64Count_;
					} while (l_type64Count_);
				}

				g_currentType64Region = -1;
				Levels::g_unused11 = 1;
				Toy2::Level::UpdateVisibilityRegion();
				Levels::g_unused10 = 0;
			}

			Levels::g_unused12 = 400;

			if (g_objectDescCache)
				free(g_objectDescCache);

			l_descCache = malloc(28 * (g_objectListBase->count + 1));
			l_objectList = g_objectListBase;
			g_objectDescCache = l_descCache;

			l_objIdx = 0;

			if (g_objectListBase->count >= 0)
			{
				l_cacheByteOffset = 0;
				l_objEntryOffset = 4;

				do
				{
					l_meshPtr = *(&l_objectList->count + l_objEntryOffset);

					if (l_meshPtr)
					{
						*(&l_objectList->count + l_objEntryOffset) = g_levelDataBase + l_meshPtr;

						g_objectDescCache[l_cacheByteOffset].unkInt7 = *(*(&g_objectListBase->count + l_objEntryOffset) + 14);

						l_objDesc = *(*(*(&g_objectListBase->count + l_objEntryOffset) + 16) + 20);

						g_objectDescCache[l_cacheByteOffset].unkInt8 = *(l_objDesc + 12);
						g_objectDescCache[l_cacheByteOffset].unkInt9 = *(l_objDesc + 14);
						g_objectDescCache[l_cacheByteOffset].unkInt10 = *(l_objDesc + 16);
						g_objectDescCache[l_cacheByteOffset].unkInt4 = *(l_objDesc + 18);
						g_objectDescCache[l_cacheByteOffset].unkInt5 = *(l_objDesc + 20);
						g_objectDescCache[l_cacheByteOffset].unkInt6 = *(l_objDesc + 22);

						l_objectList = g_objectListBase;
					}

					++l_objIdx;
					l_objEntryOffset += 4;
					++l_cacheByteOffset;
				} while (l_objIdx <= l_objectList->count);
			}

			if (g_hasZoneData)
				Toy2::InitZoneData();
			*/

			return 32 * g_instanceRecordCount;
		}

		// FUNCTION: TOY2 0x004521A0 [MATCHED]
		void BuildLevelPath(int32_t level, char* output, const char* suffix)
		{
			if (level < 10)
			{
				strcpy(output, "level0");
				output[6] = level + 48;
			}
			else
			{
				strcpy(output, "level");
				output[5] = level / 10 + 48;
				output[6] = level % 10 + 48;
			}

			output[7] = '\\';
			output[8] = '\0';
			strcat(output, suffix);
		}

		// FUNCTION: TOY2 0x00452EF0 [MATCHED]
		void BuildLevelBinPath(int32_t level, char* output, int32_t useAlternateRange)
		{
			strcpy(output, "..\\level");

			if (useAlternateRange > 0)
				level += 10;

			char digits[4];
			if (level < 10)
			{
				digits[0] = '0';
				digits[1] = level + '0';
			}
			else
			{
				digits[0] = level / 10 + '0';
				digits[1] = level % 10 + '0';
			}

			digits[2] = '\0';
			strcat(output, digits);
			strcat(output, ".bin");
		}

		// FUNCTION: TOY2 0x00452FC0 [PROVISIONAL]
		void InitLevelPlay(int32_t levelId)
		{
			SoftwareRenderer::SetLevelFileIndex(g_levelFileIndex);
			FlushRenderer();
			InitLevelDefaults();

			FileUtils::LoadFile("rand.dat", g_randDatBuffer);

			int32_t levelIdCpy = levelId;

			AudioManager::g_curTrackIndex = -1;
			g_isElevatorHopLevel = levelId != 10;

			Logger::Log("InitLevelPlay : START.\n");

			AudioManager::LoadSfxPackForLevel(levelId);

			memset(&g_recordData, 0, sizeof(g_recordData));
			memset(CharacterLoader::g_boneTransforms, 0, sizeof(CharacterLoader::g_boneTransforms));
			memset(Collision::g_collisionMeshInstances, 0, sizeof(Collision::g_collisionMeshInstances));
			memset(Collision::g_mathScratch, 0, sizeof(Collision::g_mathScratch));

			SoftwareRenderer::g_backdropScrollOverride.x = -32768;
			SoftwareRenderer::g_backdropScrollOverride.y = -32768;

			g_hasBackdrop = 0;

			CharacterLoader::g_boneRemapCount = 0;
			CharacterLoader::g_processedBoneRemapCount = 0;
			CharacterLoader::g_animationSlotCount = 0;

			SoftwareRenderer::g_backdropTextureColumn = -1;

			Renderer::Shadows::g_shadowCount = 0;
			Renderer::Shadows::g_unusedShadowVar = 0;

			for (int32_t particleIdx = 0; particleIdx < 64; particleIdx++)
				Nu3D::Particles::g_particleInstances[particleIdx].lifetime = 0;

			Nu3D::Camera::InitViewMatrixGlobals();

			memset(CharacterLoader::g_alternateAllParse, 0, sizeof(CharacterLoader::g_alternateAllParse));
			memset(CharacterLoader::g_charFileDataCache, 0, sizeof(CharacterLoader::g_charFileDataCache));

			g_cachedAllBuffer = 0;

			if (levelId > 10)
			{
				g_levelLoadConfig |= 256;

				if ((g_levelLoadConfig & 192) == 0)
					g_levelLoadConfig |= 64;

				levelIdCpy = levelId - 10;
			}

			int32_t loadConfigCpy = g_levelLoadConfig;
			g_initialLevelLoadConfig = g_levelLoadConfig;

			uint8_t* dataBuffer = g_levelDataHeapBase;

			g_levelDataHeapBasePtr = g_levelDataHeapBase;
			g_levelLoadArena = g_levelDataHeapBase;

			char fileNameBuffer[128];
			fileNameBuffer[0] = '\0';

			if ((loadConfigCpy & 1) == 0)
			{
				int32_t rawVariant = ((loadConfigCpy >> 6) & 3) - 1;
				const char* rawFileName;

				if (rawVariant)
				{
					int32_t rawVariantStep = rawVariant - 1;

					if (rawVariantStep)
					{
						if (rawVariantStep == 1)
						{
							if (levelIdCpy >= 10)
							{
								strcpy(fileNameBuffer, "level");
								fileNameBuffer[5] = levelIdCpy / 10 + 48;
								fileNameBuffer[6] = levelIdCpy % 10 + 48;
							}
							else
							{
								strcpy(fileNameBuffer, "level0");
								fileNameBuffer[6] = levelIdCpy + 48;
							}

							strcpy(&fileNameBuffer[7], "\\");
							rawFileName = "level3.raw";
						}
						else
						{
							if (levelIdCpy >= 10)
							{
								strcpy(fileNameBuffer, "level");
								fileNameBuffer[5] = levelIdCpy / 10 + 48;
								fileNameBuffer[6] = levelIdCpy % 10 + 48;
							}
							else
							{
								strcpy(fileNameBuffer, "level0");
								fileNameBuffer[6] = levelIdCpy + 48;
							}

							strcpy(&fileNameBuffer[7], "\\");
							rawFileName = "level.raw";
						}
					}
					else
					{
						if (levelIdCpy >= 10)
						{
							strcpy(fileNameBuffer, "level");
							fileNameBuffer[5] = levelIdCpy / 10 + 48;
							fileNameBuffer[6] = levelIdCpy % 10 + 48;
						}
						else
						{
							strcpy(fileNameBuffer, "level0");
							fileNameBuffer[6] = levelIdCpy + 48;
						}

						strcpy(&fileNameBuffer[7], "\\");
						rawFileName = "level2.raw";
					}
				}
				else
				{
					if (levelIdCpy >= 10)
					{
						strcpy(fileNameBuffer, "level");
						fileNameBuffer[5] = levelIdCpy / 10 + 48;
						fileNameBuffer[6] = levelIdCpy % 10 + 48;
					}
					else
					{
						strcpy(fileNameBuffer, "level0");
						fileNameBuffer[6] = levelIdCpy + 48;
					}

					strcpy(&fileNameBuffer[7], "\\");
					rawFileName = "level1.raw";
				}

				strcat(fileNameBuffer, rawFileName);
				RawLoader::LoadPacketData(fileNameBuffer);
				loadConfigCpy = g_levelLoadConfig;
			}

			dataBuffer = g_levelLoadArena;

			char levelDigits[4];

			if ((loadConfigCpy & 2) == 0)
			{
				int32_t binVariant = ((loadConfigCpy >> 8) & 3) - 1;

				if (binVariant)
				{
					int32_t binVariantStep = binVariant - 1;

					if (binVariantStep)
					{
						if (binVariantStep == 1)
						{
							strcpy(fileNameBuffer, "..\\level");

							if (levelIdCpy + 10 >= 10)
							{
								levelDigits[2] = 0;
								levelDigits[0] = (levelIdCpy + 10) / 10 + 48;
								levelDigits[1] = (levelIdCpy + 10) % 10 + 48;
							}
							else
							{
								levelDigits[0] = '0';
								levelDigits[1] = levelIdCpy + 58;
								levelDigits[2] = 0;
							}
						}
						else
						{
							strcpy(fileNameBuffer, "..\\level");

							if (levelIdCpy >= 10)
							{
								levelDigits[2] = 0;
								levelDigits[0] = levelIdCpy / 10 + 48;
								levelDigits[1] = levelIdCpy % 10 + 48;
							}
							else
							{
								levelDigits[0] = '0';
								levelDigits[2] = 0;
								levelDigits[1] = levelIdCpy + 48;
							}
						}
					}
					else
					{
						strcpy(fileNameBuffer, "..\\level");

						if (levelIdCpy + 10 >= 10)
						{
							levelDigits[2] = 0;
							levelDigits[0] = (levelIdCpy + 10) / 10 + 48;
							levelDigits[1] = (levelIdCpy + 10) % 10 + 48;
						}
						else
						{
							levelDigits[0] = '0';
							levelDigits[1] = levelIdCpy + 58;
							levelDigits[2] = 0;
						}
					}
				}
				else
				{
					strcpy(fileNameBuffer, "..\\level");

					if (levelIdCpy + 10 >= 10)
					{
						levelDigits[0] = (levelIdCpy + 10) / 10 + 48;
						levelDigits[1] = (levelIdCpy + 10) % 10 + 48;
					}
					else
					{
						levelDigits[0] = 48;
						levelDigits[1] = levelIdCpy + 58;
					}

					levelDigits[2] = 0;
				}

				strcat(fileNameBuffer, levelDigits);
				strcat(fileNameBuffer, ".bin");
			}

			Renderer::InitSpriteSheets();

			const char* datFileName;
			void* bufferPtr;
			int32_t loadedCharacterBoneCount = 0;

			memset(fileNameBuffer, 0, sizeof(fileNameBuffer));

			if ((g_levelLoadConfig & 4) == 0)
			{
				bufferPtr = dataBuffer;
				g_levelDataBase = dataBuffer;

				switch ((g_levelLoadConfig >> 8) & 3)
				{
					case 1:
						if (levelIdCpy >= 10)
						{
							strcpy(fileNameBuffer, "level");
							fileNameBuffer[5] = levelIdCpy / 10 + 48;
							fileNameBuffer[6] = levelIdCpy % 10 + 48;
						}
						else
						{
							strcpy(fileNameBuffer, "level0");
							fileNameBuffer[6] = levelIdCpy + 48;
						}

						strcpy(&fileNameBuffer[7], "\\");
						datFileName = "level1.dat";
						break;

					case 2:
						if (levelIdCpy >= 10)
						{
							strcpy(fileNameBuffer, "level");
							fileNameBuffer[5] = levelIdCpy / 10 + 48;
							fileNameBuffer[6] = levelIdCpy % 10 + 48;
						}
						else
						{
							strcpy(fileNameBuffer, "level0");
							fileNameBuffer[6] = levelIdCpy + 48;
						}

						strcpy(&fileNameBuffer[7], "\\");
						datFileName = "level2.dat";
						break;

					case 3:
						if (levelIdCpy >= 10)
						{
							strcpy(fileNameBuffer, "level");
							fileNameBuffer[5] = levelIdCpy / 10 + 48;
							fileNameBuffer[6] = levelIdCpy % 10 + 48;
						}
						else
						{
							strcpy(fileNameBuffer, "level0");
							fileNameBuffer[6] = levelIdCpy + 48;
						}

						strcpy(&fileNameBuffer[7], "\\");
						datFileName = "level3.dat";
						break;

					default:
						if (levelIdCpy >= 10)
						{
							strcpy(fileNameBuffer, "level");
							fileNameBuffer[5] = levelIdCpy / 10 + 48;
							fileNameBuffer[6] = levelIdCpy % 10 + 48;
						}
						else
						{
							strcpy(fileNameBuffer, "level0");
							fileNameBuffer[6] = levelIdCpy + 48;
						}

						strcpy(&fileNameBuffer[7], "\\");
						datFileName = "level.dat";
						break;
				}

				strcat(fileNameBuffer, datFileName);

				int32_t fileSize = FileUtils::LoadFile(fileNameBuffer, bufferPtr);
				dataBuffer = dataBuffer + fileSize;

				int32_t offset = LoadDAT(levelIdCpy, fileSize);
				dataBuffer = dataBuffer + offset;
			}

			fileNameBuffer[0] = 0;
			FileUtils::AppendRegPathToBuffer();

			int32_t variant;
			int32_t variantStep;
			const char* ngnBaseName;
			int32_t loadConfigCpy2 = g_levelLoadConfig;

			Logger::Log("Level load config -> %d\n", g_levelLoadConfig);

			if ((g_levelLoadConfig & 4) == 0 || (g_levelLoadConfig & 1) == 0)
			{
				g_levelDataBase = dataBuffer;

				if (((g_levelLoadConfig >> 8) & 3) == 1)
				{
					if (levelIdCpy >= 10)
					{
						strcpy(fileNameBuffer, "level");
						fileNameBuffer[5] = levelIdCpy / 10 + 48;
						fileNameBuffer[6] = levelIdCpy % 10 + 48;
						loadConfigCpy2 = g_levelLoadConfig;
					}
					else
					{
						strcpy(fileNameBuffer, "level0");
						fileNameBuffer[6] = levelIdCpy + 48;
					}

					strcpy(&fileNameBuffer[7], "\\");
					ngnBaseName = "level1";
				}
				else
				{
					if (((g_levelLoadConfig >> 8) & 3) != 2)
					{
						if (((g_levelLoadConfig >> 8) & 3) == 3)
							BuildLevelPath(levelIdCpy, fileNameBuffer, "level3");
						else
							BuildLevelPath(levelIdCpy, fileNameBuffer, "level");

						loadConfigCpy2 = g_levelLoadConfig;
						ngnBaseName = 0;
					}
					else
					{
						if (levelIdCpy >= 10)
						{
							strcpy(fileNameBuffer, "level");
							fileNameBuffer[5] = levelIdCpy / 10 + 48;
							fileNameBuffer[6] = levelIdCpy % 10 + 48;
							loadConfigCpy2 = g_levelLoadConfig;
						}
						else
						{
							strcpy(fileNameBuffer, "level0");
							fileNameBuffer[6] = levelIdCpy + 48;
						}

						strcpy(&fileNameBuffer[7], "\\");
						ngnBaseName = "level2";
					}
				}

				if (ngnBaseName)
					strcat(fileNameBuffer, ngnBaseName);

				if ((loadConfigCpy2 & 4) != 0 || levelIdCpy == 0 || levelId == 16)
				{
					const char* textureSuffix = 0;
					variant = ((loadConfigCpy2 >> 6) & 3) - 1;

					if (variant)
					{
						variantStep = variant - 1;
						if (variantStep)
						{
							if (variantStep == 1)
								textureSuffix = "t3";
						}
						else
						{
							textureSuffix = "t2";
						}
					}
					else
					{
						textureSuffix = "t1";
					}

					if (textureSuffix)
						strcat(fileNameBuffer, textureSuffix);
				}

				strcat(fileNameBuffer, ".ngn");
				strcat(FileUtils::g_fileNameBuffer, fileNameBuffer);
				NGNLoader::SetNewImage(FileUtils::g_fileNameBuffer);
				NGNLoader::DetectBackdropTextures();

				loadConfigCpy2 = g_levelLoadConfig;
			}

			if ((loadConfigCpy2 & 8) == 0)
			{
				if (! g_cachedAllBuffer)
				{
					Collision::BuildCollisionWorld(levelIdCpy, &dataBuffer, (loadConfigCpy2 >> 8) & 3);
					loadConfigCpy2 = (loadConfigCpy2 & ~0xFF) | (g_levelLoadConfig & 0xFF);
				}

				if ((loadConfigCpy2 & 8) == 0)
				{
					Collectables::BuildPickupTable();
					loadConfigCpy2 = (loadConfigCpy2 & ~0xFF) | (g_levelLoadConfig & 0xFF);
				}
			}

			if ((loadConfigCpy2 & 16) == 0)
			{
				uint8_t creatureIdList[128];
				Actor::GetCreatureList(creatureIdList);
				CharacterLoader::Start(&loadedCharacterBoneCount, &dataBuffer, creatureIdList);
			}

			g_levelLoadArena = dataBuffer;
			g_levelLoadConfig = 0;

			Logger::Log("InitLevelPlay : END.\n");
		}
	}
}
