#pragma once

#include "Numerics.h"
#include <directx6/ddraw.h>
#include <directx6/d3d.h>

namespace NGNLoader
{
	struct NGNImage;
}

namespace Nu3D
{
	namespace Link
	{
		struct DynamicScaler
		{
			DynamicScaler* next;
			DynamicScaler* prev;
			DynamicScaler** cellHead;
			Vector3F translation;
			Vector3I rotation;
			Vector3F scale;
			D3DMATRIX transformMatrix;
			int32_t shapeId;
			Vector3F boundsCenterWorld;
			int32_t packedAreaData;
			int32_t areaIndex;
			int32_t packedFlags;
			int32_t flags;
			int32_t gscaleType;
		};

		struct Linker
		{
			Vector3F currentPos;
			Vector3F targetPos;
			Vector3I currentRot;
			Vector3F currentScale;
			DynamicScaler* dynamicScaler;
			D3DMATRIX transformMatrix;
		};

		void SetScaleFromFixedOffsets(int32_t linkId, int32_t x, int32_t y, int32_t z);
		void RebuildMatrixAndCommit(Linker* link);
		void SetRotationRelative8bit(int32_t linkId, int32_t x, int32_t y, int32_t z);
		void SetRotationAbsolute8bit(int32_t linkId, int32_t x, int32_t y, int32_t z);
		void GetRotation8Bit(int32_t linkId, Vector3I* output);
		void TransformVectorInt3x3(int32_t linkId, Vector3I* vector);
		void SetPositionRawAndCommit(int32_t linkId, int32_t x, int32_t y, int32_t z);
		void GetCurrentPosFixed(int32_t linkId, Vector3I* output);
		void GetTargetPosFixed(int32_t linkId, Vector3I* output);
		void SnapToOtherLinkUsingScale(int32_t linkId, int32_t targetLinkId);
		void CopyShapeId(int32_t destinationLinkId, int32_t sourceLinkId);

		STATIC_ASSERT(sizeof(DynamicScaler) == 0x94);
		STATIC_ASSERT(sizeof(Linker) == 0x74);
	}

	namespace Spatial
	{
		struct CellLocation
		{
			int32_t x;
			int32_t z;
			int32_t scalerType;
		};

		int32_t ComputeCellFromXZ(CellLocation* output, float x, float z, int32_t scalerType,
			NGNLoader::NGNImage* image);
		Link::DynamicScaler** GetCellByPos(const CellLocation* location, NGNLoader::NGNImage* image);
		void UnlinkScalerThenReinsert(Link::DynamicScaler* scaler, NGNLoader::NGNImage* image);
		void InsertScalerAtComputedCell(Link::DynamicScaler* scaler, int32_t scalerType,
			NGNLoader::NGNImage* image);

		STATIC_ASSERT(sizeof(CellLocation) == 0xC);
	}
}
