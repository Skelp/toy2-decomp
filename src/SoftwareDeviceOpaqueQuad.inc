// The body of RasterizeOpaqueQuad555 and RasterizeOpaqueQuad565.
// Both walk the same untextured quad and differ only in how they pack a
// colour into a 16-bit pixel, so each includer states its own red and green
// positions and includes this body.

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
		int32_t edgeARedStep;
		int32_t edgeAGreenStep;
		int32_t edgeABlueStep;
		if (edgeARemaining > 0)
		{
			edgeAXStep = ((*edgeAEnd)->position.x - (*edgeAStart)->position.x) / (float)edgeARemaining;
			edgeARedStep = (edgeAEndRed - edgeARed) / edgeARemaining;
			edgeAGreenStep = (edgeAEndGreen - edgeAGreen) / edgeARemaining;
			edgeABlueStep = (edgeAEndBlue - edgeABlue) / edgeARemaining;
		}

		float edgeBXStep;
		int32_t edgeBRedStep;
		int32_t edgeBGreenStep;
		int32_t edgeBBlueStep;
		if (edgeBRemaining > 0)
		{
			edgeBXStep = ((*edgeBEnd)->position.x - (*edgeBStart)->position.x) / (float)edgeBRemaining;
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
				int32_t leftRed = edgeBRed;
				int32_t leftGreen = edgeBGreen;
				int32_t leftBlue = edgeBBlue;
				Nu3D::VertexTL* rightEdge = &edgeA;
				int32_t rightRed = edgeARed;
				int32_t rightGreen = edgeAGreen;
				int32_t rightBlue = edgeABlue;
				int32_t width = (int32_t)edgeA.position.x - (int32_t)edgeB.position.x;
				if (width < 0)
				{
					leftEdge = &edgeA;
					leftRed = edgeARed;
					leftGreen = edgeAGreen;
					leftBlue = edgeABlue;
					rightEdge = &edgeB;
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
					if (startX & 1)
					{
						*destRow = (uint16_t)QUAD_PACK_RED(leftRed) + (uint16_t)QUAD_PACK_GREEN(leftGreen)
							+ (uint16_t)(((uint32_t)leftBlue & 0xf800) >> 11);
						destRow++;
						pairCount = (width - 1) >> 1;
					}

					while (pairCount != 0)
					{
						uint32_t pixel = QUAD_PACK_RED(leftRed) + QUAD_PACK_GREEN(leftGreen) + ((uint32_t)leftBlue >> 11);
						*(uint32_t*)destRow = MAKELONG(pixel, pixel);
						destRow += 2;
						leftRed += stepRed;
						leftGreen += stepGreen;
						leftBlue += stepBlue;
						pairCount--;
					}

					if (endX & 1)
					{
						*destRow = (uint16_t)QUAD_PACK_RED(leftRed) + (uint16_t)QUAD_PACK_GREEN(leftGreen)
							+ (uint16_t)(((uint32_t)leftBlue & 0xf800) >> 11);
					}
				}

				edgeA.position.x += edgeAXStep;
				edgeARed += edgeARedStep;
				edgeBRed += edgeBRedStep;
				edgeB.position.x += edgeBXStep;
				edgeAGreen += edgeAGreenStep;
				edgeBGreen += edgeBGreenStep;
				edgeABlue += edgeABlueStep;
				destination += g_primarySurfacePitch;
				edgeBBlue += edgeBBlueStep;
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
				edgeAXStep = ((*edgeAEnd)->position.x - (*edgeAStart)->position.x) / (float)edgeARemaining;
				edgeARedStep = (edgeAEndRed - edgeARed) / edgeARemaining;
				edgeAGreenStep = (edgeAEndGreen - edgeAGreen) / edgeARemaining;
				edgeABlueStep = (edgeAEndBlue - edgeABlue) / edgeARemaining;
			}
			if (edgeBRemaining > 0 && completedEdge != 0)
			{
				edgeBXStep = ((*edgeBEnd)->position.x - (*edgeBStart)->position.x) / (float)edgeBRemaining;
				edgeBRedStep = (edgeBEndRed - edgeBRed) / edgeBRemaining;
				edgeBGreenStep = (edgeBEndGreen - edgeBGreen) / edgeBRemaining;
				edgeBBlueStep = (edgeBEndBlue - edgeBBlue) / edgeBRemaining;
			}
			completedEdge = nextCompletedEdge;
			rows = nextRows;
			if (edgeAStart >= edgeBStart)
				return;
		}
