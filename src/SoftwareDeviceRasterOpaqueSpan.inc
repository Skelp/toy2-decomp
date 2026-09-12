// The body of RasterizeOpaqueSpan555 and RasterizeOpaqueSpan565: an untextured Gouraud span.
// The two surface formats place red and green at different bit positions,
// so each includer states how it unpacks and packs them.

		int32_t width = (int32_t)edgeA->position.x - (int32_t)edgeB->position.x;
		if (width == 0)
		{
			return;
		}

		int32_t farRed;
		int32_t farGreen;
		int32_t farBlue;
		if (width < 0)
		{
			farRed = edgeBRed;
			farGreen = edgeBGreen;
			farBlue = edgeBBlue;
			edgeBRed = edgeARed;
			edgeBGreen = edgeAGreen;
			edgeBBlue = edgeABlue;
			Nu3D::VertexTL* swap = edgeA;
			edgeA = edgeB;
			edgeB = swap;
			width = -width;
		}
		else
		{
			farRed = edgeARed;
			farGreen = edgeAGreen;
			farBlue = edgeABlue;
		}

		if (width > 0)
		{
			int32_t pairCount = width >> 1;
			int32_t stepRed;
			int32_t stepGreen;
			int32_t stepBlue;
			if (pairCount > 0)
			{
				stepRed = (farRed - edgeBRed) / pairCount;
				stepGreen = (farGreen - edgeBGreen) / pairCount;
				stepBlue = (farBlue - edgeBBlue) / pairCount;
			}

			int32_t startX = (int32_t)edgeB->position.x;
			int32_t endX = (int32_t)edgeA->position.x;
			destRow += startX;

			if (startX & 1)
			{
				*destRow = (uint16_t)SPAN_PACK_RED(edgeBRed) + (uint16_t)SPAN_PACK_GREEN(edgeBGreen)
					+ (uint16_t)(((uint32_t)edgeBBlue & 0xf800) >> 11);
				destRow++;
				pairCount = (width - 1) >> 1;
			}

			while (pairCount != 0)
			{
				uint32_t pixel = SPAN_PACK_RED(edgeBRed) + SPAN_PACK_GREEN(edgeBGreen) + ((uint32_t)edgeBBlue >> 11);
				*(uint32_t*)destRow = MAKELONG(pixel, pixel);
				destRow += 2;

				edgeBRed += stepRed;
				edgeBGreen += stepGreen;
				edgeBBlue += stepBlue;
				pairCount--;
			}

			if (endX & 1)
			{
				*destRow = (uint16_t)SPAN_PACK_RED(edgeBRed) + (uint16_t)SPAN_PACK_GREEN(edgeBGreen)
					+ (uint16_t)(((uint32_t)edgeBBlue & 0xf800) >> 11);
			}
		}
