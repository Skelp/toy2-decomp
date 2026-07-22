#include "Nu3D/Scene.h"

#include "NGNLoader/NGNLoader.h"
#include "NGNLoader/NGNTypes.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Nu3D/Portal.h"
#include "Nu3D/Primitive.h"
#include "Nu3D/Viewport.h"
#include "Renderer/Renderer.h"
#include <FLOAT.H>

namespace Nu3D
{
	namespace Scene
	{
		// GLOBAL: TOY2 0x00508D04
		float g_secondaryFarClip = 48000.0f;

		// GLOBAL: TOY2 0x00508D08
		float g_secondaryNearClip = 40.0f;

		// GLOBAL: TOY2 0x00508D0C
		float g_primaryFarClip = 48000.0f;

		// GLOBAL: TOY2 0x00508D10
		float g_primaryNearClip = 60.0f;

		// GLOBAL: TOY2 0x00508D14
		float g_secondaryPortalNearClip = 10000.0f;

		// GLOBAL: TOY2 0x00508D18
		float g_primaryFogFarClip = 12000.0f;

		// GLOBAL: TOY2 0x00508D1C
		int32_t g_renderSecondaryGeometry = 1;

		// GLOBAL: TOY2 0x00508D20
		int32_t g_renderPrimaryGeometry = 1;

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

		// FUNCTION: TOY2 0x004CDDD0 [MATCHED]
		void RenderWorldGeometry(int32_t areaIndex, int32_t renderFlags)
		{
			if (! NGNLoader::g_ngnImage)
				return;

			Viewport::Reset();
			Viewport::ViewportCache viewportCache;
			Viewport::CacheViewport(&viewportCache);

			if (g_renderSecondaryGeometry)
			{
				Camera::g_currentCamera->farClip = g_secondaryFarClip;
				Camera::g_currentCamera->nearClip = g_secondaryNearClip;
				Camera::g_currentCamera->portalNearClip = g_secondaryPortalNearClip;
				Camera::g_currentCamera->fogFarClip = FLT_MAX;

				if ((renderFlags & 3) == 3)
				{
					Camera::g_currentCamera->portalNearClip = 0.0f;
					Camera::g_currentCamera->fogFarClip = FLT_MAX;
					Renderer::SetRenderDistance(FLT_MAX, 0.0f);
				}

				Camera::ApplyTransformToCamera(&Camera::g_activeCameraTransform);
				if (NGNLoader::g_ngnImage->shapeCounts[1])
				{
					if (! NGNLoader::g_ngnImage->portalEntryCount || areaIndex < 0 || (renderFlags & 1))
					{
						RenderCellsInRadius((renderFlags & 1) ? 30 : 20, 1, NGNLoader::g_ngnImage);
					}
					else
					{
						Area::ResetPortalStates(NGNLoader::g_ngnImage);
						Area::RenderBucketThroughPortals(NGNLoader::g_ngnImage, areaIndex, 1, 0);
						Area::RenderBucketThroughPortals(NGNLoader::g_ngnImage, 0, 1, 0);
					}
				}

				Renderer::FlushRenderQueues();
			}

			Viewport::RestoreViewportCache(&viewportCache);
			Viewport::Reset();

			Camera::g_currentCamera->farClip = g_primaryFarClip;
			Camera::g_currentCamera->nearClip = g_primaryNearClip;
			Camera::g_currentCamera->portalNearClip = 0.0f;
			Camera::g_currentCamera->fogFarClip = g_primaryFogFarClip;

			if ((renderFlags & 3) == 3)
			{
				Camera::g_currentCamera->portalNearClip = 0.0f;
				Camera::g_currentCamera->fogFarClip = FLT_MAX;
				Renderer::SetRenderDistance(FLT_MAX, 0.0f);
			}

			Camera::ApplyTransformToCamera(&Camera::g_activeCameraTransform);
			Viewport::CacheViewport(&viewportCache);

			if (g_renderPrimaryGeometry && NGNLoader::g_ngnImage->shapeCounts[0])
			{
				if (! NGNLoader::g_ngnImage->portalEntryCount || areaIndex < 0 || (renderFlags & 2))
				{
					Portal::MarkAllAreasVisible();
					RenderCellsInRadius((renderFlags & 2) ? 30 : 15, 0, NGNLoader::g_ngnImage);
				}
				else
				{
					Portal::ClearVisibleAreaFlags();
					Area::ResetPortalStates(NGNLoader::g_ngnImage);
					Area::RenderBucketThroughPortals(NGNLoader::g_ngnImage, areaIndex, 0, 0);
					Area::RenderBucketThroughPortals(NGNLoader::g_ngnImage, 0, 0, 0);
				}
			}

			Viewport::RestoreViewportCache(&viewportCache);
			Camera::g_currentCamera->portalNearClip = 0.0f;
			Camera::g_currentCamera->fogFarClip = FLT_MAX;
			Camera::ApplyTransformToCamera(&Camera::g_activeCameraTransform);
		}
	}
}
