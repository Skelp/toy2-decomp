#include "Toy2/Toy2.h"
#include "Toy2/Toy2Internal.h"
#include "Toy2/Actor.h"
#include "Toy2/Animation.h"
#include "Toy2/Buzz.h"
#include "Toy2/Camera.h"
#include "Toy2/Collision.h"
#include "Toy2/Levels.h"
#include "Toy2/MainMenu.h"
#include "Toy2/Weather.h"
#include "Toy2/KiteTail.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Light.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Nu3D/Scene.h"
#include "Nu3D/SoftwareProjectionPoint.h"
#include "Nu3D/Viewport.h"
#include "Renderer/Renderer.h"
#include "Renderer/Glue.h"
#include "Renderer/Shadows.h"
#include "Renderer/Sprite.h"
#include "Renderer/SpriteSheets.h"
#include "SoftwareRenderer.h"
#include "NGNLoader/NGNLoader.h"
#include "Numerics.h"
#include <LIMITS.H>
#include <MATH.H>

// The sector the camera stands in, and the frame it renders. FloodVisibility
// walks the portals of a zone to mark the sectors the camera can see,
// UpdateActiveSector picks the sector a position belongs to, UpdateLighting
// applies that sector's light set, and RenderGame draws one game frame through
// them. DrawCinematicBars is the letterbox that the cutscene frames use.
//
// Retail run 0x0043F3D0-0x00440F70, between the Levels.cpp and MainMenu.cpp
// runs. A source file cannot cross a run: the retail link keeps object order,
// which the nine assert-named units prove, and the compiler emits one file in
// source order.

namespace Toy2
{
	namespace Portal
	{
		struct ClipRect
		{
			int16_t minX;
			int16_t minY;
			int16_t maxX;
			int16_t maxY;
		};

		enum PortalClipFlags
		{
			PORTAL_CLIP_NEAR_SCREEN = 1,
			PORTAL_CLIP_PARTIAL_MASK = 3,
			PORTAL_CLIP_BEHIND_CAMERA = 8,
			PORTAL_CLIP_REJECTED = 32,
			PROJECT_QUAD_NEEDS_NEAR_CLIP = 0x20000,
		};

		static __forceinline int32_t PackScreenPoint(int32_t x, int32_t y) { return (uint16_t)x | (y << 16); }

		static __forceinline int32_t ClipPortalPoint(const Vector3I& point, const Vector3I& next, const Vector3I& previous, int32_t& clipFlags)
		{
			if (point.z > 10)
				return PackScreenPoint(point.x * 160 / point.z + 256, point.y * 160 / point.z + 128);

			if (point.z < 0)
				clipFlags += PORTAL_CLIP_BEHIND_CAMERA;

			int32_t clippedX;
			int32_t clippedY;
			if (next.z > 10 && next.z > previous.z)
			{
				clippedX = (point.x - next.x) * next.z / (next.z - point.z) + next.x;
				clippedY = (point.y - next.y) * next.z / (next.z - point.z) + next.y;
			}
			else if (previous.z > 10)
			{
				clippedX = (point.x - previous.x) * previous.z / (previous.z - point.z) + previous.x;
				clippedY = (point.y - previous.y) * previous.z / (previous.z - point.z) + previous.y;
			}
			else
			{
				clippedX = point.x;
				clippedY = point.y;
			}

			if (abs(clippedX) < 512 || abs(clippedY) < 512)
				clipFlags += PORTAL_CLIP_NEAR_SCREEN;

			int32_t screenX = clippedX < 0 ? 0 : 512;
			int32_t screenY = clippedY < 0 ? 0 : 512;
			return PackScreenPoint(screenX, screenY);
		}

		// Record index that ends a sector portal list.
		const uint8_t kPortalListEnd = 0xFF;

	}
	namespace Sector
	{
		struct SectorZoneRenderData
		{
			uint8_t visibilityDepth;
			uint8_t isProcessed;
			uint8_t portalRecordIndex;
			uint8_t hasViewTransform;
			int16_t minX;
			int16_t minY;
			int16_t maxX;
			int16_t maxY;
			int16_t viewAngleX;
			int16_t viewAngleY;
			int16_t primaryInstanceBytes;
			int16_t secondaryInstanceBytes;
			Matrix3x3I16 viewTransform;
			uint8_t reserved26[14];
		};

		struct ViewFrustumScratch
		{
			Vector3I leftEdge;
			int32_t reservedLeft;
			Vector3I rightEdge;
			int32_t reservedRight;
			Vector3I normalizedLeft;
			int32_t reservedNormalizedLeft;
			union
			{
				Vector3I normalizedRight;
				Vector3I16 viewAngles;
			};
		};

		static __forceinline int32_t ShiftFixedTowardZero(int32_t value, int32_t bits) { return (value + ((value >> 31) & ((1 << bits) - 1))) >> bits; }

		STATIC_ASSERT(sizeof(SectorZoneRenderData) == sizeof(ZoneRenderData));
		STATIC_ASSERT(offsetof(SectorZoneRenderData, viewAngleX) == 0xC);
		STATIC_ASSERT(offsetof(SectorZoneRenderData, viewTransform) == 0x14);

	}
	// RenderGame constants.
	enum
	{
		COLOR_CHANNEL_MAX = 0xFF,
		TEXTURE_SIZE_UNKNOWN = 0xFF,
		NEARBY_EFFECT_LIST_END = 0xFF,
		RECORD_TYPE_EFFECTS = 59,
		SPRITE_FLAGS_BLENDED = 0xC40,
		PICKUP_DISTANCE_BIAS = 100,
		BEAM_QUEUE_SIZE = 100,
		LEVEL2_SECTOR3_UNDERGROUND_Y = 0x19640,
		LEVEL2_UNDERGROUND_Y = 74000,
		LEVEL4_DETAIL_CENTER_X = 0x51B97,
		LEVEL4_DETAIL_CENTER_Y = -0x104FD,
		LEVEL4_DETAIL_CENTER_Z = 0x5E58E,
		LEVEL4_DETAIL_RADIUS = 0xFA
	};

	// GLOBAL: TOY2 0x004F7280
	int32_t g_showBlackFrames = 1;

	namespace Portal
	{
		// GLOBAL: TOY2 0x0054D920
		Vector3I g_previousSectorPosition;

		// GLOBAL: TOY2 0x0054DD74
		int32_t g_backdropClipCount;

	}
	// GLOBAL: TOY2 0x005546A0
	int32_t g_renderSectorIndex;

	namespace Portal
	{
		// GLOBAL: TOY2 0x005551B0
		Vector3I16 g_portalNormal;

		// GLOBAL: TOY2 0x005551E0
		Vector3I g_portalPlaneNormal;

		// GLOBAL: TOY2 0x00555368
		Vector3I g_portalIntersectionPoint;

		// Half size of the box around a portal that Buzz must be in (world >> 7 units).
		const int32_t kPortalNearRange = 0x4000;
		// Edge tolerance for the portal polygon test.
		const int32_t kPortalEdgeTolerance = 400;

		// GLOBAL: TOY2 0x00557BB0
		ClipRect g_backdropClipRects[12];

	}
	// GLOBAL: TOY2 0x00559C5C
	int32_t g_renderDisabled;

}
namespace Renderer
{
	// GLOBAL: TOY2 0x00559C60
	int32_t g_cinematicBarProgress;

}
namespace Toy2
{
	namespace Portal
	{
		// FUNCTION: TOY2 0x0043F3D0 [PROVISIONAL]
		void FloodVisibility(Levels::PortalZone* portalZone, int32_t minX, int32_t maxX, int32_t minY, int32_t maxY, int32_t depth)
		{
			Levels::PortalEntry* entry = portalZone->entries;
			while (entry->recordIdx != kPortalListEnd)
			{
				Levels::PortalRecord* portal = reinterpret_cast<Levels::PortalRecord*>(Levels::g_recordData[entry->recordIdx]);
				ZoneRenderData& zone = g_zoneRenderData[entry->categoryIdx];
				if (portal != NULL && zone.visibilityDepth < depth)
				{
					int32_t* projected = reinterpret_cast<int32_t*>(&Collision::g_mathScratch[0]);
					int32_t projectionFlags;
					int32_t projectionScratch;
					Nu3D::Camera::ProjectQuad(reinterpret_cast<Nu3D::Camera::SoftwareProjectionPoint*>(&portal->origin),
						reinterpret_cast<Nu3D::Camera::SoftwareProjectionPoint*>(&portal->vertices[0]),
						reinterpret_cast<Nu3D::Camera::SoftwareProjectionPoint*>(&portal->vertices[2]),
						reinterpret_cast<Nu3D::Camera::SoftwareProjectionPoint*>(&portal->vertices[1]),
						&projected[0],
						&projected[1],
						&projected[2],
						&projected[3],
						&projectionFlags,
						&projectionScratch);

					bool overlapsX = (int16_t)projected[0] <= maxX || (int16_t)projected[1] <= maxX || (int16_t)projected[2] <= maxX
						|| (int16_t)projected[3] <= maxX;
					overlapsX = overlapsX
						&& (minX <= (int16_t)projected[0] || minX <= (int16_t)projected[1] || minX <= (int16_t)projected[2] || minX <= (int16_t)projected[3]);
					bool overlapsY = (int16_t)(projected[0] >> 16) <= maxY || (int16_t)(projected[1] >> 16) <= maxY || (int16_t)(projected[2] >> 16) <= maxY
						|| (int16_t)(projected[3] >> 16) <= maxY;
					overlapsY = overlapsY
						&& (minY <= (int16_t)(projected[0] >> 16) || minY <= (int16_t)(projected[1] >> 16) || minY <= (int16_t)(projected[2] >> 16)
							|| minY <= (int16_t)(projected[3] >> 16));

					if (overlapsX && overlapsY)
					{
						int32_t clipFlags = 0;
						if ((projectionFlags & PROJECT_QUAD_NEEDS_NEAR_CLIP) != 0 && (g_levelFileIndex != 7 || entry->categoryIdx != 15))
						{
							zone.isProcessed = 1;
							Vector3I* view0 = &Collision::g_mathScratch[1].value;
							Vector3I* view1 = &Collision::g_mathScratch[2].value;
							Vector3I* view2 = &Collision::g_mathScratch[3].value;
							Vector3I* view3 = &Collision::g_mathScratch[4].value;
							Nu3D::Camera::WorldToView(&portal->origin, view0, &projectionFlags);
							Nu3D::Camera::WorldToView(&portal->vertices[0].position, view1, &projectionFlags);
							Nu3D::Camera::WorldToView(&portal->vertices[1].position, view2, &projectionFlags);
							Nu3D::Camera::WorldToView(&portal->vertices[2].position, view3, &projectionFlags);

							projected[0] = ClipPortalPoint(*view0, *view1, *view3, clipFlags);
							projected[1] = ClipPortalPoint(*view1, *view2, *view0, clipFlags);
							projected[3] = ClipPortalPoint(*view2, *view3, *view1, clipFlags);
							projected[2] = ClipPortalPoint(*view3, *view0, *view2, clipFlags);
						}

						if (Nu3D::Math::Cross2D(projected[0], projected[1], projected[2]) >= 0
							|| Nu3D::Math::Cross2D(projected[1], projected[3], projected[2]) >= 0 || clipFlags != 0)
						{
							if (clipFlags < PORTAL_CLIP_REJECTED)
							{
								zone.visibilityDepth = (uint8_t)depth;
								zone.portalRecordIndex = (zone.portalRecordIndex & 0xFF00) | entry->recordIdx;
								if ((clipFlags & PORTAL_CLIP_PARTIAL_MASK) == 0)
								{
									int32_t portalMinX = (int16_t)projected[0];
									int32_t portalMaxX = portalMinX;
									int32_t portalMinY = (int16_t)(projected[0] >> 16);
									int32_t portalMaxY = portalMinY;
									for (int32_t i = 1; i < 4; i++)
									{
										int32_t x = (int16_t)projected[i];
										int32_t y = (int16_t)(projected[i] >> 16);
										if (x < portalMinX)
											portalMinX = x;
										if (x > portalMaxX)
											portalMaxX = x;
										if (y < portalMinY)
											portalMinY = y;
										if (y > portalMaxY)
											portalMaxY = y;
									}
									zone.minX = (int16_t)(portalMinX < minX ? minX : portalMinX);
									zone.minY = (int16_t)(portalMinY < minY ? minY : portalMinY);
									zone.maxX = (int16_t)(portalMaxX > maxX ? maxX : portalMaxX);
									zone.maxY = (int16_t)(portalMaxY > maxY ? maxY : portalMaxY);
								}
								else
								{
									zone.minX = (int16_t)minX;
									zone.minY = (int16_t)minY;
									zone.maxX = (int16_t)maxX;
									zone.maxY = (int16_t)maxY;
								}

								if (entry->categoryIdx == 15 && g_hasBackdrop != 0 && g_backdropClipCount < 12)
								{
									g_zoneRenderData[15].visibilityDepth = 0;
									g_backdropClipRects[g_backdropClipCount].minX = zone.minX;
									g_backdropClipRects[g_backdropClipCount].maxX = zone.maxX;
									g_backdropClipRects[g_backdropClipCount].minY = zone.minY;
									g_backdropClipCount++;
									g_backdropClipRects[g_backdropClipCount - 1].maxY = zone.maxY;
								}
								else
								{
									FloodVisibility(&Levels::g_portalZones[entry->categoryIdx], zone.minX, zone.maxX, zone.minY, zone.maxY, depth - 1);
								}
							}
							else
								zone.visibilityDepth = 0;
						}
					}
					else
						zone.visibilityDepth = 0;
				}

				entry++;
			}
		}

		// FUNCTION: TOY2 0x0043FEF0 [PROVISIONAL]
		void UpdateActiveSector()
		{
			Levels::PortalRecord** portalRecords = reinterpret_cast<Levels::PortalRecord**>(Levels::g_recordData);
			Levels::PortalEntry* entry = Levels::g_portalZones[Sector::g_currentSectorIndex].entries;
			// Stop when the sector has no portal entries.
			if (entry->recordIdx == kPortalListEnd)
				return;

			// Test each portal of the sector for a crossing by Buzz.
			do
			{
				if (entry->categoryIdx != 15 || g_hasBackdrop == 0)
				{
					if ((abs((g_buzzActor.posAngles.pos.x >> 7) - portalRecords[entry->recordIdx]->origin.x) < kPortalNearRange
							&& abs((g_buzzActor.posAngles.pos.y >> 7) - portalRecords[entry->recordIdx]->origin.y) < kPortalNearRange
							&& abs((g_buzzActor.posAngles.pos.z >> 7) - portalRecords[entry->recordIdx]->origin.z) < kPortalNearRange)
						|| (abs((g_buzzActor.posAngles.pos.x >> 7) - portalRecords[entry->recordIdx]->vertices[1].position.x) < kPortalNearRange
							&& abs((g_buzzActor.posAngles.pos.y >> 7) - portalRecords[entry->recordIdx]->vertices[1].position.y) < kPortalNearRange
							&& abs((g_buzzActor.posAngles.pos.z >> 7) - portalRecords[entry->recordIdx]->vertices[1].position.z) < kPortalNearRange))
					{
						g_portalPlaneNormal.x = portalRecords[entry->recordIdx]->normal.x;
						g_portalPlaneNormal.y = portalRecords[entry->recordIdx]->normal.y;
						g_portalPlaneNormal.z = portalRecords[entry->recordIdx]->normal.z;

						int32_t currentDistance = ((g_buzzActor.posAngles.pos.x >> 7) - portalRecords[entry->recordIdx]->origin.x) * g_portalPlaneNormal.x
							+ ((g_buzzActor.posAngles.pos.y >> 7) - portalRecords[entry->recordIdx]->origin.y) * g_portalPlaneNormal.y
							+ ((g_buzzActor.posAngles.pos.z >> 7) - portalRecords[entry->recordIdx]->origin.z) * g_portalPlaneNormal.z;

						if (currentDistance < 0)
						{
							int32_t previousDistance = ((g_previousSectorPosition.x >> 7) - portalRecords[entry->recordIdx]->origin.x) * g_portalPlaneNormal.x
								+ ((g_previousSectorPosition.y >> 7) - portalRecords[entry->recordIdx]->origin.y) * g_portalPlaneNormal.y
								+ ((g_previousSectorPosition.z >> 7) - portalRecords[entry->recordIdx]->origin.z) * g_portalPlaneNormal.z;

							if (previousDistance >= 0)
							{
								g_portalNormal.x = (int16_t)g_portalPlaneNormal.x;
								g_portalNormal.y = (int16_t)g_portalPlaneNormal.y;
								g_portalNormal.z = (int16_t)g_portalPlaneNormal.z;

								g_portalIntersectionPoint.x =
									(((g_buzzActor.posAngles.pos.x - g_previousSectorPosition.x) * previousDistance / (previousDistance - currentDistance)
										 + g_previousSectorPosition.x)
										>> 7)
									- portalRecords[entry->recordIdx]->origin.x;
								g_portalIntersectionPoint.y =
									(((g_buzzActor.posAngles.pos.y - g_previousSectorPosition.y) * previousDistance / (previousDistance - currentDistance)
										 + g_previousSectorPosition.y)
										>> 7)
									- portalRecords[entry->recordIdx]->origin.y;
								g_portalIntersectionPoint.z =
									(((g_buzzActor.posAngles.pos.z - g_previousSectorPosition.z) * previousDistance / (previousDistance - currentDistance)
										 + g_previousSectorPosition.z)
										>> 7)
									- portalRecords[entry->recordIdx]->origin.z;

								if (Nu3D::Math::InsidePolLines(g_portalIntersectionPoint.x,
										g_portalIntersectionPoint.y,
										g_portalIntersectionPoint.z,
										portalRecords[entry->recordIdx]->vertices[0].position.x - portalRecords[entry->recordIdx]->origin.x,
										portalRecords[entry->recordIdx]->vertices[0].position.y - portalRecords[entry->recordIdx]->origin.y,
										portalRecords[entry->recordIdx]->vertices[0].position.z - portalRecords[entry->recordIdx]->origin.z,
										portalRecords[entry->recordIdx]->vertices[1].position.x - portalRecords[entry->recordIdx]->origin.x,
										portalRecords[entry->recordIdx]->vertices[1].position.y - portalRecords[entry->recordIdx]->origin.y,
										portalRecords[entry->recordIdx]->vertices[1].position.z - portalRecords[entry->recordIdx]->origin.z,
										&g_portalNormal,
										kPortalEdgeTolerance)
									|| Nu3D::Math::InsidePolLines(g_portalIntersectionPoint.x,
										g_portalIntersectionPoint.y,
										g_portalIntersectionPoint.z,
										portalRecords[entry->recordIdx]->vertices[1].position.x - portalRecords[entry->recordIdx]->origin.x,
										portalRecords[entry->recordIdx]->vertices[1].position.y - portalRecords[entry->recordIdx]->origin.y,
										portalRecords[entry->recordIdx]->vertices[1].position.z - portalRecords[entry->recordIdx]->origin.z,
										portalRecords[entry->recordIdx]->vertices[2].position.x - portalRecords[entry->recordIdx]->origin.x,
										portalRecords[entry->recordIdx]->vertices[2].position.y - portalRecords[entry->recordIdx]->origin.y,
										portalRecords[entry->recordIdx]->vertices[2].position.z - portalRecords[entry->recordIdx]->origin.z,
										&g_portalNormal,
										kPortalEdgeTolerance))
								{
									Sector::g_currentSectorIndex = entry->categoryIdx;
								}
							}
						}
					}
				}

				// Go to the next portal entry until the list end.
				++entry;
			} while (entry->recordIdx != kPortalListEnd);
		}

		// FUNCTION: TOY2 0x00440260 [MATCHED]
		void UpdateActiveSectorAt(int32_t x, int32_t y, int32_t z)
		{
			int32_t previousX = g_buzzActor.posAngles.pos.x;
			int32_t previousY = g_buzzActor.posAngles.pos.y;
			int32_t previousZ = g_buzzActor.posAngles.pos.z;

			g_buzzActor.posAngles.pos.x = x;
			g_buzzActor.posAngles.pos.y = y;
			g_buzzActor.posAngles.pos.z = z;
			UpdateActiveSector();
			g_buzzActor.posAngles.pos.x = previousX;
			g_buzzActor.posAngles.pos.y = previousY;
			g_buzzActor.posAngles.pos.z = previousZ;
		}

	}
	namespace Sector
	{
		// FUNCTION: TOY2 0x004402B0 [PROVISIONAL]
		void UpdateLighting()
		{
			SectorZoneRenderData* zoneData = reinterpret_cast<SectorZoneRenderData*>(g_zoneRenderData);
			g_buzzActor.posAngles.pos.y -= 0x2000;
			if (g_currentSectorIndex == -1 || g_levelFileIndex == 2)
				g_currentSectorIndex = Level::GetSectorAtPosition(&g_buzzActor.posAngles.pos);
			else
				Portal::UpdateActiveSector();

			Portal::g_previousSectorPosition = g_buzzActor.posAngles.pos;
			g_buzzActor.posAngles.pos.y += 0x2000;

			Vector3I cameraPosition = {
				Nu3D::Camera::g_fixedViewPosition.x,
				Nu3D::Camera::g_fixedViewPosition.y,
				Nu3D::Camera::g_fixedViewPosition.z,
			};
			int32_t cameraSector = Level::GetSectorAtPosition(&cameraPosition);
			int32_t selectedSector = cameraSector;
			if (g_currentSectorIndex == cameraSector || Camera::g_cutsceneTransitionTimer != 0)
			{
				g_activeSectorIndex = selectedSector;
			}
			else
			{
				selectedSector = g_currentSectorIndex;
				if (cameraSector >= 0 && g_currentSectorIndex >= 0)
				{
					Levels::PortalEntry* entry = Levels::g_portalZones[g_currentSectorIndex].entries;
					if (entry->recordIdx != Portal::kPortalListEnd)
					{
						bool sectorNotFound = true;
						do
						{
							if (entry->categoryIdx == cameraSector)
							{
								g_activeSectorIndex = cameraSector;
								sectorNotFound = false;
							}
							entry++;
						} while (entry->recordIdx != Portal::kPortalListEnd);
						if (! sectorNotFound)
							selectedSector = cameraSector;
					}
				}
				g_activeSectorIndex = selectedSector;
			}

			if (g_activeSectorIndex == 0xFF)
				g_activeSectorIndex = 1;

			if (g_levelFileIndex == 2)
			{
				if (g_activeSectorIndex == 3)
				{
					if (Nu3D::Camera::g_fixedViewPosition.y < -0x19640)
						goto show_all_zones;
				}
				else if (Nu3D::Camera::g_fixedViewPosition.y < -74000)
				{
				show_all_zones:
					if (g_zoneCount > 0)
					{
						Portal::g_backdropClipCount = 0;
						for (int32_t i = 0; i < g_zoneCount; i++)
						{
							zoneData[g_activeSectorIndex].visibilityDepth = 0xFF;
							zoneData[i].visibilityDepth = 0xFF;
							zoneData[g_activeSectorIndex].minX = 0;
							zoneData[i].minX = 0;
							zoneData[g_activeSectorIndex].minY = 0;
							zoneData[i].minY = 0;
							zoneData[g_activeSectorIndex].maxX = 0x200;
							zoneData[i].maxX = 0x200;
							zoneData[g_activeSectorIndex].maxY = 0x100;
							zoneData[i].maxY = 0x100;
						}
					}
					goto calculate_zone_views;
				}
			}

			{
				for (int32_t i = 0; i < g_zoneCount; i++)
				{
					zoneData[i].visibilityDepth = 0;
					zoneData[i].isProcessed = 0;
				}

				SectorZoneRenderData& activeZone = zoneData[g_activeSectorIndex];
				activeZone.visibilityDepth = 0xFF;
				zoneData[0].visibilityDepth = 0xFF;
				activeZone.minX = zoneData[0].minX = 0;
				activeZone.minY = zoneData[0].minY = 0;
				activeZone.maxX = zoneData[0].maxX = 0x200;
				activeZone.maxY = zoneData[0].maxY = 0x100;
				Portal::g_backdropClipCount = 0;

				Nu3D::Camera::FixedViewTransform& view = Nu3D::Camera::g_fixedViewTransform;
				const Vector3I& position = Nu3D::Camera::g_sectorViewPosition;
				view.position.x = -ShiftFixedTowardZero(view.rotation.m00 * position.x + view.rotation.m01 * position.y + view.rotation.m02 * position.z, 14);
				view.position.y = -ShiftFixedTowardZero(view.rotation.m10 * position.x + view.rotation.m11 * position.y + view.rotation.m12 * position.z, 14);
				view.position.z = -ShiftFixedTowardZero(view.rotation.m20 * position.x + view.rotation.m21 * position.y + view.rotation.m22 * position.z, 14);
				Nu3D::Camera::SetObjectViewPosition(&view);
				Nu3D::Camera::SetObjectViewMatrix(&view);
				Portal::FloodVisibility(&Levels::g_portalZones[g_activeSectorIndex], 0, 0x200, 0, 0x100, 0xFE);
			}

		calculate_zone_views:
			Nu3D::Math::SetRotationXYZ(&g_viewRotation, &Animation::g_nextKeyframeRotation.matrix);
			ViewFrustumScratch* scratch = reinterpret_cast<ViewFrustumScratch*>(&Collision::g_mathScratch[42].value.z);
			for (int32_t i = 0; i < g_zoneCount; i++)
			{
				SectorZoneRenderData& zone = zoneData[i];
				if (zone.visibilityDepth == 0)
					continue;

				if (zone.minX < 3 && zone.maxX > 0x1FD && zone.minY < 3 && zone.maxY > 0xFD)
				{
					zone.hasViewTransform = 0;
					continue;
				}

				scratch->leftEdge.x = (zone.minX - 0x100) * 0x140 / 0x200;
				scratch->leftEdge.y = zone.minY - 0x80;
				scratch->leftEdge.z = 0xA0;
				scratch->rightEdge.x = (zone.maxX - 0x100) * 0x140 / 0x200;
				scratch->rightEdge.y = zone.maxY - 0x80;
				scratch->rightEdge.z = 0xA0;
				Nu3D::Math::NormalizeToFixedPoint(&scratch->leftEdge, &scratch->normalizedLeft);
				Nu3D::Math::NormalizeToFixedPoint(&scratch->rightEdge, &scratch->normalizedRight);
				scratch->normalizedLeft.x += scratch->normalizedRight.x;
				scratch->normalizedLeft.y += scratch->normalizedRight.y;
				scratch->normalizedLeft.z += scratch->normalizedRight.z;

				const Matrix3x3I16& rotation = Animation::g_nextKeyframeRotation.matrix;
				Portal::g_portalIntersectionPoint.x = ShiftFixedTowardZero(
					rotation.m00 * scratch->normalizedLeft.x + rotation.m10 * scratch->normalizedLeft.y + rotation.m20 * scratch->normalizedLeft.z, 12);
				Portal::g_portalIntersectionPoint.y = ShiftFixedTowardZero(
					rotation.m01 * scratch->normalizedLeft.x + rotation.m11 * scratch->normalizedLeft.y + rotation.m21 * scratch->normalizedLeft.z, 12);
				Portal::g_portalIntersectionPoint.z = ShiftFixedTowardZero(
					rotation.m02 * scratch->normalizedLeft.x + rotation.m12 * scratch->normalizedLeft.y + rotation.m22 * scratch->normalizedLeft.z, 12);

				Vector3I& direction = Portal::g_portalIntersectionPoint;
				int16_t yaw = (int16_t)-Nu3D::Math::CartesianToFixedAngle(direction.x, direction.z);
				int32_t sine = Numerics::g_sinCosLUT[yaw & 0xFFF];
				int32_t cosine = Numerics::g_sinCosLUT[(yaw + 0x400) & 0xFFF];
				int32_t x = direction.x;
				direction.x = (direction.z * sine + x * cosine) >> 14;
				direction.z = (direction.z * cosine - x * sine) >> 14;

				Vector3I16& viewAngles = scratch->viewAngles;
				viewAngles.x = (int16_t)Nu3D::Math::CartesianToFixedAngle(direction.y, direction.z);
				viewAngles.y = yaw;
				viewAngles.z = 0;

				int32_t leftYaw = Nu3D::Math::CartesianToFixedAngle(scratch->rightEdge.x, 0xA0) - Nu3D::Math::CartesianToFixedAngle(scratch->leftEdge.x, 0xA0);
				zone.viewAngleX = (int16_t)(Numerics::g_sinCosLUT[leftYaw & 0xFFF] * 3 >> 2);
				int32_t topPitch = Nu3D::Math::CartesianToFixedAngle(scratch->rightEdge.y, 0xA0) - Nu3D::Math::CartesianToFixedAngle(scratch->leftEdge.y, 0xA0);
				zone.viewAngleY = (int16_t)(Numerics::g_sinCosLUT[topPitch & 0xFFF] * 3 >> 2);
				zone.viewAngleX += (int16_t)Nu3D::Math::CartesianToFixedAngle(scratch->rightEdge.x, 0xA0)
					- (int16_t)Nu3D::Math::CartesianToFixedAngle(scratch->leftEdge.x, 0xA0);
				zone.viewAngleY += (int16_t)Nu3D::Math::CartesianToFixedAngle(scratch->rightEdge.y, 0xA0)
					- (int16_t)Nu3D::Math::CartesianToFixedAngle(scratch->leftEdge.y, 0xA0);
				zone.viewAngleX = (int16_t)(zone.viewAngleX * 0xA0 / 0xA0);
				zone.viewAngleY = (int16_t)(zone.viewAngleY * 0x80 / 0xA0);
				zone.hasViewTransform = 1;
				Nu3D::Math::SetRotationXYZ(&viewAngles, &zone.viewTransform);
			}

			if (Levels::g_hasZoneData != 0)
			{
				SectorZoneRenderData& activeZone = zoneData[g_activeSectorIndex];
				activeZone.minX = (int16_t)Nu3D::Camera::g_zoneViewportLeftOffset;
				activeZone.minY = (int16_t)Nu3D::Camera::g_zoneViewportTopOffset;
				activeZone.maxX = (int16_t)(Nu3D::Camera::g_zoneViewportRightOffset + 0x200);
				activeZone.maxY = (int16_t)(Nu3D::Camera::g_zoneViewportBottomOffset + 0x100);
			}
		}

	}
}
namespace Renderer
{
	// FUNCTION: TOY2 0x00440E90 [EFFECTIVE]
	void DrawCinematicBars()
	{
		int32_t frameDelta = g_frameDelta;
		int32_t barProgress;
		Vector2F uvTopLeft = { 0.0f, 0.0f };
		Vector2F uvBottomRight = { 1.0f, 1.0f };

		if (Nu3D::Camera::g_viewHistoryInitialized != 0)
		{
			barProgress = g_cinematicBarProgress + frameDelta;
			if (barProgress > 25)
			{
				g_cinematicBarProgress = 25;
				goto drawBars;
			}
		}
		else
		{
			barProgress = g_cinematicBarProgress - frameDelta;
			if (barProgress < 0)
			{
				g_cinematicBarProgress = 0;
				return;
			}
		}

		g_cinematicBarProgress = barProgress;
		if (barProgress <= 0)
			return;

	drawBars:
		float barHeight = (float)g_cinematicBarProgress * 0.004f;
		RGBA barColor;
		barColor.value = 0xFF000000;
		int32_t renderFlags = RENDER_ALPHA_DEFAULT | RENDER_ZWRITE | RENDER_CULL_NONE;
		Sprite::Queue2DSprite(0.0f, 0.0f, 1.0f, barHeight, &uvTopLeft, &uvBottomRight, 0, barColor, renderFlags);
		Sprite::Queue2DSprite(0.0f, 1.0f - barHeight, 1.0f, barHeight, &uvTopLeft, &uvBottomRight, 0, barColor, renderFlags);
	}

}
namespace Toy2
{
	// FUNCTION: TOY2 0x00440F70 [PROVISIONAL]
	void RenderGame(int32_t fullRender)
	{
		// Skip the frame while rendering is off.
		if (g_renderDisabled != 0)
			return;

		// Show the pending black frames once.
		if (g_showBlackFrames != 0)
		{
			g_showBlackFrames = 0;
			Renderer::ShowBlackFrames();
		}

		Renderer::Sprite::g_parallaxDepthZPos = 0.9999f;
		RGBA clearColor;
		clearColor.a = COLOR_CHANNEL_MAX;
		clearColor.r = MainMenu::g_menuClearColor.r >> 1;
		clearColor.g = MainMenu::g_menuClearColor.g >> 1;
		clearColor.b = MainMenu::g_menuClearColor.b >> 1;

		Renderer::Glue::ReleaseBackdrop();
		int32_t renderSector = Level::GetSectorAtPosition(reinterpret_cast<const Vector3I*>(&Nu3D::Camera::g_fixedViewPosition));
		g_renderSectorIndex = renderSector;
		Sector::g_activeSectorIndex = renderSector;
		Sector::g_currentSectorIndex = renderSector;
		Sector::UpdateLighting();
		Renderer::SetViewportPreset();

		int32_t worldRenderFlags = 0;
		// Level-specific sector and viewport overrides.
		switch (g_levelFileIndex)
		{
			// Hide the sector geometry below the floor.
			case 2:
				if ((Sector::g_activeSectorIndex == 3 && Nu3D::Camera::g_fixedViewPosition.y < -LEVEL2_SECTOR3_UNDERGROUND_Y)
					|| (Sector::g_activeSectorIndex != 3 && Nu3D::Camera::g_fixedViewPosition.y < -LEVEL2_UNDERGROUND_Y))
				{
					worldRenderFlags = 1;
					renderSector = -1;
				}
				break;
			// Force sector 1 below the floor and full detail near the center.
			case 4:
				if (Sector::g_currentSectorIndex == 2 && Nu3D::Camera::g_cameraPosition.y < -7900.0f)
				{
					Sector::g_activeSectorIndex = 1;
					g_renderSectorIndex = 1;
					Sector::g_currentSectorIndex = 1;
					renderSector = 1;
				}
				{
					Vector3I detailCenter = { LEVEL4_DETAIL_CENTER_X, LEVEL4_DETAIL_CENTER_Y, LEVEL4_DETAIL_CENTER_Z };
					if (Sector::g_currentSectorIndex == 2 || Nu3D::Math::IsWithinDistance(&detailCenter, &g_buzzActor.posAngles.pos, LEVEL4_DETAIL_RADIUS) != 0)
						Renderer::SetViewportPresetByDetail(INT_MAX);
				}
				break;
			// Full detail for all of the level.
			case 3:
			// fall through
			case 9:
				Renderer::SetViewportPresetByDetail(INT_MAX);
				worldRenderFlags = 3;
				break;
			// Use sector 5 inside the lower area of sector 4.
			case 10:
				if (g_renderSectorIndex == 4 && Nu3D::Camera::g_cameraPosition.y < -40800.0f && Nu3D::Camera::g_cameraPosition.x > -5500.0f
					&& Nu3D::Camera::g_cameraPosition.x < 800.0f && Nu3D::Camera::g_cameraPosition.z > -800.0f && Nu3D::Camera::g_cameraPosition.z < -3900.0f)
				{
					renderSector = 5;
					Sector::g_activeSectorIndex = 5;
					g_renderSectorIndex = 5;
				}
				break;
			// Full detail in sectors 5 and 7.
			case 11:
				if (g_renderSectorIndex == 5 || g_renderSectorIndex == 7)
					Renderer::SetViewportPresetByDetail(INT_MAX);
				break;
			// Near fog.
			case 14:
				Renderer::ConfigureFog(24000.0f, 46000.0f, clearColor);
				break;
		}

		Animation::AnimateActors(Actor::g_renderActors);
		ProcessMiscEventsEx();
		// Clear the screen; with no backdrop, use far fog.
		if (g_hasBackdrop == 0 && g_hasStaticBackdrop == 0)
		{
			Renderer::ClearScreen(clearColor, 3);
			Renderer::ConfigureFog(24000.0f, 48000.0f, clearColor);
		}
		else // backdrop present
		{
			Renderer::ClearScreen(clearColor, 2);
		}

		Nu3D::Light::SetDirectionalLight(-g_buzzActor.lightDirection.x,
			-g_buzzActor.lightDirection.y,
			-g_buzzActor.lightDirection.z,
			g_buzzActor.color.r,
			g_buzzActor.color.g,
			g_buzzActor.color.b);

		// Draw the scene.
		if (Renderer::BeginScene() != 0)
		{
			Renderer::UpdateBackgroundScroll((int16_t)Nu3D::Camera::g_cameraPitch, -Nu3D::Camera::g_cameraYaw);
			Renderer::SetFogEnable(0);
			Renderer::RenderParallaxBackground(1);
			Renderer::SetFogEnable(1);
			Nu3D::Camera::FixedViewTransform objectView;
			Nu3D::Camera::SetObjectViewMatrix(&objectView);
			Nu3D::Scene::RenderWorldGeometry(renderSector, worldRenderFlags);
			Nu3D::Scene::RenderActors(Actor::g_renderActors);

			if (fullRender != 0)
			{
				Renderer::Shadows::DrawAll();

				// Pickups: sheet pickups draw a billboard and a shadow, link pickups spin.
				enum
				{
					PICKUP_FIRST_LINK_INDEX = 48,
					PICKUP_TILE_STEP = 2,
					PICKUP_TILE_COUNT = 10,
					PICKUP_FLAG_DRAWN = 0x80,
					PICKUP_SPRITE_FRAME_COUNT = 20
				};
				uint32_t textureWidth = TEXTURE_SIZE_UNKNOWN;
				uint32_t textureHeight = TEXTURE_SIZE_UNKNOWN;
				if (g_isPaused == 0)
				{
					g_pickupSpriteFrame += (int16_t)Renderer::g_frameDelta;
					if (g_pickupSpriteFrame > PICKUP_SPRITE_FRAME_COUNT - 1)
						g_pickupSpriteFrame -= PICKUP_SPRITE_FRAME_COUNT;
				}
				int32_t tileIndex = g_pickupSpriteFrame >> 1;

				Vector3F cameraPosition;
				Nu3D::Math::GetPositionVector(&Nu3D::Camera::g_activeCamera.transform, &cameraPosition);
				int32_t cullRadius = Levels::g_type63CullDistance / 4;
				int32_t cullRadiusSquared = cullRadius * cullRadius;

				struct PickupRenderRecord
				{
					Vector3I position;
					uint8_t spriteSheetIndex;
					uint8_t flags;
					int16_t groundY;
				};
				STATIC_ASSERT(sizeof(PickupRenderRecord) == 0x10);

				if (Levels::g_recordData[Levels::RECORD_TYPE_PICKUPS] != 0)
				{
					PickupRenderRecord* pickup = reinterpret_cast<PickupRenderRecord*>(Levels::g_recordData[Levels::RECORD_TYPE_PICKUPS] + 1);
					for (int32_t pickupIndex = 0; pickupIndex < Levels::g_recordData[Levels::RECORD_TYPE_PICKUPS]->recordCount; ++pickupIndex, ++pickup)
					{
						// Offset the animation phase of each pickup.
						tileIndex += PICKUP_TILE_STEP;
						if (tileIndex > PICKUP_TILE_COUNT - 1)
							tileIndex -= PICKUP_TILE_COUNT;

						pickup->flags &= ~PICKUP_FLAG_DRAWN;
						if (pickup->position.y != INT_MIN)
						{
							int32_t deltaZ = ((int32_t)cameraPosition.z - pickup->position.z) >> 4;
							int32_t deltaX = ((int32_t)cameraPosition.x - pickup->position.x) >> 4;
							int32_t distanceSquared = deltaX * deltaX + deltaZ * deltaZ - PICKUP_DISTANCE_BIAS;
							if (distanceSquared < cullRadiusSquared)
							{
								int32_t alphaDistance = cullRadiusSquared - distanceSquared;
								int32_t alpha = (alphaDistance & ~0x3F) > 0x3FC0 ? COLOR_CHANNEL_MAX : alphaDistance >> 6;
								if (pickup->spriteSheetIndex >= PICKUP_FIRST_LINK_INDEX)
								{
									if (g_isPaused == 0)
									{
										Vector3I rotation;
										Nu3D::Link::GetRotation8Bit(pickup->spriteSheetIndex, &rotation);
										Nu3D::Link::SetRotationAbsolute8bit(pickup->spriteSheetIndex,
											rotation.x + Renderer::g_frameDelta * 10,
											rotation.y + ((pickupIndex >> 2 & 3) * 3 + 7) * Renderer::g_frameDelta * 2,
											rotation.z + ((pickupIndex & 7) + 4) * Renderer::g_frameDelta * 2);
									}
									pickup->flags |= PICKUP_FLAG_DRAWN;
								}
								else
								{
									Vector3F position = { (float)pickup->position.x, (float)pickup->position.y, (float)pickup->position.z };
									Renderer::SpriteSheet* sheet = Renderer::g_spriteSheets[pickup->spriteSheetIndex];
									int32_t textureIndex = NGNLoader::GetTextureDataIndex(sheet->texIndex);
									if (textureIndex != 0)
										NGNLoader::RetrieveTextureData(textureIndex, &textureWidth, &textureHeight, 0, 0, 0);

									Vector2F uvTopLeft;
									uvTopLeft.x = (float)sheet->tiles[tileIndex].x / (int32_t)textureWidth;
									uvTopLeft.y = (float)sheet->tiles[tileIndex].y / (int32_t)textureHeight;
									Vector2F uvBottomRight;
									uvBottomRight.x = ((float)sheet->tileWidth + sheet->tiles[tileIndex].x) / (int32_t)textureWidth;
									uvBottomRight.y = ((float)sheet->tileHeight + sheet->tiles[tileIndex].y) / (int32_t)textureHeight;
									pickup->flags |= PICKUP_FLAG_DRAWN;
									RGBA spriteColor;
									spriteColor.value = alpha * 0x1000000 + 0xFFFFFF;
									Renderer::Sprite::QueueQuadSprite(
										&position, 0, 100.0f, 100.0f, &uvTopLeft, &uvBottomRight, textureIndex, spriteColor, SPRITE_FLAGS_BLENDED);

									Vector3F shadowPosition = { (float)pickup->position.x, (float)pickup->groundY - 10.0f, (float)pickup->position.z };
									textureIndex = NGNLoader::GetTextureDataIndex(31);
									if (textureIndex != 0)
										NGNLoader::RetrieveTextureData(textureIndex, &textureWidth, &textureHeight, 0, 0, 0);
									uvTopLeft.x = 96.0f / (int32_t)textureWidth;
									uvTopLeft.y = 96.0f / (int32_t)textureHeight;
									uvBottomRight.x = 127.0f / (int32_t)textureWidth;
									uvBottomRight.y = 127.0f / (int32_t)textureHeight;
									int32_t shadowFlags;
									if ((Renderer::g_deviceBlendShadeCapsCpy & 4) != 0)
									{
										spriteColor.value = 0xFFAAAAAA;
										shadowFlags = 0x20840;
									}
									else
									{
										spriteColor.value = 0x88000000;
										shadowFlags = SPRITE_FLAGS_BLENDED;
									}
									Renderer::Sprite::QueueGroundAlignedSprite(
										&shadowPosition, 0, 80.0f, 80.0f, &uvTopLeft, &uvBottomRight, textureIndex, spriteColor, shadowFlags);
								}
							}
						}
					}
				}

				// Effects: queue a sprite for each effect in range and list the ones near the camera.
				uint32_t nearbyCount = 0;
				uint32_t effectTextureWidth = TEXTURE_SIZE_UNKNOWN;
				uint32_t effectTextureHeight = TEXTURE_SIZE_UNKNOWN;
				cullRadius = Levels::g_type63CullDistance / 4;
				float effectCullRadiusSquared = (float)(cullRadius * cullRadius);
				Nu3D::Math::GetPositionVector(&Nu3D::Camera::g_activeCamera.transform, &cameraPosition);
				if (Levels::g_recordData[RECORD_TYPE_EFFECTS] != 0)
				{
					Vector3I* effect = Levels::g_recordData[RECORD_TYPE_EFFECTS]->data;
					for (int32_t effectIndex = 0; effectIndex < Levels::g_recordData[RECORD_TYPE_EFFECTS]->recordCount; ++effectIndex, ++effect)
					{
						Vector3F position = { (float)effect->x, (float)(effect->y - 250), (float)effect->z };
						Vector3F delta;
						Nu3D::Math::VertexSubtract(&delta, &position, &cameraPosition);
						float distanceSquared = (delta.x * delta.x + delta.y * delta.y + delta.z * delta.z) / 1024.0f;
						if (distanceSquared < effectCullRadiusSquared && effect->y != INT_MIN)
						{
							if (distanceSquared < 65536.0f && (Nu3D::Frustum::TestSphereAllPlanes(&position, 250.0f) & 0x55555575) == 0)
								g_nearbyEffectRecordIndices[nearbyCount++] = (uint8_t)effectIndex;

							Renderer::SpriteSheet* sheet = Renderer::g_spriteSheets[30];
							RGBA color;
							color.a = COLOR_CHANNEL_MAX;
							color.r = COLOR_CHANNEL_MAX;
							color.g = COLOR_CHANNEL_MAX;
							color.b = COLOR_CHANNEL_MAX;
							int32_t textureIndex = NGNLoader::GetTextureDataIndex(sheet->texIndex);
							if (textureIndex != 0)
								NGNLoader::RetrieveTextureData(textureIndex, &effectTextureWidth, &effectTextureHeight, 0, 0, 0);
							Vector2F uvTopLeft = { (float)sheet->tiles[0].x / (int32_t)effectTextureWidth,
								(float)sheet->tiles[0].y / (int32_t)effectTextureHeight };
							Vector2F uvBottomRight = { ((float)sheet->tileWidth + sheet->tiles[0].x) / (int32_t)effectTextureWidth,
								((float)sheet->tileHeight + sheet->tiles[0].y) / (int32_t)effectTextureHeight };
							Renderer::Sprite::QueueQuadSprite(
								&position, 0, 150.0f, 250.0f, &uvTopLeft, &uvBottomRight, textureIndex, color, SPRITE_FLAGS_BLENDED);
						}
					}
				}
				g_nearbyEffectRecordIndices[nearbyCount] = NEARBY_EFFECT_LIST_END;

				Renderer::Particles::DrawParticles();
				while (Renderer::Beam::g_queueHead < BEAM_QUEUE_SIZE)
				{
					int32_t head = Renderer::Beam::g_queueHead;
					Renderer::Beam::DrawSegmentedBeam(Renderer::Beam::g_commands[head].spriteSheetIndex,
						Renderer::Beam::g_commands[head].width,
						Renderer::Beam::g_commands[head].segmentLength,
						&Renderer::Beam::g_commands[head].position,
						&Renderer::Beam::g_commands[head].direction,
						Renderer::Beam::g_commands[head].color.r,
						Renderer::Beam::g_commands[head].color.g,
						Renderer::Beam::g_commands[head].color.b);
					++Renderer::Beam::g_queueHead;
				}
				Renderer::DrawFallingParticles();
				Renderer::DrawLensFlares();
				if (g_levelFileIndex == 2)
					KiteTail::Draw();
			}

			if (Renderer::GetIsSoftwareRendering() == 0)
				Renderer::DrawCinematicBars();
			Renderer::DrawTintOverlay();
			Renderer::FlushRenderQueues();
			Renderer::SetFogEnable(0);
			Renderer::Sprite::DrawQueuedSprite();
			DevDraw::DrawSlots();
			Renderer::EndScene(1);
		}

		Renderer::DoFrameDelay(1);
		Nu3D::Camera::ToggleCameraSkew(0);
		Nu3D::Camera::SetSkewPhase(Renderer::g_frameDelta * 0x10000 / 0x168);
	}

}
