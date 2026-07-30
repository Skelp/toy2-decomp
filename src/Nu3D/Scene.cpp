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
#include "Toy2/Actor.h"
#include "Toy2/Animation.h"
#include <FLOAT.H>

namespace Nu3D
{
	namespace Scene
	{
		enum CreatureNodeFlags
		{
			CREATURE_NODE_HIDDEN = 0x1,
			CREATURE_NODE_BILLBOARD = 0x2,
			CREATURE_NODE_VERTEX_LIGHTING = 0x4,
		};

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

		// FUNCTION: TOY2 0x004CAA60 [PROVISIONAL]
		void RenderActor(Creature* creature, D3DMATRIX* matrices, int32_t renderFlags)
		{
			int32_t nodeIndex = 0;
			if (creature->nodeCount > 0)
			{
				D3DMATRIX* nodeMatrix = matrices;
				do
				{
					if (creature->primitives[nodeIndex] != 0 && (creature->flagsList[nodeIndex] & CREATURE_NODE_HIDDEN) != CREATURE_NODE_HIDDEN)
					{
						int32_t previousVertexLighting;
						if ((creature->flagsList[nodeIndex] & CREATURE_NODE_VERTEX_LIGHTING) != 0)
							previousVertexLighting = Renderer::EnableVertexLighting(1);

						if ((creature->flagsList[nodeIndex] & CREATURE_NODE_BILLBOARD) != 0)
						{
							D3DMATRIX billboardMatrix = Camera::g_activeCamera.transform;
							Math::ScaleMatrix(&billboardMatrix);
							billboardMatrix._41 = nodeMatrix->_41;
							billboardMatrix._42 = nodeMatrix->_42;
							billboardMatrix._43 = nodeMatrix->_43;
							Renderer::RenderPrimitive(creature->primitives[nodeIndex], &billboardMatrix, renderFlags);
						}
						else
						{
							Renderer::RenderPrimitive(creature->primitives[nodeIndex], nodeMatrix, renderFlags);
						}

						if ((creature->flagsList[nodeIndex] & CREATURE_NODE_VERTEX_LIGHTING) != 0)
							Renderer::EnableVertexLighting(previousVertexLighting);
					}

					nodeIndex++;
					nodeMatrix++;
				} while (nodeIndex < creature->nodeCount);
			}

			Renderer::RenderPatchList(creature->patch, matrices, creature->flagsList, renderFlags);
		}

		// FUNCTION: TOY2 0x004CDC20 [PROVISIONAL]
		void RenderActors(Toy2::Actor::Toy2Actor** actors)
		{
			int32_t previousRenderFlags = Renderer::Set508718(0);
			if (NGNLoader::g_ngnImage->creatureCount != 0)
			{
				if (*actors != 0)
				{
					Toy2::Actor::Toy2Actor** actorCursor = actors;
					D3DMATRIX(*worldMatrices)[32] = Toy2::Animation::g_worldNodeMatrices;
					do
					{
						if (((*actorCursor)->actorFlags & Toy2::Actor::ACTOR_FLAG_TARGETABLE) != 0)
						{
							if ((*actorCursor)->useTint == 1)
							{
								Renderer::SetVertexColorModulation((*actorCursor)->actorTint.r, (*actorCursor)->actorTint.g, (*actorCursor)->actorTint.b);
								Renderer::EnableVertexColorModulation(1);
							}

							Creature* creature = NGNLoader::g_ngnImage->creatureData[(*actorCursor)->creatureId];
							if (creature != 0)
								RenderActor(creature, *worldMatrices, 0);

							if ((*actorCursor)->useTint == 1)
								Renderer::EnableVertexColorModulation(0);
						}

						actorCursor++;
						worldMatrices++;
					} while (*actorCursor != 0);
				}
			}

			Renderer::Set508718(previousRenderFlags);
		}

		// FUNCTION: TOY2 0x004BC720 [PROVISIONAL]
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
							Renderer::RenderPrimitive(image->primitives[scaler->shapeId], &scaler->transformMatrix, Renderer::RENDER_NO_CLIP);
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

				if ((renderFlags & WORLD_RENDER_BYPASS_ALL_PORTALS) == WORLD_RENDER_BYPASS_ALL_PORTALS)
				{
					Camera::g_currentCamera->portalNearClip = 0.0f;
					Camera::g_currentCamera->fogFarClip = FLT_MAX;
					Renderer::SetRenderDistance(FLT_MAX, 0.0f);
				}

				Camera::ApplyTransformToCamera(&Camera::g_activeCameraTransform);
				if (NGNLoader::g_ngnImage->shapeCounts[1])
				{
					if (! NGNLoader::g_ngnImage->portalEntryCount || areaIndex < 0 || (renderFlags & WORLD_RENDER_BYPASS_SECONDARY_PORTALS))
					{
						RenderCellsInRadius((renderFlags & WORLD_RENDER_BYPASS_SECONDARY_PORTALS) ? 30 : 20, 1, NGNLoader::g_ngnImage);
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

			if ((renderFlags & WORLD_RENDER_BYPASS_ALL_PORTALS) == WORLD_RENDER_BYPASS_ALL_PORTALS)
			{
				Camera::g_currentCamera->portalNearClip = 0.0f;
				Camera::g_currentCamera->fogFarClip = FLT_MAX;
				Renderer::SetRenderDistance(FLT_MAX, 0.0f);
			}

			Camera::ApplyTransformToCamera(&Camera::g_activeCameraTransform);
			Viewport::CacheViewport(&viewportCache);

			if (g_renderPrimaryGeometry && NGNLoader::g_ngnImage->shapeCounts[0])
			{
				if (! NGNLoader::g_ngnImage->portalEntryCount || areaIndex < 0 || (renderFlags & WORLD_RENDER_BYPASS_PRIMARY_PORTALS))
				{
					Portal::MarkAllAreasVisible();
					RenderCellsInRadius((renderFlags & WORLD_RENDER_BYPASS_PRIMARY_PORTALS) ? 30 : 15, 0, NGNLoader::g_ngnImage);
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
