#pragma once

#include "Numerics.h"
#include <directx6/ddraw.h>
#include <directx6/d3d.h>

namespace Nu3D
{
	namespace ViewMatrix
	{
		Matrix3x3I16* MultiplyFixed(const Matrix3x3I16* left, const Matrix3x3I16* right, Matrix3x3I16* output);
	}

	namespace Math
	{
		extern Vector3F g_matrixScale;

		Matrix3x3I16* EulerToRotationMatrix(const Vector3I16* angles, Matrix3x3I16* output);
		int32_t Cross2D(Point2I16 point1, Point2I16 point2, Point2I16 point3);
		int32_t NormalizeToFixedPoint(const Vector3I* input, Vector3I* output);
		int32_t NormalizeToFixedPoint16(const Vector3I16* input, Vector3I16* output);
		int32_t CartesianToFixedAngle(int32_t x, int32_t y);
		int16_t PointIntersectsTriangle(int32_t pointX,
			int32_t pointY,
			int32_t pointZ,
			int32_t edge1X,
			int32_t edge1Y,
			int32_t edge1Z,
			int32_t edge2X,
			int32_t edge2Y,
			int32_t edge2Z,
			const Vector3I16* normal,
			int32_t tolerance);
		void CrossProduct3D(const Vector3I* left, const Vector3I* right, Vector3I* output);
		int32_t IsWithinDistanceXZ(const Vector3I* left, const Vector3I* right, int32_t radius);
		float Abs(float value);
		void TransformPointByMatrix(Vector3F* result, const Vector3F* sourceVector, const D3DMATRIX* matrix);
		void ProjectPoint(Vector3F* result, const Vector3F* sourceVector, const D3DMATRIX* matrix);
		void TransformPointPerspective(Vector3F* result, const Vector3F* sourceVector, const D3DMATRIX* matrix);
		void BuildIdentityMatrix(D3DMATRIX* matrix);
		void ApplyRotateXFromLut(D3DMATRIX* matrix, int32_t trigOffset);
		void SetRotationYFromU16AngleLUT(D3DMATRIX* matrix, int32_t trigOffset);
		void ScaleMatrixByVector(D3DMATRIX* matrix, Vector3F* vector);
		void MatrixApplyScale(D3DMATRIX* matrix, const Vector3F* scale);
		void MatrixRotatePitch(D3DMATRIX* matrix, int32_t trigOffset);
		void MatrixRotateYaw(D3DMATRIX* matrix, int32_t trigOffset);
		void MatrixRotateRoll(D3DMATRIX* matrix, int32_t trigOffset);
		void RotateZFromLut(D3DMATRIX* matrix, int32_t trigOffset);
		void RotateYFromLut(D3DMATRIX* matrix, int32_t trigOffset);
		void PostRotateXFromLut(D3DMATRIX* matrix, int32_t trigOffset);
		void AddWorldSpaceTransform(D3DMATRIX* matrix, Vector3F* offset);
		void GetPositionVector(D3DMATRIX* matrix, Vector3F* output);
		void GetRightVector(D3DMATRIX* matrix, Vector3F* output);
		void GetUpVector(D3DMATRIX* matrix, Vector3F* output);
		void GetForwardVector(D3DMATRIX* matrix, Vector3F* output);
		void TransformVectorByMatrix(Vector3F* result, Vector3F* sourceVector, D3DMATRIX* matrix);
		void VertexAdd(Vector3F* result, Vector3F* v1, Vector3F* v2);
		void CalculatePlaneFromTriangle(Vector3F* point1, Vector3F* point2, Vector3F* point3, Plane* plane);
		void VertexSubtract(Vector3F* result, Vector3F* v1, Vector3F* v2);
		void ScaleVector(Vector3F* result, Vector3F* vector, float scale);
		void VertexCrossProduct(Vector3F* result, Vector3F* v1, Vector3F* v2);
		float VectorNormalize(Vector3F* output, Vector3F* vector);
		void MultiplyMatrix3x4(D3DMATRIX* output, const D3DMATRIX* left, const D3DMATRIX* right);
		void FullMatrixMultiply(D3DMATRIX* output, const D3DMATRIX* left, const D3DMATRIX* right);
		void CreateInverseMatrix(D3DMATRIX* output, const D3DMATRIX* input);
		void InvertAffineMatrix(D3DMATRIX* output, const D3DMATRIX* input);
		void BuildMatrixFromDirection(D3DMATRIX* matrix, Vector3F* direction);
		void ScaleMatrix(D3DMATRIX* matrix);
		float GetSignedDistanceToPlane(const Vector3F* point, const Plane* plane);
	}
}
