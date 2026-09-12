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
#include "NGNLoader/NGNLoaderInternal.h"

// The NGN image and its geometry parse: retail holds 0x004C3240 through
// 0x004C3EB0 as one object, in the order of this file.
namespace NGNLoader
{
	int32_t ParseGeometry(FILE* stream, NGNImage* ngnImage);
	int32_t ParseGscale(FILE* stream, NGNImage* ngnImage);
	void ParseAreaPortalPos(FILE* stream, NGNImage* ngnImage);
	void ParseAreaPortalIdx(FILE* stream, NGNImage* ngnImage);
	void Parse266(FILE* stream, NGNImage* ngnImage);
	void ParseLinker(FILE* stream, NGNImage* ngnImage);

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
}
