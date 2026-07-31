#include "NGNLoader/NGNLoader.h"
#include "Nu3D/CreatureFlags.h"
#include "Nu3D/Portal.h"
#include "Toy2/Toy2.h"
#include "Nu3D/BmpDataNode.h"
#include "Logger.h"
#include "Renderer/Glue.h"
#include "Renderer/Renderer.h"
#include "Nu3D/Math.h"
#include "Nu3D/ObjLoad.h"

#include <windows.h>

namespace NGNLoader
{
	// GLOBAL: TOY2 0x00B62410
	NGNImage* g_ngnImage;

	// FUNCTION: TOY2 0x004B33A0 [MATCHED]
	void NGNImage::DestroyPortal(Nu3D::Portal::AreaPortal* portal) { free(portal); }

	// FUNCTION: TOY2 0x004C32B0 [MATCHED]
	void NGNImage::Destroy(NGNImage* ngnImage)
	{
		if (ngnImage)
		{
			int32_t i;

			for (i = 0; i < 64; i++)
			{
				if (ngnImage->textureEntries[i].unused1)
					free(ngnImage->textureEntries[i].unused1);
			}

			if (ngnImage->creatureData)
			{
				for (i = 0; i < ngnImage->creatureCount; i++)
				{
					if (ngnImage->creatureData[i])
						Nu3D::Creature::Destroy(ngnImage->creatureData[i]);
				}
				free(ngnImage->creatureData);
			}

			if (ngnImage->primitives)
			{
				for (i = 0; i < ngnImage->primCount; i++)
					Nu3D::Primitive::Destroy(ngnImage->primitives[i]);
				free(ngnImage->primitives);
			}

			for (i = 0; i < ngnImage->gscaleType; i++)
			{
				if (ngnImage->dynamicScalers[i])
					free(ngnImage->dynamicScalers[i]);
				if (ngnImage->spacialGrid[i])
					free(ngnImage->spacialGrid[i]);
			}

			if (ngnImage->areaPortals)
			{
				for (i = 0; i < ngnImage->actualPortalCount; i++)
					DestroyPortal(ngnImage->areaPortals[i]);
				free(ngnImage->areaPortals);
			}

			if (ngnImage->links)
				free(ngnImage->links);

			if (ngnImage->portalHashTable)
				DestroyPools(ngnImage);

			free(ngnImage);
		}
	}

	// GLOBAL: TOY2 0x009F6240
	NGNTextureData g_textureDataFreeList[2000];

	// GLOBAL: TOY2 0x00A03D00
	NGNTextureDataSentinal g_textureData;

	// GLOBAL: TOY2 0x00A03D08
	NGNTextureCache g_textureCacheFreeList[1000];

	// GLOBAL: TOY2 0x00A4C148
	NGNTextureCacheSentinal g_textureCache;

	// GLOBAL: TOY2 0x00AAD7AC
	char* g_curFileName;

	// GLOBAL: TOY2 0x00AAD7A8;
	int32_t g_curPrimCount;

	// GLOBAL: TOY2 0x00508A58
	Vector3F g_vertexScaleVector = { 1.0, 1.0, 1.0 };

	// GLOBAL: TOY2 0x009F5FE4
	Nu3D::Material* g_tex14Materials[3];

	// FUNCTION: TOY2 0x004B1190 [MATCHED]
	void FreeAllBmpDataNodes() { Nu3D::FreeAllBmpDataNodes_T(); }

	// FUNCTION: TOY2 0x004CB300 [MATCHED]
	void GetScaleVector(Vector3F* output) { *output = g_vertexScaleVector; }

	// FUNCTION: TOY2 0x004BB320 [MATCHED]
	NGNTextureData* GetTextureData(NGNTextureParams* texParams, int32_t ignoreParams)
	{
		NGNTextureParams localTexParams;
		NGNTextureCache* cacheHead = g_textureCache.activeList;

		memcpy(&localTexParams, texParams, sizeof(localTexParams));
		localTexParams.rawTexStr = 0;
		uint32_t textureParamsSize = sizeof(NGNTextureParams);

		while (cacheHead)
		{
			int32_t comparison = 0;
			if (! ignoreParams)
				comparison = memcmp(&cacheHead->params, &localTexParams, textureParamsSize);

			if (comparison == 0 && strcmpi(cacheHead->texName, texParams->rawTexStr) == 0)
			{
				NGNTextureData* result = g_textureData.activeList;

				while (result)
				{
					if (result->textureCacheIndex == cacheHead->textureIndex)
						return result;

					result = result->next;
				}

				return 0;
			}

			cacheHead = cacheHead->next;
		}

		return 0;
	}

	// FUNCTION: TOY2 0x004BB4C0 [MATCHED]
	NGNTextureCache* AllocateTextureCache()
	{
		NGNTextureCache* result = g_textureCache.freeList;

		if (g_textureCache.freeList)
		{
			g_textureCache.freeList = g_textureCache.freeList->next;
			result->next = g_textureCache.activeList;

			if (g_textureCache.activeList)
				g_textureCache.activeList->prev = result;

			result->prev = 0;
			g_textureCache.activeList = result;
		}

		return result;
	}

	// FUNCTION: TOY2 0x004BB1B0 [MATCHED]
	NGNTextureData* AllocateTextureData()
	{
		NGNTextureData* result = g_textureData.freeList;

		if (g_textureData.freeList)
		{
			g_textureData.freeList = g_textureData.freeList->next;
			result->next = g_textureData.activeList;

			if (g_textureData.activeList)
				g_textureData.activeList->prev = result;

			result->prev = 0;
			g_textureData.activeList = result;
		}

		return result;
	}

	// FUNCTION: TOY2 0x004BB150 [MATCHED]
	void ReleaseTextureData(NGNTextureData* textureData)
	{
		if (textureData->bmpDataNode)
			Nu3D::ReleaseBmpDataNode_T(textureData->bmpDataNode);

		textureData->bmpDataNode = 0;

		if (textureData->next)
			textureData->next->prev = textureData->prev;

		if (textureData->prev)
		{
			textureData->prev->next = textureData->next;
		}
		else
		{
			g_textureData.activeList = textureData->next;
		}

		textureData->next = g_textureData.freeList;
		g_textureData.freeList = textureData;
	}

	// FUNCTION: TOY2 0x004BB2F0 [MATCHED]
	uint32_t GetTextureDataIndexByName(char* textureName)
	{
		NGNTextureParams texParams;
		texParams.rawTexStr = textureName;

		NGNTextureData* textureData = GetTextureData(&texParams, 1);
		return textureData ? textureData->textureIndex : 0;
	}

	// FUNCTION: TOY2 0x004BB810 [MATCHED]
	void ReleaseTextureCache(NGNTextureCache* textureCache)
	{
		if (textureCache->next)
			textureCache->next->prev = textureCache->prev;

		if (textureCache->prev)
		{
			textureCache->prev->next = textureCache->next;
		}
		else
		{
			g_textureCache.activeList = textureCache->next;
		}

		textureCache->next = g_textureCache.freeList;
		g_textureCache.freeList = textureCache;
	}

	// FUNCTION: TOY2 0x004BB7D0 [MATCHED]
	void ReleaseAllTextures()
	{
		while (g_textureData.activeList)
			ReleaseTextureData(g_textureData.activeList);

		while (g_textureCache.activeList)
			ReleaseTextureCache(g_textureCache.activeList);

		Nu3D::FreeAllBmpDataNodes_T();
	}

	// FUNCTION: TOY2 0x004AC240 [MATCHED]
	Nu3D::BmpDataNode* LoadLocalBmpTexture(const char* rawTexStr, int32_t flags) { return Nu3D::LoadLocalBmpTexture(rawTexStr, flags); }

	// FUNCTION: TOY2 0x004BB3C0 [MATCHED]
	uint32_t GetOrAllocateTexture(NGNTextureParams* texParams)
	{
		char rawTexStrBuffer[256];
		NGNTextureCache* textureCache;

		NGNTextureData* textureData = GetTextureData(texParams, 0);

		if (textureData)
			goto texture_ready;

		textureCache = AllocateTextureCache();

		if (textureCache)
		{
			memcpy(&textureCache->params, texParams, sizeof(textureCache->params));

			textureCache->params.rawTexStr = 0;

			strcpy(textureCache->texName, texParams->rawTexStr);

			textureData = AllocateTextureData();

			if (textureData)
			{
				textureData->textureFlags = texParams->textureFlags;
				textureData->color.b = texParams->color.b;
				textureData->color.g = texParams->color.g;
				textureData->color.r = texParams->color.r;

				strcpy(rawTexStrBuffer, texParams->rawTexStr);

				int32_t flags;

				if ((texParams->textureFlags & TEXTURE_FLAG_ALPHA_BITMAP) != 0)
					flags = Nu3D::BMP_TEXTURE_ALPHA_BITMAP;
				else
					flags = (texParams->textureFlags & TEXTURE_FLAG_COLOR_KEY) != 0 ? Nu3D::BMP_TEXTURE_TRANSPARENT_GREEN : 0;

				textureData->bmpDataNode = LoadLocalBmpTexture(rawTexStrBuffer, flags);
				textureData->textureCacheIndex = textureCache->textureIndex;

				goto texture_ready;
			}
		}

		return 0;

	texture_ready:
		return textureData->textureIndex;
	}

	// FUNCTION: TOY2 0x004AC220 [MATCHED]
	Nu3D::BmpDataNode* LoadTextureContents(FILE* stream, const char* rawTexStr, int32_t flags) { return Nu3D::LoadTextureByStream(stream, rawTexStr, flags); }

	// FUNCTION: TOY2 0x004BC320 [MATCHED]
	Nu3D::Portal::PortalState* AllocAreaPortal(NGNImage* ngnImage)
	{
		if (ngnImage->portalEntryCount < ngnImage->areaPortalCount && ngnImage->portalStatePool)
			return &ngnImage->portalStatePool[ngnImage->portalEntryCount++];

		return 0;
	}

	// FUNCTION: TOY2 0x004BC1B0 [MATCHED]
	void DestroyPools(NGNImage* ngnImage)
	{
		if (ngnImage->scalerEntryPool)
			free(ngnImage->scalerEntryPool);
		if (ngnImage->portalStatePool)
			free(ngnImage->portalStatePool);
		if (ngnImage->portalHashTable)
			free(ngnImage->portalHashTable);

		ngnImage->scalerEntryPool = 0;
		ngnImage->portalStatePool = 0;
		ngnImage->portalHashTable = 0;
		ngnImage->scalerEntryCount = 0;
		ngnImage->maxScalerEntries = 0;
		ngnImage->portalEntryCount = 0;
		ngnImage->areaPortalCount = 0;
		ngnImage->bucketCount = 0;
	}

	// FUNCTION: TOY2 0x004BC2C0 [MATCHED]
	int32_t InsertPortal(NGNImage* ngnImage, int32_t sourceAreaIdx, int32_t targetAreaIdx, Nu3D::Portal::AreaPortal* portal)
	{
		if (Toy2::g_isElevatorHopLevel && targetAreaIdx == 15)
			targetAreaIdx = -1;

		Nu3D::Portal::PortalState* head = AllocAreaPortal(ngnImage);

		if (head)
		{
			head->targetAreaIdx = targetAreaIdx;
			head->portal = portal;
			head->sourceAreaIdx = sourceAreaIdx;

			head->next = ngnImage->portalHashTable->buckets[sourceAreaIdx].portalStateHead;
			ngnImage->portalHashTable->buckets[sourceAreaIdx].portalStateHead = head;

			return 1;
		}

		return 0;
	}

	// FUNCTION: TOY2 0x004BC230 [MATCHED]
	void AllocPools(NGNImage* ngnImage, int32_t portalCount, int32_t maxScalerEntries)
	{
		ngnImage->portalEntryCount = 0;
		ngnImage->scalerEntryCount = 0;

		ngnImage->scalerEntryPool = (Nu3D::Portal::ScalerEntry*)malloc(sizeof(Nu3D::Portal::ScalerEntry) * maxScalerEntries);
		ngnImage->maxScalerEntries = maxScalerEntries;

		ngnImage->scalerEntryCount = 0;
		ngnImage->portalStatePool = (Nu3D::Portal::PortalState*)malloc(sizeof(Nu3D::Portal::PortalState) * portalCount);
		ngnImage->portalEntryCount = 0;
		ngnImage->areaPortalCount = portalCount;

		Nu3D::Portal::PortalHashTable* rotLookup = (Nu3D::Portal::PortalHashTable*)malloc(sizeof(Nu3D::Portal::PortalHashTable));

		ngnImage->portalHashTable = rotLookup;
		ngnImage->bucketCount = 64;

		memset(rotLookup, 0, sizeof(Nu3D::Portal::PortalHashTable));
	}

	// FUNCTION: TOY2 0x004B3350 [MATCHED]
	Nu3D::Portal::AreaPortal* AllocPortalVertices(int32_t vertexCount)
	{
		Nu3D::Portal::AreaPortal* portal = (Nu3D::Portal::AreaPortal*)malloc(sizeof(Vector3F) * vertexCount + sizeof(Nu3D::Portal::AreaPortal));

		if (portal)
		{
			memset(portal, 0, sizeof(Vector3F) * vertexCount + sizeof(Nu3D::Portal::AreaPortal));

			if (vertexCount)
			{
				// Points to space after struct, is the vertex space.
				portal->vertexCount = vertexCount;
				portal->vertices = reinterpret_cast<Vector3F*>(&portal[1]);
			}
		}

		return portal;
	}

	// FUNCTION: TOY2 0x004C3EB0 [PROVISIONAL]
	void ParseLinker(FILE* stream, NGNImage* ngnImage)
	{
		int32_t maxLinkId;
		int32_t linkCount;

		fread(&linkCount, sizeof(int32_t), 1, stream);
		fread(&maxLinkId, sizeof(int32_t), 1, stream);

		++maxLinkId;

		Nu3D::Link::Linker* linkerArray = (Nu3D::Link::Linker*)malloc(sizeof(Nu3D::Link::Linker) * maxLinkId);

		ngnImage->links = linkerArray;

		if (! linkerArray)
			return;

		int32_t count = 0;

		memset(linkerArray, 0, sizeof(Nu3D::Link::Linker) * maxLinkId);

		ngnImage->maxLinkId = maxLinkId;

		if (linkCount > 0)
		{
			do
			{
				uint16_t linkId;
				uint16_t shapeId;

				fread(&linkId, sizeof(uint16_t), 1, stream);
				fread(&shapeId, sizeof(uint16_t), 1, stream);

				if (linkId >= 0 && linkId < maxLinkId)
				{
					int32_t type;

					if (shapeId & 0x8000 == 0)
					{
						type = 0;
					}
					else
					{
						shapeId = -shapeId & 0x3FFF;
						type = 1;
					}

					if (shapeId < ngnImage->shapeCounts[type])
					{
						Nu3D::Link::Linker* link = &ngnImage->links[linkId];
						Nu3D::Link::DynamicScaler* dynamicScaler = &ngnImage->dynamicScalers[type][shapeId];

						link->dynamicScaler = dynamicScaler;

						memcpy(&link->transformMatrix, &dynamicScaler->transformMatrix, sizeof(link->transformMatrix));

						link->dynamicScaler->flags |= 2;

						link->currentPos = link->dynamicScaler->translation;
						link->targetPos = link->dynamicScaler->translation;
						link->currentRot = link->dynamicScaler->rotation;
						link->currentScale = link->dynamicScaler->scale;
					}
				}

				count += 1;

			} while (count < linkCount);
		}
	}

	// FUNCTION: TOY2 0x004C3CA0 [PROVISIONAL]
	void Parse266(FILE* stream, NGNImage* ngnImage)
	{
		// data266 isn't actually used anywhere in the game
		Vector3F scale;
		GetScaleVector(&scale);

		memset(ngnImage->data266, 0, sizeof(ngnImage->data266));

		int32_t entryCount = 0;
		fread(&entryCount, sizeof(entryCount), 1, stream);

		for (int32_t i = 0; i < entryCount; ++i)
		{
			int16_t count = 0;
			int16_t index = 0;

			fread(&count, sizeof(count), 1, stream);
			fread(&index, sizeof(index), 1, stream);

			Type266Entry* entry = (Type266Entry*)malloc(sizeof(Type266Entry));

			entry->count = 0;
			entry->index = 0;
			entry->points = 0;

			entry->count = count;
			entry->index = index;
			entry->points = (Vector3F*)malloc(sizeof(Vector3F) * count);

			const bool emptyCount = entry->count == 0;
			const bool negativeCount = entry->count < 0;

			if (! emptyCount && ! negativeCount)
			{
				for (int32_t j = 0; j < count; ++j)
				{
					fread(&entry->points[j].x, sizeof(float), 1, stream);
					fread(&entry->points[j].y, sizeof(float), 1, stream);
					fread(&entry->points[j].z, sizeof(float), 1, stream);

					entry->points[j].x *= scale.x;
					entry->points[j].y *= scale.y;
					entry->points[j].z *= scale.z;
				}
			}

			ngnImage->data266[index] = entry;
		}
	}

	// FUNCTION: TOY2 0x004C3DF0 [PROVISIONAL]
	void ParseAreaPortalIdx(FILE* stream, NGNImage* ngnImage)
	{
		int32_t portalId, targetAreaIdx, sourceAreaIdx, portalCount;

		fread(&portalCount, sizeof(int32_t), 1, stream);

		if (! portalCount)
			return;

		AllocPools(ngnImage, portalCount, 4000);

		for (int32_t index = 0; index < portalCount; ++index)
		{
			fread(&portalId, sizeof(int32_t), 1, stream);
			fread(&sourceAreaIdx, sizeof(int32_t), 1, stream);
			fread(&targetAreaIdx, sizeof(int32_t), 1, stream);

			Nu3D::Portal::AreaPortal::CalculateBoundingSphere(ngnImage->areaPortals[portalId]);
			InsertPortal(ngnImage, sourceAreaIdx, targetAreaIdx, ngnImage->areaPortals[portalId]);
		}
	}

	// FUNCTION: TOY2 0x004C3BE0 [PROVISIONAL]
	void ParseAreaPortalPos(FILE* stream, NGNImage* ngnImage)
	{
		int32_t portalCount;
		fread(&portalCount, sizeof(int32_t), 1, stream);

		if (! portalCount)
			return;

		Nu3D::Portal::AreaPortal** portalAlloc = (Nu3D::Portal::AreaPortal**)malloc(sizeof(Nu3D::Portal::AreaPortal*) * portalCount);
		ngnImage->areaPortals = portalAlloc;

		if (! portalAlloc)
			return;

		for (int32_t portalId = 0; portalId < portalCount; ++portalId)
		{
			int32_t vertexCount;
			fread(&vertexCount, sizeof(int32_t), 1, stream);

			ngnImage->areaPortals[portalId] = AllocPortalVertices(vertexCount);

			if (! ngnImage->areaPortals[portalId])
			{
				ngnImage->actualPortalCount = portalId;
				return;
			}

			ngnImage->areaPortals[portalId]->portalId = portalId;

			fread(ngnImage->areaPortals[portalId]->vertices, sizeof(Vector3F), vertexCount, stream);
		}
	}

	// FUNCTION: TOY2 0x004C3740 [PROVISIONAL]
	int32_t ParseGscale(FILE* stream, NGNImage* ngnImage)
	{
		Vector3F scaleVector;
		int32_t shapeDataLength;
		int32_t shapeCount;
		int32_t flipY;

		GetScaleVector(&scaleVector);

		int32_t flipX = scaleVector.x < 0.0f;
		int32_t curShape = 0;
		flipY = scaleVector.y < 0.0f;
		int32_t flipZ = scaleVector.z < 0.0f;

		fread(&shapeCount, sizeof(int32_t), 1, stream);
		fread(&shapeDataLength, sizeof(int32_t), 1, stream);

		shapeDataLength -= 40;
		int32_t hasExtraData = 0;

		if (shapeDataLength != 0)
		{
			hasExtraData = 1;
			shapeDataLength -= 4;
		}

		if (shapeCount)
		{
			ngnImage->dynamicScalers[ngnImage->gscaleType] = (Nu3D::Link::DynamicScaler*)malloc(sizeof(Nu3D::Link::DynamicScaler) * shapeCount);

			if (ngnImage->dynamicScalers[ngnImage->gscaleType] != 0)
			{
				ngnImage->shapeCounts[ngnImage->gscaleType] = shapeCount;

				if (shapeCount > curShape)
				{
					int32_t flipXOrig = flipX;
					int32_t flipYZ = flipZ ^ flipY;

					flipX = flipZ ^ flipX;
					flipY = flipY ^ flipXOrig;

					do
					{
						// yes, the original ASM does every single dynamicScalers load individually, which is odd because you would think
						// the compiler would just optimize it down to one load and then pointer re-use, but I guess not

						ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].flags = 0;

						fread(&ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].translation, sizeof(Vector3F), 1, stream);
						fread(&ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].rotation, sizeof(Vector3F), 1, stream);
						fread(&ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].scale, sizeof(Vector3F), 1, stream);
						fread(&ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].shapeId, sizeof(int32_t), 1, stream);

						ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].shapeId += g_curPrimCount;
						ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].gscaleType = ngnImage->gscaleType;

						if (hasExtraData)
						{
							fread(&ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].packedAreaData, sizeof(int32_t), 1, stream);

							ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].packedFlags =
								(ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].packedAreaData >> 16) & 0xF;

							ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].areaIndex =
								ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].packedAreaData & 0xFF;
						}
						else
						{
							ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].packedAreaData = 0;
							ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].areaIndex = 0;
							ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].packedFlags = 0;
						}

						ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].translation.x =
							scaleVector.x * ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].translation.x;

						ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].translation.y =
							scaleVector.y * ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].translation.y;

						ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].translation.z =
							scaleVector.z * ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].translation.z;

						if (flipYZ)
							ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].rotation.x =
								-ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].rotation.x;

						if (flipX)
							ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].rotation.y =
								-ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].rotation.y;

						if (flipY)
							ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].rotation.z =
								-ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].rotation.z;

						Nu3D::Math::BuildIdentityMatrix(&ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].transformMatrix);

						// clang-format off
					Nu3D::Math::ScaleMatrixByVector(
						&ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].transformMatrix, 
						&ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].scale
					);

					Nu3D::Math::RotateZFromLut(
						&ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].transformMatrix,
						ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].rotation.z
					);

					Nu3D::Math::RotateYFromLut(
						&ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].transformMatrix,
						ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].rotation.y
					);

					Nu3D::Math::PostRotateXFromLut(
						&ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].transformMatrix,
						ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].rotation.x
					);

					Nu3D::Math::AddWorldSpaceTransform(
						&ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].transformMatrix,
						&ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].translation
					);

					ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].next = 0;

					Nu3D::Math::TransformVectorByMatrix(
						&ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].boundsCenterWorld,
						&ngnImage->primitives[ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].shapeId]->boundsCenter,
						&ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].transformMatrix
					);
						// clang-format on

						if (curShape || ngnImage->gscaleType)
						{
							ngnImage->worldMinX = ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].translation.x > ngnImage->worldMinX
								? ngnImage->worldMinX
								: ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].translation.x;

							ngnImage->worldMaxX = ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].translation.x < ngnImage->worldMaxX
								? ngnImage->worldMaxX
								: ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].translation.x;

							ngnImage->worldMinZ = ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].translation.z > ngnImage->worldMinZ
								? ngnImage->worldMinZ
								: ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].translation.z;

							ngnImage->worldMaxZ = ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].translation.z < ngnImage->worldMaxZ
								? ngnImage->worldMaxZ
								: ngnImage->dynamicScalers[ngnImage->gscaleType][curShape].translation.z;
						}
						else
						{
							ngnImage->worldMaxX = ngnImage->dynamicScalers[0]->translation.x;
							ngnImage->worldMinX = ngnImage->dynamicScalers[0]->translation.x;
							ngnImage->worldMaxZ = ngnImage->dynamicScalers[0]->translation.z;
							ngnImage->worldMinZ = ngnImage->dynamicScalers[0]->translation.z;
						}

						if (shapeDataLength)
							fseek(stream, shapeDataLength, 1);

						++curShape;

					} while (curShape < shapeCount);
				}

				ngnImage->worldMaxX = ngnImage->worldMaxX + 10.0f;
				ngnImage->worldMaxZ = ngnImage->worldMaxZ + 10.0f;
				ngnImage->worldMinX = ngnImage->worldMinX - 10.0f;
				ngnImage->worldMinZ = ngnImage->worldMinZ - 10.0f;

				return shapeCount;
			}
		}

		return 0;
	}

	// FUNCTION: TOY2 0x004C35C0 [MATCHED]
	int32_t ParseGeometry(FILE* stream, NGNImage* ngnImage)
	{
		int32_t shapeCount;
		fread(&shapeCount, sizeof(int32_t), 1, stream);

		if (shapeCount)
		{
			g_curPrimCount = ngnImage->primCount;

			if (ngnImage->primCount)
			{
				shapeCount += ngnImage->primCount;

				Nu3D::Primitive** primList = (Nu3D::Primitive**)malloc(sizeof(Nu3D::Primitive*) * shapeCount);

				if (primList)
				{
					memcpy(primList, ngnImage->primitives, sizeof(Nu3D::Primitive*) * ngnImage->primCount);
					free(ngnImage->primitives);

					ngnImage->primitives = primList;
				}
			}
			else
			{
				ngnImage->primitives = (Nu3D::Primitive**)malloc(sizeof(Nu3D::Primitive*) * shapeCount);
			}

			if (ngnImage->primitives)
			{
				ngnImage->primCount = shapeCount;

				int32_t index = g_curPrimCount;

				if (g_curPrimCount < shapeCount)
				{
					do
					{
						ngnImage->primitives[index] = ObjectLoad::ExtractShapeData(stream);
						Nu3D::Primitive::CreateAllVertexBuffers(ngnImage->primitives[index], 3);

						++index;

					} while (index < shapeCount);
				}

				return shapeCount;
			}
		}

		return 0;
	}

	// FUNCTION: TOY2 0x004B9630 [MATCHED]
	void BuildTex14(int32_t unused)
	{
		RGBColor color;

		g_tex14Materials[0] = Nu3D::Material::CreateFromColor(&color);
		if (g_tex14Materials[0])
		{
			Nu3D::Material::SetOpacity(g_tex14Materials[0], 0.5f);
			Nu3D::Material::AttachTexture(g_tex14Materials[0], GetTextureDataIndexByName("tex14"));
			g_tex14Materials[0]->metadata |= 2;
		}

		g_tex14Materials[1] = Nu3D::Material::CreateFromColor(&color);
		if (g_tex14Materials[1])
		{
			Nu3D::Material::SetOpacity(g_tex14Materials[1], 0.5f);
			Nu3D::Material::AttachTexture(g_tex14Materials[1], GetTextureDataIndexByName("tex14"));
			g_tex14Materials[1]->metadata |= 0x10;
		}

		g_tex14Materials[2] = Nu3D::Material::CreateFromColor(&color);
		if (g_tex14Materials[2])
		{
			Nu3D::Material::SetOpacity(g_tex14Materials[2], 0.5f);
			Nu3D::Material::AttachTexture(g_tex14Materials[2], GetTextureDataIndexByName("tex14"));
			g_tex14Materials[2]->metadata |= 0x20;
		}
	}

	// FUNCTION: TOY2 0x004C36A0 [MATCHED]
	void BuildGrid(int32_t gridWidth, int32_t gridHeight, int32_t type, NGNImage* ngnImage)
	{
		int32_t gridByteSize = sizeof(Nu3D::Link::DynamicScaler*) * gridWidth * gridHeight;
		ngnImage->spacialGrid[type] = (Nu3D::Link::DynamicScaler**)malloc(gridByteSize);

		if (ngnImage->spacialGrid)
		{
			memset(ngnImage->spacialGrid[type], 0, gridByteSize);

			ngnImage->gridWidth = gridWidth;
			ngnImage->gridHeight = gridHeight;
			ngnImage->cellWidthInWorldUnits = (ngnImage->worldMaxX - ngnImage->worldMinX) / gridWidth;
			ngnImage->cellHeightInWorldUnits = (ngnImage->worldMaxZ - ngnImage->worldMinZ) / gridHeight;

			for (int32_t index = 0; index < ngnImage->shapeCounts[type]; ++index)
				Nu3D::Spatial::InsertScalerAtComputedCell(&ngnImage->dynamicScalers[type][index], type, ngnImage);
		}
	}

	// FUNCTION: TOY2 0x004C3240 [MATCHED]
	void BuildScalerEntries(NGNImage* ngnImage)
	{
		for (int32_t type = 0; ngnImage->dynamicScalers[type] && type < 2; ++type)
		{
			for (int32_t index = 0; index < ngnImage->shapeCounts[type]; ++index)
			{
				Nu3D::Portal::AreaPortal::BuildScalerEntry(ngnImage, ngnImage->dynamicScalers[type][index].areaIndex, &ngnImage->dynamicScalers[type][index]);
			}
		}
	}

	// FUNCTION: TOY2 0x004C33F0 [MATCHED]
	NGNImage* BuildImage(char* fileName)
	{
		int32_t terminate = 0;
		NGNImage* ngnImage = 0;

		g_curFileName = fileName;

		FILE* fileHandle = fopen(fileName, "rb");

		if (fileHandle)
		{
			ngnImage = (NGNImage*)malloc(sizeof(NGNImage));

			if (ngnImage)
			{
				memset(ngnImage, 0, sizeof(NGNImage));

				g_curPrimCount = 0;

				int32_t chunkSize;
				int32_t chunkHeaderId;

				if (fread(&chunkHeaderId, sizeof(int32_t), 1, fileHandle))
				{
					do
					{
						fread(&chunkSize, sizeof(int32_t), 1, fileHandle);
						ftell(fileHandle);

						DECOMP_PRINT(("[NGNLoader]: Loading chunk type %d\n", chunkHeaderId));

						switch (chunkHeaderId)
						{
							case 256:
								ParseGeometry(fileHandle, ngnImage);
								break;

							case 257:
								ParseGscale(fileHandle, ngnImage);
								++ngnImage->gscaleType;
								break;

							case 258:
								ParseAreaPortalPos(fileHandle, ngnImage);
								break;

							case 259:
								ParseAreaPortalIdx(fileHandle, ngnImage);
								break;

							case 266:
								Parse266(fileHandle, ngnImage);
								break;

							case 260:
								ParseTextures(fileHandle, ngnImage);
								break;

							case 261:
								ParseLinker(fileHandle, ngnImage);
								break;

							case 262:
								ParseCreatures(fileHandle, ngnImage);
								break;

							case 0:
								terminate = 1;
								break;

							default:
								fseek(fileHandle, chunkSize, 1);
								break;
						}

						ftell(fileHandle);

					} while (! terminate && fread(&chunkHeaderId, sizeof(int32_t), 1, fileHandle));
				}
			}

			fclose(fileHandle);
		}

		BuildTex14(14);

		if (ngnImage)
		{
			for (int32_t typeIndex = 0; typeIndex < ngnImage->gscaleType; ++typeIndex)
				BuildGrid(20, 20, typeIndex, ngnImage);

			BuildScalerEntries(ngnImage);
		}

		return ngnImage;
	}

	// FUNCTION: TOY2 0x004CEAE0 [MATCHED]
	void SetNewImage(char* fileName)
	{
		DECOMP_PRINT(("[NGNLoader]: Loading file %s\n", fileName));

		g_ngnImage = BuildImage(fileName);

		Nu3D::Portal::ClearVisibleAreaFlags();
	}

	// FUNCTION: TOY2 0x004BB720 [PROVISIONAL]
	void Init()
	{
		FreeAllBmpDataNodes();

		uint32_t textureDataCount = 1;
		NGNTextureData* dataFreeListPtr = g_textureDataFreeList;

		do
		{
			dataFreeListPtr->next = dataFreeListPtr + 1;
			++textureDataCount;
			dataFreeListPtr[1].prev = dataFreeListPtr;
			dataFreeListPtr[1].textureIndex = textureDataCount;
			++dataFreeListPtr;

		} while (reinterpret_cast<int32_t>(dataFreeListPtr) < reinterpret_cast<int32_t>(&g_textureDataFreeList[1999]));

		g_textureDataFreeList[0].textureIndex = 1;
		g_textureDataFreeList[0].prev = 0;
		g_textureDataFreeList[textureDataCount - 1].next = 0;

		g_textureData.freeList = g_textureDataFreeList;
		g_textureData.activeList = 0;

		uint32_t textureCacheCount = 1;
		NGNTextureCache* cacheFreeListPtr = g_textureCacheFreeList;

		do
		{
			cacheFreeListPtr->next = cacheFreeListPtr + 1;
			cacheFreeListPtr[1].prev = cacheFreeListPtr;
			cacheFreeListPtr[1].textureIndex = textureCacheCount;
			++cacheFreeListPtr;
			++textureCacheCount;

		} while (reinterpret_cast<int32_t>(cacheFreeListPtr) < reinterpret_cast<int32_t>(&g_textureCacheFreeList[999]));

		g_textureCacheFreeList[0].textureIndex = textureCacheCount;
		g_textureCacheFreeList[0].prev = 0;

		g_textureCache.freeList = g_textureCacheFreeList;
		g_textureCache.activeList = 0;

		g_textureCacheFreeList[textureCacheCount - 1].next = 0;
	}

	// FUNCTION: TOY2 0x0044FF50 [MATCHED]
	void DetectBackdropTextures()
	{
		if (GetTextureDataIndex(36))
		{
			Toy2::g_hasBackdrop = 2;
			Toy2::g_nextBackdropId = 36;
		}

		if (GetTextureDataIndex(37))
		{
			Toy2::g_hasStaticBackdrop = 1;
			Toy2::g_nextBackdropId = 37;
		}

		if (! Toy2::g_hasBackdrop)
		{
			int32_t idx;

			for (idx = 40; idx < 48; ++idx)
			{
				if (GetTextureDataIndex(idx))
				{
					Toy2::g_hasBackdrop = 1;
					Toy2::g_nextBackdropId = idx;
					break;
				}
			}

			if (! Toy2::g_hasBackdrop)
			{
				for (idx = 88; idx < 96; ++idx)
				{
					if (GetTextureDataIndex(idx))
					{
						Toy2::g_hasBackdrop = 2;
						Toy2::g_nextBackdropId = idx;
						break;
					}
				}
			}
		}

		if (Toy2::g_hasStaticBackdrop)
			Renderer::Glue::SetBackdrop(Toy2::g_nextBackdropId);
	}

	// FUNCTION: TOY2 0x004CE2C0 [MATCHED]
	int32_t GetTextureDataIndex(int32_t textureIndex)
	{
		if (g_ngnImage && textureIndex >= 0 && textureIndex < 64)
			return g_ngnImage->textureEntries[textureIndex].textureDataIndex;
		else
			return 0;
	}

	// FUNCTION: TOY2 0x004BB0E0 [TOOL]
	NGNTextureData* GetTextureDataByIndex(uint32_t texDataIndex) { return &g_textureDataFreeList[texDataIndex - 1]; }

	// FUNCTION: TOY2 0x004BB5E0 [TOOL]
	void RetrieveTextureData(
		int32_t texDataIndex, uint32_t* bitmapWidthOut, uint32_t* bitmapHeightOut, uint32_t* textureWidth, uint32_t* textureHeight, uint32_t** textureData)
	{
		if (texDataIndex)
		{
			Nu3D::BmpDataNode* bmpDataNode = g_textureDataFreeList[texDataIndex - 1].bmpDataNode;

			if (bmpDataNode)
			{
				if (bitmapWidthOut)
					*bitmapWidthOut = bmpDataNode->bitmapWidth;

				if (bitmapHeightOut)
					*bitmapHeightOut = bmpDataNode->bitmapHeight;

				if (textureWidth)
					*textureWidth = bmpDataNode->textureWidth;

				if (textureHeight)
					*textureHeight = bmpDataNode->textureHeight;

				if (textureData)
					*textureData = bmpDataNode->texData;
			}
		}
	}

	// FUNCTION: TOY2 0x004BB6C0 [TOOL]
	HBITMAP GetBmpHandle(int32_t index)
	{
		if (index)
			return g_textureDataFreeList[index - 1].bmpDataNode->bitmapHandle;
		else
			return 0;
	}

	// FUNCTION: TOY2 0x004BB690 [TOOL]
	int32_t CopyToDDSurfaceByIndex(int32_t texIndex, LPDIRECTDRAWSURFACE4 ddSurface)
	{
		if (texIndex)
			return Nu3D::CopyToDDSurface(g_textureDataFreeList[texIndex - 1].bmpDataNode, ddSurface);
		else
			return 0;
	}
}

namespace Nu3D
{
	// FUNCTION: TOY2 0x004CAB80 [PROVISIONAL]
	int32_t Creature::SetNodeVisible(Creature* creature, int32_t nodeIndex, int32_t visible)
	{
		int32_t wasVisible = -1;
		int32_t* nodeFlags = creature->flagsList;
		if (nodeIndex >= 0 && nodeIndex < creature->nodeCount && nodeFlags != 0)
		{
			int32_t flags = nodeFlags[nodeIndex];
			nodeFlags[nodeIndex] = flags & ~CREATURE_NODE_HIDDEN;
			wasVisible = ~flags & CREATURE_NODE_HIDDEN;
			if (visible == 0)
			{
				creature->flagsList[nodeIndex] |= CREATURE_NODE_HIDDEN;
			}
		}
		return wasVisible;
	}

	// FUNCTION: TOY2 0x004CE570 [MATCHED]
	void Creature::SetNodeVisibleByIndex(int32_t creatureIndex, int32_t nodeIndex, int32_t visible)
	{
		if (creatureIndex >= 0 && creatureIndex < NGNLoader::g_ngnImage->creatureCount)
		{
			Creature* creature = NGNLoader::g_ngnImage->creatureData[creatureIndex];
			if (creature != 0)
			{
				SetNodeVisible(creature, nodeIndex, visible);
			}
		}
	}

	// FUNCTION: TOY2 0x004C9F50 [MATCHED]
	void Creature::Destroy(Creature* creature)
	{
		if (creature->matrixList1)
			free(creature->matrixList1);
		if (creature->matrixList2)
			free(creature->matrixList2);
		if (creature->matrixList3)
			free(creature->matrixList3);

		int32_t i;

		if (creature->nodeNames)
		{
			for (i = 0; i < creature->nodeCount; i++)
				free(creature->nodeNames[i]);
			free(creature->nodeNames);
		}

		if (creature->primitives)
		{
			for (i = 0; i < creature->nodeCount; i++)
			{
				if (creature->primitives[i])
					Primitive::Destroy(creature->primitives[i]);
			}
		}

		if (creature->animCount)
		{
			for (i = 0; i < creature->animCount; i++)
				free(creature->animData[i]);
			free(creature->animData);
		}

		if (creature->patch)
			Patch::Destroy(creature->patch);

		if (creature->flagsList)
			free(creature->flagsList);

		free(creature);
	}

	// FUNCTION: TOY2 0x004CA0C0 [MATCHED]
	void CopyNormalsFromNearestVertex(Creature* creature, int32_t nodeIndex, Vertex* vertex)
	{
		float closestDistanceSquared = 3.402823466e+38F;
		Primitive* primitive = creature->primitives[nodeIndex];

		for (; primitive; primitive = primitive->next)
		{
			for (int32_t index = 0; index < primitive->patchVerts.vertexCount; ++index)
			{
				Vertex* vertices = primitive->patchVerts.data.vertices;
				Vertex* candidate = &vertices[index];
				float deltaZ = candidate->position.z - vertex->position.z;
				float deltaY = candidate->position.y - vertex->position.y;
				float deltaX = candidate->position.x - vertex->position.x;
				float distanceSquared = deltaZ * deltaZ + deltaY * deltaY + deltaX * deltaX;

				if (distanceSquared < closestDistanceSquared && distanceSquared < 1024.0f)
				{
					closestDistanceSquared = distanceSquared;
					vertex->normals = candidate->normals;
					if (distanceSquared < 1.0f)
						return;
				}
			}
		}

		if (closestDistanceSquared == 3.402823466e+38F)
			Logger::DebugLog("warning - unable to find similar vertex in limb\r\n");
	}
}
