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

// The texture data slots: retail holds 0x004BB0E0 through 0x004BB1B0 as one
// object, in the order of this file.
namespace NGNLoader
{
	// FUNCTION: TOY2 0x004BB0E0 [TOOL]
	NGNTextureData* GetTextureDataByIndex(uint32_t texDataIndex) { return &g_textureDataFreeList[texDataIndex - 1]; }

	// FUNCTION: TOY2 0x004BB150 [MATCHED]
	void ReleaseTextureData(NGNTextureData* textureData)
	{
		if (textureData->bmpDataNode)
			Nu3D::ReleaseBmpDataNode_T(textureData->bmpDataNode);

		textureData->bmpDataNode = 0;

		if (textureData->next)
			textureData->next->prev = textureData->prev;

		if (textureData->prev)
		{
			textureData->prev->next = textureData->next;
		}
		else
		{
			g_textureData.activeList = textureData->next;
		}

		textureData->next = g_textureData.freeList;
		g_textureData.freeList = textureData;
	}

	// FUNCTION: TOY2 0x004BB1B0 [MATCHED]
	NGNTextureData* AllocateTextureData()
	{
		NGNTextureData* result = g_textureData.freeList;

		if (g_textureData.freeList)
		{
			g_textureData.freeList = g_textureData.freeList->next;
			result->next = g_textureData.activeList;

			if (g_textureData.activeList)
				g_textureData.activeList->prev = result;

			result->prev = 0;
			g_textureData.activeList = result;
		}

		return result;
	}
}
