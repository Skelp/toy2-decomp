#include "Nu3D/Portal.h"
#include "Nu3D/Math.h"
#include "NGNLoader/NGNTypes.h"

#include <FLOAT.H>
#include <MATH.H>
#include <MEMORY.H>

namespace Nu3D
{
	// GLOBAL: TOY2 0x00A4C414
	int8_t Portal::g_visibleAreaFlags[64];

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

		const double radiusSquared = maximum.x * maximum.x + maximum.y * maximum.y
			+ maximum.z * maximum.z;
		portal->radiusSquared = (float)radiusSquared;
		portal->radius = (float)sqrt(radiusSquared);
	}

	// FUNCTION: TOY2 0x004BC120
	void Portal::ClearVisibleAreaFlags()
	{
		g_visibleAreaFlags[0] = 1;
		memset(&g_visibleAreaFlags[1], 0, sizeof(g_visibleAreaFlags) - 1);
	}

	// FUNCTION: TOY2 0x004BC140
	void Portal::MarkAllAreasVisible()
	{
		memset(g_visibleAreaFlags, 1, sizeof(g_visibleAreaFlags));
	}

	// FUNCTION: TOY2 0x004BC160
	int32_t Portal::IsAreaVisible(int32_t areaIndex)
	{
		return g_visibleAreaFlags[areaIndex & 63];
	}

	// FUNCTION: TOY2 0x004BC360
	int32_t Portal::AreaPortal::BuildScalerEntry(NGNLoader::NGNImage* image, int32_t areaIndex,
		Nu3D::Link::DynamicScaler* scaler)
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
		if (image->scalerEntryCount >= image->maxScalerEntries || !image->scalerEntryPool)
			return 0;

		return &image->scalerEntryPool[image->scalerEntryCount++];
	}
}
