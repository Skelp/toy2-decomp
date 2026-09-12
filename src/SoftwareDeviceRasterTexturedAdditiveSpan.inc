// The body of RasterizeTexturedAdditiveSpan555 and RasterizeTexturedAdditiveSpan565: a textured additive span.
// The two surface formats place red and green at different bit positions, so
// each includer states how it reads a channel out of a pixel and puts one back.

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
			int32_t stepRed = (farRed - edgeBRed) / width;
			int32_t stepGreen = (farGreen - edgeBGreen) / width;
			int32_t stepBlue = (farBlue - edgeBBlue) / width;

			destRow += (int32_t)edgeB->position.x;

			SPAN_SETUP_TEXTURE_WALK();

			do
			{
				int32_t textureIndex = ((textureV >> k_textureCoordinateShift) & k_lowerByteMask) * k_textureDimension
					+ ((textureU >> k_textureCoordinateShift) & k_lowerByteMask);
				uint32_t texel = texData[textureIndex];
				if (texel > k_rgbMask)
				{
					uint32_t pixel = *(const uint32_t*)destRow;

					int32_t blue = (int32_t)(pixel & k_fiveBitChannelMask) + g_colourScaleTable3[(edgeBBlue & k_upperByteMask) + (texel & k_lowerByteMask)];
					if (blue >= k_fiveBitChannelLimit)
					{
						blue = k_fiveBitChannelMask;
					}

					int32_t green =
						(int32_t)SPAN_UNPACK_GREEN(pixel) + g_colourScaleTable3[(edgeBGreen & k_upperByteMask) + ((texel >> 8) & k_lowerByteMask)];
					if (green >= k_fiveBitChannelLimit)
					{
						green = k_fiveBitChannelMask;
					}

					int32_t red =
						(int32_t)SPAN_UNPACK_RED(pixel) + g_colourScaleTable3[(edgeBRed & k_upperByteMask) + ((texel >> 16) & k_lowerByteMask)];
					if (red >= k_fiveBitChannelLimit)
					{
						red = k_fiveBitChannelMask;
					}

					*destRow = (uint16_t)(SPAN_SHIFT_RED(red) + SPAN_SHIFT_GREEN(green) + blue);
				}

				destRow++;
				textureU += stepTextureU;
				textureV += stepTextureV;
				edgeBRed += stepRed;
				edgeBGreen += stepGreen;
				edgeBBlue += stepBlue;
				width--;
			} while (width != 0);
		}
