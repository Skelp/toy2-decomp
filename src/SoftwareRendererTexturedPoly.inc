// The body of RasterizeTexturedPolygon555 and RasterizeTexturedPolygon565: a textured polygon.
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

		RASTERIZE_TEXTURED_EDGE(&item->vertices[0], &item->vertices[1], edge01DoneTextured);
		RASTERIZE_TEXTURED_EDGE(&item->vertices[1], &item->vertices[2], edge12DoneTextured);
		if ((item->renderFlags & SOFTWARE_RENDER_QUAD) == 0)
		{
			RASTERIZE_TEXTURED_EDGE(&item->vertices[2], &item->vertices[0], edge20DoneTextured);
		}
		else
		{
			RASTERIZE_TEXTURED_EDGE(&item->vertices[2], &item->vertices[3], edge23DoneTextured);
			RASTERIZE_TEXTURED_EDGE(&item->vertices[3], &item->vertices[0], edge30DoneTextured);
		}

		uint16_t* texture = (uint16_t*)g_softwareTextureData[item->textureIndex];
		uint16_t* rowStart = (uint16_t*)g_lockedBackBuffer + g_backBufferPitchPixels * topY + Toy2::g_screenClipLeft;
		if (item->renderFlags & SOFTWARE_RENDER_COLOUR_KEY)
		{
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
							rowStart[leftX - Toy2::g_screenClipLeft] = texel;
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
								*pixel = texel;
							u += uStep;
							v += vStep;
							pixelCount--;
							pixel++;
						} while (pixelCount > 0);
					}
				}
				scanline++;
				rowStart += g_backBufferPitchPixels;
				scanlineCount--;
			} while (scanlineCount != 0);
			return;
		}

		do
		{
			if (scanline->populated != 0 && scanline->leftXFixed <= Toy2::g_screenClipRightFixed && scanline->rightXFixed >= Toy2::g_screenClipLeftFixed)
			{
				int32_t leftX = scanline->leftXFixed >> SOLID_EDGE_X_FRACTION_BITS;
				int32_t rightX = scanline->rightXFixed >> SOLID_EDGE_X_FRACTION_BITS;
				if (leftX == rightX)
				{
					rowStart[leftX - Toy2::g_screenClipLeft] =
						texture[(scanline->leftInterpolants[0] >> k_fixedPointShift) + (scanline->leftInterpolants[1] >> 8 & 0xFFFFFF00)];
				}
				else
				{
					int32_t width = rightX - leftX;
					int32_t u = scanline->leftInterpolants[0];
					int32_t v = scanline->leftInterpolants[1];
					int32_t uStep = (scanline->rightInterpolants[0] - u) / width;
					int32_t vStep = (scanline->rightInterpolants[1] - v) / width;
					int32_t pixelCount;
					uint16_t* pixel;
					if (leftX < Toy2::g_screenClipLeft)
					{
						int32_t clippedPixels = Toy2::g_screenClipLeft - leftX;
						v += clippedPixels * vStep;
						u += clippedPixels * uStep;
						pixelCount = Toy2::g_softWindowWidth;
						pixel = rowStart;
						if (rightX <= Toy2::g_screenClipRight)
							pixelCount = rightX - Toy2::g_screenClipLeft + 1;
					}
					else
					{
						pixel = rowStart + leftX - Toy2::g_screenClipLeft;
						if (rightX <= Toy2::g_screenClipRight)
							pixelCount = rightX - leftX + 1;
						else
							pixelCount = Toy2::g_screenClipRight - leftX + 1;
					}

					do
					{
						*pixel = texture[(u >> k_fixedPointShift) + (v >> 8 & 0xFFFFFF00)];
						u += uStep;
						v += vStep;
						pixelCount--;
						pixel++;
					} while (pixelCount > 0);
				}
			}
			scanline++;
			rowStart += g_backBufferPitchPixels;
			scanlineCount--;
		} while (scanlineCount != 0);
