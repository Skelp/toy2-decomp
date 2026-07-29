#include "Nu3D/Math.h"

namespace Nu3D
{
	namespace Math
	{
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
