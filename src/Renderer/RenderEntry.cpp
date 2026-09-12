#include "Renderer/Renderer.h"
#include "Renderer/RendererInternal.h"
#include "Nu3D/Material.h"
#include "NGNLoader/NGNLoader.h"
#include "Nu3D/Camera.h"
#include "Logger.h"
#include "Nu3D/Math.h"
#include "Nu3D/Patch.h"
#include "Nu3D/Primitive.h"
#include "Nu3D/Scene.h"
#include "Nu3D/Sprite.h"
#include "Toy2/Toy2.h"
#include <cstring>

// Queueing one primitive or one patch for the frame. RenderPrimitive and
// RenderPatchList walk what a node holds, InstanceData::AllocFromMatrix and
// AllocFromNodeMatrices take the transform pool entry the draw needs, and
// RenderEntry::AllocObj, AllocPatch and InsertIntoBucket put the result in the
// depth bucket the material asks for.
//
// Retail run 0x004B8490-0x004B89B0, between two Renderer/Sprite.cpp runs.

namespace Renderer
{
	// GLOBAL: TOY2 0x0088C7D0
	Nu3D::InstanceData g_instanceDataPool[1000];

	// GLOBAL: TOY2 0x0095C860
	RenderEntry g_renderEntryPool[3000];

	// FUNCTION: TOY2 0x004B8490 [MATCHED]
	void RenderPrimitive(Nu3D::Primitive* primitive, const D3DMATRIX* transform, int32_t renderFlags)
	{
		if (0.0f != primitive->originRadius)
		{
			renderFlags |= g_additionalRenderFlags;
			Nu3D::InstanceData* instanceData = Nu3D::InstanceData::AllocFromMatrix(transform, renderFlags);
			if (instanceData)
			{
				for (; primitive; primitive = primitive->listNext)
					ProcessPrimitive(instanceData, primitive);
			}
		}
	}

}
namespace Nu3D
{
	using namespace Renderer;

	// FUNCTION: TOY2 0x004B84E0 [PROVISIONAL]
	InstanceData* InstanceData::AllocFromMatrix(const D3DMATRIX* matrix, int32_t renderFlags)
	{
		if (! g_instanceDataFreeCount)
			return 0;

		Nu3D::InstanceData* instanceData = &g_instanceDataPool[--g_instanceDataFreeCount];
		memcpy(instanceData->matrices, matrix, sizeof(D3DMATRIX));
		instanceData->renderFlags = renderFlags;
		instanceData->lodFactor = g_lodFactor;
		instanceData->horzOffset = g_materialHorzOffset;
		instanceData->vertOffset = g_materialVertOffset;
		instanceData->renderModeFlags = g_vertexLightingEnabled != 0;

		if (g_useVertexColorMod)
		{
			instanceData->renderModeFlags |= Nu3D::INSTANCE_RENDER_VERTEX_COLOR_MODULATION;
			instanceData->vertexModColor.r = g_vertexColorModRed;
			instanceData->vertexModColor.g = g_vertexColorModGreen;
			instanceData->vertexModColor.b = g_vertexColorModBlue;
		}

		Nu3D::Viewport::GetViewClipRect(&instanceData->clipRect);
		instanceData->unkInt6 = g_primitiveRenderFlags;
		instanceData->sprite = g_instanceSpriteTemplate;
		return instanceData;
	}

}
namespace Renderer
{
	// FUNCTION: TOY2 0x004B85E0 [MATCHED]
	void ProcessPrimitive(Nu3D::InstanceData* instanceData, Nu3D::Primitive* primitive)
	{
		Nu3D::Material* material = Nu3D::Material::GetFreeByIndex(primitive->materialIndex);
		for (;;)
		{
			if (! material)
				return;

			RenderEntry::AllocObj(material, primitive, instanceData);
			if ((material->metadata & 4) != 0)
				RenderEntry::AllocObj(NGNLoader::g_tex14Materials[0], primitive, instanceData);
			if ((material->metadata & 0x40) != 0)
				RenderEntry::AllocObj(NGNLoader::g_tex14Materials[1], primitive, instanceData);
			if ((material->metadata & 0x80) != 0)
				RenderEntry::AllocObj(NGNLoader::g_tex14Materials[2], primitive, instanceData);

			for (int32_t textureStage = 0; textureStage < g_maxSimultaneousTextures; ++textureStage)
			{
				if (! material)
					return;
				material = material->nextPass;
			}
		}
	}

	// FUNCTION: TOY2 0x004B8670 [MATCHED]
	RenderEntry* RenderEntry::AllocObj(Nu3D::Material* material, Nu3D::Primitive* primitive, Nu3D::InstanceData* instanceData)
	{
		if (g_renderEntryFreeCount)
		{
			RenderEntry* entry = &g_renderEntryPool[--g_renderEntryFreeCount];
			entry->primitive = primitive;
			entry->instanceData = instanceData;
			entry->material = material;

			if (instanceData->lodFactor == 1.0f && (material->metadata & 0xE33) == 0)
			{
				entry->type = RENDER_TYPE7;
				entry->next = material->renderEntryHead;
				material->renderEntryHead = entry;
			}
			else
			{
				entry->type = RENDER_TYPE9;
				InsertIntoBucket(entry);
			}
			return entry;
		}

		Logger::DebugLog("rndrentryAllocObj - out of rndrentries");
		return 0;
	}

	// FUNCTION: TOY2 0x004B86F0 [PROVISIONAL]
	void RenderEntry::InsertIntoBucket(RenderEntry* entry)
	{
		Vector3F cameraPosition;
		Vector3F instancePosition;
		Nu3D::Math::GetPositionVector(&Nu3D::Camera::g_activeCamera.transform, &cameraPosition);
		Nu3D::Math::GetPositionVector(&entry->instanceData->matrices[0], &instancePosition);
		Nu3D::Math::VertexSubtract(&cameraPosition, &cameraPosition, &instancePosition);

		float distanceSquared = cameraPosition.x * cameraPosition.x + cameraPosition.y * cameraPosition.y + cameraPosition.z * cameraPosition.z;
		if (distanceSquared < 1.0f)
			distanceSquared = 1.0f;
		entry->distanceSquared = distanceSquared;

		union
		{
			float value;
			uint32_t bits;
		} distance;
		distance.value = distanceSquared;
		int32_t bucketIndex = (distance.bits >> 20) - 0x3F8;
		if (bucketIndex > 255)
			return;

		int32_t depth = 0;
		RenderEntry* previous = 0;
		RenderEntry* current = (RenderEntry*)Nu3D::g_spriteBuckets[bucketIndex];
		while (current && distanceSquared < current->distanceSquared)
		{
			++depth;
			previous = current;
			current = current->next;
		}

		entry->next = current;
		if (previous)
			previous->next = entry;
		else
			Nu3D::g_spriteBuckets[bucketIndex] = (Nu3D::Sprite*)entry;

		if (depth >= Nu3D::g_maxBucketDepth)
			Nu3D::g_maxBucketDepth = depth;
	}

	// FUNCTION: TOY2 0x004B87F0 [MATCHED]
	void RenderPatchList(Nu3D::Patch* patch, const D3DMATRIX* matrices, int32_t* flags, int32_t renderFlags)
	{
		renderFlags |= g_additionalRenderFlags;

		while (patch != 0)
		{
			Nu3D::InstanceData* instanceData =
				Nu3D::InstanceData::AllocFromNodeMatrices(matrices, patch->controlPointIndices, patch->controlPointCount, flags, renderFlags);

			if (instanceData != 0)
			{
				ProcessPatch(instanceData, patch);
			}

			patch = patch->listNext;
		}
	}

}
namespace Nu3D
{
	using namespace Renderer;

	// FUNCTION: TOY2 0x004B8840 [PROVISIONAL]
	InstanceData* InstanceData::AllocFromNodeMatrices(const D3DMATRIX* matrices, int32_t* nodeIndices, int32_t count, int32_t* flags, int32_t renderFlags)
	{
		if (! g_instanceDataFreeCount)
			return 0;

		--g_instanceDataFreeCount;
		D3DMATRIX* dest = g_instanceDataPool[g_instanceDataFreeCount].matrices;

		while (count != 0)
		{
			if (flags && (flags[*nodeIndices] & 1))
			{
				++g_instanceDataFreeCount;
				return 0;
			}

			memcpy(dest, &matrices[*nodeIndices], sizeof(D3DMATRIX));
			dest++;
			nodeIndices++;
			count--;
		}

		g_instanceDataPool[g_instanceDataFreeCount].renderFlags = renderFlags;
		g_instanceDataPool[g_instanceDataFreeCount].lodFactor = g_lodFactor;
		g_instanceDataPool[g_instanceDataFreeCount].horzOffset = g_materialHorzOffset;
		g_instanceDataPool[g_instanceDataFreeCount].vertOffset = g_materialVertOffset;
		g_instanceDataPool[g_instanceDataFreeCount].renderModeFlags = g_vertexLightingEnabled != 0;
		Nu3D::Viewport::GetViewClipRect(&g_instanceDataPool[g_instanceDataFreeCount].clipRect);
		g_instanceDataPool[g_instanceDataFreeCount].unkInt6 = g_primitiveRenderFlags;
		return &g_instanceDataPool[g_instanceDataFreeCount];
	}

}
namespace Renderer
{
	// FUNCTION: TOY2 0x004B8940 [MATCHED]
	void ProcessPatch(Nu3D::InstanceData* instanceData, Nu3D::Patch* patch)
	{
		Nu3D::Material* material = Nu3D::Material::GetFreeByIndex(patch->materialId);
		RenderEntry::AllocPatch(material, patch, instanceData);

		if ((material->metadata & 4) != 0)
		{
			RenderEntry::AllocPatch(NGNLoader::g_tex14Materials[0], patch, instanceData);
		}

		if ((material->metadata & 0x40) != 0)
		{
			RenderEntry::AllocPatch(NGNLoader::g_tex14Materials[1], patch, instanceData);
		}

		if ((material->metadata & 0x80) != 0)
		{
			RenderEntry::AllocPatch(NGNLoader::g_tex14Materials[2], patch, instanceData);
		}
	}

	// FUNCTION: TOY2 0x004B89B0 [PROVISIONAL]
	RenderEntry* RenderEntry::AllocPatch(Nu3D::Material* material, Nu3D::Patch* patch, Nu3D::InstanceData* instanceData)
	{
		if (g_renderEntryFreeCount)
		{
			RenderEntry* entry = &g_renderEntryPool[--g_renderEntryFreeCount];
			entry->patch = patch;
			entry->instanceData = instanceData;
			entry->material = material;

			if (instanceData->lodFactor == 1.0f && (material->metadata & 0xE33) == 0)
			{
				entry->type = RENDER_TYPE6;
				entry->next = material->renderEntryHead;
				material->renderEntryHead = entry;
				return entry;
			}

			entry->type = RENDER_TYPE8;
			InsertIntoBucket(entry);
			return entry;
		}

		Logger::DebugLog("rndrentryAllocPatch - out of rndrentries");
		return 0;
	}

}
