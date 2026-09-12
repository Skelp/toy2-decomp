// The shared tail of RasterizeTriangleSpans and RasterizeQuadSpans: the walk
// down both edges of the primitive that calls g_spanRasterizer once per
// scanline. Each includer first orders its own vertices into vertexOrder and
// then includes this walk.

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
				g_spanRasterizer(&edgeA, &edgeB, (uint16_t*)destination, texData, edgeARed, edgeAGreen, edgeABlue, edgeBRed, edgeBGreen, edgeBBlue);
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
