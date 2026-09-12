// The body of RasterizeAdditiveSpan555 and RasterizeAdditiveSpan565: an
// untextured additive span. The two surface formats place red and green at
// different bit positions, so each includer states how it reads a channel out
// of a pixel and how it puts one back.

		int32_t width = (int32_t)edgeA->position.x - (int32_t)edgeB->position.x;
		if (width == 0)
		{
			return;
		}

		// The caller does not order the endpoints. Keep the one with the smaller x
		// in edgeB and its colours in the edgeB accumulators, so the walk below
		// always runs to increasing columns. farRed/Green/Blue hold the other end.
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

			do
			{
				// Retail reads a full dword through the 16-bit cursor and keeps only
				// the low pixel. The upper half is discarded by the channel masks.
				uint32_t pixel = *(const uint32_t*)destRow;

				int32_t channel = (int32_t)(pixel & 0x1f) + (int32_t)((uint32_t)edgeBBlue >> 11);
				if (channel >= 0x20)
				{
					channel = 0x1f;
				}
				int32_t out = channel;

				channel = (int32_t)SPAN_UNPACK_GREEN(pixel) + (int32_t)((uint32_t)edgeBGreen >> 11);
				if (channel >= 0x20)
				{
					channel = 0x1f;
				}
				out += SPAN_SHIFT_GREEN(channel);

				channel = (int32_t)SPAN_UNPACK_RED(pixel) + (int32_t)((uint32_t)edgeBRed >> 11);
				if (channel >= 0x20)
				{
					channel = 0x1f;
				}

				*destRow = (uint16_t)(SPAN_SHIFT_RED(channel) + out);
				destRow++;

				edgeBRed += stepRed;
				edgeBGreen += stepGreen;
				edgeBBlue += stepBlue;
				width--;
			} while (width != 0);
		}
