#include "Nu3D/Math.h"
#include <MATH.H>

// FUNCTION: TOY2 0x004A92C0 [MATCHED]
float Vector3F::DotProduct(const Vector3F* left, const Vector3F* right) { return left->x * right->x + left->y * right->y + left->z * right->z; }

// FUNCTION: TOY2 0x004A92E0 [MATCHED]
float Vector3F::Length(const Vector3F* vector) { return (float)sqrt(vector->x * vector->x + vector->y * vector->y + vector->z * vector->z); }

namespace Nu3D
{

	namespace Math
	{
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

		// FUNCTION: TOY2 0x004A8C60 [MATCHED]
		void ProjectPoint(Vector3F* result, const Vector3F* sourceVector, const D3DMATRIX* matrix)
		{
			float transformedW = matrix->_34 * sourceVector->z;
			transformedW += matrix->_24 * sourceVector->y;
			transformedW += matrix->_14 * sourceVector->x;
			transformedW += matrix->_44;
			float inverseW = 1.0f / transformedW;
			float transformedY = (matrix->_32 * sourceVector->z + matrix->_22 * sourceVector->y + matrix->_12 * sourceVector->x + matrix->_42) * inverseW;
			float transformedZ = (matrix->_33 * sourceVector->z + matrix->_23 * sourceVector->y + matrix->_13 * sourceVector->x + matrix->_43) * inverseW;
			float transformedX = matrix->_31 * sourceVector->z + matrix->_21 * sourceVector->y;

			result->x = (transformedX + sourceVector->x * matrix->_11 + matrix->_41) * inverseW;
			result->y = transformedY;
			result->z = transformedZ;
		}

		// FUNCTION: TOY2 0x004A8D80 [MATCHED]
		void TransformPointPerspective(Vector3F* result, const Vector3F* sourceVector, const D3DMATRIX* matrix)
		{
			float transformedW = matrix->_34 * sourceVector->z;
			transformedW += matrix->_24 * sourceVector->y;
			transformedW += matrix->_14 * sourceVector->x;
			float inverseW = 1.0f / transformedW;
			float transformedY = (matrix->_32 * sourceVector->z + matrix->_22 * sourceVector->y + matrix->_12 * sourceVector->x) * inverseW;
			float transformedZ = (matrix->_33 * sourceVector->z + matrix->_23 * sourceVector->y + matrix->_13 * sourceVector->x) * inverseW;
			float transformedX = matrix->_31 * sourceVector->z + matrix->_21 * sourceVector->y;

			result->x = (transformedX + sourceVector->x * matrix->_11) * inverseW;
			result->y = transformedY;
			result->z = transformedZ;
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

		// FUNCTION: TOY2 0x004A91B0 [MATCHED]
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

		// FUNCTION: TOY2 0x004A9210 [MATCHED]
		void ScaleVector(Vector3F* result, Vector3F* vector, float scale)
		{
			result->x = scale * vector->x;
			result->y = scale * vector->y;
			result->z = scale * vector->z;
		}

		// FUNCTION: TOY2 0x004A9270 [EFFECTIVE]
		void VertexCrossProduct(Vector3F* result, Vector3F* v1, Vector3F* v2)
		{
			float y = v2->x * v1->z - v2->z * v1->x;
			float z = v2->y * v1->x - v2->x * v1->y;

			result->x = v2->z * v1->y - v2->y * v1->z;
			result->y = y;
			result->z = z;
		}

		// FUNCTION: TOY2 0x004A9340 [MATCHED]
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

	}
}
