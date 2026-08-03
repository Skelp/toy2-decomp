#include "Nu3D/Math.h"
#include "Nu3D/NuQuat.h"
#include <MATH.H>

namespace Nu3D
{

	namespace Math
	{
		// GLOBAL: TOY2 0x004DDA48
		D3DMATRIX g_identityMatrix = { 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0 };

		// GLOBAL: TOY2 0x00505548
		D3DMATRIX g_matrixMultiplyResult = { 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0 };

		// GLOBAL: TOY2 0x00508898
		Vector3F g_matrixScale = { 1.0f, 1.0f, 1.0f };

		// FUNCTION: TOY2 0x004A9400 [MATCHED]
		void BuildIdentityMatrix(D3DMATRIX* matrix) { memcpy(matrix, &g_identityMatrix, sizeof(D3DMATRIX)); }

		// FUNCTION: TOY2 0x004A94C0 [MATCHED]
		void ApplyRotateXFromLut(D3DMATRIX* matrix, int32_t trigOffset)
		{
			float cosine = Numerics::g_trigLUT[(trigOffset + 0x4000) & 0xFFFF];
			matrix->_33 = cosine;
			matrix->_22 = cosine;
			float sine = Numerics::g_trigLUT[trigOffset & 0xFFFF];
			matrix->_11 = 1.0f;
			matrix->_23 = sine;
			matrix->_32 = -sine;
			matrix->_43 = 0.0f;
			matrix->_42 = 0.0f;
			matrix->_41 = 0.0f;
			matrix->_24 = 0.0f;
			matrix->_31 = 0.0f;
			matrix->_21 = 0.0f;
			matrix->_14 = 0.0f;
			matrix->_13 = 0.0f;
			matrix->_12 = 0.0f;
			matrix->_44 = 1.0f;
		}

		// FUNCTION: TOY2 0x004A9530 [MATCHED]
		void SetRotationYFromU16AngleLUT(D3DMATRIX* matrix, int32_t trigOffset)
		{
			float cosine = Numerics::g_trigLUT[(trigOffset + 0x4000) & 0xFFFF];
			matrix->_33 = cosine;
			matrix->_11 = cosine;
			float sine = Numerics::g_trigLUT[trigOffset & 0xFFFF];
			matrix->_31 = sine;
			matrix->_13 = -sine;
			matrix->_22 = 1.0f;
			matrix->_43 = 0.0f;
			matrix->_42 = 0.0f;
			matrix->_41 = 0.0f;
			matrix->_24 = 0.0f;
			matrix->_32 = 0.0f;
			matrix->_23 = 0.0f;
			matrix->_14 = 0.0f;
			matrix->_21 = 0.0f;
			matrix->_12 = 0.0f;
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

		// FUNCTION: TOY2 0x004A9D50 [EFFECTIVE]
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

		// FUNCTION: TOY2 0x004A9B40 [EFFECTIVE]
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

		// FUNCTION: TOY2 0x004A9930 [EFFECTIVE]
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

		// FUNCTION: TOY2 0x004A9AA0 [PROVISIONAL]
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

		// FUNCTION: TOY2 0x004A9CB0 [PROVISIONAL]
		void MatrixRotateYaw(D3DMATRIX* matrix, int32_t trigOffset)
		{
			float cosine = Numerics::g_trigLUT[(trigOffset + 0x4000) & 0xFFFF];
			float sine = Numerics::g_trigLUT[trigOffset & 0xFFFF];
			float m11 = matrix->_11;
			float rotated11 = m11 * cosine - sine * matrix->_31;
			float m12 = matrix->_12;
			float m13 = matrix->_13;

			matrix->_11 = rotated11;
			matrix->_12 = m12 * cosine - sine * matrix->_32;
			matrix->_13 = m13 * cosine - sine * matrix->_33;
			matrix->_31 = cosine * matrix->_31 + m11 * sine;
			matrix->_32 = cosine * matrix->_32 + m12 * sine;
			matrix->_33 = cosine * matrix->_33 + m13 * sine;
		}

		// FUNCTION: TOY2 0x004A9EC0 [EFFECTIVE]
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

		// FUNCTION: TOY2 0x004A97E0 [MATCHED]
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

		// FUNCTION: TOY2 0x004A9840 [MATCHED]
		void GetForwardVector(D3DMATRIX* matrix, Vector3F* output)
		{
			output->x = matrix->_31;
			output->y = matrix->_32;
			output->z = matrix->_33;
		}

		// FUNCTION: TOY2 0x004A9F60 [PROVISIONAL]
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

		// FUNCTION: TOY2 0x004AA100 [PROVISIONAL]
		void FullMatrixMultiply(D3DMATRIX* output, const D3DMATRIX* left, const D3DMATRIX* right)
		{
			g_matrixMultiplyResult._11 = right->_21 * left->_12 + left->_13 * right->_31 + left->_14 * right->_41 + left->_11 * right->_11;
			g_matrixMultiplyResult._12 = right->_12 * left->_11 + right->_32 * left->_13 + left->_12 * right->_22 + right->_42 * left->_14;
			g_matrixMultiplyResult._13 = right->_23 * left->_12 + left->_14 * right->_43 + right->_13 * left->_11 + right->_33 * left->_13;
			g_matrixMultiplyResult._14 = right->_24 * left->_12 + left->_11 * right->_14 + left->_13 * right->_34 + right->_44 * left->_14;
			g_matrixMultiplyResult._21 = left->_21 * right->_11 + right->_21 * left->_22 + left->_24 * right->_41 + left->_23 * right->_31;
			g_matrixMultiplyResult._22 = left->_24 * right->_42 + left->_22 * right->_22 + left->_21 * right->_12 + left->_23 * right->_32;
			g_matrixMultiplyResult._23 = left->_21 * right->_13 + right->_23 * left->_22 + left->_23 * right->_33 + left->_24 * right->_43;
			g_matrixMultiplyResult._24 = right->_24 * left->_22 + left->_21 * right->_14 + left->_23 * right->_34 + right->_44 * left->_24;
			g_matrixMultiplyResult._31 = left->_31 * right->_11 + right->_21 * left->_32 + left->_34 * right->_41 + left->_33 * right->_31;
			g_matrixMultiplyResult._32 = left->_34 * right->_42 + left->_32 * right->_22 + left->_31 * right->_12 + left->_33 * right->_32;
			g_matrixMultiplyResult._33 = left->_31 * right->_13 + right->_23 * left->_32 + left->_33 * right->_33 + left->_34 * right->_43;
			g_matrixMultiplyResult._34 = right->_24 * left->_32 + left->_31 * right->_14 + left->_33 * right->_34 + right->_44 * left->_34;
			g_matrixMultiplyResult._41 = left->_41 * right->_11 + right->_21 * left->_42 + left->_44 * right->_41 + left->_43 * right->_31;
			g_matrixMultiplyResult._42 = left->_44 * right->_42 + left->_42 * right->_22 + left->_41 * right->_12 + left->_43 * right->_32;
			g_matrixMultiplyResult._43 = left->_41 * right->_13 + right->_23 * left->_42 + left->_43 * right->_33 + left->_44 * right->_43;
			g_matrixMultiplyResult._44 = right->_24 * left->_42 + left->_41 * right->_14 + left->_43 * right->_34 + right->_44 * left->_44;

			memcpy(output, &g_matrixMultiplyResult, sizeof(D3DMATRIX));
		}

		// FUNCTION: TOY2 0x004AA520 [EFFECTIVE]
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

		// FUNCTION: TOY2 0x004AA620 [PROVISIONAL]
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

			row1[1] -= row1[0] * row0[1];
			row2[1] -= row2[0] * row0[1];
			source._42 -= source._41 * row0[1];
			row1[2] -= row1[0] * row0[2];
			row2[2] -= row2[0] * row0[2];
			source._43 -= source._41 * row0[2];
			output->_21 -= row1[0] * output->_11;
			output->_22 -= row1[0] * output->_12;
			output->_23 -= row1[0] * output->_13;
			output->_31 -= row2[0] * output->_11;
			output->_32 -= row2[0] * output->_12;
			output->_33 -= row2[0] * output->_13;
			output->_41 -= source._41 * output->_11;
			output->_42 -= source._41 * output->_12;
			output->_43 -= source._41 * output->_13;

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

			row0[2] -= row0[1] * row1[2];
			row2[2] -= row2[1] * row1[2];
			source._43 -= source._42 * row1[2];
			output->_11 -= row0[1] * output->_21;
			output->_12 -= row0[1] * output->_22;
			output->_13 -= row0[1] * output->_23;
			output->_31 -= row2[1] * output->_21;
			output->_32 -= row2[1] * output->_22;
			output->_33 -= row2[1] * output->_23;
			output->_41 -= source._42 * output->_21;
			output->_42 -= source._42 * output->_22;
			output->_43 -= source._42 * output->_23;

			scale = 1.0f / row2[2];
			output->_31 *= scale;
			output->_32 *= scale;
			output->_33 *= scale;

			output->_11 -= row0[2] * output->_31;
			output->_12 -= row0[2] * output->_32;
			output->_13 -= row0[2] * output->_33;
			output->_21 -= row1[2] * output->_31;
			output->_22 -= row1[2] * output->_32;
			output->_23 -= row1[2] * output->_33;
			output->_41 -= source._43 * output->_31;
			output->_42 -= source._43 * output->_32;
			output->_43 -= source._43 * output->_33;
		}

		// FUNCTION: TOY2 0x004AAC40 [PROVISIONAL]
		void BuildMatrixFromDirection(D3DMATRIX* matrix, Vector3F* direction)
		{
			Vector3F* right = (Vector3F*)&matrix->_11;
			Vector3F* up = (Vector3F*)&matrix->_21;
			Vector3F* forward = (Vector3F*)&matrix->_31;

			float rightLengthSquared = right->x * right->x + right->y * right->y + right->z * right->z;
			float upLengthSquared = up->x * up->x + up->y * up->y + up->z * up->z;
			float forwardLengthSquared = forward->x * forward->x + forward->y * forward->y + forward->z * forward->z;
			float directionLengthSquared = direction->x * direction->x + direction->y * direction->y + direction->z * direction->z;
			float scale = (float)sqrt(forwardLengthSquared / directionLengthSquared);

			forward->x = direction->x * scale;
			forward->y = direction->y * scale;
			forward->z = direction->z * scale;

			float alignment = Abs(Vector3F::DotProduct(up, forward));

			if (alignment > 0.8660253882408142f)
			{
				VertexCrossProduct(up, forward, right);

				float newLengthSquared = up->x * up->x + up->y * up->y + up->z * up->z;
				scale = (float)sqrt(upLengthSquared / newLengthSquared);
				up->x *= scale;
				up->y *= scale;
				up->z *= scale;

				VertexCrossProduct(right, up, forward);
				newLengthSquared = right->x * right->x + right->y * right->y + right->z * right->z;
				scale = (float)sqrt(rightLengthSquared / newLengthSquared);
				right->x *= scale;
				right->y *= scale;
				right->z *= scale;
			}
			else
			{
				VertexCrossProduct(right, up, forward);

				float newLengthSquared = right->x * right->x + right->y * right->y + right->z * right->z;
				scale = (float)sqrt(rightLengthSquared / newLengthSquared);
				right->x *= scale;
				right->y *= scale;
				right->z *= scale;

				VertexCrossProduct(up, forward, right);
				newLengthSquared = up->x * up->x + up->y * up->y + up->z * up->z;
				scale = (float)sqrt(upLengthSquared / newLengthSquared);
				up->x *= scale;
				up->y *= scale;
				up->z *= scale;
			}
		}

		// FUNCTION: TOY2 0x004AB090 [PROVISIONAL]
		void MatrixToQuaternion(D3DMATRIX* matrix, Quaternion* quaternion)
		{
			float trace = matrix->_22 + matrix->_33 + matrix->_11;
			int32_t nextIndex[3] = { 1, 2, 0 };

			if (trace > 0.0)
			{
				float root = (float)sqrt(trace + 1.0f);
				quaternion->w = root * 0.5f;
				root = 0.5f / root;
				quaternion->x = (matrix->_23 - matrix->_32) * root;
				quaternion->y = (matrix->_31 - matrix->_13) * root;
				quaternion->z = (matrix->_12 - matrix->_21) * root;
				return;
			}
			else
			{
				int32_t index = matrix->_22 > matrix->_11;
				float* diagonal = &matrix->_11;
				if (matrix->_33 > diagonal[index * 5])
					index = 2;

				int32_t next = nextIndex[index];
				int32_t last = nextIndex[next];
				Quaternion result;
				float scale = (float)sqrt((diagonal[index * 5] - (diagonal[next * 5] + diagonal[last * 5])) + 1.0f);
				float* values = &result.x;
				values[index] = scale * 0.5f;
				if (scale != 0.0f)
					scale = 0.5f / scale;

				result.w = (diagonal[next * 4 + last] - diagonal[last * 4 + next]) * scale;
				values[next] = (diagonal[index * 4 + next] + diagonal[next * 4 + index]) * scale;
				values[last] = (diagonal[index * 4 + last] + diagonal[last * 4 + index]) * scale;
				*quaternion = result;
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

	}
}
