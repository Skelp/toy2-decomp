#include "Nu3D/Scene.h"

#include "NGNLoader/NGNTypes.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Nu3D/Primitive.h"
#include "Nu3D/Viewport.h"
#include "Renderer/Renderer.h"

namespace Nu3D
{
	namespace Scene
	{
		// FUNCTION: TOY2 0x004BC720
		void RenderCellsInRadius(int32_t cellRadius, int32_t scalerType, NGNLoader::NGNImage* image)
		{
			Vector3F cameraPosition;
			Math::GetPositionVector(&Camera::g_activeCamera.transform, &cameraPosition);

			Spatial::CellLocation cameraCell;
			if (! Spatial::ComputeCellFromXZ(&cameraCell, cameraPosition.x, cameraPosition.z, scalerType, image))
				return;

			Spatial::CellLocation cell = cameraCell;
			for (int32_t cellZ = cameraCell.z - cellRadius; cellZ < cameraCell.z + cellRadius; ++cellZ)
			{
				for (int32_t cellX = cameraCell.x - cellRadius; cellX < cameraCell.x + cellRadius; ++cellX)
				{
					cell.x = cellX;
					cell.z = cellZ;
					Link::DynamicScaler** cellHead = Spatial::GetCellByPos(&cell, image);
					if (! cellHead)
						continue;

					for (Link::DynamicScaler* scaler = *cellHead; scaler; scaler = scaler->next)
					{
						if (scaler->gscaleType != scalerType || (scaler->flags & 1) != 0)
							continue;

						Vector3F worldCenter;
						Vector3F offset;
						Math::VertexAdd(&worldCenter, &scaler->translation, &scaler->boundsCenterWorld);
						Math::VertexSubtract(&offset, &worldCenter, &cameraPosition);
						float distanceSquared = offset.x * offset.x + offset.y * offset.y + offset.z * offset.z;

						if (scalerType == 1)
						{
							if (distanceSquared <= Renderer::g_secondaryRenderDistanceSquared)
								continue;
						}
						else if (distanceSquared >= Renderer::g_primaryRenderDistanceSquared)
						{
							continue;
						}

						uint32_t frustumResult = Frustum::TestSphereAllPlanesAlt(&worldCenter, image->primitives[scaler->shapeId]->boundSphereRadius);
						if ((frustumResult & 0x55555555) != 0)
							continue;

						Renderer::Set508718(scaler->packedFlags);
						if (frustumResult)
							Renderer::RenderPrimitive(image->primitives[scaler->shapeId], &scaler->transformMatrix, 0);
						else
							Renderer::RenderPrimitive(image->primitives[scaler->shapeId], &scaler->transformMatrix, 0x2000);
					}
				}
			}

			Renderer::Set508718(5);
		}
	}
}
