// The body of RasterizeTexturedAlphaBlendSpan555 and RasterizeTexturedAlphaBlendSpan565: a textured alpha-blended span.
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
				return;
			farRed = edgeARed;
			farGreen = edgeAGreen;
			farBlue = edgeABlue;
		}

		destRow += (int32_t)edgeB->position.x;

		int32_t textureU = (int32_t)(edgeB->uv.x * k_textureCoordinateScale) << k_textureCoordinateShift;
		int32_t textureVValue = (int32_t)(edgeB->uv.y * k_textureCoordinateScale);
		if (textureU > k_textureCoordinateFixedMax)
			textureU = k_textureCoordinateFixedMax;
		if (textureVValue > k_textureCoordinateMax)
			textureVValue = k_textureCoordinateMax;
		int32_t textureV = (k_textureCoordinateMax - textureVValue) << k_textureCoordinateShift;

		int32_t farTextureU = (int32_t)(edgeA->uv.x * k_textureCoordinateScale) << k_textureCoordinateShift;
		int32_t farTextureVValue = (int32_t)(edgeA->uv.y * k_textureCoordinateScale);
		if (farTextureU > k_textureCoordinateFixedMax)
			farTextureU = k_textureCoordinateFixedMax;
		if (farTextureVValue > k_textureCoordinateMax)
			farTextureVValue = k_textureCoordinateMax;
		int32_t farTextureV = (k_textureCoordinateMax - farTextureVValue) << k_textureCoordinateShift;

		int32_t stepRed = (farRed - edgeBRed) / width;
		int32_t stepGreen = (farGreen - edgeBGreen) / width;
		int32_t stepBlue = (farBlue - edgeBBlue) / width;
		int32_t stepTextureU = (farTextureU - textureU) / width;
		int32_t stepTextureV = (farTextureV - textureV) / width;

		do
		{
			int32_t textureIndex =
				((textureV >> k_textureCoordinateShift) & k_lowerByteMask) * k_textureDimension + ((textureU >> k_textureCoordinateShift) & k_lowerByteMask);
			uint32_t texel = texData[textureIndex];
			if (texel > k_rgbMask)
			{
				int32_t alpha = g_spanAlpha;
				int32_t invAlpha = g_spanInvAlpha;
				uint16_t pixel = *destRow;

				int32_t blue = ((uint32_t)(((texel & 0xff) * edgeBBlue) >> 8) * alpha >> 8) + ((pixel & 0x1f) << 3) * invAlpha;
				if (blue > 0xf800)
					blue = 0xf800;

				int32_t green = SPAN_UNPACK_GREEN(pixel) * invAlpha + ((uint32_t)(alpha * (((texel >> 8) & 0xff) * edgeBGreen >> 8)) >> 8);
				if (green > 0xf800)
					green = 0xf800;

				int32_t red = SPAN_UNPACK_RED(pixel) * invAlpha + ((uint32_t)(alpha * (((texel >> 16) & 0xff) * edgeBRed >> 8)) >> 8);
				if (red > 0xf800)
					red = 0xf800;

				*destRow = (uint16_t)(SPAN_PACK_RED(red) + SPAN_PACK_GREEN(green) + (blue >> 11));
			}

			textureU += stepTextureU;
			textureV += stepTextureV;
			edgeBRed += stepRed;
			edgeBGreen += stepGreen;
			edgeBBlue += stepBlue;
			destRow++;
			width--;
		} while (width != 0);
