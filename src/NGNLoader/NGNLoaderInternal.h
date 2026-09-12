#ifndef NGNLOADERINTERNAL_H
#define NGNLOADERINTERNAL_H

#include "NGNLoader/NGNLoader.h"
#include "NGNLoader/NGNTypes.h"
#include "Nu3D/BmpDataNode.h"
#include "Nu3D/Portal.h"

// State and entry points the objects of the loader share but that no public
// header states. NGNLoader.cpp held these; the objects split out of it need the
// same view.
namespace NGNLoader
{
	// The texture store that NGNLoader.cpp holds: the sentinel of the live slots,
	// the free list of cache records, and the count of primitives the image parse
	// has emitted so far.
	extern NGNTextureDataSentinal g_textureData;
	extern NGNTextureCache g_textureCacheFreeList[1000];
	extern NGNTextureCacheSentinal g_textureCache;
	extern int32_t g_curPrimCount;

	void FreeAllBmpDataNodes();
	void GetScaleVector(Vector3F* output);
	Nu3D::BmpDataNode* LoadLocalBmpTexture(const char* rawTexStr, int32_t flags);
	Nu3D::Portal::AreaPortal* AllocPortalVertices(int32_t vertexCount);

	NGNImage* BuildImage(char* fileName);

	void AllocPools(NGNImage* ngnImage, int32_t portalCount, int32_t maxScalerEntries);
	int32_t InsertPortal(NGNImage* ngnImage, int32_t sourceAreaIdx, int32_t targetAreaIdx, Nu3D::Portal::AreaPortal* portal);
}

#endif
