#include "Nu3D/Math.h"
#include <MATH.H>

// FUNCTION: TOY2 0x004A92C0 [MATCHED]
float Vector3F::DotProduct(const Vector3F* left, const Vector3F* right) { return left->x * right->x + left->y * right->y + left->z * right->z; }

// FUNCTION: TOY2 0x004A92E0 [MATCHED]
float Vector3F::Length(const Vector3F* vector) { return (float)sqrt(vector->x * vector->x + vector->y * vector->y + vector->z * vector->z); }

namespace Nu3D
{
	namespace ViewMatrix
	{
		// FUNCTION: TOY2 0x00450AC0 [MATCHED]
		Matrix3x3I16* MultiplyFixed(const Matrix3x3I16* left, const Matrix3x3I16* right, Matrix3x3I16* output)
		{
			Matrix3x3I16 result;
			result.m00 = (int16_t)((left->m00 * right->m00 + left->m01 * right->m10 + left->m02 * right->m20) >> 12);
			result.m01 = (int16_t)((left->m00 * right->m01 + left->m01 * right->m11 + left->m02 * right->m21) >> 12);
			result.m02 = (int16_t)((left->m00 * right->m02 + left->m01 * right->m12 + left->m02 * right->m22) >> 12);
			result.m10 = (int16_t)((left->m10 * right->m00 + left->m11 * right->m10 + left->m12 * right->m20) >> 12);
			result.m11 = (int16_t)((left->m10 * right->m01 + left->m11 * right->m11 + left->m12 * right->m21) >> 12);
			result.m12 = (int16_t)((left->m10 * right->m02 + left->m11 * right->m12 + left->m12 * right->m22) >> 12);
			result.m20 = (int16_t)((left->m20 * right->m00 + left->m21 * right->m10 + left->m22 * right->m20) >> 12);
			result.m21 = (int16_t)((left->m20 * right->m01 + left->m21 * right->m11 + left->m22 * right->m21) >> 12);
			result.m22 = (int16_t)((left->m20 * right->m02 + left->m21 * right->m12 + left->m22 * right->m22) >> 12);
			*output = result;
			return output;
		}
	}

	namespace Math
	{
		// NOTE: Methods in this class are kind of critical to get correct, since minor changes in how
		// math is calculated could cause weeks of debugging effort to fix. So I take extra care ensuring
		// that most, if not all of these methods are instruction matched from the moment they are implemented.

		// GLOBAL: TOY2 0x004DDA48
		D3DMATRIX g_identityMatrix = { 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0 };

		// GLOBAL: TOY2 0x00505548
		D3DMATRIX g_matrixMultiplyResult;

		// GLOBAL: TOY2 0x00508898
		Vector3F g_matrixScale = { 1.0f, 1.0f, 1.0f };

		static __forceinline int32_t ShiftFixedTowardZero(int32_t value, int32_t bits) { return (value + ((value >> 31) & ((1 << bits) - 1))) >> bits; }

		static __forceinline int32_t MultiplyFixed12(int32_t left, int32_t right) { return ShiftFixedTowardZero(left * right, 12); }

		static int32_t Cross2D32(const PointI& point1, const PointI& point2, const PointI& point3)
		{ return (point1.y - point2.y) * (point3.x - point2.x) - (point3.y - point2.y) * (point1.x - point2.x); }

		static int32_t PointNearLineSegment(const Vector3I* point, const Vector3I* edge, int32_t tolerance)
		{
			int32_t projection = point->x * edge->x + point->y * edge->y + point->z * edge->z;
			int32_t lengthSquared = edge->x * edge->x + edge->y * edge->y + edge->z * edge->z;

			if (projection < 0 || projection > lengthSquared)
				return 0;

			int32_t divisor = lengthSquared;
			if ((divisor & 0xFFFFFF00) == 0)
				divisor = 0x100;

			int32_t fraction = (projection << 6) / (divisor >> 8);
			int32_t deltaX = point->x - ((fraction * edge->x) >> 14);
			int32_t deltaY = point->y - ((fraction * edge->y) >> 14);
			int32_t deltaZ = point->z - ((fraction * edge->z) >> 14);
			int32_t radius = tolerance / 31;

			return deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ < 2 * radius * radius;
		}

		// FUNCTION: TOY2 0x00450C70
		Matrix3x3I16* EulerToRotationMatrix(const Vector3I16* angles, Matrix3x3I16* output)
		{
			int16_t sineXRaw = Numerics::g_fixedTrigLUT[angles->x & 0xFFF];
			int32_t cosineX = ShiftFixedTowardZero(Numerics::g_fixedTrigLUT[(angles->x + 0x400) & 0xFFF], 2);
			int32_t sineY = ShiftFixedTowardZero(Numerics::g_fixedTrigLUT[angles->y & 0xFFF], 2);
			int32_t cosineZ = ShiftFixedTowardZero(Numerics::g_fixedTrigLUT[(angles->z + 0x400) & 0xFFF], 2);
			int32_t cosineY = ShiftFixedTowardZero(Numerics::g_fixedTrigLUT[(angles->y + 0x400) & 0xFFF], 2);
			int32_t sineZ = ShiftFixedTowardZero(Numerics::g_fixedTrigLUT[angles->z & 0xFFF], 2);

			output->m00 = (int16_t)MultiplyFixed12(cosineZ, cosineY);
			output->m01 = (int16_t)-MultiplyFixed12(sineZ, cosineY);
			int32_t sineX = ShiftFixedTowardZero(sineXRaw, 2);
			output->m02 = (int16_t)sineY;

			int32_t sineXsineY = MultiplyFixed12(sineY, sineX);
			output->m10 = (int16_t)ShiftFixedTowardZero(sineXsineY * cosineZ + sineZ * cosineX, 12);
			output->m11 = (int16_t)ShiftFixedTowardZero(cosineZ * cosineX - sineXsineY * sineZ, 12);
			output->m12 = (int16_t)-MultiplyFixed12(cosineY, sineX);

			int32_t cosineXsineY = MultiplyFixed12(sineY, cosineX);
			output->m20 = (int16_t)ShiftFixedTowardZero(sineZ * sineX - cosineXsineY * cosineZ, 12);
			output->m21 = (int16_t)ShiftFixedTowardZero(cosineXsineY * sineZ + cosineZ * sineX, 12);
			output->m22 = (int16_t)MultiplyFixed12(cosineY, cosineX);

			return output;
		}

		// FUNCTION: TOY2 0x00451F80
		int32_t Cross2D(Point2I16 point1, Point2I16 point2, Point2I16 point3)
		{ return (point1.y - point2.y) * (point3.x - point2.x) - (point3.y - point2.y) * (point1.x - point2.x); }

		// FUNCTION: TOY2 0x00451FD0
		int32_t NormalizeToFixedPoint(const Vector3I* input, Vector3I* output)
		{
			float x = (float)input->x;
			float y = (float)input->y;
			float z = (float)input->z;
			float magnitude = (float)sqrt(x * x + y * y + z * z);
			output->x = (int32_t)((float)input->x * 4096.0f / magnitude);
			output->y = (int32_t)((float)input->y * 4096.0f / magnitude);
			output->z = (int32_t)((float)input->z * 4096.0f / magnitude);
			return 0;
		}

		// FUNCTION: TOY2 0x00452040 [MATCHED]
		int32_t NormalizeToFixedPoint16(const Vector3I16* input, Vector3I16* output)
		{
			int16_t x = input->x;
			int32_t z = input->z;
			int32_t y = input->y;
			int32_t magnitude = (int32_t)sqrt((float)((int32_t)x * x + y * y + z * z));

			if (x != 0)
				output->x = (int16_t)(((int32_t)x << 12) / magnitude);
			if (input->y != 0)
				output->y = (int16_t)(((int32_t)input->y << 12) / magnitude);
			if (input->z != 0)
				output->z = (int16_t)(((int32_t)input->z << 12) / magnitude);

			return 0;
		}

		// FUNCTION: TOY2 0x004520D0
		int32_t CartesianToFixedAngle(int32_t x, int32_t y)
		{ return (int32_t)(atan2(x * 0.000244140625 * 6.283185308, y * 0.000244140625 * 6.283185308) * 0.15915494307111402 * 4096.0); }

		// FUNCTION: TOY2 0x00480AE0
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
			int32_t tolerance)
		{
			PointI point;
			PointI edge1;
			PointI edge2;
			int32_t normalDirection;

			int32_t absNormalX = normal->x < 0 ? -normal->x : normal->x;
			int32_t absNormalY = normal->y < 0 ? -normal->y : normal->y;
			int32_t absNormalZ = normal->z < 0 ? -normal->z : normal->z;

			if (absNormalX >= absNormalY && absNormalX >= absNormalZ)
			{
				point.x = pointY;
				point.y = pointZ;
				edge1.x = edge1Y;
				edge1.y = edge1Z;
				edge2.x = edge2Y;
				edge2.y = edge2Z;
				normalDirection = normal->x;
			}
			else if (absNormalY >= absNormalZ)
			{
				point.x = pointX;
				point.y = pointZ;
				edge1.x = edge1X;
				edge1.y = edge1Z;
				edge2.x = edge2X;
				edge2.y = edge2Z;
				normalDirection = -normal->y;
			}
			else
			{
				point.x = pointX;
				point.y = pointY;
				edge1.x = edge1X;
				edge1.y = edge1Y;
				edge2.x = edge2X;
				edge2.y = edge2Y;
				normalDirection = normal->z;
			}

			PointI origin = { 0, 0 };
			int32_t side1 = Cross2D32(origin, edge1, point);
			int32_t side2 = Cross2D32(edge1, edge2, point);
			int32_t side3 = Cross2D32(edge2, origin, point);
			if (normalDirection < 0 ? side1 >= 0 && side2 >= 0 && side3 >= 0 : side1 <= 0 && side2 <= 0 && side3 <= 0)
				return 1;

			Vector3I point3D = { pointX, pointY, pointZ };
			Vector3I edge1_3D = { edge1X, edge1Y, edge1Z };
			Vector3I edge2_3D = { edge2X, edge2Y, edge2Z };
			if (PointNearLineSegment(&point3D, &edge1_3D, tolerance) || PointNearLineSegment(&point3D, &edge2_3D, tolerance))
				return 1;

			Vector3I finalEdge = { edge1X - edge2X, edge1Y - edge2Y, edge1Z - edge2Z };
			Vector3I pointFromEdge2 = { pointX - edge2X, pointY - edge2Y, pointZ - edge2Z };
			return (int16_t)PointNearLineSegment(&pointFromEdge2, &finalEdge, tolerance);
		}

		// FUNCTION: TOY2 0x00490B90
		void CrossProduct3D(const Vector3I* left, const Vector3I* right, Vector3I* output)
		{
			int32_t z = right->y * left->x - right->x * left->y;
			int32_t y = right->x * left->z - right->z * left->x;
			int32_t x = right->z * left->y - right->y * left->z;

			output->x = x;
			output->y = y;
			output->z = z;
		}

		// FUNCTION: TOY2 0x0049F3C0 [MATCHED]
		int32_t IsWithinDistanceXZ(const Vector3I* left, const Vector3I* right, int32_t radius)
		{
			int32_t deltaX = (left->x - right->x) >> 8;
			int32_t deltaZ = (left->z - right->z) >> 8;
			return deltaX * deltaX + deltaZ * deltaZ < radius * radius;
		}

		// FUNCTION: TOY2 0x004A8B30
		float Abs(float value)
		{
			float result;
			uint32_t bits = *reinterpret_cast<const uint32_t*>(&value);
			bits &= 0x7FFFFFFF;
			*reinterpret_cast<uint32_t*>(&result) = bits;
			return result;
		}

		// FUNCTION: TOY2 0x004A8BF0 [MATCHED]
		void TransformPointByMatrix(Vector3F* result, const Vector3F* sourceVector, const D3DMATRIX* matrix)
		{
			float transformedY = matrix->_32 * sourceVector->z + matrix->_12 * sourceVector->x + matrix->_22 * sourceVector->y + matrix->_42;
			float transformedZ = matrix->_33 * sourceVector->z + matrix->_13 * sourceVector->x + matrix->_23 * sourceVector->y + matrix->_43;
			float transformedX = matrix->_31 * sourceVector->z + matrix->_21 * sourceVector->y;

			result->x = transformedX + sourceVector->x * matrix->_11 + matrix->_41;
			result->y = transformedY;
			result->z = transformedZ;
		}

		// FUNCTION: TOY2 0x004A8C60
		void ProjectPoint(Vector3F* result, const Vector3F* sourceVector, const D3DMATRIX* matrix)
		{
			float inverseW = 1.0f / (matrix->_34 * sourceVector->z + matrix->_14 * sourceVector->x + matrix->_24 * sourceVector->y + matrix->_44);
			float transformedY = matrix->_32 * sourceVector->z + matrix->_12 * sourceVector->x + matrix->_22 * sourceVector->y + matrix->_42;
			float transformedZ = matrix->_33 * sourceVector->z + matrix->_13 * sourceVector->x + matrix->_23 * sourceVector->y + matrix->_43;
			float transformedX = matrix->_31 * sourceVector->z + matrix->_21 * sourceVector->y;

			result->x = (transformedX + sourceVector->x * matrix->_11 + matrix->_41) * inverseW;
			result->y = transformedY * inverseW;
			result->z = transformedZ * inverseW;
		}

		// FUNCTION: TOY2 0x004A8D80
		void TransformPointPerspective(Vector3F* result, const Vector3F* sourceVector, const D3DMATRIX* matrix)
		{
			float inverseW = 1.0f / (matrix->_34 * sourceVector->z + matrix->_14 * sourceVector->x + matrix->_24 * sourceVector->y);
			float transformedY = matrix->_32 * sourceVector->z + matrix->_12 * sourceVector->x + matrix->_22 * sourceVector->y;
			float transformedZ = matrix->_33 * sourceVector->z + matrix->_13 * sourceVector->x + matrix->_23 * sourceVector->y;
			float transformedX = matrix->_31 * sourceVector->z + matrix->_21 * sourceVector->y;

			result->x = (transformedX + sourceVector->x * matrix->_11) * inverseW;
			result->y = transformedY * inverseW;
			result->z = transformedZ * inverseW;
		}

		// FUNCTION: TOY2 0x004A9400 [MATCHED]
		void BuildIdentityMatrix(D3DMATRIX* matrix) { memcpy(matrix, &g_identityMatrix, sizeof(D3DMATRIX)); }

		// FUNCTION: TOY2 0x004A94C0
		void ApplyRotateXFromLut(D3DMATRIX* matrix, int32_t trigOffset)
		{
			float sine = Numerics::g_trigLUT[trigOffset & 0xFFFF];
			float cosine = Numerics::g_trigLUT[(trigOffset + 0x4000) & 0xFFFF];

			matrix->_11 = 1.0f;
			matrix->_12 = 0.0f;
			matrix->_13 = 0.0f;
			matrix->_14 = 0.0f;
			matrix->_21 = 0.0f;
			matrix->_22 = cosine;
			matrix->_23 = sine;
			matrix->_24 = 0.0f;
			matrix->_31 = 0.0f;
			matrix->_32 = -sine;
			matrix->_33 = cosine;
			matrix->_34 = 0.0f;
			matrix->_41 = 0.0f;
			matrix->_42 = 0.0f;
			matrix->_43 = 0.0f;
			matrix->_44 = 1.0f;
		}

		// FUNCTION: TOY2 0x004A9530
		void SetRotationYFromU16AngleLUT(D3DMATRIX* matrix, int32_t trigOffset)
		{
			float cosine = Numerics::g_trigLUT[(trigOffset + 0x4000) & 0xFFFF];
			float sine = Numerics::g_trigLUT[trigOffset & 0xFFFF];

			matrix->_11 = cosine;
			matrix->_12 = 0.0f;
			matrix->_13 = -sine;
			matrix->_14 = 0.0f;
			matrix->_21 = 0.0f;
			matrix->_22 = 1.0f;
			matrix->_23 = 0.0f;
			matrix->_24 = 0.0f;
			matrix->_31 = sine;
			matrix->_32 = 0.0f;
			matrix->_33 = cosine;
			matrix->_34 = 0.0f;
			matrix->_41 = 0.0f;
			matrix->_42 = 0.0f;
			matrix->_43 = 0.0f;
			matrix->_44 = 1.0f;
		}

		// FUNCTION: TOY2 0x004A9860 [MATCHED]
		void ScaleMatrixByVector(D3DMATRIX* matrix, Vector3F* vector)
		{
			matrix->_11 = matrix->_11 * vector->x;
			matrix->_12 = vector->y * matrix->_12;
			matrix->_13 = vector->z * matrix->_13;
			matrix->_21 = matrix->_21 * vector->x;
			matrix->_22 = matrix->_22 * vector->y;
			matrix->_23 = matrix->_23 * vector->z;
			matrix->_31 = matrix->_31 * vector->x;
			matrix->_32 = matrix->_32 * vector->y;
			matrix->_33 = matrix->_33 * vector->z;
			matrix->_41 = matrix->_41 * vector->x;
			matrix->_42 = matrix->_42 * vector->y;
			matrix->_43 = matrix->_43 * vector->z;
		}

		// FUNCTION: TOY2 0x004A98D0 [MATCHED]
		void MatrixApplyScale(D3DMATRIX* matrix, const Vector3F* scale)
		{
			matrix->_11 *= scale->x;
			matrix->_12 *= scale->x;
			matrix->_13 *= scale->x;
			matrix->_21 *= scale->y;
			matrix->_22 *= scale->y;
			matrix->_23 *= scale->y;
			matrix->_31 *= scale->z;
			matrix->_32 *= scale->z;
			matrix->_33 *= scale->z;
		}

		// FUNCTION: TOY2 0x004A9D50 [MATCHED]
		void RotateZFromLut(D3DMATRIX* matrix, int32_t trigOffset)
		{
			float cos = Numerics::g_trigLUT[(trigOffset + 0x4000) & 0xFFFF];
			float sin = Numerics::g_trigLUT[trigOffset & 0xFFFF];

			float r1 = matrix->_11;
			float r0 = r1 * cos - sin * matrix->_12;
			float r2 = matrix->_21;
			float r3 = matrix->_31;

			matrix->_11 = r0;

			float r4 = matrix->_41;
			matrix->_12 = r1 * sin + cos * matrix->_12;

			matrix->_21 = r2 * cos - sin * matrix->_22;
			matrix->_22 = cos * matrix->_22 + r2 * sin;

			matrix->_31 = r3 * cos - sin * matrix->_32;
			matrix->_32 = cos * matrix->_32 + r3 * sin;

			matrix->_41 = r4 * cos - sin * matrix->_42;
			matrix->_42 = cos * matrix->_42 + r4 * sin;
		}

		// FUNCTION: TOY2 0x004A9B40 [MATCHED]
		void RotateYFromLut(D3DMATRIX* matrix, int32_t trigOffset)
		{
			float cos = Numerics::g_trigLUT[(trigOffset + 0x4000) & 0xFFFF];
			float sin = Numerics::g_trigLUT[trigOffset & 0xFFFF];

			float _11 = matrix->_11;

			float _21 = matrix->_21;
			float _31 = matrix->_31;

			matrix->_11 = matrix->_13 * sin + _11 * cos;

			float _41 = matrix->_41;
			matrix->_13 = matrix->_13 * cos - _11 * sin;

			matrix->_21 = matrix->_23 * sin + _21 * cos;
			matrix->_23 = matrix->_23 * cos - _21 * sin;

			matrix->_31 = matrix->_33 * sin + _31 * cos;
			matrix->_33 = matrix->_33 * cos - _31 * sin;

			matrix->_41 = matrix->_43 * sin + _41 * cos;
			matrix->_43 = matrix->_43 * cos - _41 * sin;
		}

		// FUNCTION: TOY2 0x004A9930 [MATCHED]
		void PostRotateXFromLut(D3DMATRIX* matrix, int32_t trigOffset)
		{
			float cosAngle = Numerics::g_trigLUT[(trigOffset + 0x4000) & 0xFFFF];
			float sinAngle = Numerics::g_trigLUT[trigOffset & 0xFFFF];

			float temp12 = matrix->_12;
			float temp13 = temp12 * cosAngle - sinAngle * matrix->_13;

			float temp22 = matrix->_22;
			float temp32 = matrix->_32;

			matrix->_12 = temp13;

			float temp42 = matrix->_42;
			matrix->_13 = cosAngle * matrix->_13 + temp12 * sinAngle;

			matrix->_22 = temp22 * cosAngle - sinAngle * matrix->_23;
			matrix->_23 = cosAngle * matrix->_23 + temp22 * sinAngle;

			matrix->_32 = temp32 * cosAngle - sinAngle * matrix->_33;
			matrix->_33 = cosAngle * matrix->_33 + temp32 * sinAngle;

			matrix->_42 = temp42 * cosAngle - sinAngle * matrix->_43;
			matrix->_43 = temp42 * sinAngle + cosAngle * matrix->_43;
		}

		// FUNCTION: TOY2 0x004A9AA0
		void MatrixRotatePitch(D3DMATRIX* matrix, int32_t trigOffset)
		{
			float cosine = Numerics::g_trigLUT[(trigOffset + 0x4000) & 0xFFFF];
			float sine = Numerics::g_trigLUT[trigOffset & 0xFFFF];
			float m21 = matrix->_21;
			float m22 = matrix->_22;
			float m23 = matrix->_23;

			matrix->_21 = m21 * cosine + sine * matrix->_31;
			matrix->_22 = m22 * cosine + sine * matrix->_32;
			matrix->_23 = m23 * cosine + sine * matrix->_33;
			matrix->_31 = cosine * matrix->_31 - m21 * sine;
			matrix->_32 = cosine * matrix->_32 - m22 * sine;
			matrix->_33 = cosine * matrix->_33 - m23 * sine;
		}

		// FUNCTION: TOY2 0x004A9CB0
		void MatrixRotateYaw(D3DMATRIX* matrix, int32_t trigOffset)
		{
			float cosine = Numerics::g_trigLUT[(trigOffset + 0x4000) & 0xFFFF];
			float sine = Numerics::g_trigLUT[trigOffset & 0xFFFF];
			float m11 = matrix->_11;
			float m12 = matrix->_12;
			float m13 = matrix->_13;

			matrix->_11 = m11 * cosine - sine * matrix->_31;
			matrix->_12 = m12 * cosine - sine * matrix->_32;
			matrix->_13 = m13 * cosine - sine * matrix->_33;
			matrix->_31 = cosine * matrix->_31 + m11 * sine;
			matrix->_32 = cosine * matrix->_32 + m12 * sine;
			matrix->_33 = cosine * matrix->_33 + m13 * sine;
		}

		// FUNCTION: TOY2 0x004A9EC0 [MATCHED]
		void MatrixRotateRoll(D3DMATRIX* matrix, int32_t trigOffset)
		{
			float cosine = Numerics::g_trigLUT[(trigOffset + 0x4000) & 0xFFFF];
			float sine = Numerics::g_trigLUT[trigOffset & 0xFFFF];
			float m11 = matrix->_11;
			float m12 = matrix->_12;
			float m13 = matrix->_13;

			matrix->_11 = m11 * cosine + sine * matrix->_21;
			matrix->_12 = m12 * cosine + sine * matrix->_22;
			matrix->_13 = m13 * cosine + sine * matrix->_23;
			matrix->_21 = cosine * matrix->_21 - m11 * sine;
			matrix->_22 = cosine * matrix->_22 - m12 * sine;
			matrix->_23 = cosine * matrix->_23 - m13 * sine;
		}

		// FUNCTION: TOY2 0x004A9750 [MATCHED]
		void AddWorldSpaceTransform(D3DMATRIX* matrix, Vector3F* offset)
		{
			matrix->_41 = offset->x + matrix->_41;
			matrix->_42 = offset->y + matrix->_42;
			matrix->_43 = offset->z + matrix->_43;
		}

		// FUNCTION: TOY2 0x004A97E0
		void GetPositionVector(D3DMATRIX* matrix, Vector3F* output)
		{
			output->x = matrix->_41;
			output->y = matrix->_42;
			output->z = matrix->_43;
		}

		// FUNCTION: TOY2 0x004A9800 [MATCHED]
		void GetRightVector(D3DMATRIX* matrix, Vector3F* output)
		{
			output->x = matrix->_11;
			output->y = matrix->_12;
			output->z = matrix->_13;
		}

		// FUNCTION: TOY2 0x004A9820 [MATCHED]
		void GetUpVector(D3DMATRIX* matrix, Vector3F* output)
		{
			output->x = matrix->_21;
			output->y = matrix->_22;
			output->z = matrix->_23;
		}

		// FUNCTION: TOY2 0x004A9840
		void GetForwardVector(D3DMATRIX* matrix, Vector3F* output)
		{
			output->x = matrix->_31;
			output->y = matrix->_32;
			output->z = matrix->_33;
		}

		// FUNCTION: TOY2 0x004A8D20 [MATCHED]
		void TransformVectorByMatrix(Vector3F* result, Vector3F* sourceVector, D3DMATRIX* matrix)
		{
			float transformedY = matrix->_32 * sourceVector->z + matrix->_12 * sourceVector->x + matrix->_22 * sourceVector->y;
			float tempZ = matrix->_33 * sourceVector->z + matrix->_13 * sourceVector->x + matrix->_23 * sourceVector->y;

			float transformedX = matrix->_31 * sourceVector->z + matrix->_21 * sourceVector->y;
			float transformedZ = sourceVector->x * matrix->_11;

			result->x = transformedX + transformedZ;
			result->y = transformedY;
			result->z = tempZ;
		}

		// FUNCTION: TOY2 0x004A91B0
		void VertexAdd(Vector3F* result, Vector3F* v1, Vector3F* v2)
		{
			result->x = v1->x + v2->x;
			result->y = v1->y + v2->y;
			result->z = v1->z + v2->z;
		}

		// FUNCTION: TOY2 0x004A91E0 [MATCHED]
		void VertexSubtract(Vector3F* result, Vector3F* v1, Vector3F* v2)
		{
			result->x = v1->x - v2->x;
			result->y = v1->y - v2->y;
			result->z = v1->z - v2->z;
		}

		// FUNCTION: TOY2 0x004A9210
		void ScaleVector(Vector3F* result, Vector3F* vector, float scale)
		{
			result->x = scale * vector->x;
			result->y = scale * vector->y;
			result->z = scale * vector->z;
		}

		// FUNCTION: TOY2 0x004A9270 [MATCHED]
		void VertexCrossProduct(Vector3F* result, Vector3F* v1, Vector3F* v2)
		{
			float y = v2->x * v1->z - v2->z * v1->x;
			float z = v2->y * v1->x - v2->x * v1->y;

			result->x = v2->z * v1->y - v2->y * v1->z;
			result->y = y;
			result->z = z;
		}

		// FUNCTION: TOY2 0x004A9340
		float VectorNormalize(Vector3F* output, Vector3F* vector)
		{
			float lengthSquared = vector->x * vector->x + vector->y * vector->y + vector->z * vector->z;
			float magnitude = sqrt(lengthSquared);
			float scale = 1.0f / magnitude;

			output->x = scale * vector->x;
			output->y = scale * vector->y;
			output->z = scale * vector->z;

			return magnitude;
		}

		// FUNCTION: TOY2 0x004A9F60
		void MultiplyMatrix3x4(D3DMATRIX* output, const D3DMATRIX* left, const D3DMATRIX* right)
		{
			g_matrixMultiplyResult._11 = left->_13 * right->_31 + left->_11 * right->_11 + left->_12 * right->_21;
			g_matrixMultiplyResult._12 = left->_13 * right->_32 + left->_11 * right->_12 + left->_12 * right->_22;
			g_matrixMultiplyResult._13 = left->_13 * right->_33 + left->_11 * right->_13 + left->_12 * right->_23;
			g_matrixMultiplyResult._14 = 0.0f;
			g_matrixMultiplyResult._21 = left->_23 * right->_31 + left->_21 * right->_11 + left->_22 * right->_21;
			g_matrixMultiplyResult._22 = left->_23 * right->_32 + left->_21 * right->_12 + left->_22 * right->_22;
			g_matrixMultiplyResult._23 = left->_23 * right->_33 + left->_21 * right->_13 + left->_22 * right->_23;
			g_matrixMultiplyResult._24 = 0.0f;
			g_matrixMultiplyResult._31 = left->_33 * right->_31 + left->_31 * right->_11 + left->_32 * right->_21;
			g_matrixMultiplyResult._32 = left->_33 * right->_32 + left->_31 * right->_12 + left->_32 * right->_22;
			g_matrixMultiplyResult._33 = left->_33 * right->_33 + left->_31 * right->_13 + left->_32 * right->_23;
			g_matrixMultiplyResult._34 = 0.0f;
			g_matrixMultiplyResult._41 = left->_43 * right->_31 + left->_41 * right->_11 + left->_42 * right->_21 + right->_41;
			g_matrixMultiplyResult._42 = left->_43 * right->_32 + left->_41 * right->_12 + left->_42 * right->_22 + right->_42;
			g_matrixMultiplyResult._43 = left->_43 * right->_33 + left->_41 * right->_13 + left->_42 * right->_23 + right->_43;
			g_matrixMultiplyResult._44 = 1.0f;

			memcpy(output, &g_matrixMultiplyResult, sizeof(D3DMATRIX));
		}

		// FUNCTION: TOY2 0x004AA100
		void FullMatrixMultiply(D3DMATRIX* output, const D3DMATRIX* left, const D3DMATRIX* right)
		{
			g_matrixMultiplyResult._11 = left->_14 * right->_41 + left->_13 * right->_31 + left->_11 * right->_11 + left->_12 * right->_21;
			g_matrixMultiplyResult._12 = left->_14 * right->_42 + left->_13 * right->_32 + left->_11 * right->_12 + left->_12 * right->_22;
			g_matrixMultiplyResult._13 = left->_14 * right->_43 + left->_13 * right->_33 + left->_11 * right->_13 + left->_12 * right->_23;
			g_matrixMultiplyResult._14 = left->_14 * right->_44 + left->_13 * right->_34 + left->_11 * right->_14 + left->_12 * right->_24;
			g_matrixMultiplyResult._21 = left->_24 * right->_41 + left->_23 * right->_31 + left->_21 * right->_11 + left->_22 * right->_21;
			g_matrixMultiplyResult._22 = left->_24 * right->_42 + left->_23 * right->_32 + left->_21 * right->_12 + left->_22 * right->_22;
			g_matrixMultiplyResult._23 = left->_24 * right->_43 + left->_23 * right->_33 + left->_21 * right->_13 + left->_22 * right->_23;
			g_matrixMultiplyResult._24 = left->_24 * right->_44 + left->_23 * right->_34 + left->_21 * right->_14 + left->_22 * right->_24;
			g_matrixMultiplyResult._31 = left->_34 * right->_41 + left->_33 * right->_31 + left->_31 * right->_11 + left->_32 * right->_21;
			g_matrixMultiplyResult._32 = left->_34 * right->_42 + left->_33 * right->_32 + left->_31 * right->_12 + left->_32 * right->_22;
			g_matrixMultiplyResult._33 = left->_34 * right->_43 + left->_33 * right->_33 + left->_31 * right->_13 + left->_32 * right->_23;
			g_matrixMultiplyResult._34 = left->_34 * right->_44 + left->_33 * right->_34 + left->_31 * right->_14 + left->_32 * right->_24;
			g_matrixMultiplyResult._41 = left->_44 * right->_41 + left->_43 * right->_31 + left->_41 * right->_11 + left->_42 * right->_21;
			g_matrixMultiplyResult._42 = left->_44 * right->_42 + left->_43 * right->_32 + left->_41 * right->_12 + left->_42 * right->_22;
			g_matrixMultiplyResult._43 = left->_44 * right->_43 + left->_43 * right->_33 + left->_41 * right->_13 + left->_42 * right->_23;
			g_matrixMultiplyResult._44 = left->_44 * right->_44 + left->_43 * right->_34 + left->_41 * right->_14 + left->_42 * right->_24;

			output->_11 = g_matrixMultiplyResult._11;
			output->_12 = g_matrixMultiplyResult._12;
			output->_13 = g_matrixMultiplyResult._13;
			output->_14 = g_matrixMultiplyResult._14;
			output->_21 = g_matrixMultiplyResult._21;
			output->_22 = g_matrixMultiplyResult._22;
			output->_23 = g_matrixMultiplyResult._23;
			output->_24 = g_matrixMultiplyResult._24;
			output->_31 = g_matrixMultiplyResult._31;
			output->_32 = g_matrixMultiplyResult._32;
			output->_33 = g_matrixMultiplyResult._33;
			output->_34 = g_matrixMultiplyResult._34;
			output->_41 = g_matrixMultiplyResult._41;
			output->_42 = g_matrixMultiplyResult._42;
			output->_43 = g_matrixMultiplyResult._43;
			output->_44 = g_matrixMultiplyResult._44;
		}

		// FUNCTION: TOY2 0x004AA520 [MATCHED]
		void CreateInverseMatrix(D3DMATRIX* output, const D3DMATRIX* input)
		{
			float translationX = -input->_41;
			float translationY = -input->_42;
			float translationZ = -input->_43;

			float transposeValue = input->_12;
			output->_12 = input->_21;
			output->_21 = transposeValue;
			transposeValue = input->_13;
			output->_13 = input->_31;
			output->_31 = transposeValue;
			transposeValue = input->_23;
			output->_23 = input->_32;
			output->_32 = transposeValue;
			output->_11 = input->_11;
			output->_22 = input->_22;
			output->_33 = input->_33;
			output->_34 = 0.0f;
			output->_24 = 0.0f;
			output->_14 = 0.0f;
			output->_44 = 1.0f;
			output->_41 = translationZ * output->_31 + translationX * output->_11 + translationY * output->_21;
			output->_42 = translationZ * output->_32 + translationX * output->_12 + translationY * output->_22;
			output->_43 = translationZ * output->_33 + translationX * output->_13 + translationY * output->_23;
		}

		// FUNCTION: TOY2 0x004AA620
		void InvertAffineMatrix(D3DMATRIX* output, const D3DMATRIX* input)
		{
			D3DMATRIX source = *input;
			BuildIdentityMatrix(output);

			float* row0 = &source._11;
			float* row1 = &source._21;
			float* row2 = &source._31;

			if (Abs(row2[0]) < Abs(row1[0]))
			{
				if (Abs(row0[0]) < Abs(row1[0]))
				{
					row0 = &source._21;
					row1 = &source._11;
					output->_11 = 0.0f;
					output->_12 = 1.0f;
					output->_21 = 1.0f;
					output->_22 = 0.0f;
				}
			}
			else if (Abs(row0[0]) < Abs(row2[0]))
			{
				row0 = &source._31;
				row2 = &source._11;
				output->_11 = 0.0f;
				output->_13 = 1.0f;
				output->_31 = 1.0f;
				output->_33 = 0.0f;
			}

			float scale = 1.0f / row0[0];
			row0[1] *= scale;
			row0[2] *= scale;
			output->_11 *= scale;
			output->_12 *= scale;
			output->_13 *= scale;

			float factor1 = row1[0];
			float factor2 = row2[0];
			float factor3 = source._41;
			row1[1] -= factor1 * row0[1];
			row2[1] -= factor2 * row0[1];
			source._42 -= factor3 * row0[1];
			row1[2] -= factor1 * row0[2];
			row2[2] -= factor2 * row0[2];
			source._43 -= factor3 * row0[2];
			output->_21 -= factor1 * output->_11;
			output->_22 -= factor1 * output->_12;
			output->_23 -= factor1 * output->_13;
			output->_31 -= factor2 * output->_11;
			output->_32 -= factor2 * output->_12;
			output->_33 -= factor2 * output->_13;
			output->_41 -= factor3 * output->_11;
			output->_42 -= factor3 * output->_12;
			output->_43 -= factor3 * output->_13;

			if (Abs(row1[1]) < Abs(row2[1]))
			{
				float* rowSwap = row1;
				row1 = row2;
				row2 = rowSwap;

				float value = output->_21;
				output->_21 = output->_31;
				output->_31 = value;
				value = output->_22;
				output->_22 = output->_32;
				output->_32 = value;
				value = output->_23;
				output->_23 = output->_33;
				output->_33 = value;
			}

			scale = 1.0f / row1[1];
			row1[2] *= scale;
			output->_21 *= scale;
			output->_22 *= scale;
			output->_23 *= scale;

			factor1 = row0[1];
			factor2 = row2[1];
			factor3 = source._42;
			row0[2] -= factor1 * row1[2];
			row2[2] -= factor2 * row1[2];
			source._43 -= factor3 * row1[2];
			output->_11 -= factor1 * output->_21;
			output->_12 -= factor1 * output->_22;
			output->_13 -= factor1 * output->_23;
			output->_31 -= factor2 * output->_21;
			output->_32 -= factor2 * output->_22;
			output->_33 -= factor2 * output->_23;
			output->_41 -= factor3 * output->_21;
			output->_42 -= factor3 * output->_22;
			output->_43 -= factor3 * output->_23;

			scale = 1.0f / row2[2];
			output->_31 *= scale;
			output->_32 *= scale;
			output->_33 *= scale;

			factor1 = row0[2];
			factor2 = row1[2];
			factor3 = source._43;
			output->_11 -= factor1 * output->_31;
			output->_12 -= factor1 * output->_32;
			output->_13 -= factor1 * output->_33;
			output->_21 -= factor2 * output->_31;
			output->_22 -= factor2 * output->_32;
			output->_23 -= factor2 * output->_33;
			output->_41 -= factor3 * output->_31;
			output->_42 -= factor3 * output->_32;
			output->_43 -= factor3 * output->_33;
		}

		// FUNCTION: TOY2 0x004AAC40
		void BuildMatrixFromDirection(D3DMATRIX* matrix, Vector3F* direction)
		{
			Vector3F* right = (Vector3F*)&matrix->_11;
			Vector3F* up = (Vector3F*)&matrix->_21;
			Vector3F* forward = (Vector3F*)&matrix->_31;

			float rightLengthSquared = right->x * right->x + right->y * right->y + right->z * right->z;
			float upLengthSquared = up->x * up->x + up->y * up->y + up->z * up->z;
			float forwardLengthSquared = forward->x * forward->x + forward->y * forward->y + forward->z * forward->z;
			float directionLengthSquared = direction->x * direction->x + direction->y * direction->y + direction->z * direction->z;
			float scale = 0.0f;

			if (directionLengthSquared != 0.0f)
				scale = (float)sqrt(forwardLengthSquared / directionLengthSquared);

			forward->x = direction->x * scale;
			forward->y = direction->y * scale;
			forward->z = direction->z * scale;

			float alignment = (float)fabs(up->x * forward->x + up->y * forward->y + up->z * forward->z);

			if (alignment <= 0.8660253882408142f)
			{
				VertexCrossProduct(right, up, forward);

				float newLengthSquared = right->x * right->x + right->y * right->y + right->z * right->z;
				scale = 0.0f;
				if (newLengthSquared != 0.0f)
					scale = (float)sqrt(rightLengthSquared / newLengthSquared);
				right->x *= scale;
				right->y *= scale;
				right->z *= scale;

				VertexCrossProduct(up, forward, right);
				newLengthSquared = up->x * up->x + up->y * up->y + up->z * up->z;
				scale = 0.0f;
				if (newLengthSquared != 0.0f)
					scale = (float)sqrt(upLengthSquared / newLengthSquared);
				up->x *= scale;
				up->y *= scale;
				up->z *= scale;
			}
			else
			{
				VertexCrossProduct(up, forward, right);

				float newLengthSquared = up->x * up->x + up->y * up->y + up->z * up->z;
				scale = 0.0f;
				if (newLengthSquared != 0.0f)
					scale = (float)sqrt(upLengthSquared / newLengthSquared);
				up->x *= scale;
				up->y *= scale;
				up->z *= scale;

				VertexCrossProduct(right, up, forward);
				newLengthSquared = right->x * right->x + right->y * right->y + right->z * right->z;
				scale = 0.0f;
				if (newLengthSquared != 0.0f)
					scale = (float)sqrt(rightLengthSquared / newLengthSquared);
				right->x *= scale;
				right->y *= scale;
				right->z *= scale;
			}
		}

		// FUNCTION: TOY2 0x004BB920 [MATCHED]
		void ScaleMatrix(D3DMATRIX* matrix)
		{
			matrix->_11 *= g_matrixScale.x;
			matrix->_12 *= g_matrixScale.x;
			matrix->_13 *= g_matrixScale.x;
			matrix->_14 *= g_matrixScale.x;
			matrix->_21 *= g_matrixScale.y;
			matrix->_22 *= g_matrixScale.y;
			matrix->_23 *= g_matrixScale.y;
			matrix->_24 *= g_matrixScale.y;
			matrix->_31 *= g_matrixScale.z;
			matrix->_32 *= g_matrixScale.z;
			matrix->_33 *= g_matrixScale.z;
			matrix->_34 *= g_matrixScale.z;
		}

		// FUNCTION: TOY2 0x004DA850 [MATCHED]
		void CalculatePlaneFromTriangle(Vector3F* point1, Vector3F* point2, Vector3F* point3, Plane* plane)
		{
			Vector3F edge1;
			Vector3F edge2;

			VertexSubtract(&edge2, point2, point1);
			VertexSubtract(&edge1, point3, point1);

			Vector3F normal;

			VertexCrossProduct(&normal, &edge2, &edge1);
			VectorNormalize(&plane->normal, &normal);

			plane->distance = -(point1->z * plane->normal.z + point1->y * plane->normal.y + point1->x * plane->normal.x);
		}

		// FUNCTION: TOY2 0x004DB040 [MATCHED]
		float GetSignedDistanceToPlane(const Vector3F* point, const Plane* plane) { return Vector3F::DotProduct(point, &plane->normal) + plane->distance; }
	}
}
