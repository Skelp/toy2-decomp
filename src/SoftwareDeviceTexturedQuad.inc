// The body of RasterizeTexturedQuad and RasterizeTexturedQuadPairSample.
// Both walk the same textured quad and write two pixels at a time. They
// differ only in how they fill that pair: one samples the texture once per
// pixel, the other samples it once per pair. Each includer states that step
// as QUAD_EMIT_PIXEL_PAIR and includes this body.

		Nu3D::VertexTL* vertex0 = &command->vertices[0];
		Nu3D::VertexTL* vertex1 = &command->vertices[1];
		Nu3D::VertexTL* vertex2 = &command->vertices[2];
		Nu3D::VertexTL* vertex3 = &command->vertices[3];
		Nu3D::VertexTL* topCandidate = vertex0;
		int32_t topCandidateIndex = 0;
		if (vertex1->position.y <= topCandidate->position.y)
		{
			topCandidate = vertex1;
			topCandidateIndex = 1;
		}
		if (vertex2->position.y <= topCandidate->position.y)
		{
			topCandidate = vertex2;
			topCandidateIndex = 2;
		}
		if (vertex3->position.y <= topCandidate->position.y)
			topCandidateIndex = 3;

		Nu3D::VertexTL* vertexOrder[5];
		switch (topCandidateIndex)
		{
			case 0:
				vertexOrder[0] = vertex0;
				vertexOrder[1] = vertex1;
				vertexOrder[2] = vertex2;
				vertexOrder[3] = vertex3;
				vertexOrder[4] = vertex0;
				break;
			case 1:
				vertexOrder[0] = vertex1;
				vertexOrder[1] = vertex2;
				vertexOrder[2] = vertex3;
				vertexOrder[3] = vertex0;
				vertexOrder[4] = vertex1;
				break;
			case 2:
				vertexOrder[0] = vertex2;
				vertexOrder[1] = vertex3;
				vertexOrder[2] = vertex0;
				vertexOrder[3] = vertex1;
				vertexOrder[4] = vertex2;
				break;
			default:
				vertexOrder[0] = vertex3;
				vertexOrder[1] = vertex0;
				vertexOrder[2] = vertex1;
				vertexOrder[3] = vertex2;
				vertexOrder[4] = vertex3;
				break;
		}

		Nu3D::VertexTL** edgeAStart = &vertexOrder[0];
		Nu3D::VertexTL** edgeAEnd = &vertexOrder[1];
		Nu3D::VertexTL** edgeBEnd = &vertexOrder[3];
		Nu3D::VertexTL** edgeBStart = &vertexOrder[4];
		uint8_t* destination = (uint8_t*)g_backBuffer + (int32_t)(*edgeAStart)->position.y * g_primarySurfacePitch;

		int32_t edgeARemaining = (int32_t)(*edgeAEnd)->position.y - (int32_t)(*edgeAStart)->position.y;
		int32_t edgeBRemaining = (int32_t)(*edgeBEnd)->position.y - (int32_t)(*edgeBStart)->position.y;

		int32_t edgeARed;
		int32_t edgeAGreen;
		int32_t edgeABlue;
		int32_t edgeAEndRed;
		int32_t edgeAEndGreen;
		int32_t edgeAEndBlue;
		int32_t edgeBRed;
		int32_t edgeBGreen;
		int32_t edgeBBlue;
		int32_t edgeBEndRed;
		int32_t edgeBEndGreen;
		int32_t edgeBEndBlue;
		UnpackColourChannels((*edgeAStart)->diffuse.value, &edgeARed, &edgeAGreen, &edgeABlue);
		edgeBRed = edgeARed;
		edgeBGreen = edgeAGreen;
		edgeBBlue = edgeABlue;
		UnpackColourChannels((*edgeAEnd)->diffuse.value, &edgeAEndRed, &edgeAEndGreen, &edgeAEndBlue);
		UnpackColourChannels((*edgeBEnd)->diffuse.value, &edgeBEndRed, &edgeBEndGreen, &edgeBEndBlue);
		Nu3D::VertexTL edgeA = **edgeAStart;
		Nu3D::VertexTL edgeB = **edgeBStart;
		int32_t completedEdge;
		int32_t rows;
		if (edgeARemaining < edgeBRemaining)
		{
			completedEdge = 0;
			rows = edgeARemaining;
		}
		else
		{
			rows = edgeBRemaining;
			completedEdge = edgeBRemaining < edgeARemaining ? 1 : 2;
		}
		if (rows < 0)
			return;

		float edgeAXStep;
		float edgeAUStep;
		float edgeAVStep;
		int32_t edgeARedStep;
		int32_t edgeAGreenStep;
		int32_t edgeABlueStep;
		if (edgeARemaining > 0)
		{
			float edgeHeight = (float)edgeARemaining;
			edgeAXStep = ((*edgeAEnd)->position.x - (*edgeAStart)->position.x) / edgeHeight;
			edgeAUStep = ((*edgeAEnd)->uv.x - (*edgeAStart)->uv.x) / edgeHeight;
			edgeAVStep = ((*edgeAEnd)->uv.y - (*edgeAStart)->uv.y) / edgeHeight;
			edgeARedStep = (edgeAEndRed - edgeARed) / edgeARemaining;
			edgeAGreenStep = (edgeAEndGreen - edgeAGreen) / edgeARemaining;
			edgeABlueStep = (edgeAEndBlue - edgeABlue) / edgeARemaining;
		}

		float edgeBXStep;
		float edgeBUStep;
		float edgeBVStep;
		int32_t edgeBRedStep;
		int32_t edgeBGreenStep;
		int32_t edgeBBlueStep;
		if (edgeBRemaining > 0)
		{
			float edgeHeight = (float)edgeBRemaining;
			edgeBXStep = ((*edgeBEnd)->position.x - (*edgeBStart)->position.x) / edgeHeight;
			edgeBUStep = ((*edgeBEnd)->uv.x - (*edgeBStart)->uv.x) / edgeHeight;
			edgeBVStep = ((*edgeBEnd)->uv.y - (*edgeBStart)->uv.y) / edgeHeight;
			edgeBRedStep = (edgeBEndRed - edgeBRed) / edgeBRemaining;
			edgeBGreenStep = (edgeBEndGreen - edgeBGreen) / edgeBRemaining;
			edgeBBlueStep = (edgeBEndBlue - edgeBBlue) / edgeBRemaining;
		}

		for (;;)
		{
			int32_t remainingRows = rows;
			while (remainingRows > 0)
			{
				Nu3D::VertexTL* leftEdge = &edgeB;
				Nu3D::VertexTL* rightEdge = &edgeA;
				int32_t leftRed = edgeBRed;
				int32_t leftGreen = edgeBGreen;
				int32_t leftBlue = edgeBBlue;
				int32_t rightRed = edgeARed;
				int32_t rightGreen = edgeAGreen;
				int32_t rightBlue = edgeABlue;
				int32_t width = (int32_t)edgeA.position.x - (int32_t)edgeB.position.x;
				if (width < 0)
				{
					leftEdge = &edgeA;
					rightEdge = &edgeB;
					leftRed = edgeARed;
					leftGreen = edgeAGreen;
					leftBlue = edgeABlue;
					rightRed = edgeBRed;
					rightGreen = edgeBGreen;
					rightBlue = edgeBBlue;
					width = -width;
				}

				if (width != 0)
				{
					int32_t pairCount = width >> 1;
					int32_t stepRed;
					int32_t stepGreen;
					int32_t stepBlue;
					if (pairCount > 0)
					{
						stepRed = (rightRed - leftRed) / pairCount;
						stepGreen = (rightGreen - leftGreen) / pairCount;
						stepBlue = (rightBlue - leftBlue) / pairCount;
					}

					int32_t startX = (int32_t)leftEdge->position.x;
					int32_t endX = (int32_t)rightEdge->position.x;
					uint16_t* destRow = (uint16_t*)destination + startX;

					int32_t textureU = (int32_t)(leftEdge->uv.x * k_textureCoordinateScale);
					if (textureU > k_textureCoordinateMax)
						textureU = k_textureCoordinateMax;
					textureU <<= k_textureCoordinateShift;

					int32_t textureVValue = (int32_t)(leftEdge->uv.y * k_textureCoordinateScale);
					if (textureVValue > k_textureCoordinateMax)
						textureVValue = k_textureCoordinateMax;
					int32_t textureV = (k_textureCoordinateMax - textureVValue) << k_textureCoordinateShift;

					int32_t farTextureU = (int32_t)(rightEdge->uv.x * k_textureCoordinateScale);
					if (farTextureU > k_textureCoordinateMax)
						farTextureU = k_textureCoordinateMax;
					farTextureU <<= k_textureCoordinateShift;
					if (farTextureU > k_textureCoordinateFixedMax)
						farTextureU = k_textureCoordinateFixedMax;
					int32_t stepTextureU = (farTextureU - textureU) / width;

					int32_t farTextureVValue = (int32_t)(rightEdge->uv.y * k_textureCoordinateScale);
					if (farTextureVValue > k_textureCoordinateMax)
						farTextureVValue = k_textureCoordinateMax;
					int32_t farTextureV = (k_textureCoordinateMax - farTextureVValue) << k_textureCoordinateShift;
					if (farTextureV > k_textureCoordinateFixedMax)
						farTextureV = k_textureCoordinateFixedMax;
					int32_t stepTextureV = (farTextureV - textureV) / width;

					if (startX & 1)
					{
						int32_t textureIndex = ((textureV >> k_textureCoordinateShift) & k_lowerByteMask) * k_textureDimension
							+ ((textureU >> k_textureCoordinateShift) & k_lowerByteMask);
						uint32_t texel = texData[textureIndex];
						*destRow++ = g_colourScaleTable0[(leftBlue & k_upperByteMask) + (texel & k_lowerByteMask)]
							+ g_colourScaleTable0[k_colourScaleSubtableSize + (leftGreen & k_upperByteMask) + ((texel >> 8) & k_lowerByteMask)]
							+ g_colourScaleTable0[k_colourScaleSubtableSize * 2 + (leftRed & k_upperByteMask) + ((texel >> 16) & k_lowerByteMask)];
						pairCount = (width - 1) >> 1;
						textureU += stepTextureU;
						textureV += stepTextureV;
					}

					while (pairCount != 0)
					{
						QUAD_EMIT_PIXEL_PAIR();
						destRow += 2;
						textureU += stepTextureU * 2;
						textureV += stepTextureV * 2;
						leftRed += stepRed;
						leftGreen += stepGreen;
						leftBlue += stepBlue;
						pairCount--;
					}

					if (endX & 1)
					{
						int32_t textureIndex = ((textureV >> k_textureCoordinateShift) & k_lowerByteMask) * k_textureDimension
							+ ((textureU >> k_textureCoordinateShift) & k_lowerByteMask);
						uint32_t texel = texData[textureIndex];
						*destRow = g_colourScaleTable0[(leftBlue & k_upperByteMask) + (texel & k_lowerByteMask)]
							+ g_colourScaleTable0[k_colourScaleSubtableSize + (leftGreen & k_upperByteMask) + ((texel >> 8) & k_lowerByteMask)]
							+ g_colourScaleTable0[k_colourScaleSubtableSize * 2 + (leftRed & k_upperByteMask) + ((texel >> 16) & k_lowerByteMask)];
					}
				}

				edgeA.position.x += edgeAXStep;
				edgeARed += edgeARedStep;
				edgeA.uv.x += edgeAUStep;
				edgeAGreen += edgeAGreenStep;
				edgeA.uv.y += edgeAVStep;
				edgeABlue += edgeABlueStep;
				edgeBRed += edgeBRedStep;
				edgeB.position.x += edgeBXStep;
				edgeBGreen += edgeBGreenStep;
				edgeBBlue += edgeBBlueStep;
				edgeB.uv.x += edgeBUStep;
				destination += g_primarySurfacePitch;
				edgeB.uv.y += edgeBVStep;
				remainingRows--;
			}

			if (completedEdge == 0)
			{
				edgeBRemaining -= rows;
				edgeAStart++;
				edgeAEnd++;
				edgeARemaining = (int32_t)(*edgeAEnd)->position.y - (int32_t)(*edgeAStart)->position.y;
			}
			else if (completedEdge == 1)
			{
				edgeARemaining -= rows;
				edgeBStart--;
				edgeBEnd--;
				edgeBRemaining = (int32_t)(*edgeBEnd)->position.y - (int32_t)(*edgeBStart)->position.y;
			}
			else
			{
				edgeAStart++;
				edgeAEnd++;
				edgeBStart--;
				edgeBEnd--;
				edgeARemaining = (int32_t)(*edgeAEnd)->position.y - (int32_t)(*edgeAStart)->position.y;
				edgeBRemaining = (int32_t)(*edgeBEnd)->position.y - (int32_t)(*edgeBStart)->position.y;
			}

			int32_t nextCompletedEdge;
			int32_t nextRows;
			if (edgeARemaining < edgeBRemaining)
			{
				nextCompletedEdge = 0;
				nextRows = edgeARemaining;
			}
			else
			{
				nextRows = edgeBRemaining;
				nextCompletedEdge = edgeBRemaining < edgeARemaining ? 1 : 2;
			}
			if (nextRows < 0)
				return;

			if (completedEdge == 0)
			{
				edgeA = **edgeAStart;
				UnpackColourChannels((*edgeAStart)->diffuse.value, &edgeARed, &edgeAGreen, &edgeABlue);
				UnpackColourChannels((*edgeAEnd)->diffuse.value, &edgeAEndRed, &edgeAEndGreen, &edgeAEndBlue);
			}
			else if (completedEdge == 1)
			{
				edgeB = **edgeBStart;
				UnpackColourChannels((*edgeBStart)->diffuse.value, &edgeBRed, &edgeBGreen, &edgeBBlue);
				UnpackColourChannels((*edgeBEnd)->diffuse.value, &edgeBEndRed, &edgeBEndGreen, &edgeBEndBlue);
			}
			else
			{
				edgeA = **edgeAStart;
				edgeB = **edgeBStart;
				UnpackColourChannels((*edgeAStart)->diffuse.value, &edgeARed, &edgeAGreen, &edgeABlue);
				UnpackColourChannels((*edgeAEnd)->diffuse.value, &edgeAEndRed, &edgeAEndGreen, &edgeAEndBlue);
				UnpackColourChannels((*edgeBStart)->diffuse.value, &edgeBRed, &edgeBGreen, &edgeBBlue);
				UnpackColourChannels((*edgeBEnd)->diffuse.value, &edgeBEndRed, &edgeBEndGreen, &edgeBEndBlue);
			}

			if (edgeARemaining > 0 && completedEdge != 1)
			{
				float edgeHeight = (float)edgeARemaining;
				edgeAXStep = ((*edgeAEnd)->position.x - (*edgeAStart)->position.x) / edgeHeight;
				edgeAUStep = ((*edgeAEnd)->uv.x - (*edgeAStart)->uv.x) / edgeHeight;
				edgeAVStep = ((*edgeAEnd)->uv.y - (*edgeAStart)->uv.y) / edgeHeight;
				edgeARedStep = (edgeAEndRed - edgeARed) / edgeARemaining;
				edgeAGreenStep = (edgeAEndGreen - edgeAGreen) / edgeARemaining;
				edgeABlueStep = (edgeAEndBlue - edgeABlue) / edgeARemaining;
			}
			if (edgeBRemaining > 0 && completedEdge != 0)
			{
				float edgeHeight = (float)edgeBRemaining;
				edgeBXStep = ((*edgeBEnd)->position.x - (*edgeBStart)->position.x) / edgeHeight;
				edgeBUStep = ((*edgeBEnd)->uv.x - (*edgeBStart)->uv.x) / edgeHeight;
				edgeBVStep = ((*edgeBEnd)->uv.y - (*edgeBStart)->uv.y) / edgeHeight;
				edgeBRedStep = (edgeBEndRed - edgeBRed) / edgeBRemaining;
				edgeBGreenStep = (edgeBEndGreen - edgeBGreen) / edgeBRemaining;
				edgeBBlueStep = (edgeBEndBlue - edgeBBlue) / edgeBRemaining;
			}
			completedEdge = nextCompletedEdge;
			rows = nextRows;
			if (edgeAStart >= edgeBStart)
				return;
		}
