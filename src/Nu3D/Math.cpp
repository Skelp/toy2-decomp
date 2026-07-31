#include "Nu3D/Math.h"
#include <MATH.H>

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
		// NOTE: These routines use fixed-point idioms (integer LUT lookups, manual
		// round-toward-zero shifts, IEEE 754 bit manipulation) rather than CRT calls.
		// Match the retail structure where the evidence supports it, but correctness of
		// the calculation matters more than incidental instruction differences; do not
		// distort otherwise plausible source solely to raise a similarity score.

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

		// FUNCTION: TOY2 0x00450C70 [PROVISIONAL]
		Matrix3x3I16* SetRotationXYZ(const Vector3I16* angles, Matrix3x3I16* output)
		{
			int32_t cosineX = Numerics::g_sinCosLUT[(angles->x + 0x400) & 0xFFF];
			cosineX = ShiftFixedTowardZero(cosineX, 2);
			int32_t sineX = Numerics::g_sinCosLUT[angles->x & 0xFFF];
			sineX += (sineX >> 31) & 3;

			int32_t cosineY = Numerics::g_sinCosLUT[(angles->y + 0x400) & 0xFFF];
			cosineY += (cosineY >> 31) & 3;
			int32_t sineY = Numerics::g_sinCosLUT[angles->y & 0xFFF];
			sineY = ShiftFixedTowardZero(sineY, 2);

			int32_t cosineZ = Numerics::g_sinCosLUT[(angles->z + 0x400) & 0xFFF];
			cosineZ = ShiftFixedTowardZero(cosineZ, 2);
			int32_t sineZ = Numerics::g_sinCosLUT[angles->z & 0xFFF];
			sineZ += (sineZ >> 31) & 3;

			cosineY >>= 2;
			sineZ >>= 2;

			output->m00 = (int16_t)MultiplyFixed12(cosineZ, cosineY);
			output->m01 = (int16_t)-MultiplyFixed12(sineZ, cosineY);
			output->m02 = (int16_t)sineY;

			sineX >>= 2;
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

		// FUNCTION: TOY2 0x00451F80 [PROVISIONAL]
		int32_t Cross2D(Point2I16 point1, Point2I16 point2, Point2I16 point3)
		{ return (point1.y - point2.y) * (point3.x - point2.x) - (point3.y - point2.y) * (point1.x - point2.x); }

		// FUNCTION: TOY2 0x00451FD0 [MATCHED]
		int32_t NormalizeToFixedPoint(const Vector3I* input, Vector3I* output)
		{
			float z = (float)input->z;
			float y = (float)input->y;
			float x = (float)input->x;
			float magnitude = (float)sqrt(x * x + y * y + z * z);
			output->x = (int32_t)(x * 4096.0f / magnitude);
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

		// FUNCTION: TOY2 0x004520D0 [MATCHED]
		int32_t CartesianToFixedAngle(int32_t x, int32_t y)
		{
			double xRadians = x * 6.283185308;
			xRadians *= 0.000244140625;
			double yRadians = y * 6.283185308;
			yRadians *= 0.000244140625;
			double fixedAngle = atan2(xRadians, yRadians) * 4096.0;
			return (int32_t)(fixedAngle * 0.15915494307111402);
		}

		// FUNCTION: TOY2 0x00480AE0 [PROVISIONAL]
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

		// FUNCTION: TOY2 0x00490B90 [PROVISIONAL]
		void CrossProduct3D(const Vector3I* left, const Vector3I* right, Vector3I* output)
		{
			int32_t z = left->x * right->y - left->y * right->x;
			int32_t y = left->z * right->x - left->x * right->z;
			int32_t x = left->y * right->z - left->z * right->y;

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

		// FUNCTION: TOY2 0x0049F400 [PROVISIONAL]
		int32_t IsWithinDistance(const Vector3I* left, const Vector3I* right, int32_t radius)
		{
			int32_t deltaX = (left->x - right->x) >> 8;
			int32_t deltaY = (left->y - right->y) >> 8;
			int32_t deltaZ = (left->z - right->z) >> 8;
			int32_t distSq = deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;
			if (distSq < radius * radius)
				return distSq + 1;
			return 0;
		}

		// FUNCTION: TOY2 0x004A8B30 [MATCHED]
		float Abs(float value)
		{
			uint32_t bits = *(uint32_t*)&value;
			bits &= 0x7FFFFFFF;
			return *(float*)&bits;
		}

	}
}
