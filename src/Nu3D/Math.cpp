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

		// FUNCTION: TOY2 0x00450C70 [MATCHED]
		Matrix3x3I16* SetRotationXYZ(const Vector3I16* angles, Matrix3x3I16* output)
		{
			int32_t sineX = Numerics::g_sinCosLUT[angles->x & 0xFFF] / 4;
			int32_t cosineX = Numerics::g_sinCosLUT[(angles->x + 0x400) & 0xFFF] / 4;
			int32_t sineY = Numerics::g_sinCosLUT[angles->y & 0xFFF] / 4;
			int32_t cosineY = Numerics::g_sinCosLUT[(angles->y + 0x400) & 0xFFF] / 4;
			int32_t sineZ = Numerics::g_sinCosLUT[angles->z & 0xFFF] / 4;
			int32_t cosineZ = Numerics::g_sinCosLUT[(angles->z + 0x400) & 0xFFF] / 4;

			output->m00 = (int16_t)(cosineZ * cosineY / 4096);
			output->m01 = (int16_t)-(sineZ * cosineY / 4096);
			output->m02 = (int16_t)sineY;

			int32_t sineXsineY = sineY * sineX / 4096;
			output->m10 = (int16_t)((sineXsineY * cosineZ + sineZ * cosineX) / 4096);
			output->m11 = (int16_t)((cosineZ * cosineX - sineXsineY * sineZ) / 4096);
			output->m12 = (int16_t)-(cosineY * sineX / 4096);

			int32_t cosineXsineY = sineY * cosineX / 4096;
			output->m20 = (int16_t)((sineZ * sineX - cosineXsineY * cosineZ) / 4096);
			output->m21 = (int16_t)((cosineXsineY * sineZ + cosineZ * sineX) / 4096);
			output->m22 = (int16_t)(cosineY * cosineX / 4096);

			return output;
		}

		// FUNCTION: TOY2 0x00451F80 [MATCHED]
		int32_t Cross2D(int32_t packedPoint1, int32_t packedPoint2, int32_t packedPoint3)
		{
			return ((int16_t)(packedPoint1 >> 16) - (int16_t)(packedPoint2 >> 16)) * ((int16_t)packedPoint3 - (int16_t)packedPoint2)
				- ((int16_t)(packedPoint3 >> 16) - (int16_t)(packedPoint2 >> 16)) * ((int16_t)packedPoint1 - (int16_t)packedPoint2);
		}

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

		static __forceinline int16_t InsideLine(int32_t x, int32_t y, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
		{ return (x - x0) * (y1 - y0) + (y - y0) * (x0 - x1) >= 0; }

		// FUNCTION: TOY2 0x00480AE0 [PROVISIONAL]
		int16_t InsidePolLines(int32_t pointX,
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
			int16_t normalY = normal->y;
			int16_t normalX = normal->x;
			int32_t absNormalY = abs((int32_t)normalY);
			int32_t absNormalX = abs((int32_t)normalX);

			if (absNormalY >= absNormalX && absNormalY >= abs((int32_t)normal->z))
			{
				if (normalY < 0)
				{
					if (InsideLine(pointX, pointZ, 0, 0, edge1X, edge1Z))
					{
						if (InsideLine(pointX, pointZ, edge2X, edge2Z, 0, 0))
						{
							if (InsideLine(pointX, pointZ, edge1X, edge1Z, edge2X, edge2Z))
								return 1;
						}
					}
				}
				else if (InsideLine(pointX, pointZ, edge1X, edge1Z, 0, 0))
				{
					if (InsideLine(pointX, pointZ, 0, 0, edge2X, edge2Z))
					{
						if (InsideLine(pointX, pointZ, edge2X, edge2Z, edge1X, edge1Z))
							return 1;
					}
				}
			}
			else if (absNormalX >= absNormalY && absNormalX >= abs((int32_t)normal->z))
			{
				if (normalX < 0)
				{
					if (InsideLine(pointY, pointZ, edge1Y, edge1Z, 0, 0))
					{
						if (InsideLine(pointY, pointZ, 0, 0, edge2Y, edge2Z))
						{
							if (InsideLine(pointY, pointZ, edge2Y, edge2Z, edge1Y, edge1Z))
								return 1;
						}
					}
				}
				else if (InsideLine(pointY, pointZ, 0, 0, edge1Y, edge1Z))
				{
					if (InsideLine(pointY, pointZ, edge2Y, edge2Z, 0, 0))
					{
						if (InsideLine(pointY, pointZ, edge1Y, edge1Z, edge2Y, edge2Z))
							return 1;
					}
				}
			}
			else
			{
				if (normal->z < 0)
				{
					if (InsideLine(pointY, pointX, 0, 0, edge1Y, edge1X) && InsideLine(pointY, pointX, edge2Y, edge2X, 0, 0)
						&& InsideLine(pointY, pointX, edge1Y, edge1X, edge2Y, edge2X))
						return 1;
				}
				else if (InsideLine(pointY, pointX, edge1Y, edge1X, 0, 0) && InsideLine(pointY, pointX, 0, 0, edge2Y, edge2X)
					&& InsideLine(pointY, pointX, edge2Y, edge2X, edge1Y, edge1X))
				{
					return 1;
				}
			}

			int32_t projection = pointX * edge2X + pointY * edge2Y + pointZ * edge2Z;
			int32_t lengthSquared = edge2X * edge2X + edge2Y * edge2Y + edge2Z * edge2Z;
			if (projection >= 0 && projection <= lengthSquared)
			{
				if ((lengthSquared & 0xFFFFFF00) == 0)
					lengthSquared = 0x100;
				int32_t fraction = (projection << 6) / (lengthSquared >> 8);
				int32_t deltaX = pointX - ((fraction * edge2X) >> 14);
				int32_t deltaY = pointY - ((fraction * edge2Y) >> 14);
				int32_t deltaZ = pointZ - ((fraction * edge2Z) >> 14);
				int32_t radius = tolerance / 31;
				if (deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ < 2 * radius * radius)
					return 1;
			}

			projection = pointX * edge1X + pointY * edge1Y + pointZ * edge1Z;
			lengthSquared = edge1X * edge1X + edge1Y * edge1Y + edge1Z * edge1Z;
			if (projection >= 0 && projection <= lengthSquared)
			{
				if ((lengthSquared & 0xFFFFFF00) == 0)
					lengthSquared = 0x100;
				int32_t fraction = (projection << 6) / (lengthSquared >> 8);
				int32_t deltaX = pointX - ((fraction * edge1X) >> 14);
				int32_t deltaY = pointY - ((fraction * edge1Y) >> 14);
				int32_t deltaZ = pointZ - ((fraction * edge1Z) >> 14);
				int32_t radius = tolerance / 31;
				if (deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ < 2 * radius * radius)
					return 1;
			}

			int32_t finalEdgeX = edge1X - edge2X;
			int32_t finalEdgeY = edge1Y - edge2Y;
			int32_t finalEdgeZ = edge1Z - edge2Z;
			int32_t relativeX = pointX - edge2X;
			int32_t relativeY = pointY - edge2Y;
			int32_t relativeZ = pointZ - edge2Z;
			projection = relativeX * finalEdgeX + relativeY * finalEdgeY + relativeZ * finalEdgeZ;
			lengthSquared = finalEdgeX * finalEdgeX + finalEdgeY * finalEdgeY + finalEdgeZ * finalEdgeZ;
			if (projection >= 0 && projection <= lengthSquared)
			{
				if ((lengthSquared & 0xFFFFFF00) == 0)
					lengthSquared = 0x100;
				int32_t fraction = (projection << 6) / (lengthSquared >> 8);
				int32_t deltaX = relativeX - ((fraction * finalEdgeX) >> 14);
				int32_t deltaY = relativeY - ((fraction * finalEdgeY) >> 14);
				int32_t deltaZ = relativeZ - ((fraction * finalEdgeZ) >> 14);
				int32_t radius = tolerance / 31;
				if (deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ < 2 * radius * radius)
					return 1;
			}

			int32_t radius = tolerance / 31;
			int32_t radiusSquared = 2 * radius * radius;
			if (pointX * pointX + pointY * pointY + pointZ * pointZ < radiusSquared
				|| (pointX - edge1X) * (pointX - edge1X) + (pointY - edge1Y) * (pointY - edge1Y) + (pointZ - edge1Z) * (pointZ - edge1Z) < radiusSquared
				|| relativeX * relativeX + relativeY * relativeY + relativeZ * relativeZ < radiusSquared)
			{
				return 1;
			}

			return 0;
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
