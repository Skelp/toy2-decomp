#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Nu3D/Primitive.h"
#include "NGNLoader/NGNLoader.h"

namespace Nu3D
{
	namespace Spatial
	{
		// FUNCTION: TOY2 0x004C30D0
		int32_t ComputeCellFromXZ(CellLocation* output, float x, float z, int32_t scalerType,
			NGNLoader::NGNImage* image)
		{
			int32_t cellX = (int32_t)((x - image->worldMinX) / image->cellWidthInWorldUnits);
			int32_t cellZ = (int32_t)((z - image->worldMinZ) / image->cellHeightInWorldUnits);

			if (cellX < 0 || cellX >= image->gridWidth || cellZ < 0 || cellZ >= image->gridHeight)
				return 0;

			output->x = cellX;
			output->z = cellZ;
			output->scalerType = scalerType;
			return 1;
		}

		// FUNCTION: TOY2 0x004C3130
		Link::DynamicScaler** GetCellByPos(const CellLocation* location, NGNLoader::NGNImage* image)
		{
			if (location->x < 0 || location->x >= image->gridWidth
				|| location->z < 0 || location->z >= image->gridHeight)
			{
				return 0;
			}

			return &image->spacialGrid[location->scalerType]
				[location->z * image->gridWidth + location->x];
		}

		// FUNCTION: TOY2 0x004C3170
		void UnlinkScalerThenReinsert(Link::DynamicScaler* scaler, NGNLoader::NGNImage* image)
		{
			if (scaler->cellHead)
			{
				if (scaler->prev)
					scaler->prev->next = scaler->next;
				else
					*scaler->cellHead = scaler->next;

				if (scaler->next)
					scaler->next->prev = scaler->prev;

				scaler->cellHead = 0;
			}

			InsertScalerAtComputedCell(scaler, scaler->gscaleType, image);
		}

		// FUNCTION: TOY2 0x004C31C0
		void InsertScalerAtComputedCell(Link::DynamicScaler* scaler, int32_t scalerType,
			NGNLoader::NGNImage* image)
		{
			Vector3F center;
			Math::VertexAdd(&center, &scaler->translation, &scaler->boundsCenterWorld);

			CellLocation location;
			if (ComputeCellFromXZ(&location, center.x, center.z, scalerType, image))
			{
				Link::DynamicScaler** cellHead = GetCellByPos(&location, image);
				scaler->next = *cellHead;
				if (scaler->next)
					scaler->next->prev = scaler;

				scaler->prev = 0;
				scaler->cellHead = cellHead;
				*cellHead = scaler;
			}
			else
			{
				scaler->cellHead = 0;
			}
		}
	}

	namespace Link
	{
		// FUNCTION: TOY2 0x004CCB20
		void SetScaleFromFixedOffsets(int32_t linkId, int32_t x, int32_t y, int32_t z)
		{
			NGNLoader::NGNImage* image = NGNLoader::g_ngnImage;
			if (!image || !image->links || linkId >= image->maxLinkId)
				return;

			Linker* link = &image->links[linkId];
			DynamicScaler* scaler = link->dynamicScaler;
			if (!scaler)
				return;

			if (x == 0 && y == 0 && z == 0)
			{
				scaler->flags |= 1;
				return;
			}

			scaler->flags &= ~1;
			const float fixedScale = 1.0f / 4096.0f;
			link->currentScale.x = (float)x * scaler->scale.x * fixedScale;
			link->currentScale.y = (float)y * scaler->scale.y * fixedScale;
			link->currentScale.z = (float)z * scaler->scale.z * fixedScale;
			RebuildMatrixAndCommit(link);
		}

		// FUNCTION: TOY2 0x004CCBE0
		void RebuildMatrixAndCommit(Linker* link)
		{
			DynamicScaler* scaler = link->dynamicScaler;
			D3DMATRIX* transform = &scaler->transformMatrix;

			Math::BuildIdentityMatrix(transform);
			Math::ScaleMatrixByVector(transform, &link->currentScale);
			Math::RotateZFromLut(transform, link->currentRot.z);
			Math::RotateYFromLut(transform, link->currentRot.y);
			Math::PostRotateXFromLut(transform, link->currentRot.x);
			Math::AddWorldSpaceTransform(transform, &link->currentPos);

			Primitive* primitive = NGNLoader::g_ngnImage->primitives[scaler->shapeId];
			Math::TransformVectorByMatrix(&scaler->boundsCenterWorld, &primitive->boundsCenter, transform);
			scaler->translation = link->currentPos;
			Spatial::UnlinkScalerThenReinsert(scaler, NGNLoader::g_ngnImage);
		}

		// FUNCTION: TOY2 0x004CCC70
		void SetRotationRelative8bit(int32_t linkId, int32_t x, int32_t y, int32_t z)
		{
			NGNLoader::NGNImage* image = NGNLoader::g_ngnImage;
			if (!image || !image->links || linkId >= image->maxLinkId)
				return;

			Linker* link = &image->links[linkId];
			if (!link->dynamicScaler)
				return;

			link->currentRot.x = link->dynamicScaler->rotation.x + (x << 4);
			link->currentRot.y = link->dynamicScaler->rotation.y + (y << 4);
			link->currentRot.z = link->dynamicScaler->rotation.z + (z << 4);
			RebuildMatrixAndCommit(link);
		}

		// FUNCTION: TOY2 0x004CCCE0
		void SetRotationAbsolute8bit(int32_t linkId, int32_t x, int32_t y, int32_t z)
		{
			NGNLoader::NGNImage* image = NGNLoader::g_ngnImage;
			if (!image || !image->links || linkId >= image->maxLinkId)
				return;

			Linker* link = &image->links[linkId];
			if (!link->dynamicScaler)
				return;

			link->currentRot.x = x << 4;
			link->currentRot.y = y << 4;
			link->currentRot.z = z << 4;
			RebuildMatrixAndCommit(link);
		}

		// FUNCTION: TOY2 0x004CCD40
		void GetRotation8Bit(int32_t linkId, Vector3I* output)
		{
			NGNLoader::NGNImage* image = NGNLoader::g_ngnImage;
			if (!image || !image->links || linkId >= image->maxLinkId)
				return;

			Linker* link = &image->links[linkId];
			if (!link->dynamicScaler)
				return;

			output->x = link->currentRot.x >> 4;
			output->y = link->currentRot.y >> 4;
			output->z = link->currentRot.z >> 4;
		}

		// FUNCTION: TOY2 0x004CCDA0
		void TransformVectorInt3x3(int32_t linkId, Vector3I* vector)
		{
			NGNLoader::NGNImage* image = NGNLoader::g_ngnImage;
			if (!image || !image->links || linkId >= image->maxLinkId)
				return;

			Linker* link = &image->links[linkId];
			if (!link->dynamicScaler)
				return;

			Vector3F input;
			input.x = (float)vector->x;
			input.y = (float)vector->y;
			input.z = (float)vector->z;

			Vector3F transformed;
			Math::TransformVectorByMatrix(&transformed, &input, &link->dynamicScaler->transformMatrix);
			vector->x = (int32_t)transformed.x;
			vector->y = (int32_t)transformed.y;
			vector->z = (int32_t)transformed.z;
		}

		// FUNCTION: TOY2 0x004CCE30
		void SetPositionRawAndCommit(int32_t linkId, int32_t x, int32_t y, int32_t z)
		{
			NGNLoader::NGNImage* image = NGNLoader::g_ngnImage;
			if (!image || !image->links || linkId >= image->maxLinkId)
				return;

			Linker* link = &image->links[linkId];
			DynamicScaler* scaler = link->dynamicScaler;
			if (!scaler)
				return;

			if (scaler->gscaleType)
			{
				x <<= 2;
				y <<= 2;
				z <<= 2;
			}

			link->currentPos.x = (float)x;
			link->currentPos.y = (float)y;
			link->currentPos.z = (float)z;
			scaler->transformMatrix._41 = link->currentPos.x;
			scaler->transformMatrix._42 = link->currentPos.y;
			scaler->transformMatrix._43 = link->currentPos.z;
			scaler->translation = link->currentPos;
			Spatial::UnlinkScalerThenReinsert(scaler, image);
		}

		// FUNCTION: TOY2 0x004CCEF0
		void GetCurrentPosFixed(int32_t linkId, Vector3I* output)
		{
			NGNLoader::NGNImage* image = NGNLoader::g_ngnImage;
			if (!image || !image->links || linkId >= image->maxLinkId)
				return;

			Linker* link = &image->links[linkId];
			if (!link->dynamicScaler)
				return;

			int32_t shift = link->dynamicScaler->gscaleType ? 3 : 5;
			output->x = (int32_t)link->currentPos.x << shift;
			output->y = (int32_t)link->currentPos.y << shift;
			output->z = (int32_t)link->currentPos.z << shift;
		}

		// FUNCTION: TOY2 0x004CCF70
		void GetTargetPosFixed(int32_t linkId, Vector3I* output)
		{
			NGNLoader::NGNImage* image = NGNLoader::g_ngnImage;
			if (!image || !image->links || linkId >= image->maxLinkId)
				return;

			Linker* link = &image->links[linkId];
			if (!link->dynamicScaler)
				return;

			int32_t shift = link->dynamicScaler->gscaleType ? 3 : 5;
			output->x = (int32_t)link->targetPos.x << shift;
			output->y = (int32_t)link->targetPos.y << shift;
			output->z = (int32_t)link->targetPos.z << shift;
		}

		// FUNCTION: TOY2 0x004CCFF0
		void SnapToOtherLinkUsingScale(int32_t linkId, int32_t targetLinkId)
		{
			NGNLoader::NGNImage* image = NGNLoader::g_ngnImage;
			if (!image || !image->links || linkId >= image->maxLinkId || targetLinkId >= image->maxLinkId)
				return;

			Linker* link = &image->links[linkId];
			Linker* target = &image->links[targetLinkId];
			if (!target->dynamicScaler || !link->dynamicScaler)
				return;

			link->currentPos.x = (link->currentPos.x - target->currentPos.x) * target->currentScale.x
				+ target->currentPos.x;
			link->currentPos.y = (link->currentPos.y - target->currentPos.y) * target->currentScale.y
				+ target->currentPos.y;
			link->currentPos.z = (link->currentPos.z - target->currentPos.z) * target->currentScale.z
				+ target->currentPos.z;

			DynamicScaler* scaler = link->dynamicScaler;
			scaler->translation = link->currentPos;
			scaler->transformMatrix._41 = link->currentPos.x;
			scaler->transformMatrix._42 = link->currentPos.y;
			scaler->transformMatrix._43 = link->currentPos.z;
			Spatial::UnlinkScalerThenReinsert(scaler, image);
		}

		// FUNCTION: TOY2 0x004CD0C0
		void CopyShapeId(int32_t destinationLinkId, int32_t sourceLinkId)
		{
			NGNLoader::NGNImage* image = NGNLoader::g_ngnImage;
			if (!image || !image->links || destinationLinkId >= image->maxLinkId
				|| sourceLinkId >= image->maxLinkId)
			{
				return;
			}

			image->links[destinationLinkId].dynamicScaler->shapeId
				= image->links[sourceLinkId].dynamicScaler->shapeId;
		}
	}
}
