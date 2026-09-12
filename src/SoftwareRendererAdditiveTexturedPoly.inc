// The body of RasterizeAdditiveTexturedPolygon555 and RasterizeAdditiveTexturedPolygon565: a textured polygon added to the target.
// The two surface formats place red and green at different bit positions and
// key on a different texel, so each includer states those four values.

		int32_t bottomY = item->vertices[0].y;
		int32_t topY = bottomY;
		if (item->vertices[1].y < topY)
			topY = item->vertices[1].y;
		else if (item->vertices[1].y > bottomY)
			bottomY = item->vertices[1].y;

		if (item->vertices[2].y < topY)
			topY = item->vertices[2].y;
		else if (item->vertices[2].y > bottomY)
			bottomY = item->vertices[2].y;

		if (item->renderFlags & SOFTWARE_RENDER_QUAD)
		{
			if (item->vertices[3].y < topY)
				topY = item->vertices[3].y;
			else if (item->vertices[3].y > bottomY)
				bottomY = item->vertices[3].y;
		}

		if (topY < Toy2::g_screenClipTop)
			topY = Toy2::g_screenClipTop;
		if (bottomY > Toy2::g_screenClipBottom)
			bottomY = Toy2::g_screenClipBottom;

		int32_t scanlineCount = bottomY - topY + 1;
		ScanlineScratch* scanline = &g_scanlineScratch[topY];
		ClearScanlineFlags(scanline, scanlineCount);

		RASTERIZE_TEXTURED_EDGE(&item->vertices[0], &item->vertices[1], edge01DoneAdditive);
		RASTERIZE_TEXTURED_EDGE(&item->vertices[1], &item->vertices[2], edge12DoneAdditive);
		if ((item->renderFlags & SOFTWARE_RENDER_QUAD) == 0)
		{
			RASTERIZE_TEXTURED_EDGE(&item->vertices[2], &item->vertices[0], edge20DoneAdditive);
		}
		else
		{
			RASTERIZE_TEXTURED_EDGE(&item->vertices[2], &item->vertices[3], edge23DoneAdditive);
			RASTERIZE_TEXTURED_EDGE(&item->vertices[3], &item->vertices[0], edge30DoneAdditive);
		}

		uint16_t* texture = (uint16_t*)g_softwareTextureData[item->textureIndex];
		uint16_t* rowStart = (uint16_t*)g_lockedBackBuffer + g_backBufferPitchPixels * topY + Toy2::g_screenClipLeft;
		do
		{
			if (scanline->populated != 0 && scanline->leftXFixed <= Toy2::g_screenClipRightFixed && scanline->rightXFixed >= Toy2::g_screenClipLeftFixed)
			{
				int32_t leftX = scanline->leftXFixed >> SOLID_EDGE_X_FRACTION_BITS;
				int32_t rightX = scanline->rightXFixed >> SOLID_EDGE_X_FRACTION_BITS;
				if (leftX == rightX)
				{
					uint16_t texel = texture[(scanline->leftInterpolants[0] >> k_fixedPointShift) + (scanline->leftInterpolants[1] >> 8 & 0xFFFFFF00)];
					if (texel != POLYGON_COLOUR_KEY)
					{
						uint16_t& pixel = rowStart[leftX - Toy2::g_screenClipLeft];
						int32_t red = (pixel & POLYGON_RED_MASK) + (texel & POLYGON_RED_MASK);
						if (red > POLYGON_RED_MASK)
							red = POLYGON_RED_MASK;
						int32_t green = (pixel & POLYGON_GREEN_MASK) + (texel & POLYGON_GREEN_MASK);
						if (green > POLYGON_GREEN_MASK)
							green = POLYGON_GREEN_MASK;
						int32_t blue = (pixel & 0x1F) + (texel & 0x1F);
						if (blue > 0x1F)
							blue = 0x1F;
						pixel = (uint16_t)(red + green + blue);
					}
				}
				else
				{
					int32_t width = rightX - leftX;
					int32_t u = scanline->leftInterpolants[0];
					int32_t v = scanline->leftInterpolants[1];
					int32_t uStep = (scanline->rightInterpolants[0] - u) / width;
					int32_t vStep = (scanline->rightInterpolants[1] - v) / width;
					int32_t pixelCount = width;
					uint16_t* pixel;
					if (leftX < Toy2::g_screenClipLeft)
					{
						int32_t clippedPixels = Toy2::g_screenClipLeft - leftX;
						u += clippedPixels * uStep;
						v += clippedPixels * vStep;
						pixelCount = Toy2::g_softWindowWidth;
						pixel = rowStart;
						if (rightX <= Toy2::g_screenClipRight)
							pixelCount = rightX - Toy2::g_screenClipLeft + 1;
					}
					else
					{
						if (rightX > Toy2::g_screenClipRight)
							pixelCount = Toy2::g_screenClipRight - leftX;
						pixelCount++;
						pixel = rowStart + leftX - Toy2::g_screenClipLeft;
					}

					do
					{
						uint16_t texel = texture[(u >> k_fixedPointShift) + (v >> 8 & 0xFFFFFF00)];
						if (texel != POLYGON_COLOUR_KEY)
						{
							int32_t red = (*pixel & POLYGON_RED_MASK) + (texel & POLYGON_RED_MASK);
							if (red > POLYGON_RED_MASK)
								red = POLYGON_RED_MASK;
							int32_t green = (*pixel & POLYGON_GREEN_MASK) + (texel & POLYGON_GREEN_MASK);
							if (green > POLYGON_GREEN_MASK)
								green = POLYGON_GREEN_MASK;
							int32_t blue = (*pixel & 0x1F) + (texel & 0x1F);
							if (blue > 0x1F)
								blue = 0x1F;
							*pixel = (uint16_t)(red + green + blue);
						}
						u += uStep;
						v += vStep;
						pixel++;
						pixelCount--;
					} while (pixelCount > 0);
				}
			}
			scanline++;
			rowStart += g_backBufferPitchPixels;
			scanlineCount--;
		} while (scanlineCount != 0);
