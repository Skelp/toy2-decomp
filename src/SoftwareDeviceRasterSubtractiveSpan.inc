// The body of RasterizeSubtractiveSpan555 and RasterizeSubtractiveSpan565: an untextured subtractive span.
// The two surface formats place red and green at different bit positions, so
// each includer states how it reads a channel out of a pixel and puts one back.

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
				uint16_t pixel = *destRow;

				int32_t channel = (pixel & 0x1f) - ((uint16_t)edgeBBlue >> 11);
				if (channel < 0)
				{
					channel = 0;
				}
				int32_t out = channel;

				channel = SPAN_UNPACK_GREEN(pixel) - ((uint16_t)edgeBGreen >> 11);
				if (channel < 0)
				{
					channel = 0;
				}
				out += SPAN_SHIFT_GREEN(channel);

				channel = SPAN_UNPACK_RED(pixel) - ((uint16_t)edgeBRed >> 11);
				if (channel < 0)
				{
					channel = 0;
				}

				*destRow = (uint16_t)(SPAN_SHIFT_RED(channel) + out);
				destRow++;

				edgeBRed += stepRed;
				edgeBGreen += stepGreen;
				edgeBBlue += stepBlue;
				width--;
			} while (width != 0);
		}
