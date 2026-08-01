#pragma once

#include "Common.h"

#include <stddef.h>

namespace DevDraw
{
	struct TexturedQuadPoint
	{
		int16_t x;
		int16_t y;
	};

	struct TexturedQuadTexCoord
	{
		int32_t u;
		int32_t v;
	};

	union TexturedQuad
	{
		uint32_t raw[19];
		struct
		{
			TexturedQuadPoint points[4];
			TexturedQuadTexCoord texCoords[4];
			int32_t depth;
			int32_t drawSlot;
			int32_t textureWidth;
			int32_t textureHeight;
			int32_t blue;
			int32_t green;
			int32_t red;
		};
	};

	STATIC_ASSERT(offsetof(TexturedQuad, texCoords) == 0x10);
	STATIC_ASSERT(offsetof(TexturedQuad, depth) == 0x30);
	STATIC_ASSERT(offsetof(TexturedQuad, drawSlot) == 0x34);
	STATIC_ASSERT(sizeof(TexturedQuad) == 0x4C);

	int16_t SubmitTexturedQuad(TexturedQuad* quad);
	int16_t SubmitColouredTexturedQuad(TexturedQuad* quad);
	int16_t DrawFullScreenTexturePair(int16_t leftTextureSlot, int16_t rightTextureSlot);
}
