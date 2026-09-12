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

// The texture store of the loader: retail holds 0x004BB5E0 through 0x004BB810
// as one object, in the order of this file.
namespace NGNLoader
{
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

	// FUNCTION: TOY2 0x004BB690 [TOOL]
	int32_t CopyToDDSurfaceByIndex(int32_t texIndex, LPDIRECTDRAWSURFACE4 ddSurface)
	{
		if (texIndex)
			return Nu3D::CopyToDDSurface(g_textureDataFreeList[texIndex - 1].bmpDataNode, ddSurface);
		else
			return 0;
	}

	// FUNCTION: TOY2 0x004BB6C0 [TOOL]
	HBITMAP GetBmpHandle(int32_t index)
	{
		if (index)
			return g_textureDataFreeList[index - 1].bmpDataNode->bitmapHandle;
		else
			return 0;
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

	// FUNCTION: TOY2 0x004BB7D0 [MATCHED]
	void ReleaseAllTextures()
	{
		while (g_textureData.activeList)
			ReleaseTextureData(g_textureData.activeList);

		while (g_textureCache.activeList)
			ReleaseTextureCache(g_textureCache.activeList);

		Nu3D::FreeAllBmpDataNodes_T();
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
}
