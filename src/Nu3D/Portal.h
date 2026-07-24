#pragma once

#include "Nu3D/Nu3D.h"
#include "Nu3D/Link.h"

namespace NGNLoader
{
	struct NGNImage;
}

namespace Nu3D
{
	namespace Portal
	{
		struct ScalerEntry;

		struct AreaPortal
		{
			int32_t portalId;
			int32_t vertexCount;
			Vector3F center;
			float radius;
			float radiusSquared;
			Vector3F* vertices;

			static void CalculateBoundingSphere(AreaPortal* portal);
			static int32_t BuildScalerEntry(NGNLoader::NGNImage* image, int32_t areaIndex, Nu3D::Link::DynamicScaler* scaler);
			static ScalerEntry* AllocScalerEntry(NGNLoader::NGNImage* image);
		};

		struct PortalState
		{
			PortalState* next;
			AreaPortal* portal;
			int32_t sourceAreaIdx;
			int32_t targetAreaIdx;
			int32_t visited;
		};

		struct ScalerEntry
		{
			ScalerEntry* next;
			Nu3D::Link::DynamicScaler* scaler;
		};

		struct PortalBucket
		{
			ScalerEntry* scalerHead;
			PortalState* portalStateHead;
		};

		struct PortalHashTable
		{
			PortalBucket buckets[64];
		};

		extern int8_t g_visibleAreaFlags[64];

		void ClearVisibleAreaFlags();
		void MarkAllAreasVisible();
		int32_t IsAreaVisible(int32_t areaIndex);

		STATIC_ASSERT(sizeof(AreaPortal) == 0x20);
		STATIC_ASSERT(sizeof(PortalState) == 0x14);
		STATIC_ASSERT(sizeof(PortalBucket) == 0x8);
		STATIC_ASSERT(sizeof(ScalerEntry) == 0x8);
		STATIC_ASSERT(sizeof(PortalHashTable) == 0x200);
	}

	void DrawDebugPortalOutlines(Portal::AreaPortal* portal);

	namespace Area
	{
		extern int32_t g_portalTraversalDepth;
		extern int32_t g_excludedShapeId;
		extern int32_t g_lastPopulatedArea;
		extern int8_t g_portalRenderDepthThreshold;
		extern int32_t g_renderedScalerCount;
		extern int32_t g_specialScalerToggle;
		extern Link::DynamicScaler* g_specialScaler;

		void ResetPortalStates(NGNLoader::NGNImage* image);
		void RenderBucketThroughPortals(NGNLoader::NGNImage* image, int32_t areaIndex, int32_t scalerType, Portal::PortalState* incomingPortal);
	}
}
