#include "NGNLoader/NGNLoader.h"
#include "Nu3D/Portal.h"
#include "Toy2/Toy2.h"
#include "Nu3D/BmpDataNode.h"
#include "Logger.h"
#include "Renderer/Glue.h"
#include "Renderer/Renderer.h"
#include "Nu3D/Math.h"
#include "Nu3D/ObjLoad.h"

#include <windows.h>

namespace NGNLoader
{
	// FUNCTION: TOY2 0x004C4080 [PROVISIONAL]
	void ParseTextures(FILE* stream, NGNImage* ngnImage)
	{
		NGNTextureParams texParams;
		char rawTexStr[256];

		memset(&texParams, 0, sizeof(texParams));
		texParams.rawTexStr = rawTexStr;

		int32_t textureCount;
		fread(&textureCount, sizeof(int32_t), 1, stream);

		for (int32_t textureIndex = textureCount; textureIndex > 0; --textureIndex)
		{
			uint32_t dataOffset;
			uint32_t rawTexStrLen;

			fread(&dataOffset, sizeof(uint32_t), 1, stream);
			fread(&rawTexStrLen, sizeof(uint32_t), 1, stream);
			fread(rawTexStr, sizeof(char), rawTexStrLen, stream);

			rawTexStr[rawTexStrLen] = '\0';

			strlwr(rawTexStr);
			int32_t textureId = atoi(&rawTexStr[3]);

			int32_t flags = 0;
			int32_t isBGR = 0;

			if (textureId != 14 && textureId != 36 && textureId != 37)
				flags = 8;

			if (textureId > 31)
				flags |= 32;

			if (strstr(rawTexStr, "bgr"))
			{
				memcpy(rawTexStr, "tex", 3);
				isBGR = 1;
			}

			int32_t beforeOffset = ftell(stream);

			LoadTextureContents(stream, rawTexStr, flags);

			int32_t afterOffset = ftell(stream);

			if (afterOffset - beforeOffset != dataOffset)
				Logger::GetErrorHandler("C:\\projects\\nu3d\\world.c", 573)("bitmap parsed incorrectly in file %s", g_curFileName);

			texParams.isTex14 = strcmpi("tex14", rawTexStr) == 0;

			int32_t index = GetOrAllocateTexture(&texParams);

			if (textureId < 64)
			{
				ngnImage->textureEntries[textureId].isBGR = isBGR;
				ngnImage->textureEntries[textureId].textureDataIndex = index;
			}
		}
	}

	// FUNCTION: TOY2 0x004C4220 [PROVISIONAL]
	uint32_t ParseCreatures(FILE* stream, NGNImage* ngnImage)
	{
		uint32_t creatureCount;
		fread(&creatureCount, 1, sizeof(uint32_t), stream);

		ngnImage->creatureCount = creatureCount;

		if (creatureCount > 0)
		{
			Nu3D::Creature** creatureArray = (Nu3D::Creature**)malloc(sizeof(Nu3D::Creature*) * creatureCount);
			ngnImage->creatureData = creatureArray;

			if (! creatureArray)
				Logger::GetErrorHandler("C:\\projects\\nu3d\\world.c", 617)("unable to alloc space for %d creatures", creatureCount);

			memset(ngnImage->creatureData, 0, sizeof(Nu3D::Creature*) * creatureCount);

			uint8_t creatureValidFlags[512];
			memset(creatureValidFlags, 0, creatureCount);

			if (creatureCount > 0)
			{
				int32_t count;

				do
				{
					uint8_t charNameLen;
					fread(&charNameLen, 1, sizeof(uint8_t), stream);

					if (charNameLen)
					{
						char charNameBuffer[256];
						fread(charNameBuffer, charNameLen, sizeof(char), stream);
						creatureValidFlags[count] = 1;
					}

					++count;

				} while (count < creatureCount);
			}

			for (int32_t index = 0; index < creatureCount; ++index)
			{
				if (creatureValidFlags[index])
					ngnImage->creatureData[index] = ExtractCreatureData(stream);
			}

			return creatureCount;
		}

		return creatureCount;
	}

}
