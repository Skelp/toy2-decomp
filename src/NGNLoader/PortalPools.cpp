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

// The area portal pools: retail holds 0x004BC1B0 through 0x004BC320 as one
// object, in the order of this file.
namespace NGNLoader
{
	Nu3D::Portal::PortalState* AllocAreaPortal(NGNImage* ngnImage);

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

	// FUNCTION: TOY2 0x004BC320 [MATCHED]
	Nu3D::Portal::PortalState* AllocAreaPortal(NGNImage* ngnImage)
	{
		if (ngnImage->portalEntryCount < ngnImage->areaPortalCount && ngnImage->portalStatePool)
			return &ngnImage->portalStatePool[ngnImage->portalEntryCount++];

		return 0;
	}
}
