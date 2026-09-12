#include "Renderer/Renderer.h"
#include "Renderer/RendererInternal.h"
#include "SoftwareRenderer.h"
#include "DrawingDevice.h"
#include "Nu3D/Light.h"
#include "Nu3D/Font.h"
#include "Nu3D/Camera.h"
#include "Nu3D/BmpDataNode.h"
#include "Nu3D/Material.h"
#include "Nu3D/Math.h"
#include "NGNLoader/NGNLoader.h"
#include "Nu3D/Patch.h"
#include "Nu3D/Primitive.h"
#include "Nu3D/Particles.h"
#include "Nu3D/Scene.h"
#include "Renderer/Sprite.h"
#include "Renderer/SpriteSheets.h"
#include "Toy2/Toy2.h"
#include "Toy2/Weather.h"
#include "Renderer/Glue.h"
#include "Toy2/Camera.h"
#include "Toy2/Collision.h"
#include "Toy2/Direct6.h"
#include "Logger.h"
#include <cstdarg>
#include <cstring>
#include <cstdio>

// The segmented beam: retail holds 0x0044E100 and 0x0044E1A0 as one object.
namespace Renderer
{
	namespace Beam
	{
		// FUNCTION: TOY2 0x0044E100 [MATCHED]
		void QueueBeam(uint32_t spriteSheetIndex,
			uint32_t width,
			int32_t segmentLength,
			const Vector4I* position,
			const Vector4I* direction,
			uint32_t red,
			uint32_t green,
			uint32_t blue)
		{
			if (g_queueHead != 0)
			{
				g_queueHead--;
				g_commands[g_queueHead].spriteSheetIndex = spriteSheetIndex;
				g_commands[g_queueHead].width = width;
				g_commands[g_queueHead].segmentLength = segmentLength;
				g_commands[g_queueHead].position = *position;
				g_commands[g_queueHead].direction = *direction;
				g_commands[g_queueHead].color.r = red;
				g_commands[g_queueHead].color.g = green;
				g_commands[g_queueHead].color.b = blue;
			}
		}

		// FUNCTION: TOY2 0x0044E1A0 [PROVISIONAL]
		void DrawSegmentedBeam(uint32_t spriteSheetIndex,
			int32_t width,
			int32_t segmentLength,
			const Vector4I* position,
			const Vector4I* direction,
			uint32_t red,
			uint32_t green,
			uint32_t blue)
		{
			SpriteSheet* sheet = g_spriteSheets[spriteSheetIndex];
			uint32_t bitmapWidth = 255;
			uint32_t bitmapHeight = 255;
			int32_t textureDataIndex = NGNLoader::GetTextureDataIndex(sheet->texIndex);
			if (textureDataIndex != 0)
				NGNLoader::RetrieveTextureData(textureDataIndex, &bitmapWidth, &bitmapHeight, 0, 0, 0);

			RGBA color;
			color.r = (uint8_t)red;
			color.g = (uint8_t)green;
			color.b = (uint8_t)blue;
			color.a = 255;

			Vector3F cameraPosition;
			Nu3D::Math::GetPositionVector(&Nu3D::Camera::g_activeCamera.transform, &cameraPosition);

			Vector3F start;
			start.x = (float)position->x * LensFlare::k_positionScale;
			start.y = (float)position->y * LensFlare::k_positionScale;
			start.z = (float)position->z * LensFlare::k_positionScale;

			Vector3F beamDirection;
			beamDirection.x = (float)direction->x * LensFlare::k_positionScale;
			beamDirection.y = (float)direction->y * LensFlare::k_positionScale;
			beamDirection.z = (float)direction->z * LensFlare::k_positionScale;

			Nu3D::Math::VertexSubtract(&cameraPosition, &cameraPosition, &start);

			Vector3F halfWidth;
			Nu3D::Math::VertexCrossProduct(&halfWidth, &cameraPosition, &beamDirection);
			Nu3D::Math::VectorNormalize(&halfWidth, &halfWidth);
			Nu3D::Math::ScaleVector(&halfWidth, &halfWidth, (float)width);

			int32_t beamLength = (int32_t)Vector3F::Length(&beamDirection);
			float remainingLength = (float)beamLength;
			Nu3D::Math::ScaleVector(&beamDirection, &beamDirection, 1.0f / remainingLength);

			Vector3F segmentPoint;
			Nu3D::Math::ScaleVector(&segmentPoint, &beamDirection, remainingLength);
			Nu3D::Math::VertexAdd(&segmentPoint, &segmentPoint, &start);

			Vector3F vertices[4];
			Nu3D::Math::VertexSubtract(&vertices[2], &segmentPoint, &halfWidth);
			Nu3D::Math::VertexAdd(&vertices[3], &segmentPoint, &halfWidth);

			float segmentLengthF = (float)segmentLength;
			int32_t tileIndex = 2;
			do
			{
				float nextLength = remainingLength - segmentLengthF;
				if (nextLength < 0.0f)
					remainingLength = 0.0f;
				else
					remainingLength = nextLength;

				Nu3D::Math::ScaleVector(&segmentPoint, &beamDirection, remainingLength);
				Nu3D::Math::VertexAdd(&segmentPoint, &segmentPoint, &start);
				Nu3D::Math::VertexSubtract(&vertices[0], &segmentPoint, &halfWidth);
				Nu3D::Math::VertexAdd(&vertices[1], &segmentPoint, &halfWidth);

				Vector2F uvTopLeft;
				uvTopLeft.x = (float)sheet->tiles[tileIndex].x / (float)(int32_t)bitmapWidth;
				uvTopLeft.y = (float)sheet->tiles[tileIndex].y / (float)(int32_t)bitmapHeight;

				Vector2F uvBottomRight;
				uvBottomRight.x = ((float)sheet->tileWidth + (float)sheet->tiles[tileIndex].x) / (float)(int32_t)bitmapWidth;
				uvBottomRight.y = ((float)sheet->tiles[tileIndex].y + (float)sheet->tileHeight) / (float)(int32_t)bitmapHeight;

				Sprite::QueueQuadSpriteFromVerts(
					vertices, &uvTopLeft, &uvBottomRight, textureDataIndex, color, RENDER_ZWRITE | RENDER_CULL_NONE | RENDER_ALPHA_CUSTOM);

				vertices[2] = vertices[0];
				vertices[3] = vertices[1];
				tileIndex = 1;
			} while (remainingLength > 0.0f);
		}
	}
}
