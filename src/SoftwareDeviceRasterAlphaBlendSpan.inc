// The body of RasterizeAlphaBlendSpan555 and RasterizeAlphaBlendSpan565: an untextured alpha-blended span.
// The two surface formats place red and green at different bit positions,
// so each includer states how it unpacks and packs them.

		int32_t width = (int32_t)edgeA->position.x - (int32_t)edgeB->position.x;
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
			edgeB = edgeA;
			width = -width;
		}
		else
		{
			if (width == 0)
			{
				return;
			}
			farRed = edgeARed;
			farGreen = edgeAGreen;
			farBlue = edgeABlue;
		}

		destRow += (int32_t)edgeB->position.x;
		int32_t stepRed = (farRed - edgeBRed) / width;
		int32_t stepGreen = (farGreen - edgeBGreen) / width;
		int32_t stepBlue = (farBlue - edgeBBlue) / width;

		while (width > 0)
		{
			int32_t alpha = g_spanAlpha;
			uint16_t pixel = *destRow;
			int32_t invAlpha = g_spanInvAlpha;

			int32_t blue = ((pixel & 0x1f) << 3) * invAlpha + ((uint32_t)(alpha * edgeBBlue) >> 8);
			if (blue > 0xf800)
			{
				blue = 0xf800;
			}

			int32_t green = SPAN_UNPACK_GREEN(pixel) * invAlpha + ((uint32_t)(alpha * edgeBGreen) >> 8);
			if (green > 0xf800)
			{
				green = 0xf800;
			}

			int32_t red = SPAN_UNPACK_RED(pixel) * invAlpha + ((uint32_t)(alpha * edgeBRed) >> 8);
			if (red > 0xf800)
			{
				red = 0xf800;
			}

			*destRow = (uint16_t)(SPAN_PACK_RED(red) + SPAN_PACK_GREEN(green) + (blue >> 11));
			destRow++;

			edgeBRed += stepRed;
			edgeBGreen += stepGreen;
			edgeBBlue += stepBlue;
			width--;
		}
