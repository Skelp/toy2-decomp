#include "NGNLoader/NGNLoader.h"
#include "Nu3D/CreatureFlags.h"
#include "Nu3D/Portal.h"
#include "Toy2/Toy2.h"
#include "Nu3D/BmpDataNode.h"
#include "Logger.h"
#include "Renderer/Glue.h"
#include "Renderer/Renderer.h"
#include "Nu3D/Math.h"
#include "Nu3D/NuQuat.h"
#include "Nu3D/ObjLoad.h"

#include <windows.h>
#include <math.h>
#include <string.h>

namespace NGNLoader
{
	// FUNCTION: TOY2 0x004CA420 [PROVISIONAL]
	Nu3D::Creature* ExtractCreatureData(FILE* stream)
	{
		Nu3D::Creature* creature = 0;
		int32_t stop = 0;
		int32_t chunkId;

		while (! stop && fread(&chunkId, sizeof(chunkId), 1, stream))
		{
			int32_t chunkSize;
			fread(&chunkSize, sizeof(chunkSize), 1, stream);
			int32_t chunkEnd = ftell(stream) + chunkSize;

			switch (chunkId)
			{
				case 0:
					stop = 1;
					break;

				case 0x200:
					creature = (Nu3D::Creature*)malloc(sizeof(Nu3D::Creature));
					if (creature)
					{
						memset(creature, 0, sizeof(Nu3D::Creature));
						fread(&creature->nodeCount, sizeof(creature->nodeCount), 1, stream);
						creature->flagsList = (int32_t*)malloc(sizeof(int32_t) * creature->nodeCount);
						if (creature->flagsList)
							memset(creature->flagsList, 0, sizeof(int32_t) * creature->nodeCount);
					}
					break;

				case 0x201:
					if (creature)
					{
						creature->dataFlags |= Nu3D::CREATURE_DATA_MATRICES;
						creature->matrixList1 = (D3DMATRIX*)malloc(sizeof(D3DMATRIX) * creature->nodeCount);
						creature->matrixList2 = (D3DMATRIX*)malloc(sizeof(D3DMATRIX) * creature->nodeCount);
						creature->matrixList3 = (D3DMATRIX*)malloc(sizeof(D3DMATRIX) * creature->nodeCount);

						if (creature->matrixList1 && creature->matrixList2 && creature->matrixList3)
						{
							fread(creature->matrixList1, sizeof(D3DMATRIX), creature->nodeCount, stream);
							memcpy(creature->matrixList3, creature->matrixList1, sizeof(D3DMATRIX) * creature->nodeCount);
							for (int32_t index = 0; index < creature->nodeCount; ++index)
								Nu3D::Math::BuildIdentityMatrix(&creature->matrixList2[index]);
						}
					}
					break;

				case 0x202:
					if (creature)
					{
						creature->dataFlags |= Nu3D::CREATURE_DATA_NODE_NAMES;
						creature->nodeNames = (char**)malloc(sizeof(char*) * creature->nodeCount);
						if (creature->nodeNames)
						{
							for (int32_t index = 0; index < creature->nodeCount; ++index)
							{
								uint8_t nameLength;
								fread(&nameLength, sizeof(nameLength), 1, stream);
								creature->nodeNames[index] = (char*)malloc(nameLength + 1);
								if (creature->nodeNames[index])
								{
									fread(creature->nodeNames[index], sizeof(char), nameLength, stream);
									creature->nodeNames[index][nameLength] = '\0';
								}
							}
						}
					}
					break;

				case 0x203:
					if (creature)
					{
						creature->dataFlags |= Nu3D::CREATURE_DATA_PRIMITIVES;
						creature->primitives = (Nu3D::Primitive**)malloc(sizeof(Nu3D::Primitive*) * creature->nodeCount);
						creature->nodeMetadata = (int32_t*)malloc(sizeof(int32_t) * creature->nodeCount);

						if (creature->primitives)
						{
							for (int32_t index = 0; index < creature->nodeCount; ++index)
							{
								uint16_t nodeFlags;
								fread(&nodeFlags, sizeof(nodeFlags), 1, stream);

								creature->nodeMetadata[index] = 0;
								fread(&creature->nodeMetadata[index], sizeof(uint16_t), 1, stream);

								if (nodeFlags & Nu3D::CREATURE_NODE_DATA_HAS_PRIMITIVE)
								{
									if (nodeFlags & Nu3D::CREATURE_NODE_DATA_BILLBOARD)
										creature->flagsList[index] |= Nu3D::CREATURE_NODE_BILLBOARD;
									if (nodeFlags & Nu3D::CREATURE_NODE_DATA_VERTEX_LIGHTING)
										creature->flagsList[index] |= Nu3D::CREATURE_NODE_VERTEX_LIGHTING;

									creature->primitives[index] = ObjectLoad::ExtractShapeData(stream);
									Nu3D::Primitive::CreateAllVertexBuffers(creature->primitives[index], 3);
								}
								else
								{
									creature->primitives[index] = 0;
								}
							}
						}
					}
					break;

				case 0x204:
					ExtractAnimations(stream, creature, chunkSize);
					break;

				case 0x205:
					ObjectLoad::PrepareGlobals();
					ObjectLoad::ExtractShapeTextures(stream);
					break;

				case 0x206:
					ObjectLoad::ExtractShapeMaterials(stream);
					break;

				case 0x207:
					ObjectLoad::ExtractShapeVertices(stream);
					break;

				case 0x208:
					ExtractShapePatch(stream, creature);
					break;

				default:
					Logger::GetErrorHandler("C:\\projects\\nu3d\\hobjload.c", 354)("err");
					fseek(stream, chunkSize, SEEK_CUR);
					break;
			}

			if (ftell(stream) != chunkEnd)
				fseek(stream, chunkEnd, SEEK_SET);
		}

		return creature;
	}

	// FUNCTION: TOY2 0x004CA040 [MATCHED]
	int32_t ExtractAnimations(FILE* stream, Nu3D::Creature* creature, uint32_t dataSize)
	{
		if (! creature->animData)
		{
			creature->animData = (int16_t**)malloc(sizeof(int16_t*) * 100);
			creature->animCount = 0;
			if (! creature->animData)
				return -1;
		}

		if (creature->animCount < 100)
		{
			creature->animData[creature->animCount] = (int16_t*)malloc(dataSize);

			if (creature->animData[creature->animCount])
			{
				fread(creature->animData[creature->animCount], 1, dataSize, stream);
				return ++creature->animCount;
			}
		}

		return -1;
	}

	// FUNCTION: TOY2 0x004CA1D0 [MATCHED]
	int32_t ExtractShapePatch(FILE* stream, Nu3D::Creature* creature)
	{
		int16_t stripVertexCount;
		int16_t materialIndex;
		int16_t controlPointIndices[4];
		int16_t vertexIndices[4];
		int32_t patchCount;
		fread(&patchCount, sizeof(patchCount), 1, stream);

		int32_t patchIndex = 0;
		for (; patchIndex < patchCount; ++patchIndex)
		{
			fread(&stripVertexCount, sizeof(stripVertexCount), 1, stream);
			fread(&materialIndex, sizeof(materialIndex), 1, stream);
			fread(controlPointIndices, sizeof(int16_t), 4, stream);
			fread(vertexIndices, sizeof(int16_t), 4, stream);

			Nu3D::Patch* patch;
			if (stripVertexCount)
			{
				patch = Nu3D::Patch::AllocAndResize(stripVertexCount * 2, 2);
				if (! patch)
					goto unable_to_create_patch;

				for (int32_t index = 0; index < stripVertexCount; ++index)
				{
					Nu3D::CopyShapeVertex(vertexIndices[0] + index, &patch->patchVertices.data.vertices[index]);
					Nu3D::CopyNormalsFromNearestVertex(creature, controlPointIndices[0], &patch->patchVertices.data.vertices[index]);

					Nu3D::CopyShapeVertex(vertexIndices[1] + index, &patch->patchVertices.data.vertices[stripVertexCount + index]);
					Nu3D::CopyNormalsFromNearestVertex(creature, controlPointIndices[1], &patch->patchVertices.data.vertices[stripVertexCount + index]);
				}
			}
			else
			{
				patch = Nu3D::Patch::AllocAndResize(4, 4);
				if (patch)
				{
					Nu3D::CopyShapeVertex(vertexIndices[0], &patch->patchVertices.data.vertices[0]);
					Nu3D::CopyNormalsFromNearestVertex(creature, controlPointIndices[0], &patch->patchVertices.data.vertices[0]);
					Nu3D::CopyShapeVertex(vertexIndices[1], &patch->patchVertices.data.vertices[1]);
					Nu3D::CopyNormalsFromNearestVertex(creature, controlPointIndices[1], &patch->patchVertices.data.vertices[1]);
					Nu3D::CopyShapeVertex(vertexIndices[2], &patch->patchVertices.data.vertices[2]);
					Nu3D::CopyNormalsFromNearestVertex(creature, controlPointIndices[2], &patch->patchVertices.data.vertices[2]);
					Nu3D::CopyShapeVertex(vertexIndices[3], &patch->patchVertices.data.vertices[3]);
					Nu3D::CopyNormalsFromNearestVertex(creature, controlPointIndices[3], &patch->patchVertices.data.vertices[3]);
				}
				else
				{
				unable_to_create_patch:
					Logger::GetErrorHandler("C:\\projects\\nu3d\\hobjload.c", 220)("unable to create patch");
					continue;
				}
			}

			{
				for (int32_t index = 0; index < 4; ++index)
					patch->controlPointIndices[index] = controlPointIndices[index];

				patch->materialId = ObjectLoad::GetCurrentMatByIndex(materialIndex)->id;
				patch->listNext = creature->patch;
				creature->patch = patch;
				Nu3D::Patch::CreateAllVertexBuffers(patch);
				continue;
			}
		}

		return patchIndex;
	}

}

namespace Nu3D
{
	// FUNCTION: TOY2 0x004CA880 [MATCHED]
	int32_t FindCreatureNodeIndexByName(Creature* creature, const char* nodeName)
	{
		int32_t nodeIndex = creature->nodeCount;
		if (creature->nodeNames)
		{
			while (nodeIndex)
			{
				--nodeIndex;
				if (strcmpi(nodeName, creature->nodeNames[nodeIndex]) == 0)
					return nodeIndex;
			}
		}

		return -1;
	}

	union NamedTrackTransform
	{
		D3DMATRIX matrix;
		Quaternion rotation;
	};

	struct NamedTrackKey
	{
		NamedTrackKey* previous;
		NamedTrackKey* next;
		int32_t sampleIndex;
		int32_t trackIndex;
		NamedTrackTransform transform;
	};

	STATIC_ASSERT(sizeof(NamedTrackKey) == 0x50);

	struct NamedTrackSet
	{
		int32_t sampleCount;
		uint8_t formatData04[4];
		int32_t keyframeCount;
		int32_t trackCount;
		uint8_t formatData10[4];
		char** trackNames;
		NamedTrackKey** trackKeys;
	};

	STATIC_ASSERT(sizeof(NamedTrackSet) == 0x1C);

	static int32_t GetFileLength(const char* filename)
	{
		if (! filename || ! *filename)
			return -1;

		FILE* stream = fopen(filename, "rb");
		if (! stream)
			return -1;

		fseek(stream, 0, SEEK_END);
		int32_t length = ftell(stream);
		fclose(stream);
		return length;
	}

	// FUNCTION: TOY2 0x004CABD0 [PROVISIONAL]
	NamedTrackSet* LoadNamedTrackSet(const char* filename)
	{
		if (GetFileLength(filename) <= 0)
			return 0;

		FILE* stream = fopen(filename, "rb");
		if (! stream)
			return 0;

		NamedTrackSet* trackSet = (NamedTrackSet*)malloc(sizeof(NamedTrackSet));
		if (trackSet)
		{
			uint16_t fileHeader[4];
			fread(fileHeader, sizeof(uint16_t), 4, stream);
			fread(&trackSet->sampleCount, sizeof(trackSet->sampleCount), 1, stream);
			fread(trackSet->formatData04, sizeof(trackSet->formatData04), 1, stream);
			fread(trackSet->formatData10, sizeof(trackSet->formatData10), 1, stream);
			fread(&trackSet->trackCount, sizeof(trackSet->trackCount), 1, stream);

			trackSet->trackNames = (char**)malloc(sizeof(char*) * trackSet->trackCount);
			for (int32_t trackIndex = 0; trackIndex < trackSet->trackCount; ++trackIndex)
			{
				uint8_t nameLength;
				fread(&nameLength, sizeof(nameLength), 1, stream);
				trackSet->trackNames[trackIndex] = (char*)malloc(nameLength + 1);
				if (nameLength)
					fread(trackSet->trackNames[trackIndex], sizeof(char), nameLength, stream);
				trackSet->trackNames[trackIndex][nameLength] = '\0';
			}

			trackSet->trackKeys = (NamedTrackKey**)malloc(sizeof(NamedTrackKey*) * trackSet->trackCount);
			int32_t keyframeCount = 0;
			if (trackSet->trackKeys)
			{
				memset(trackSet->trackKeys, 0, sizeof(NamedTrackKey*) * trackSet->trackCount);

				int32_t trackIndex;
				while (fread(&trackIndex, sizeof(trackIndex), 1, stream))
				{
					int32_t keyCount;
					fread(&keyCount, sizeof(keyCount), 1, stream);
					for (int32_t keyIndex = 0; keyIndex < keyCount; ++keyIndex)
					{
						NamedTrackKey* key = (NamedTrackKey*)malloc(sizeof(NamedTrackKey));
						if (key)
						{
							fread(&key->sampleIndex, sizeof(key->sampleIndex), 1, stream);
							fread(&key->transform.matrix, sizeof(key->transform.matrix), 1, stream);
							Math::MatrixToQuaternion(&key->transform.matrix, &key->transform.rotation);

							key->next = 0;
							key->trackIndex = trackIndex;
							key->previous = trackSet->trackKeys[trackIndex];
							if (! key->previous)
							{
								trackSet->trackKeys[trackIndex] = key;
							}
							else
							{
								while (key->previous->next)
									key->previous = key->previous->next;
								key->previous->next = key;
							}
						}
					}
					++keyframeCount;
				}
			}
			trackSet->keyframeCount = keyframeCount;
		}

		fclose(stream);
		return trackSet;
	}

	// FUNCTION: TOY2 0x004CAE40 [MATCHED]
	void DestroyNamedTrackSet(NamedTrackSet* trackSet)
	{
		if (trackSet->trackNames)
		{
			for (int32_t trackIndex = 0; trackIndex < trackSet->trackCount; ++trackIndex)
			{
				if (trackSet->trackNames[trackIndex])
					free(trackSet->trackNames[trackIndex]);
			}
			free(trackSet->trackNames);
		}

		if (trackSet->trackKeys)
		{
			for (int32_t trackIndex = 0; trackIndex < trackSet->trackCount; ++trackIndex)
			{
				NamedTrackKey* trackKey = trackSet->trackKeys[trackIndex];
				while (trackKey)
				{
					NamedTrackKey* nextTrackKey = trackKey->next;
					free(trackKey);
					trackKey = nextTrackKey;
				}
			}
			free(trackSet->trackKeys);
		}

		free(trackSet);
	}

	// FUNCTION: TOY2 0x004CAED0 [MATCHED]
	int32_t FindNamedTrackIndex(NamedTrackSet* trackSet, const char* trackName)
	{
		int32_t trackIndex = trackSet->trackCount;
		while (trackIndex)
		{
			--trackIndex;
			if (trackSet->trackNames[trackIndex] && strcmpi(trackSet->trackNames[trackIndex], trackName) == 0)
				return trackIndex;
		}

		return -1;
	}
}
