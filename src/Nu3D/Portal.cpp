#include "Nu3D/Portal.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Math.h"
#include "NGNLoader/NGNTypes.h"
#include "Renderer/Renderer.h"
#include "Renderer/Sprite.h"

#include <FLOAT.H>
#include <MATH.H>
#include <MEMORY.H>

namespace Nu3D
{
	// FUNCTION: TOY2 0x004B9B90
	void DrawDebugPortalOutlines(Portal::AreaPortal* portal)
	{
		int32_t previousFlags = Renderer::SetAdditionalRenderFlags(4);
		RGBA color;
		color.value = 0xFFFFFFFF;
		for (int32_t index = 1; index < portal->vertexCount; ++index)
			Renderer::Sprite::QueueType10(&portal->vertices[index - 1], &portal->vertices[index], color);
		Renderer::SetAdditionalRenderFlags(previousFlags);
	}

	// GLOBAL: TOY2 0x00A4C414
	int8_t Portal::g_visibleAreaFlags[64];

	// GLOBAL: TOY2 0x005088A4
	int32_t Area::g_portalTraversalDepth = -1;

	// GLOBAL: TOY2 0x005088A8
	int32_t Area::g_excludedShapeId = -1;

	// GLOBAL: TOY2 0x005088AC
	int32_t Area::g_lastPopulatedArea = 64;

	// GLOBAL: TOY2 0x00A4C458
	int8_t Area::g_portalRenderDepthThreshold;

	// GLOBAL: TOY2 0x00A4CC60
	int32_t Area::g_renderedScalerCount;

	// GLOBAL: TOY2 0x00A4CC68
	int32_t Area::g_specialScalerToggle;

	// GLOBAL: TOY2 0x00E4D7C0
	Link::DynamicScaler* Area::g_specialScaler;

	// FUNCTION: TOY2 0x004B33B0
	void Portal::AreaPortal::CalculateBoundingSphere(AreaPortal* portal)
	{
		float minimumZ = FLT_MAX;
		float minimumY = FLT_MAX;
		float minimumX = FLT_MAX;

		Vector3F maximum;
		maximum.z = -FLT_MAX;
		maximum.y = -FLT_MAX;
		maximum.x = -FLT_MAX;

		if (portal->vertexCount > 0)
		{
			Vector3F* vertex = portal->vertices;
			int32_t verticesLeft = portal->vertexCount;

			do
			{
				if (vertex->x <= minimumX)
					minimumX = vertex->x;
				if (vertex->x >= maximum.x)
					maximum.x = vertex->x;
				if (vertex->y <= minimumY)
					minimumY = vertex->y;
				if (vertex->y >= maximum.y)
					maximum.y = vertex->y;
				if (minimumZ >= vertex->z)
					minimumZ = vertex->z;
				if (vertex->z >= maximum.z)
					maximum.z = vertex->z;

				++vertex;
			} while (--verticesLeft);
		}

		portal->center.x = (maximum.x + minimumX) * 0.5f;
		portal->center.y = (maximum.y + minimumY) * 0.5f;
		portal->center.z = (maximum.z + minimumZ) * 0.5f;

		Math::VertexSubtract(&maximum, &maximum, &portal->center);

		const double radiusSquared = maximum.x * maximum.x + maximum.y * maximum.y + maximum.z * maximum.z;
		portal->radiusSquared = (float)radiusSquared;
		portal->radius = (float)sqrt(radiusSquared);
	}

	// FUNCTION: TOY2 0x004BC120 [MATCHED]
	void Portal::ClearVisibleAreaFlags()
	{
		g_visibleAreaFlags[0] = 1;
		memset(&g_visibleAreaFlags[1], 0, sizeof(g_visibleAreaFlags) - 1);
	}

	// FUNCTION: TOY2 0x004BC140 [MATCHED]
	void Portal::MarkAllAreasVisible() { memset(g_visibleAreaFlags, 1, sizeof(g_visibleAreaFlags)); }

	// FUNCTION: TOY2 0x004BC160 [MATCHED]
	int32_t Portal::IsAreaVisible(int32_t areaIndex) { return g_visibleAreaFlags[areaIndex & 63]; }

	// FUNCTION: TOY2 0x004BC170 [MATCHED]
	void Area::ResetPortalStates(NGNLoader::NGNImage* image)
	{
		for (int32_t index = 0; index < image->portalEntryCount; ++index)
			image->portalStatePool[index].visited = 0;
	}

	// FUNCTION: TOY2 0x004BC460
	void Area::RenderBucketThroughPortals(NGNLoader::NGNImage* image, int32_t areaIndex, int32_t scalerType, Portal::PortalState* incomingPortal)
	{
		if (areaIndex >= 64)
		{
			areaIndex = g_lastPopulatedArea;
			if (areaIndex >= 64)
				return;
		}

		Viewport::ViewportCache viewportCache;
		Viewport::CacheViewport(&viewportCache);

		int32_t sourceAreaIndex = -1;
		if (incomingPortal)
		{
			if (incomingPortal->visited)
				return;
			sourceAreaIndex = incomingPortal->sourceAreaIdx;
			if (! Camera::ClipViewToPortal(&Camera::g_activeCamera.transform, incomingPortal->portal))
				return;
			incomingPortal->visited = 1;
		}

		Vector3F cameraPosition;
		Math::GetPositionVector(&Camera::g_activeCamera.transform, &cameraPosition);
		++g_portalTraversalDepth;

		Portal::PortalBucket* bucket = &image->portalHashTable->buckets[areaIndex];
		if (g_portalRenderDepthThreshold <= g_portalTraversalDepth)
		{
			Portal::g_visibleAreaFlags[areaIndex] = 1;
			for (Portal::ScalerEntry* entry = bucket->scalerHead; entry; entry = entry->next)
			{
				Link::DynamicScaler* scaler = entry->scaler;
				if ((scaler->flags & 1) != 0 || scaler->gscaleType != scalerType)
					continue;

				Vector3F worldCenter;
				Vector3F offset;
				Math::VertexAdd(&worldCenter, &scaler->translation, &scaler->boundsCenterWorld);
				Math::VertexSubtract(&offset, &worldCenter, &cameraPosition);
				float distanceSquared = offset.x * offset.x + offset.y * offset.y + offset.z * offset.z;

				if (scaler->gscaleType == 1)
				{
					if (distanceSquared <= Renderer::g_secondaryRenderDistanceSquared)
						continue;
				}
				else if (distanceSquared >= Renderer::g_primaryRenderDistanceSquared)
				{
					continue;
				}

				Primitive* primitive = image->primitives[scaler->shapeId];
				uint32_t frustumResult = Frustum::TestSphereAllPlanesAlt(&worldCenter, primitive->boundSphereRadius);
				if ((frustumResult & 0x55555555) != 0)
					continue;

				if ((scaler != g_specialScaler || g_specialScalerToggle) && scaler->shapeId != g_excludedShapeId)
				{
					Renderer::Set508718(scaler->packedFlags);
					Renderer::RenderPrimitive(primitive, &scaler->transformMatrix, frustumResult ? 0 : 0x2000);
				}

				if (scaler == g_specialScaler)
					g_specialScalerToggle = 1 - g_specialScalerToggle;
				++g_renderedScalerCount;
			}
		}

		for (Portal::PortalState* portalState = bucket->portalStateHead; portalState; portalState = portalState->next)
		{
			if (portalState->targetAreaIdx == sourceAreaIndex || portalState->targetAreaIdx == -1)
				continue;
			if ((Frustum::TestSphereDepthPlanes(&portalState->portal->center, portalState->portal->radius) & 0x55555555) != 0)
				continue;
			RenderBucketThroughPortals(image, portalState->targetAreaIdx, scalerType, portalState);
		}

		Viewport::RestoreViewportCache(&viewportCache);
		--g_portalTraversalDepth;
		if (areaIndex < 64 && bucket->scalerHead)
			g_lastPopulatedArea = areaIndex;
		Renderer::Set508718(5);
	}

	// FUNCTION: TOY2 0x004BC360
	int32_t Portal::AreaPortal::BuildScalerEntry(NGNLoader::NGNImage* image, int32_t areaIndex, Nu3D::Link::DynamicScaler* scaler)
	{
		ScalerEntry* entry = AllocScalerEntry(image);
		if (entry)
		{
			entry->scaler = scaler;
			entry->next = image->portalHashTable->buckets[areaIndex].scalerHead;
			image->portalHashTable->buckets[areaIndex].scalerHead = entry;
			return 1;
		}

		return 0;
	}

	// FUNCTION: TOY2 0x004BC3A0
	Portal::ScalerEntry* Portal::AreaPortal::AllocScalerEntry(NGNLoader::NGNImage* image)
	{
		if (image->scalerEntryCount >= image->maxScalerEntries || ! image->scalerEntryPool)
			return 0;

		return &image->scalerEntryPool[image->scalerEntryCount++];
	}
}
