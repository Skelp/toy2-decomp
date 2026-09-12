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

namespace NGNLoader
{
	// GLOBAL: TOY2 0x00B62410
	NGNImage* g_ngnImage;

	// FUNCTION: TOY2 0x004B33A0 [MATCHED]
	void NGNImage::DestroyPortal(Nu3D::Portal::AreaPortal* portal) { free(portal); }

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

	// FUNCTION: TOY2 0x004AC240 [MATCHED]
	Nu3D::BmpDataNode* LoadLocalBmpTexture(const char* rawTexStr, int32_t flags) { return Nu3D::LoadLocalBmpTexture(rawTexStr, flags); }

	// FUNCTION: TOY2 0x004AC220 [MATCHED]
	Nu3D::BmpDataNode* LoadTextureContents(FILE* stream, const char* rawTexStr, int32_t flags) { return Nu3D::LoadTextureByStream(stream, rawTexStr, flags); }

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

	// FUNCTION: TOY2 0x004CEAE0 [MATCHED]
	void SetNewImage(char* fileName)
	{
		DECOMP_PRINT(("[NGNLoader]: Loading file %s\n", fileName));

		g_ngnImage = BuildImage(fileName);

		Nu3D::Portal::ClearVisibleAreaFlags();
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
