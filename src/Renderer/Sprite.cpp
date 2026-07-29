#include "Renderer/Sprite.h"
#include "Renderer/Renderer.h"
#include "Renderer/SpriteSheets.h"
#include "Renderer/Vertices.h"
#include "Nu3D/Sprite.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Math.h"
#include "SoftwareRenderer.h"
#include "NGNLoader/NGNLoader.h"
#include "DrawingDevice.h"
#include "Logger.h"
#include "Renderer/Glue.h"
#include "Toy2/Toy2.h"

#include <STDIO.H>

namespace Renderer
{
	namespace Sprite
	{
		// GLOBAL: TOY2 0x005087E8
		float g_parallaxDepthZPos = 0.99989998;

		// GLOBAL: TOY2 0x00508700
		int32_t g_spriteBuffer3DCount = 2000;

		// GLOBAL: TOY2 0x009B2760
		Nu3D::Sprite g_spriteBuffer3D[2000];

		// GLOBAL: TOY2 0x00884AC0
		Nu3D::Sprite* g_queued2DSprite;

		// GLOBAL: TOY2 0x005086FC
		int32_t g_spriteBuffer2DCount = 2000;

		// GLOBAL: TOY2 0x00971F9C
		Nu3D::Sprite g_spriteBuffer2D[2000];

		// GLOBAL: TOY2 0x005087EC
		WORD g_2DSpriteIndices[4] = { 0, 1, 2, 3 };

		// GLOBAL: TOY2 0x005087BC
		WORD g_groundAlignedSpriteIndices[4] = { 0, 1, 2, 3 };

		// GLOBAL: TOY2 0x005087C4
		WORD g_quadSpriteIndices[4] = { 0, 1, 2, 3 };

		// GLOBAL: TOY2 0x005087CC
		WORD g_billboardSpriteIndices[4] = { 0, 1, 2, 3 };

		// GLOBAL: TOY2 0x005087D4
		WORD g_triangleSpriteIndices[4] = { 0, 1, 2, 3 };

		// GLOBAL: TOY2 0x005087DC
		WORD g_quadSpriteFromVertsIndices[4] = { 0, 1, 2, 3 };

		// GLOBAL: TOY2 0x005087E4
		WORD g_lineSpriteIndices[2] = { 0, 1 };

		// GLOBAL: TOY2 0x00508734
		WORD g_patchQuadIndices[4] = { 0, 1, 2, 3 };

		// GLOBAL: TOY2 0x0050873C
		// clang-format off
		WORD g_patchStripIndices[64] = {
			0, 32, 1, 33, 2, 34, 3, 35, 4, 36, 5, 37, 6, 38, 7, 39,
			8, 40, 9, 41, 10, 42, 11, 43, 12, 44, 13, 45, 14, 46, 15, 47,
			16, 48, 17, 49, 18, 50, 19, 51, 20, 52, 21, 53, 22, 54, 23, 55,
			24, 56, 25, 57, 26, 58, 27, 59, 28, 60, 29, 61, 30, 62, 31, 63
		};
		// clang-format on

		// FUNCTION: TOY2 0x004B68B0
		HRESULT Render2DSprite(Nu3D::Sprite* sprite)
		{
			float xPos = DrawingDevice::GetDestWidth() * sprite->position.x;
			float xOffset = DrawingDevice::GetDestWidth() * (sprite->width + sprite->position.x);
			float yPos = DrawingDevice::GetDestHeight() * sprite->position.y;
			float yOffset = DrawingDevice::GetDestHeight() * (sprite->height + sprite->position.y);

			int32_t renderFlags = sprite->renderFlags;

			int32_t flags;
			float zPos;

			if (renderFlags == RENDER_PARALLAX_BG)
			{
				zPos = g_parallaxDepthZPos;
				flags = RENDER_TEXTURE_WRAP_UV | RENDER_Z | RENDER_ZWRITE | RENDER_BILINEAR_FILTER | RENDER_CULL_NONE;
			}
			else
			{
				zPos = 0.0;
				flags = renderFlags | RENDER_Z | RENDER_ZWRITE | RENDER_CULL_NONE;
			}

			Nu3D::VertexTL vertexData[4];

			vertexData[3].position.x = xOffset;
			vertexData[3].position.y = yPos;
			vertexData[1].position.z = zPos;
			vertexData[3].position.z = zPos;
			vertexData[3].uv.y = sprite->uvTopRight.y;
			vertexData[3].uv.x = sprite->uvTopRight.x;
			vertexData[0].position.x = xPos;
			vertexData[0].uv.x = sprite->uvBottomLeft.x;
			vertexData[0].uv.y = sprite->uvBottomLeft.y;
			vertexData[0].position.y = yOffset;
			vertexData[0].position.z = zPos;
			vertexData[1].position.x = xPos;
			vertexData[1].position.y = yPos;
			vertexData[2].position.y = yOffset;
			vertexData[2].position.x = xOffset;
			vertexData[2].position.z = zPos;
			vertexData[1].uv.x = sprite->uvTopLeft.x;
			vertexData[1].uv.y = sprite->uvTopLeft.y;

			RGBA color = sprite->color;
			vertexData[2].uv.x = sprite->uvBottomRight.x;
			vertexData[1].diffuse = color;
			vertexData[1].rhw = 1.0;
			vertexData[3].diffuse = color;
			vertexData[3].rhw = 1.0;
			vertexData[0].diffuse = color;
			vertexData[0].rhw = 1.0;
			vertexData[2].uv.y = sprite->uvBottomRight.y;
			vertexData[2].diffuse = color;
			vertexData[2].rhw = 1.0;

			Renderer::InitRenderState(flags);
			SoftwareRenderer::g_softwarePrimitiveType = 5;

			Renderer::BindTexture(sprite->textureIndex);

			SoftwareRenderer::g_viewportRect = &sprite->viewportRect;
			SoftwareRenderer::g_reverseDepthSortEnabled = 1;

			return DrawingAPI::DrawIndexedPrimitive(D3DPT_TRIANGLESTRIP, D3DFVF_0x1C4, vertexData, 4, g_2DSpriteIndices, 4, 24);
		}

		// FUNCTION: TOY2 0x004B7B30
		void RenderQuadSprite(Nu3D::Sprite* sprite)
		{
			LPDIRECT3DVERTEXBUFFER destBuffer = g_FVF_14C_Buffer_2.vertexBuffer;
			Nu3D::Vertex* lockedData;

			if (DrawingAPI::LockVertexBuffer(g_FVF_152_Buffer.vertexBuffer, 0x801, (LPVOID*)&lockedData, 0) == 0)
			{
				lockedData[0].position.x = -sprite->width;
				lockedData[0].position.y = -sprite->height;
				lockedData[0].position.z = 0.0f;
				lockedData[0].coords.x = sprite->uvBottomLeft.x;
				lockedData[0].coords.y = sprite->uvBottomLeft.y;
				lockedData[0].diffuse = sprite->color;

				lockedData[1].position.x = -sprite->width;
				lockedData[1].position.y = sprite->height;
				lockedData[1].position.z = 0.0f;
				lockedData[1].coords.x = sprite->uvTopLeft.x;
				lockedData[1].coords.y = sprite->uvTopLeft.y;
				lockedData[1].diffuse = sprite->color;

				lockedData[2].position.x = sprite->width;
				lockedData[2].position.y = -sprite->height;
				lockedData[2].position.z = 0.0f;
				lockedData[2].coords.x = sprite->uvBottomRight.x;
				lockedData[2].coords.y = sprite->uvBottomRight.y;
				lockedData[2].diffuse = sprite->color;

				lockedData[3].position.x = sprite->width;
				lockedData[3].position.y = sprite->height;
				lockedData[3].position.z = 0.0f;
				lockedData[3].coords.x = sprite->uvTopRight.x;
				lockedData[3].coords.y = sprite->uvTopRight.y;
				lockedData[3].diffuse = sprite->color;

				DrawingAPI::UnlockVertexBuffer(g_FVF_152_Buffer.vertexBuffer);

				D3DMATRIX matrix = Nu3D::Camera::g_activeCamera.transform;
				matrix._43 = 0.0f;
				matrix._42 = 0.0f;
				matrix._41 = 0.0f;
				if (sprite->trigIndex != 0)
					Nu3D::Math::MatrixRotateRoll(&matrix, sprite->trigIndex);
				Nu3D::Math::AddWorldSpaceTransform(&matrix, &sprite->position);
				DrawingDevice::SetWorldTransform(&matrix);

				SoftwareRenderer::g_softwarePrimitiveType = 5;

				if (g_drawingTransparentBuckets == 0)
				{
					if (DrawingAPI::ProcessVerticesOnBuffer(destBuffer, 5, 0, 4, g_FVF_152_Buffer.vertexBuffer, 0, 0) == 0)
					{
						Renderer::InitRenderState(sprite->renderFlags);
						Renderer::BindTexture(sprite->textureIndex);
						SoftwareRenderer::g_viewportRect = &sprite->viewportRect;
						DrawingAPI::DrawIndexedPrimitiveVB(D3DPT_TRIANGLESTRIP, destBuffer, g_quadSpriteIndices, 4, 8);
					}
				}
				else
				{
					if (DrawingAPI::ProcessVerticesOnBuffer(destBuffer, 1, 0, 4, g_FVF_152_Buffer.vertexBuffer, 0, 0) == 0)
					{
						SoftwareRenderer::SubmitQuad(sprite->renderFlags, sprite->textureIndex, destBuffer, g_quadSpriteIndices);
					}
				}
			}
		}

		// FUNCTION: TOY2 0x004B7D60
		void RenderBillboardSprite(Nu3D::Sprite* sprite)
		{
			LPDIRECT3DVERTEXBUFFER destBuffer = g_FVF_14C_Buffer_2.vertexBuffer;
			Nu3D::Vertex* lockedData;

			if (DrawingAPI::LockVertexBuffer(g_FVF_152_Buffer.vertexBuffer, 0x801, (LPVOID*)&lockedData, 0) == 0)
			{
				lockedData[0].position.x = -sprite->width;
				lockedData[0].position.y = -sprite->height;
				lockedData[0].position.z = 0.0f;
				lockedData[0].coords.x = sprite->uvBottomLeft.x;
				lockedData[0].coords.y = sprite->uvBottomLeft.y;
				lockedData[0].diffuse = sprite->color;

				lockedData[1].position.x = -sprite->width;
				lockedData[1].position.y = sprite->height;
				lockedData[1].position.z = 0.0f;
				lockedData[1].coords.x = sprite->uvTopLeft.x;
				lockedData[1].coords.y = sprite->uvTopLeft.y;
				lockedData[1].diffuse = sprite->color;

				lockedData[2].position.x = sprite->width;
				lockedData[2].position.y = -sprite->height;
				lockedData[2].position.z = 0.0f;
				lockedData[2].coords.x = sprite->uvBottomRight.x;
				lockedData[2].coords.y = sprite->uvBottomRight.y;
				lockedData[2].diffuse = sprite->color;

				lockedData[3].position.x = sprite->width;
				lockedData[3].position.y = sprite->height;
				lockedData[3].position.z = 0.0f;
				lockedData[3].coords.x = sprite->uvTopRight.x;
				lockedData[3].coords.y = sprite->uvTopRight.y;
				lockedData[3].diffuse = sprite->color;

				DrawingAPI::UnlockVertexBuffer(g_FVF_152_Buffer.vertexBuffer);

				D3DMATRIX matrix;
				Nu3D::Math::BuildIdentityMatrix(&matrix);
				Vector3F direction;
				Nu3D::Math::GetPositionVector(&Nu3D::Camera::g_activeCamera.transform, &direction);
				Nu3D::Math::VertexSubtract(&direction, &direction, &sprite->position);
				direction.y = 0.0f;
				Nu3D::Math::VectorNormalize(&direction, &direction);
				matrix._31 = direction.x;
				matrix._33 = direction.z;
				matrix._32 = direction.y;
				matrix._12 = direction.y;
				matrix._11 = -direction.z;
				matrix._13 = direction.x;
				Nu3D::Math::ScaleMatrix(&matrix);
				Nu3D::Math::AddWorldSpaceTransform(&matrix, &sprite->position);
				DrawingDevice::SetWorldTransform(&matrix);

				SoftwareRenderer::g_softwarePrimitiveType = 5;

				if (g_drawingTransparentBuckets == 0)
				{
					if (DrawingAPI::ProcessVerticesOnBuffer(destBuffer, 5, 0, 4, g_FVF_152_Buffer.vertexBuffer, 0, 0) == 0)
					{
						Renderer::InitRenderState(sprite->renderFlags);
						Renderer::BindTexture(sprite->textureIndex);
						SoftwareRenderer::g_viewportRect = &sprite->viewportRect;
						DrawingAPI::DrawIndexedPrimitiveVB(D3DPT_TRIANGLESTRIP, destBuffer, g_billboardSpriteIndices, 4, 8);
					}
				}
				else
				{
					if (DrawingAPI::ProcessVerticesOnBuffer(destBuffer, 1, 0, 4, g_FVF_152_Buffer.vertexBuffer, 0, 0) == 0)
					{
						SoftwareRenderer::SubmitQuad(sprite->renderFlags, sprite->textureIndex, destBuffer, g_billboardSpriteIndices);
					}
				}
			}
		}

		// FUNCTION: TOY2 0x004B7FC0 [MATCHED]
		void RenderTriangleSprite(Nu3D::Sprite* sprite)
		{
			LPDIRECT3DVERTEXBUFFER destBuffer = g_FVF_14C_Buffer_1.vertexBuffer;
			Nu3D::Vertex* lockedData;

			if (DrawingAPI::LockVertexBuffer(g_FVF_152_Buffer.vertexBuffer, 0x801, (LPVOID*)&lockedData, 0) == 0)
			{
				lockedData[0].position = sprite->position;
				lockedData[0].coords.x = sprite->uvBottomLeft.x;
				lockedData[0].coords.y = sprite->uvBottomLeft.y;
				lockedData[0].diffuse = sprite->color;

				lockedData[1].position = sprite->triVerts[0];
				lockedData[1].coords.x = sprite->uvTopLeft.x;
				lockedData[1].coords.y = sprite->uvTopLeft.y;
				lockedData[1].diffuse = sprite->color;

				lockedData[2].position = sprite->triVerts[1];
				lockedData[2].coords.x = sprite->uvBottomRight.x;
				lockedData[2].coords.y = sprite->uvBottomRight.y;
				lockedData[2].diffuse = sprite->color;

				DrawingAPI::UnlockVertexBuffer(g_FVF_152_Buffer.vertexBuffer);

				D3DMATRIX matrix;
				Nu3D::Math::BuildIdentityMatrix(&matrix);
				DrawingDevice::SetWorldTransform(&matrix);

				SoftwareRenderer::g_softwarePrimitiveType = 5;

				if (g_drawingTransparentBuckets == 0)
				{
					if (DrawingAPI::ProcessVerticesOnBuffer(destBuffer, 5, 0, 3, g_FVF_152_Buffer.vertexBuffer, 0, 0) == 0)
					{
						Renderer::InitRenderState(sprite->renderFlags);
						Renderer::BindTexture(sprite->textureIndex);
						SoftwareRenderer::g_viewportRect = &sprite->viewportRect;
						DrawingAPI::DrawIndexedPrimitiveVB(D3DPT_TRIANGLESTRIP, destBuffer, g_triangleSpriteIndices, 3, 8);
					}
				}
				else
				{
					if (DrawingAPI::ProcessVerticesOnBuffer(destBuffer, 1, 0, 4, g_FVF_152_Buffer.vertexBuffer, 0, 0) == 0)
					{
						SoftwareRenderer::SubmitQuad(sprite->renderFlags, sprite->textureIndex, destBuffer, g_triangleSpriteIndices);
					}
				}
			}
		}

		// FUNCTION: TOY2 0x004B8160 [MATCHED]
		void RenderQuadSpriteFromVerts(Nu3D::Sprite* sprite)
		{
			LPDIRECT3DVERTEXBUFFER destBuffer = g_FVF_14C_Buffer_1.vertexBuffer;
			Nu3D::Vertex* lockedData;

			if (DrawingAPI::LockVertexBuffer(g_FVF_152_Buffer.vertexBuffer, 0x801, (LPVOID*)&lockedData, 0) == 0)
			{
				lockedData[0].position = sprite->position;
				lockedData[0].coords.x = sprite->uvBottomLeft.x;
				lockedData[0].coords.y = sprite->uvBottomLeft.y;
				lockedData[0].diffuse = sprite->color;

				lockedData[1].position = sprite->triVerts[0];
				lockedData[1].coords.x = sprite->uvTopLeft.x;
				lockedData[1].coords.y = sprite->uvTopLeft.y;
				lockedData[1].diffuse = sprite->color;

				lockedData[2].position = sprite->triVerts[1];
				lockedData[2].coords.x = sprite->uvBottomRight.x;
				lockedData[2].coords.y = sprite->uvBottomRight.y;
				lockedData[2].diffuse = sprite->color;

				lockedData[3].position = sprite->triVerts[2];
				lockedData[3].coords.x = sprite->uvTopRight.x;
				lockedData[3].coords.y = sprite->uvTopRight.y;
				lockedData[3].diffuse = sprite->color;

				DrawingAPI::UnlockVertexBuffer(g_FVF_152_Buffer.vertexBuffer);

				D3DMATRIX matrix;
				Nu3D::Math::BuildIdentityMatrix(&matrix);
				DrawingDevice::SetWorldTransform(&matrix);

				SoftwareRenderer::g_softwarePrimitiveType = 2;

				if (g_drawingTransparentBuckets != 0)
				{
					if (DrawingAPI::ProcessVerticesOnBuffer(destBuffer, 1, 0, 4, g_FVF_152_Buffer.vertexBuffer, 0, 0) == 0)
					{
						SoftwareRenderer::SubmitQuad(sprite->renderFlags, sprite->textureIndex, destBuffer, g_quadSpriteFromVertsIndices);
					}
				}
				else
				{
					if (DrawingAPI::ProcessVerticesOnBuffer(destBuffer, 5, 0, 4, g_FVF_152_Buffer.vertexBuffer, 0, 0) == 0)
					{
						Renderer::InitRenderState(sprite->renderFlags);
						Renderer::BindTexture(sprite->textureIndex);
						SoftwareRenderer::g_viewportRect = &sprite->viewportRect;
						DrawingAPI::DrawIndexedPrimitiveVB(D3DPT_TRIANGLESTRIP, destBuffer, g_quadSpriteFromVertsIndices, 4, 8);
					}
				}
			}
		}

		// FUNCTION: TOY2 0x004B8330
		void RenderType10(Nu3D::Sprite* sprite)
		{
			Nu3D::Vertex lineVerts[2];

			lineVerts[0].coords.x = 0.0f;
			lineVerts[0].coords.y = 0.0f;
			lineVerts[1].coords.x = 0.0f;
			lineVerts[1].coords.y = 0.0f;
			lineVerts[0].position = sprite->position;
			lineVerts[1].position = sprite->triVerts[0];
			lineVerts[0].diffuse = sprite->color;
			lineVerts[1].diffuse = sprite->color;

			D3DMATRIX matrix;
			Nu3D::Math::BuildIdentityMatrix(&matrix);
			DrawingDevice::SetWorldTransform(&matrix);

			Renderer::InitRenderState(sprite->renderFlags | RENDER_CULL_NONE);
			SoftwareRenderer::g_softwarePrimitiveType = 5;
			Renderer::BindTexture(0);
			SoftwareRenderer::g_viewportRect = &sprite->viewportRect;

			DrawingAPI::DrawIndexedPrimitive(D3DPT_LINESTRIP, D3DFVF_0x152, lineVerts, 2, g_lineSpriteIndices, 2, 24);
		}

		// FUNCTION: TOY2 0x004B8DD0
		void UpdateQueued2DRender(Nu3D::Sprite* sprite)
		{
			Nu3D::Sprite* queuedSprite = g_queued2DSprite;

			if ((sprite->renderFlags & (RENDER_PRESET_COLOR_OVERLAY | RENDER_PRESET_FADE_OVERLAY)) != 0 && g_queued2DSprite)
			{
				for (Nu3D::Sprite* idx = g_queued2DSprite->next; idx; idx = idx->next)
					queuedSprite = idx;

				sprite->next = 0;
				queuedSprite->next = sprite;
			}
			else
			{
				sprite->next = g_queued2DSprite;
				g_queued2DSprite = sprite;
			}

			int32_t renderFlags = sprite->renderFlags;

			if (renderFlags == RENDER_PRESET_COLOR_OVERLAY)
			{
				sprite->renderFlags = RENDER_ZWRITE | RENDER_CULL_NONE | RENDER_ALPHA_CUSTOM;
			}
			else if (renderFlags == RENDER_PRESET_FADE_OVERLAY)
			{
				uint8_t blue = sprite->color.b;
				uint8_t green = sprite->color.g;

				if (blue == green && blue == sprite->color.r)
				{
					sprite->renderFlags = RENDER_ZWRITE | RENDER_CULL_NONE | RENDER_ALPHA_DEFAULT;
					sprite->color.a = green;
					sprite->color.r = 0;
					sprite->color.g = 0;
					sprite->color.b = 0;
				}
				else
				{
					sprite->renderFlags = RENDER_ZWRITE | RENDER_CULL_NONE | RENDER_ALPHA_ALT;
				}
			}
		}

		// FUNCTION: TOY2 0x004B8CC0
		void Queue2DSprite(float xPosition,
			float yPosition,
			float width,
			float height,
			Vector2F* uvTopLeft,
			Vector2F* uvBottomRight,
			int32_t textureIndex,
			RGBA color,
			int32_t flags)
		{
			if (g_spriteBuffer2DCount)
			{
				RGBA modulatedColor = ModulateColorByAlpha(color, flags);

				Nu3D::Sprite* sprite;

				if (flags == RENDER_PARALLAX_BG)
					sprite = &g_spriteBuffer2D[g_spriteBuffer2DCount];
				else
					sprite = &g_spriteBuffer2D[g_spriteBuffer2DCount--];

				sprite->position.x = xPosition;
				sprite->position.y = yPosition;
				sprite->width = width;
				sprite->type = RENDER_2D_SPRITE;
				sprite->height = height;
				sprite->uvTopLeft = *uvTopLeft;
				sprite->uvBottomLeft.x = uvTopLeft->x;
				sprite->uvBottomLeft.y = uvBottomRight->y;
				sprite->uvTopRight.x = uvBottomRight->x;
				sprite->uvTopRight.y = uvTopLeft->y;
				sprite->uvBottomRight = *uvBottomRight;
				sprite->textureIndex = textureIndex;

				if (flags != RENDER_PARALLAX_BG)
					modulatedColor = ApplyGammaCorrection(modulatedColor);

				sprite->color = modulatedColor;
				sprite->renderFlags = flags;

				Nu3D::Viewport::GetViewClipRect(&sprite->viewportRect);

				if (flags == RENDER_PARALLAX_BG)
					Render2DSprite(sprite);
				else
					UpdateQueued2DRender(sprite);
			}
			else
			{
				Logger::DebugLog("sprite buffer underrun");
			}
		}

		// FUNCTION: TOY2 0x004B9210 [MATCHED]
		void QueueType10(Vector3F* start, Vector3F* end, RGBA color)
		{
			if (g_spriteBuffer3DCount)
			{
				Nu3D::Sprite* sprite = &g_spriteBuffer3D[--g_spriteBuffer3DCount];
				sprite->type = RENDER_TYPE10;
				sprite->position = *start;
				sprite->triVerts[0] = *end;
				sprite->color = Renderer::ApplyGammaCorrection(color);
				sprite->renderFlags = Renderer::g_additionalRenderFlags;
				Nu3D::Viewport::GetViewClipRect(&sprite->viewportRect);
				Nu3D::Sprite::InsertIntoBucket(sprite);
			}
			else
			{
				Logger::DebugLog("sprite buffer underrun");
			}
		}

		// FUNCTION: TOY2 0x004B8A30 [MATCHED]
		void QueueGroundAlignedSprite(Vector3F* position,
			int32_t trigIndex,
			float width,
			float height,
			Vector2F* uvTopLeft,
			Vector2F* uvBottomRight,
			int32_t textureIndex,
			RGBA color,
			int32_t flags)
		{
			if (g_spriteBuffer3DCount)
			{
				RGBA modulatedColor = ModulateColorByAlpha(color, flags);

				Nu3D::Sprite* sprite = &g_spriteBuffer3D[--g_spriteBuffer3DCount];
				sprite->type = RENDER_GROUND_ALIGNED_SPRITE;
				sprite->position = *position;
				sprite->trigIndex = trigIndex;
				sprite->width = width;
				sprite->height = height;
				sprite->uvTopLeft = *uvTopLeft;
				sprite->uvBottomLeft.x = uvTopLeft->x;
				sprite->uvBottomLeft.y = uvBottomRight->y;
				sprite->uvTopRight.x = uvBottomRight->x;
				sprite->uvTopRight.y = uvTopLeft->y;
				sprite->uvBottomRight = *uvBottomRight;
				sprite->textureIndex = textureIndex;
				modulatedColor = ApplyGammaCorrection(modulatedColor);
				sprite->color = modulatedColor;
				sprite->renderFlags = flags;
				Nu3D::Viewport::GetViewClipRect(&sprite->viewportRect);
				Nu3D::Sprite::InsertIntoBucket(sprite);
			}
			else
			{
				Logger::DebugLog("sprite buffer underrun");
			}
		}

		// FUNCTION: TOY2 0x004B8E60 [MATCHED]
		void QueueQuadSprite(Vector3F* position,
			int32_t trigIndex,
			float width,
			float height,
			Vector2F* uvTopLeft,
			Vector2F* uvBottomRight,
			int32_t textureIndex,
			RGBA color,
			int32_t flags)
		{
			if (g_spriteBuffer3DCount)
			{
				RGBA modulatedColor = ModulateColorByAlpha(color, flags);

				Nu3D::Sprite* sprite = &g_spriteBuffer3D[--g_spriteBuffer3DCount];
				sprite->type = RENDER_QUADSPRITE;
				sprite->position = *position;
				sprite->trigIndex = trigIndex;
				sprite->width = width;
				sprite->height = height;
				sprite->uvTopLeft = *uvTopLeft;
				sprite->uvBottomLeft.x = uvTopLeft->x;
				sprite->uvBottomLeft.y = uvBottomRight->y;
				sprite->uvTopRight.x = uvBottomRight->x;
				sprite->uvTopRight.y = uvTopLeft->y;
				sprite->uvBottomRight = *uvBottomRight;
				sprite->textureIndex = textureIndex;
				modulatedColor = ApplyGammaCorrection(modulatedColor);
				sprite->color = modulatedColor;
				sprite->renderFlags = flags;
				Nu3D::Viewport::GetViewClipRect(&sprite->viewportRect);
				Nu3D::Sprite::InsertIntoBucket(sprite);
			}
			else
			{
				Logger::DebugLog("sprite buffer underrun");
			}
		}

		// FUNCTION: TOY2 0x004B8F40 [MATCHED]
		void QueueBillboardSprite(Vector3F* position,
			int32_t trigIndex,
			float width,
			float height,
			Vector2F* uvTopLeft,
			Vector2F* uvBottomRight,
			int32_t textureIndex,
			RGBA color,
			int32_t flags)
		{
			if (g_spriteBuffer3DCount)
			{
				RGBA modulatedColor = ModulateColorByAlpha(color, flags);

				Nu3D::Sprite* sprite = &g_spriteBuffer3D[--g_spriteBuffer3DCount];
				sprite->type = RENDER_BILLBOARD_SPRITE;
				sprite->position = *position;
				sprite->trigIndex = trigIndex;
				sprite->width = width;
				sprite->height = height;
				sprite->uvTopLeft = *uvTopLeft;
				sprite->uvBottomLeft.x = uvTopLeft->x;
				sprite->uvBottomLeft.y = uvBottomRight->y;
				sprite->uvTopRight.x = uvBottomRight->x;
				sprite->uvTopRight.y = uvTopLeft->y;
				sprite->uvBottomRight = *uvBottomRight;
				sprite->textureIndex = textureIndex;
				modulatedColor = ApplyGammaCorrection(modulatedColor);
				sprite->color = modulatedColor;
				sprite->renderFlags = flags;
				Nu3D::Viewport::GetViewClipRect(&sprite->viewportRect);
				Nu3D::Sprite::InsertIntoBucket(sprite);
			}
			else
			{
				Logger::DebugLog("sprite buffer underrun");
			}
		}

		// FUNCTION: TOY2 0x004B9100 [MATCHED]
		void QueueQuadSpriteFromVerts(Vector3F* verts, Vector2F* uvTopLeft, Vector2F* uvBottomRight, int32_t textureIndex, RGBA color, int32_t flags)
		{
			if (g_spriteBuffer3DCount)
			{
				RGBA modulatedColor = ModulateColorByAlpha(color, flags);

				Nu3D::Sprite* sprite = &g_spriteBuffer3D[--g_spriteBuffer3DCount];
				sprite->type = RENDER_QUAD_SPRITE_FROM_VERTS;
				sprite->position = verts[0];
				sprite->triVerts[0] = verts[1];
				sprite->triVerts[1] = verts[2];
				sprite->triVerts[2] = verts[3];
				sprite->uvBottomLeft.x = uvTopLeft->x;
				sprite->uvBottomLeft.y = uvBottomRight->y;
				sprite->uvTopLeft = *uvTopLeft;
				sprite->uvBottomRight = *uvBottomRight;
				sprite->uvTopRight.x = uvBottomRight->x;
				sprite->uvTopRight.y = uvTopLeft->y;
				sprite->textureIndex = textureIndex;
				modulatedColor = ApplyGammaCorrection(modulatedColor);
				sprite->color = modulatedColor;
				sprite->renderFlags = flags;
				Nu3D::Viewport::GetViewClipRect(&sprite->viewportRect);
				Nu3D::Sprite::InsertIntoBucket(sprite);
			}
			else
			{
				Logger::DebugLog("sprite buffer underrun");
			}
		}

		// FUNCTION: TOY2 0x004946A0
		int16_t DrawTiledFixed(int16_t xPos, int16_t yPos, int16_t sheetIndex, int16_t tileIndex)
		{
			SpriteSheet* sheet = g_spriteSheets[sheetIndex];
			if (sheet)
			{
				int32_t textureDataIndex = NGNLoader::GetTextureDataIndex(sheet->texIndex);
				Vector2F uvTopLeft;
				Vector2F uvBottomRight;
				if (textureDataIndex != 0)
				{
					uint32_t bitmapWidth;
					uint32_t bitmapHeight;
					NGNLoader::RetrieveTextureData(textureDataIndex, &bitmapWidth, &bitmapHeight, 0, 0, 0);

					uvTopLeft.x = (float)sheet->tiles[tileIndex].x / (int32_t)bitmapWidth;
					uvTopLeft.y = (float)sheet->tiles[tileIndex].y / (int32_t)bitmapHeight;
					uvBottomRight.x = ((float)sheet->tileWidth + sheet->tiles[tileIndex].x) / (int32_t)bitmapWidth;
					uvBottomRight.y = ((float)sheet->tileHeight + sheet->tiles[tileIndex].y) / (int32_t)bitmapHeight;
				}

				RGBA color = { (uint8_t)Nu3D::Camera::g_cameraTintRed, (uint8_t)Nu3D::Camera::g_cameraTintGreen, (uint8_t)Nu3D::Camera::g_cameraTintBlue, 255 };

				Queue2DSprite(xPos * (1.0f / 320.0f),
					yPos * (1.0f / g_virtualScreenHeight),
					sheet->tileWidth * (1.0f / 320.0f),
					sheet->tileHeight * (1.0f / g_virtualScreenHeight),
					&uvTopLeft,
					&uvBottomRight,
					textureDataIndex,
					color,
					RENDER_ZWRITE | RENDER_CULL_NONE | RENDER_ALPHA_DEFAULT);
			}
			return 1;
		}

		// FUNCTION: TOY2 0x00494820
		int16_t DrawTile(int16_t xPos, int16_t yPos, int16_t sheetIndex, int16_t tileIndex)
		{
			SpriteSheet* sheet = g_spriteSheets[sheetIndex];
			if (sheet)
			{
				int32_t textureDataIndex = NGNLoader::GetTextureDataIndex(sheet->texIndex);
				Vector2F uvTopLeft;
				Vector2F uvBottomRight;
				if (textureDataIndex != 0)
				{
					uint32_t bitmapWidth;
					uint32_t bitmapHeight;
					NGNLoader::RetrieveTextureData(textureDataIndex, &bitmapWidth, &bitmapHeight, 0, 0, 0);

					uvTopLeft.x = (float)sheet->tiles[tileIndex].x / (int32_t)bitmapWidth;
					uvTopLeft.y = (float)sheet->tiles[tileIndex].y / (int32_t)bitmapHeight;
					uvBottomRight.x = ((float)sheet->tileWidth + sheet->tiles[tileIndex].x) / (int32_t)bitmapWidth;
					uvBottomRight.y = ((float)sheet->tileHeight + sheet->tiles[tileIndex].y) / (int32_t)bitmapHeight;
				}

				RGBA color = { (uint8_t)Nu3D::Camera::g_cameraTintRed, (uint8_t)Nu3D::Camera::g_cameraTintGreen, (uint8_t)Nu3D::Camera::g_cameraTintBlue, 255 };

				Queue2DSprite(xPos * (1.0f / g_virtualScreenWidth),
					yPos * (1.0f / g_virtualScreenHeight),
					sheet->tileWidth * (1.0f / g_virtualScreenWidth),
					sheet->tileHeight * (1.0f / g_virtualScreenHeight),
					&uvTopLeft,
					&uvBottomRight,
					textureDataIndex,
					color,
					RENDER_ZWRITE | RENDER_CULL_NONE | RENDER_ALPHA_DEFAULT);
			}
			return 1;
		}

		// FUNCTION: TOY2 0x00493DC0
		int16_t DrawColouredFixed(int16_t xPos, int16_t yPos, int16_t sheetIndex, int16_t tileIndex, uint8_t red, uint8_t green, uint8_t blue)
		{
			SpriteSheet* sheet = g_spriteSheets[sheetIndex];
			if (sheet)
			{
				int32_t textureDataIndex = NGNLoader::GetTextureDataIndex(sheet->texIndex);
				Vector2F uvTopLeft;
				Vector2F uvBottomRight;
				if (textureDataIndex != 0)
				{
					uint32_t bitmapWidth;
					uint32_t bitmapHeight;
					NGNLoader::RetrieveTextureData(textureDataIndex, &bitmapWidth, &bitmapHeight, 0, 0, 0);

					uvTopLeft.x = (float)sheet->tiles[tileIndex].x / (int32_t)bitmapWidth;
					uvTopLeft.y = (float)sheet->tiles[tileIndex].y / (int32_t)bitmapHeight;
					uvBottomRight.x = ((float)sheet->tileWidth + sheet->tiles[tileIndex].x) / (int32_t)bitmapWidth;
					uvBottomRight.y = ((float)sheet->tileHeight + sheet->tiles[tileIndex].y) / (int32_t)bitmapHeight;
				}

				RGBA color = { blue, green, red, 255 };
				Queue2DSprite((float)xPos * (1.0f / 320.0f),
					(float)yPos * (1.0f / g_virtualScreenHeight),
					(float)sheet->tileWidth * (1.0f / 320.0f),
					(float)sheet->tileHeight * (1.0f / g_virtualScreenHeight),
					&uvTopLeft,
					&uvBottomRight,
					textureDataIndex,
					color,
					RENDER_ZWRITE | RENDER_CULL_NONE | RENDER_ALPHA_DEFAULT);
			}
			return 1;
		}

		// FUNCTION: TOY2 0x00493C30
		int16_t DrawColoured(int16_t xPos, int16_t yPos, int16_t sheetIndex, int16_t tileIndex, uint8_t red, uint8_t green, uint8_t blue)
		{
			SpriteSheet* sheet = g_spriteSheets[sheetIndex];
			if (sheet)
			{
				int32_t textureDataIndex = NGNLoader::GetTextureDataIndex(sheet->texIndex);
				Vector2F uvTopLeft;
				Vector2F uvBottomRight;
				if (textureDataIndex != 0)
				{
					uint32_t bitmapWidth;
					uint32_t bitmapHeight;
					NGNLoader::RetrieveTextureData(textureDataIndex, &bitmapWidth, &bitmapHeight, 0, 0, 0);

					uvTopLeft.x = (float)sheet->tiles[tileIndex].x / (int32_t)bitmapWidth;
					uvTopLeft.y = (float)sheet->tiles[tileIndex].y / (int32_t)bitmapHeight;
					uvBottomRight.x = ((float)sheet->tileWidth + sheet->tiles[tileIndex].x) / (int32_t)bitmapWidth;
					uvBottomRight.y = ((float)sheet->tileHeight + sheet->tiles[tileIndex].y) / (int32_t)bitmapHeight;
				}

				RGBA color = { blue, green, red, 255 };
				float inverseHeight = 1.0f / g_virtualScreenHeight;
				float inverseWidth = 1.0f / g_virtualScreenWidth;
				Queue2DSprite((float)xPos * inverseWidth,
					(float)yPos * inverseHeight,
					(float)sheet->tileWidth * inverseWidth,
					(float)sheet->tileHeight * inverseHeight,
					&uvTopLeft,
					&uvBottomRight,
					textureDataIndex,
					color,
					RENDER_ZWRITE | RENDER_CULL_NONE | RENDER_ALPHA_DEFAULT);
			}
			return 1;
		}

		// FUNCTION: TOY2 0x0049D2D0 [MATCHED]
		void QueueSegment(Vector3I* start, Vector3I* delta, int32_t red, int32_t green, int32_t blue)
		{
			Vector3F startPosition;
			startPosition.x = (float)(start->x >> 5);
			startPosition.y = (float)(start->y >> 5);
			startPosition.z = (float)(start->z >> 5);

			Vector3F endPosition;
			endPosition.x = (float)((start->x + delta->x) >> 5);
			endPosition.y = (float)((start->y + delta->y) >> 5);
			endPosition.z = (float)((start->z + delta->z) >> 5);

			RGBA color;
			color.r = (uint8_t)red;
			color.g = (uint8_t)green;
			color.b = (uint8_t)blue;
			color.a = 255;
			QueueType10(&startPosition, &endPosition, color);
		}

		// FUNCTION: TOY2 0x0049D750 [MATCHED]
		void DrawWhiteText(char* text, int32_t screenY, int32_t screenX) { Renderer::DrawBitmapText(text, screenY, screenX, 255, 255, 255, 0x60); }

		// FUNCTION: TOY2 0x0049D7A0
		void DrawBackdropTransition(int32_t* framesRemaining, int32_t* backdropIndex, int32_t duration)
		{
			if (*framesRemaining > 0)
			{
				*framesRemaining -= Renderer::g_frameDelta;
			}
			else
			{
				*framesRemaining = duration;

				if (++*backdropIndex > 9)
					*backdropIndex = 0;

				Toy2::g_nextBackdropId = *backdropIndex + 48;
				Renderer::Glue::SetBackdrop(Toy2::g_nextBackdropId);
			}

			const int32_t overlayFlags = RENDER_ZWRITE | RENDER_CULL_NONE | RENDER_ALPHA_DEFAULT;

			if (*framesRemaining < 25)
			{
				Vector2F uvTopLeft = { 0.0f, 0.0f };
				Vector2F uvBottomRight = { 1.0f, 1.0f };
				RGBA fadeColor = { 0 };
				fadeColor.a = (uint8_t)(-10 * *framesRemaining - 1);
				Queue2DSprite(0.0f, 0.0f, 1.0f, 1.0f, &uvTopLeft, &uvBottomRight, 0, fadeColor, overlayFlags);
			}

			if (*framesRemaining > duration - 25)
			{
				Vector2F uvTopLeft = { 0.0f, 0.0f };
				Vector2F uvBottomRight = { 1.0f, 1.0f };
				RGBA fadeColor = { 0 };
				fadeColor.a = (uint8_t)(10 * *framesRemaining - 10 * duration - 1);
				Queue2DSprite(0.0f, 0.0f, 1.0f, 1.0f, &uvTopLeft, &uvBottomRight, 0, fadeColor, overlayFlags);
			}
		}

		// FUNCTION: TOY2 0x00493F40
		int16_t DrawScaled(int16_t xPos,
			int16_t yPos,
			int16_t sheetIndex,
			int32_t tileIndex,
			uint32_t red,
			uint32_t green,
			uint32_t blue,
			uint32_t flags,
			int32_t scaleX,
			int32_t scaleY)
		{
			SpriteSheet* sheet;

			if ((sheetIndex & 0x8000) == 0)
				sheet = g_spriteSheets[(int16_t)sheetIndex];
			else
				sheet = g_fallbackSpriteSheet;

			if (! sheet)
				return 1;

			int32_t textureDataIndex = NGNLoader::GetTextureDataIndex(sheet->texIndex);

			Vector2F uvMin;
			Vector2F uvMax;

			if (textureDataIndex)
			{
				uint32_t bitmapWidth;
				uint32_t bitmapHeight;
				NGNLoader::RetrieveTextureData(textureDataIndex, &bitmapWidth, &bitmapHeight, 0, 0, 0);

				int16_t ti = (int16_t)tileIndex;

				double dW = (double)bitmapWidth;
				double dH = (double)bitmapHeight;

				uvMin.x = sheet->tiles[ti].x / dW;
				uvMin.y = sheet->tiles[ti].y / dH;

				uvMax.x = ((double)sheet->tileWidth + (double)sheet->tiles[ti].x) / dW;
				uvMax.y = ((double)sheet->tileHeight + (double)sheet->tiles[ti].y) / dH;
			}

			RGBA packedColor;
			packedColor.g = (uint8_t)green;
			packedColor.r = (uint8_t)red;
			packedColor.b = (uint8_t)blue;

			uint8_t* alphaPtr = &packedColor.a;

			if (! alphaPtr)
				alphaPtr = (uint8_t*)&red;

			int32_t blendMode = flags & 96;
			int32_t renderFlags;

			if (blendMode != 0)
			{
				if (blendMode == 32)
				{
					*alphaPtr = 255;
					renderFlags = RENDER_ZWRITE | RENDER_CULL_NONE | RENDER_ALPHA_CUSTOM;
				}
				else if (blendMode == 64)
				{
					*alphaPtr = 255;
					renderFlags = RENDER_ZWRITE | RENDER_CULL_NONE | RENDER_ALPHA_ALT;
				}
				else
				{
					*alphaPtr = 255 - (uint8_t)((flags >> 8) & 0xFF);
					renderFlags = RENDER_ZWRITE | RENDER_CULL_NONE | RENDER_ALPHA_DEFAULT;
				}
			}
			else
			{
				*alphaPtr = 128;
				renderFlags = RENDER_ZWRITE | RENDER_CULL_NONE | RENDER_ALPHA_DEFAULT;
			}

			float invHeight = 1.0 / g_virtualScreenHeight;
			float invWidth = 1.0 / g_virtualScreenWidth;

			Queue2DSprite(xPos * invWidth,
				yPos * invHeight,
				((scaleX * sheet->tileWidth) >> 12) * invWidth,
				((scaleY * sheet->tileHeight) >> 12) * invHeight,
				&uvMin,
				&uvMax,
				textureDataIndex,
				packedColor,
				renderFlags);

			return 1;
		}

		// FUNCTION: TOY2 0x004942D0
		int16_t DrawScaledFixed(int16_t xPos,
			int16_t yPos,
			int32_t sheetIndex,
			int32_t tileIndex,
			uint32_t red,
			uint32_t green,
			uint32_t blue,
			uint32_t flags,
			int32_t scaleX,
			int32_t scaleY)
		{
			SpriteSheet* sheet = (int16_t)sheetIndex < 0 ? g_fallbackSpriteSheet : g_spriteSheets[(int16_t)sheetIndex];
			if (! sheet)
				return 1;

			int32_t textureDataIndex = NGNLoader::GetTextureDataIndex(sheet->texIndex);
			Vector2F uvMin;
			Vector2F uvMax;

			if (textureDataIndex)
			{
				uint32_t bitmapWidth;
				uint32_t bitmapHeight;
				NGNLoader::RetrieveTextureData(textureDataIndex, &bitmapWidth, &bitmapHeight, 0, 0, 0);

				int16_t tile = (int16_t)tileIndex;
				uvMin.x = (float)sheet->tiles[tile].x / (float)(int32_t)bitmapWidth;
				uvMin.y = (float)sheet->tiles[tile].y / (float)(int32_t)bitmapHeight;
				uvMax.x = ((float)sheet->tileWidth + (float)sheet->tiles[tile].x) / (float)(int32_t)bitmapWidth;
				uvMax.y = ((float)sheet->tileHeight + (float)sheet->tiles[tile].y) / (float)(int32_t)bitmapHeight;
			}

			RGBA color;
			color.r = (uint8_t)red;
			color.g = (uint8_t)green;
			color.b = (uint8_t)blue;

			int32_t renderFlags;
			switch (flags & 0x60)
			{
				case 0:
					color.a = 128;
					renderFlags = RENDER_ZWRITE | RENDER_CULL_NONE | RENDER_ALPHA_DEFAULT;
					break;
				case 0x20:
					color.a = 255;
					renderFlags = RENDER_ZWRITE | RENDER_CULL_NONE | RENDER_ALPHA_CUSTOM;
					break;
				case 0x40:
					color.a = 255;
					renderFlags = RENDER_ZWRITE | RENDER_CULL_NONE | RENDER_ALPHA_ALT;
					break;
				default:
					color.a = 255 - (uint8_t)(flags >> 8);
					renderFlags = RENDER_ZWRITE | RENDER_CULL_NONE | RENDER_ALPHA_DEFAULT;
					break;
			}

			float invHeight = 1.0f / g_virtualScreenHeight;
			Queue2DSprite((float)xPos * (1.0f / 512.0f),
				(float)yPos * invHeight,
				(float)((sheet->tileWidth * scaleX) >> 12) * (1.0f / 512.0f),
				(float)((sheet->tileHeight * scaleY) >> 12) * invHeight,
				&uvMin,
				&uvMax,
				textureDataIndex,
				color,
				renderFlags);
			return 1;
		}

		// FUNCTION: TOY2 0x004B6300 [MATCHED]
		void ResetQueue()
		{
			g_queued2DSprite = 0;
			g_spriteBuffer2DCount = 2000;
		}

		// FUNCTION: TOY2 0x004B6C10
		void RenderType8(Nu3D::Material* material, Renderer::RenderEntry* entry)
		{
			D3DMATRIX* transforms = entry->instanceData->matrices;
			Nu3D::InstanceData* instanceData = entry->instanceData;

			SoftwareRenderer::g_softwarePrimitiveType = instanceData->unkInt6;

			int32_t renderFlags = instanceData->renderFlags;
			int32_t metadata = material->metadata;
			if (metadata & 1)
				renderFlags |= RENDER_ZWRITE | RENDER_ALPHA_DEFAULT;
			if (metadata & 0x10)
				renderFlags |= RENDER_ZWRITE | RENDER_ALPHA_CUSTOM;
			if (metadata & 0x20)
				renderFlags |= RENDER_ZWRITE | RENDER_ALPHA_ALT;
			if (metadata & 0x200)
				renderFlags |= RENDER_ZWRITE | RENDER_ALPHA_TEX_MODULATE;
			if (metadata & 0x400)
				renderFlags |= RENDER_ZWRITE | RENDER_ALPHA_TEX_MOD_CUSTOM;
			if (metadata & 0x800)
				renderFlags |= RENDER_ZWRITE | RENDER_ALPHA_TEX_MOD_ALT;

			Nu3D::Patch* patch = entry->patch;
			renderFlags |= patch->renderFlags;
			if (metadata & 8)
				renderFlags = (renderFlags & ~0x30) | RENDER_CULL_NONE;

			int32_t projectTexture = material == NGNLoader::g_tex14Materials[0] || material == NGNLoader::g_tex14Materials[1]
				|| material == NGNLoader::g_tex14Materials[2];
			if (projectTexture)
				renderFlags &= ~0x180;

			Renderer::InitRenderState(renderFlags);

			LPDIRECT3DVERTEXBUFFER sourceBuffer = patch->patchVertices.vertexBuffer;
			LPDIRECT3DVERTEXBUFFER destBuffer = g_FVF_14C_Buffer_2.vertexBuffer;
			if (sourceBuffer != 0 && destBuffer != 0)
			{
				DWORD vertexOp = 1;
				if ((instanceData->renderModeFlags & Nu3D::INSTANCE_RENDER_VERTEX_LIGHTING) != 0)
					vertexOp = 0x401;
				HRESULT result;
				WORD* indices;

				if (patch->controlPointCount != 4)
				{
					int32_t halfVertexCount = patch->patchVertices.vertexCount / 2;
					DrawingDevice::SetWorldTransform(&transforms[0]);
					DrawingAPI::ProcessVerticesOnBuffer(destBuffer, vertexOp, 0, halfVertexCount, sourceBuffer, 0, 0);
					DrawingDevice::SetWorldTransform(&transforms[1]);
					result = DrawingAPI::ProcessVerticesOnBuffer(destBuffer, vertexOp, 32, halfVertexCount, sourceBuffer, halfVertexCount, 0);
					indices = g_patchStripIndices;
				}
				else
				{
					DrawingDevice::SetWorldTransform(&transforms[0]);
					result = DrawingAPI::ProcessVerticesOnBuffer(destBuffer, vertexOp, 0, 1, sourceBuffer, 0, 0);
					DrawingDevice::SetWorldTransform(&transforms[1]);
					result |= DrawingAPI::ProcessVerticesOnBuffer(destBuffer, vertexOp, 1, 1, sourceBuffer, 1, 0);
					DrawingDevice::SetWorldTransform(&transforms[2]);
					result |= DrawingAPI::ProcessVerticesOnBuffer(destBuffer, vertexOp, 2, 1, sourceBuffer, 2, 0);
					DrawingDevice::SetWorldTransform(&transforms[3]);
					result |= DrawingAPI::ProcessVerticesOnBuffer(destBuffer, vertexOp, 3, 1, sourceBuffer, 3, 0);
					indices = g_patchQuadIndices;
				}

				if (result == 0)
				{
					if (projectTexture)
						Renderer::Vertices::ProjectToScreen(&patch->patchVertices, transforms, patch->controlPointCount);

					if (g_materialHorzOffset != 0.0f || g_materialVertOffset != 0.0f)
						Renderer::Vertices::ApplyOffset(&patch->patchVertices, g_materialHorzOffset, g_materialVertOffset);

					SoftwareRenderer::g_viewportRect = &instanceData->clipRect;
					Nu3D::VertexTL* lockedVertices;
					if (DrawingAPI::LockVertexBuffer(destBuffer, 0x801, (LPVOID*)&lockedVertices, 0) == 0)
					{
						if (g_drawingTransparentBuckets != 0)
						{
							SoftwareRenderer::SubmitTriangleStripRaw(renderFlags, lockedVertices, entry, indices, patch->patchVertices.vertexCount);
						}
						else
						{
							DrawingAPI::DrawIndexedPrimitive(
								D3DPT_TRIANGLESTRIP, D3DFVF_0x1C4, lockedVertices, 64, indices, patch->patchVertices.vertexCount, 8);
						}
						DrawingAPI::UnlockVertexBuffer(destBuffer);
					}
				}
			}
		}

		// FUNCTION: TOY2 0x004B70E0
		void RenderType9(Nu3D::Material* material, Renderer::RenderEntry* entry)
		{
			Nu3D::InstanceData* instanceData = entry->instanceData;
			Nu3D::Primitive* primitive = entry->primitive;
			SoftwareRenderer::g_softwarePrimitiveType = instanceData->unkInt6;

			int32_t renderFlags = instanceData->renderFlags | primitive->renderFlags;
			if (primitive->header[0].drawType == 4 || primitive->header[0].drawType == 5)
				renderFlags |= RENDER_CULL_NONE;

			int32_t projectTexture = material == NGNLoader::g_tex14Materials[0] || material == NGNLoader::g_tex14Materials[1]
				|| material == NGNLoader::g_tex14Materials[2];

			if (g_drawingTransparentBuckets == 0 || (instanceData->renderModeFlags & Nu3D::INSTANCE_RENDER_VERTEX_LIGHTING) != 0)
				Renderer::SetupMaterialRenderState(material, renderFlags);

			LPDIRECT3DVERTEXBUFFER sourceBuffer = primitive->patchVerts.vertexBuffer;
			LPDIRECT3DVERTEXBUFFER destBuffer = g_FVF_14C_Buffer_1.vertexBuffer;
			if (instanceData->horzOffset != 0.0f || instanceData->vertOffset != 0.0f
				|| (instanceData->renderModeFlags & (Nu3D::INSTANCE_RENDER_VERTEX_LIGHTING | Nu3D::INSTANCE_RENDER_VERTEX_COLOR_MODULATION)) != 0
				|| (material->metadata & 0x100) != 0 || projectTexture || primitive->header[0].drawType == 4 || primitive->header[0].drawType == 5
				|| g_drawingTransparentBuckets != 0)
			{
				destBuffer = g_FVF_14C_Buffer_2.vertexBuffer;
			}

			++g_submittedPrimitiveCount;
			g_submittedVertexCount += primitive->patchVerts.vertexCount;
			if (sourceBuffer == 0 || destBuffer == 0)
				return;

			DWORD vertexOp = 1;
			if ((instanceData->renderModeFlags & Nu3D::INSTANCE_RENDER_VERTEX_LIGHTING) != 0)
				vertexOp = 0x401;
			if ((renderFlags & RENDER_NO_CLIP) == 0 && g_drawingTransparentBuckets == 0)
				vertexOp |= 4;

			HRESULT result;
			if (primitive->header[0].drawType < 4 || primitive->header[0].drawType > 5)
			{
				DrawingDevice::SetWorldTransform(&instanceData->matrices[0]);
				result = DrawingAPI::ProcessVerticesOnBuffer(destBuffer, vertexOp, 0, primitive->patchVerts.vertexCount, sourceBuffer, 0, 0);
			}
			else
			{
				result = DD_OK;
			}

			if ((material->metadata & 0x100) != 0)
			{
				Renderer::Vertices::ProjectCustomTextureCoordinates(&primitive->patchVerts, &instanceData->matrices[0], &instanceData->textureProjection);
			}
			if (projectTexture)
				Renderer::Vertices::ProjectTex14Coordinates(&primitive->patchVerts, &instanceData->matrices[0]);
			if ((instanceData->renderModeFlags & Nu3D::INSTANCE_RENDER_VERTEX_COLOR_MODULATION) != 0)
			{
				Renderer::Vertices::ModuleColor(
					&primitive->patchVerts, instanceData->vertexModColor.r, instanceData->vertexModColor.g, instanceData->vertexModColor.b);
			}
			if (g_materialHorzOffset != 0.0f || g_materialVertOffset != 0.0f)
				Renderer::Vertices::ApplyOffset(&primitive->patchVerts, g_materialHorzOffset, g_materialVertOffset);

			if (result != DD_OK)
				return;

			DWORD drawFlags = 8;
			if ((renderFlags & RENDER_NO_CLIP) != 0 || g_drawingTransparentBuckets != 0)
				drawFlags = 12;
			SoftwareRenderer::g_viewportRect = &instanceData->clipRect;

			for (int32_t headerIndex = 0; headerIndex < primitive->headerCount; ++headerIndex)
			{
				Nu3D::Primitive::Header& header = primitive->header[headerIndex];
				if (g_drawTriangleWireframes != 0)
				{
					for (int32_t indexOffset = 0; indexOffset < header.indexCount; indexOffset += 3)
					{
						DrawingAPI::DrawIndexedPrimitiveVB(D3DPT_LINESTRIP, destBuffer, header.indices + indexOffset, 3, drawFlags);
					}
					continue;
				}

				switch (header.drawType)
				{
					case 0:
						g_submittedTriangleCount += header.indexCount / 3;
						if (g_drawingTransparentBuckets == 0)
						{
							DrawingAPI::DrawIndexedPrimitiveVB(D3DPT_TRIANGLELIST, destBuffer, header.indices, header.indexCount, drawFlags);
						}
						else
						{
							SoftwareRenderer::SubmitTriangleList(renderFlags, destBuffer, entry, header.indices, header.indexCount);
						}
						break;

					case 2:
						g_submittedTriangleCount += header.indexCount >> 1;
						if (g_drawingTransparentBuckets == 0)
						{
							DrawingAPI::DrawIndexedPrimitiveVB(D3DPT_TRIANGLESTRIP, destBuffer, header.indices, header.indexCount, drawFlags);
						}
						else
						{
							SoftwareRenderer::SubmitTriangleStrip(renderFlags, destBuffer, entry, header.indices, header.indexCount);
						}
						break;

					case 3: {
						g_submittedTriangleCount += header.indexCount >> 1;
						WORD* indices = header.indices;
						for (int32_t indexCount = header.indexCount; indexCount != 0; indexCount -= 4)
						{
							DrawingAPI::DrawIndexedPrimitiveVB(D3DPT_TRIANGLESTRIP, destBuffer, indices, 4, drawFlags);
							indices += 4;
						}
						break;
					}

					case 4:
					case 5: {
						WORD* indices = header.indices;
						Vector3F billboardPosition;
						Nu3D::Math::TransformPointByMatrix(
							&billboardPosition, &primitive->patchVerts.data.vertices[*indices].position, &instanceData->matrices[0]);

						D3DMATRIX billboardMatrix;
						if (header.drawType == 4)
						{
							Nu3D::Math::BuildIdentityMatrix(&billboardMatrix);
							Nu3D::Math::RotateYFromLut(&billboardMatrix, Nu3D::Camera::g_billboardYaw);
						}
						else
						{
							billboardMatrix = Nu3D::Camera::g_activeCamera.transform;
						}
						billboardMatrix._41 = billboardPosition.x;
						billboardMatrix._42 = billboardPosition.y;
						billboardMatrix._43 = billboardPosition.z;
						DrawingDevice::SetWorldTransform(&billboardMatrix);

						++indices;
						DrawingAPI::ProcessVerticesOnBuffer(destBuffer, vertexOp, *indices, 4, sourceBuffer, *indices, 0);
						if (g_drawingTransparentBuckets == 0)
						{
							DrawingAPI::DrawIndexedPrimitiveVB(D3DPT_TRIANGLESTRIP, destBuffer, indices, 4, drawFlags);
						}
						else
						{
							SoftwareRenderer::SubmitTriangleStrip(renderFlags, destBuffer, entry, indices, 4);
						}
						break;
					}
				}
			}
		}

		// FUNCTION: TOY2 0x004B7920
		void RenderGroundAlignedSprite(Nu3D::Sprite* sprite)
		{
			LPDIRECT3DVERTEXBUFFER destBuffer = g_FVF_14C_Buffer_2.vertexBuffer;
			Nu3D::Vertex* lockedData;

			if (DrawingAPI::LockVertexBuffer(g_FVF_152_Buffer.vertexBuffer, 0x801, (LPVOID*)&lockedData, 0) == 0)
			{
				lockedData[0].position.x = -sprite->width;
				lockedData[0].position.y = 0.0f;
				lockedData[0].position.z = -sprite->height;
				lockedData[0].coords.x = sprite->uvBottomLeft.x;
				lockedData[0].coords.y = sprite->uvBottomLeft.y;
				lockedData[0].diffuse = sprite->color;

				lockedData[1].position.x = -sprite->width;
				lockedData[1].position.y = 0.0f;
				lockedData[1].position.z = sprite->height;
				lockedData[1].coords.x = sprite->uvTopLeft.x;
				lockedData[1].coords.y = sprite->uvTopLeft.y;
				lockedData[1].diffuse = sprite->color;

				lockedData[2].position.x = sprite->width;
				lockedData[2].position.y = 0.0f;
				lockedData[2].position.z = -sprite->height;
				lockedData[2].coords.x = sprite->uvBottomRight.x;
				lockedData[2].coords.y = sprite->uvBottomRight.y;
				lockedData[2].diffuse = sprite->color;

				lockedData[3].position.x = sprite->width;
				lockedData[3].position.y = 0.0f;
				lockedData[3].position.z = sprite->height;
				lockedData[3].coords.x = sprite->uvTopRight.x;
				lockedData[3].coords.y = sprite->uvTopRight.y;
				lockedData[3].diffuse = sprite->color;

				DrawingAPI::UnlockVertexBuffer(g_FVF_152_Buffer.vertexBuffer);

				D3DMATRIX matrix;
				Nu3D::Math::BuildIdentityMatrix(&matrix);
				if (sprite->trigIndex != 0)
					Nu3D::Math::RotateYFromLut(&matrix, sprite->trigIndex);
				Nu3D::Math::AddWorldSpaceTransform(&matrix, &sprite->position);
				DrawingDevice::SetWorldTransform(&matrix);

				if (g_drawingTransparentBuckets == 0)
				{
					if (DrawingAPI::ProcessVerticesOnBuffer(destBuffer, 5, 0, 4, g_FVF_152_Buffer.vertexBuffer, 0, 0) == 0)
					{
						Renderer::InitRenderState(sprite->renderFlags | RENDER_ZBIAS_1);
						Renderer::BindTexture(sprite->textureIndex);
						SoftwareRenderer::g_softwarePrimitiveType = 0;
						SoftwareRenderer::g_viewportRect = &sprite->viewportRect;
						DrawingAPI::DrawIndexedPrimitiveVB(D3DPT_TRIANGLESTRIP, destBuffer, g_groundAlignedSpriteIndices, 4, 8);
					}
				}
				else
				{
					if (DrawingAPI::ProcessVerticesOnBuffer(destBuffer, 1, 0, 4, g_FVF_152_Buffer.vertexBuffer, 0, 0) == 0)
					{
						SoftwareRenderer::SubmitQuad(sprite->renderFlags | RENDER_ZBIAS_1, sprite->textureIndex, destBuffer, g_groundAlignedSpriteIndices);
					}
				}
			}
		}

		// Dispatches the shared render-list header. Sprite items use the full
		// Nu3D::Sprite payload. Geometry and patch entries use Renderer::RenderEntry.
		// FUNCTION: TOY2 0x004B6AD0 [MATCHED]
		void DispatchCommand(Nu3D::Sprite* command)
		{
			Nu3D::Sprite* item = command;

			if (item)
			{
				do
				{
					switch (item->type)
					{
						case RENDER_QUADSPRITE:
							RenderQuadSprite(item);
							break;
						case RENDER_GROUND_ALIGNED_SPRITE:
							RenderGroundAlignedSprite(item);
							break;
						case RENDER_BILLBOARD_SPRITE:
							RenderBillboardSprite(item);
							break;
						case RENDER_2D_SPRITE:
							Render2DSprite(item);
							break;
						case RENDER_TRIANGLE_SPRITE:
							RenderTriangleSprite(item);
							break;
						case RENDER_QUAD_SPRITE_FROM_VERTS:
							RenderQuadSpriteFromVerts(item);
							break;
						case RENDER_TYPE6: {
							Renderer::RenderEntry* entry = (Renderer::RenderEntry*)item;
							RenderType8(entry->material, entry);
							break;
						}
						case RENDER_TYPE7: {
							Renderer::RenderEntry* entry = (Renderer::RenderEntry*)item;
							RenderType9(entry->material, entry);
							break;
						}
						case RENDER_TYPE8: {
							Renderer::RenderEntry* entry = (Renderer::RenderEntry*)item;
							if (g_drawingTransparentBuckets == 0 || (entry->instanceData->renderModeFlags & Nu3D::INSTANCE_RENDER_VERTEX_LIGHTING) != 0)
								Renderer::BindMaterial(entry->material, 1);
							RenderType8(entry->material, entry);
							break;
						}
						case RENDER_TYPE9: {
							Renderer::RenderEntry* entry = (Renderer::RenderEntry*)item;
							if ((entry->instanceData->renderModeFlags & Nu3D::INSTANCE_RENDER_VERTEX_LIGHTING) != 0)
								Renderer::BindMaterial(entry->material, 1);
							else if (g_drawingTransparentBuckets == 0)
								Renderer::BindMaterial(entry->material, 0);
							RenderType9(entry->material, entry);
							break;
						}
						case RENDER_TYPE10:
							RenderType10(item);
							break;
						default:
							break;
					}

					item = item->next;
				} while (item);
			}
		}

		// FUNCTION: TOY2 0x004B8460
		void DrawQueuedSprite()
		{
			DispatchCommand(g_queued2DSprite);
			ResetQueue();

			if (g_isSoftwareRendering)
			{
				SoftwareRenderer::FlushRenderCommands();
				SoftwareRenderer::ResetRenderCommands();
			}
		}
	}
}

namespace Nu3D
{
	// GLOBAL: TOY2 0x008846B8
	Nu3D::Sprite* g_spriteBuckets[256];

	// GLOBAL: TOY2 0x009F600C
	int32_t g_maxBucketDepth;

	// FUNCTION: TOY2 0x004B8B10
	void Sprite::InsertIntoBucket(Nu3D::Sprite* sprite)
	{
		Vector3F cameraPosition;
		Math::GetPositionVector(&Camera::g_activeCamera.transform, &cameraPosition);
		Math::VertexSubtract(&cameraPosition, &cameraPosition, &sprite->position);

		float distanceSquared = cameraPosition.x * cameraPosition.x + cameraPosition.y * cameraPosition.y + cameraPosition.z * cameraPosition.z;
		if (distanceSquared < 1.0f)
			distanceSquared = 1.0f;
		sprite->distanceSquared = distanceSquared;

		union
		{
			float value;
			uint32_t bits;
		} distance;
		distance.value = distanceSquared;
		uint32_t bucketIndex = ((distance.bits >> 20) + 8) & 0xFF;

		int32_t depth = 0;
		Nu3D::Sprite* previous = 0;
		Nu3D::Sprite* current = g_spriteBuckets[bucketIndex];
		while (current && distanceSquared < current->distanceSquared)
		{
			++depth;
			previous = current;
			current = current->next;
		}

		sprite->next = current;
		if (previous)
			previous->next = sprite;
		else
			g_spriteBuckets[bucketIndex] = sprite;

		if (depth >= g_maxBucketDepth)
			g_maxBucketDepth = depth;
	}
}
