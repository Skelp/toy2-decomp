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

// The texture lookup and the cache it allocates: retail holds 0x004BB2F0
// through 0x004BB4C0 as one object, in the order of this file.
namespace NGNLoader
{
	NGNTextureData* GetTextureData(NGNTextureParams* texParams, int32_t ignoreParams);
	NGNTextureCache* AllocateTextureCache();

	// FUNCTION: TOY2 0x004BB2F0 [MATCHED]
	uint32_t GetTextureDataIndexByName(char* textureName)
	{
		NGNTextureParams texParams;
		texParams.rawTexStr = textureName;

		NGNTextureData* textureData = GetTextureData(&texParams, 1);
		return textureData ? textureData->textureIndex : 0;
	}

	// FUNCTION: TOY2 0x004BB320 [MATCHED]
	NGNTextureData* GetTextureData(NGNTextureParams* texParams, int32_t ignoreParams)
	{
		NGNTextureParams localTexParams;
		NGNTextureCache* cacheHead = g_textureCache.activeList;

		memcpy(&localTexParams, texParams, sizeof(localTexParams));
		localTexParams.rawTexStr = 0;
		uint32_t textureParamsSize = sizeof(NGNTextureParams);

		while (cacheHead)
		{
			int32_t comparison = 0;
			if (! ignoreParams)
				comparison = memcmp(&cacheHead->params, &localTexParams, textureParamsSize);

			if (comparison == 0 && strcmpi(cacheHead->texName, texParams->rawTexStr) == 0)
			{
				NGNTextureData* result = g_textureData.activeList;

				while (result)
				{
					if (result->textureCacheIndex == cacheHead->textureIndex)
						return result;

					result = result->next;
				}

				return 0;
			}

			cacheHead = cacheHead->next;
		}

		return 0;
	}

	// FUNCTION: TOY2 0x004BB3C0 [MATCHED]
	uint32_t GetOrAllocateTexture(NGNTextureParams* texParams)
	{
		char rawTexStrBuffer[256];
		NGNTextureCache* textureCache;

		NGNTextureData* textureData = GetTextureData(texParams, 0);

		if (textureData)
			goto texture_ready;

		textureCache = AllocateTextureCache();

		if (textureCache)
		{
			memcpy(&textureCache->params, texParams, sizeof(textureCache->params));

			textureCache->params.rawTexStr = 0;

			strcpy(textureCache->texName, texParams->rawTexStr);

			textureData = AllocateTextureData();

			if (textureData)
			{
				textureData->textureFlags = texParams->textureFlags;
				textureData->color.b = texParams->color.b;
				textureData->color.g = texParams->color.g;
				textureData->color.r = texParams->color.r;

				strcpy(rawTexStrBuffer, texParams->rawTexStr);

				int32_t flags;

				if ((texParams->textureFlags & TEXTURE_FLAG_ALPHA_BITMAP) != 0)
					flags = Nu3D::BMP_TEXTURE_ALPHA_BITMAP;
				else
					flags = (texParams->textureFlags & TEXTURE_FLAG_COLOR_KEY) != 0 ? Nu3D::BMP_TEXTURE_TRANSPARENT_GREEN : 0;

				textureData->bmpDataNode = LoadLocalBmpTexture(rawTexStrBuffer, flags);
				textureData->textureCacheIndex = textureCache->textureIndex;

				goto texture_ready;
			}
		}

		return 0;

	texture_ready:
		return textureData->textureIndex;
	}

	// FUNCTION: TOY2 0x004BB4C0 [MATCHED]
	NGNTextureCache* AllocateTextureCache()
	{
		NGNTextureCache* result = g_textureCache.freeList;

		if (g_textureCache.freeList)
		{
			g_textureCache.freeList = g_textureCache.freeList->next;
			result->next = g_textureCache.activeList;

			if (g_textureCache.activeList)
				g_textureCache.activeList->prev = result;

			result->prev = 0;
			g_textureCache.activeList = result;
		}

		return result;
	}
}
