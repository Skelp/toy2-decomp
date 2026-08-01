#include "Renderer/Renderer.h"
#include "Renderer/TexturedQuad.h"
#include "D3DApp/d3dappi.h"
#include "SoftwareRenderer.h"
#include "DrawingDevice.h"
#include "Nu3D/BmpDataNode.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Math.h"
#include "Toy2/Toy2.h"
#include "Toy2/Direct6.h"
#include "Logger.h"

namespace DevDraw
{
	// GLOBAL: TOY2 0x00732FBC
	int32_t g_vertexCount;

	// GLOBAL: TOY2 0x00732FC0
	int16_t g_texturedQuadCount;

	// Extra software-render flags applied to coloured textured quads.
	// GLOBAL: TOY2 0x00559C5A
	uint16_t g_texturedQuadRenderFlags;

	// GLOBAL: TOY2 0x0072EFD0
	int16_t g_currentDrawAlpha;

	// GLOBAL: TOY2 0x00732FC8
	DrawBuffer g_drawBufferStorage;

	// GLOBAL: TOY2 0x007D2DA8
	TransparentDrawBuffer g_transparentDrawBufferStorage;

	// FUNCTION: TOY2 0x00494C30 [PROVISIONAL]
	int16_t SubmitTexturedQuad(TexturedQuad* quad)
	{
		switch (g_renderMode)
		{
			case RENDERMODE_D3D: {
				if (Toy2::drawb->VerticeCount[quad->drawSlot] >= 494)
				{
					FlushDrawBufferSlot((int16_t)quad->drawSlot);
				}

				int16_t firstVertex = Toy2::drawb->VerticeCount[quad->drawSlot];
				Toy2::drawb->Index[quad->drawSlot][Toy2::drawb->IndexCount[quad->drawSlot]++] = firstVertex;
				Toy2::drawb->Index[quad->drawSlot][Toy2::drawb->IndexCount[quad->drawSlot]++] = firstVertex + 1;
				Toy2::drawb->Index[quad->drawSlot][Toy2::drawb->IndexCount[quad->drawSlot]++] = firstVertex + 2;
				Toy2::drawb->Index[quad->drawSlot][Toy2::drawb->IndexCount[quad->drawSlot]++] = firstVertex + 1;
				Toy2::drawb->Index[quad->drawSlot][Toy2::drawb->IndexCount[quad->drawSlot]++] = firstVertex + 3;
				Toy2::drawb->Index[quad->drawSlot][Toy2::drawb->IndexCount[quad->drawSlot]++] = firstVertex + 2;

				Nu3D::VertexTL* vertex = static_cast<Nu3D::VertexTL*>(Toy2::drawb->Vertice[quad->drawSlot]) + firstVertex;
				Toy2::drawb->VerticeCount[quad->drawSlot] += 4;

				int32_t intensity = Nu3D::Camera::g_cameraTintBlue * 224 / 128;
				uint32_t diffuse = 0xFF000000 | intensity << 16 | intensity << 8 | intensity;

				vertex->position.x = quad->points[0].x;
				vertex->position.y = quad->points[0].y;
				vertex->position.z = quad->depth * 0.000244140625f;
				vertex->rhw = 1.0f;
				vertex->diffuse.value = diffuse;
				vertex->specular.value = 0;
				vertex->uv.x = (float)quad->texCoords[0].u / quad->textureWidth;
				vertex->uv.y = (float)quad->texCoords[0].v / quad->textureHeight;
				vertex++;

				vertex->position.x = quad->points[1].x;
				vertex->position.y = quad->points[1].y;
				vertex->position.z = quad->depth * 0.000244140625f;
				vertex->rhw = 1.0f;
				vertex->diffuse.value = diffuse;
				vertex->specular.value = 0;
				vertex->uv.x = (float)quad->texCoords[1].u / quad->textureWidth;
				vertex->uv.y = (float)quad->texCoords[1].v / quad->textureHeight;
				vertex++;

				vertex->position.x = quad->points[2].x;
				vertex->position.y = quad->points[2].y;
				vertex->position.z = quad->depth * 0.000244140625f;
				vertex->rhw = 1.0f;
				vertex->diffuse.value = diffuse;
				vertex->specular.value = 0;
				vertex->uv.x = (float)quad->texCoords[2].u / quad->textureWidth;
				vertex->uv.y = (float)quad->texCoords[2].v / quad->textureHeight;
				vertex++;

				vertex->position.x = quad->points[3].x;
				vertex->position.y = quad->points[3].y;
				vertex->position.z = quad->depth * 0.000244140625f;
				vertex->rhw = 1.0f;
				vertex->diffuse.value = diffuse;
				vertex->specular.value = 0;
				vertex->uv.x = (float)quad->texCoords[3].u / quad->textureWidth;
				vertex->uv.y = (float)quad->texCoords[3].v / quad->textureHeight;

				g_texturedQuadCount++;
				break;
			}
			case RENDERMODE_SOFTWARE:
				if (SoftwareRenderer::g_softwareRenderItemCount < SoftwareRenderer::g_softwareRendererBufferBlockCount)
				{
					SoftwareRenderer::SoftwareRenderItem* items =
						static_cast<SoftwareRenderer::SoftwareRenderItem*>(SoftwareRenderer::g_softwareRendererBuffer);
					SoftwareRenderer::SoftwareRenderItem* item = &items[SoftwareRenderer::g_softwareRenderItemCount++];

					item->vertices[0].x = quad->points[0].x;
					item->vertices[1].x = quad->points[1].x;
					item->vertices[2].x = quad->points[3].x;
					item->vertices[3].x = quad->points[2].x;
					item->vertices[0].y = quad->points[0].y;
					item->vertices[1].y = quad->points[1].y;
					item->vertices[2].y = quad->points[3].y;
					item->vertices[3].y = quad->points[2].y;
					item->vertices[0].u = quad->texCoords[0].u;
					item->vertices[1].u = quad->texCoords[1].u;
					item->vertices[2].u = quad->texCoords[3].u;
					item->vertices[3].u = quad->texCoords[2].u;
					item->vertices[0].v = quad->texCoords[0].v;
					item->vertices[1].v = quad->texCoords[1].v;
					item->vertices[2].v = quad->texCoords[3].v;
					item->vertices[3].v = quad->texCoords[2].v;

					item->vertices[3].blue = 0x80000;
					item->vertices[2].blue = 0x80000;
					item->vertices[1].blue = 0x80000;
					item->vertices[0].blue = 0x80000;
					item->vertices[3].green = 0x80000;
					item->vertices[2].green = 0x80000;
					item->vertices[1].green = 0x80000;
					item->vertices[0].green = 0x80000;
					item->vertices[3].red = 0x80000;
					item->vertices[2].red = 0x80000;
					item->vertices[1].red = 0x80000;
					item->vertices[0].red = 0x80000;

					item->renderFlags = SoftwareRenderer::SOFTWARE_RENDER_HIGH_PRIORITY;
					item->textureIndex = (uint8_t)quad->drawSlot;
					item->next = SoftwareRenderer::g_softwareRenderBuckets[quad->depth];
					SoftwareRenderer::g_softwareRenderBuckets[quad->depth] = item;
				}
				break;
			default:
				break;
		}

		return 1;
	}

	// FUNCTION: TOY2 0x00495060 [PROVISIONAL]
	int16_t SubmitColouredTexturedQuad(TexturedQuad* quad)
	{
		int32_t blue = quad->blue * Nu3D::Camera::g_cameraTintBlue / 128;
		int32_t green = quad->green * Nu3D::Camera::g_cameraTintGreen / 128;
		int32_t red = quad->red * Nu3D::Camera::g_cameraTintRed / 128;

		switch (g_renderMode)
		{
			case RENDERMODE_D3D: {
				Nu3D::VertexTL* vertex;
				int16_t firstVertex;

				if (g_currentDrawAlpha == 255)
				{
					if (Toy2::drawb->Vertice[quad->drawSlot] == NULL)
					{
						SetVertexBufferAllocation((int16_t)quad->drawSlot, 1);
						g_textureDimensions[quad->drawSlot].width = g_textureDimensions[quad->drawSlot].height = 256;
					}

					if (Toy2::drawb->VerticeCount[quad->drawSlot] >= 494)
					{
						FlushDrawBufferSlot((int16_t)quad->drawSlot);
					}

					firstVertex = Toy2::drawb->VerticeCount[quad->drawSlot];
					Toy2::drawb->Index[quad->drawSlot][Toy2::drawb->IndexCount[quad->drawSlot]++] = firstVertex;
					Toy2::drawb->Index[quad->drawSlot][Toy2::drawb->IndexCount[quad->drawSlot]++] = firstVertex + 1;
					Toy2::drawb->Index[quad->drawSlot][Toy2::drawb->IndexCount[quad->drawSlot]++] = firstVertex + 2;
					Toy2::drawb->Index[quad->drawSlot][Toy2::drawb->IndexCount[quad->drawSlot]++] = firstVertex + 1;
					Toy2::drawb->Index[quad->drawSlot][Toy2::drawb->IndexCount[quad->drawSlot]++] = firstVertex + 3;
					Toy2::drawb->Index[quad->drawSlot][Toy2::drawb->IndexCount[quad->drawSlot]++] = firstVertex + 2;

					vertex = static_cast<Nu3D::VertexTL*>(Toy2::drawb->Vertice[quad->drawSlot]) + firstVertex;
					Toy2::drawb->VerticeCount[quad->drawSlot] += 4;
				}
				else
				{
					if (Toy2::drawtranb->Vertice[quad->drawSlot] == NULL)
					{
						SetVertexBufferAllocation((int16_t)quad->drawSlot, 1);
						g_textureDimensions[quad->drawSlot].width = g_textureDimensions[quad->drawSlot].height = 256;
					}

					if (Toy2::drawtranb->VerticeCount[quad->drawSlot] >= 994)
					{
						FlushTransparentDrawBufferSlot((int16_t)quad->drawSlot);
					}

					firstVertex = Toy2::drawtranb->VerticeCount[quad->drawSlot];
					Toy2::drawtranb->Index[quad->drawSlot][Toy2::drawtranb->IndexCount[quad->drawSlot]++] = firstVertex;
					Toy2::drawtranb->Index[quad->drawSlot][Toy2::drawtranb->IndexCount[quad->drawSlot]++] = firstVertex + 1;
					Toy2::drawtranb->Index[quad->drawSlot][Toy2::drawtranb->IndexCount[quad->drawSlot]++] = firstVertex + 2;
					Toy2::drawtranb->Index[quad->drawSlot][Toy2::drawtranb->IndexCount[quad->drawSlot]++] = firstVertex + 1;
					Toy2::drawtranb->Index[quad->drawSlot][Toy2::drawtranb->IndexCount[quad->drawSlot]++] = firstVertex + 3;
					Toy2::drawtranb->Index[quad->drawSlot][Toy2::drawtranb->IndexCount[quad->drawSlot]++] = firstVertex + 2;

					vertex = static_cast<Nu3D::VertexTL*>(Toy2::drawtranb->Vertice[quad->drawSlot]) + firstVertex;
					Toy2::drawtranb->VerticeCount[quad->drawSlot] += 4;
				}

				quad->textureWidth = 256;
				quad->textureHeight = 256;
				uint32_t diffuse = (((g_currentDrawAlpha << 8 | blue) << 8 | green) << 8 | red);

				vertex->position.x = quad->points[0].x;
				vertex->position.y = quad->points[0].y;
				vertex->position.z = ((float)(int16_t)quad->depth - Nu3D::Camera::g_currentCamera->nearClip) * Nu3D::Camera::g_currentCamera->farClip
					/ ((Nu3D::Camera::g_currentCamera->farClip - Nu3D::Camera::g_currentCamera->nearClip) * (float)(int16_t)quad->depth);
				vertex->rhw = 1.0f;
				vertex->diffuse.value = diffuse;
				vertex->specular.value = 0;
				vertex->uv.x = (float)quad->texCoords[0].u / quad->textureWidth;
				vertex->uv.y = (float)quad->texCoords[0].v / quad->textureHeight;
				vertex++;

				vertex->position.x = quad->points[1].x;
				vertex->position.y = quad->points[1].y;
				vertex->position.z = ((float)(int16_t)quad->depth - Nu3D::Camera::g_currentCamera->nearClip) * Nu3D::Camera::g_currentCamera->farClip
					/ ((Nu3D::Camera::g_currentCamera->farClip - Nu3D::Camera::g_currentCamera->nearClip) * (float)(int16_t)quad->depth);
				vertex->rhw = 1.0f;
				vertex->diffuse.value = diffuse;
				vertex->specular.value = 0;
				vertex->uv.x = (float)quad->texCoords[1].u / quad->textureWidth;
				vertex->uv.y = (float)quad->texCoords[1].v / quad->textureHeight;
				vertex++;

				vertex->position.x = quad->points[2].x;
				vertex->position.y = quad->points[2].y;
				vertex->position.z = ((float)(int16_t)quad->depth - Nu3D::Camera::g_currentCamera->nearClip) * Nu3D::Camera::g_currentCamera->farClip
					/ ((Nu3D::Camera::g_currentCamera->farClip - Nu3D::Camera::g_currentCamera->nearClip) * (float)(int16_t)quad->depth);
				vertex->rhw = 1.0f;
				vertex->diffuse.value = diffuse;
				vertex->specular.value = 0;
				vertex->uv.x = (float)quad->texCoords[2].u / quad->textureWidth;
				vertex->uv.y = (float)quad->texCoords[2].v / quad->textureHeight;
				vertex++;

				vertex->position.x = quad->points[3].x;
				vertex->position.y = quad->points[3].y;
				vertex->position.z = ((float)(int16_t)quad->depth - Nu3D::Camera::g_currentCamera->nearClip) * Nu3D::Camera::g_currentCamera->farClip
					/ ((Nu3D::Camera::g_currentCamera->farClip - Nu3D::Camera::g_currentCamera->nearClip) * (float)(int16_t)quad->depth);
				vertex->rhw = 1.0f;
				vertex->diffuse.value = diffuse;
				vertex->specular.value = 0;
				vertex->uv.x = (float)quad->texCoords[3].u / quad->textureWidth;
				vertex->uv.y = (float)quad->texCoords[3].v / quad->textureHeight;

				g_texturedQuadCount++;
				g_currentDrawAlpha = 255;
				break;
			}
			case RENDERMODE_SOFTWARE:
				if (SoftwareRenderer::g_softwareRenderItemCount < SoftwareRenderer::g_softwareRendererBufferBlockCount)
				{
					SoftwareRenderer::SoftwareRenderItem* items =
						static_cast<SoftwareRenderer::SoftwareRenderItem*>(SoftwareRenderer::g_softwareRendererBuffer);
					SoftwareRenderer::SoftwareRenderItem* item = &items[SoftwareRenderer::g_softwareRenderItemCount++];

					item->vertices[0].x = quad->points[0].x;
					item->vertices[1].x = quad->points[1].x;
					item->vertices[2].x = quad->points[3].x;
					item->vertices[3].x = quad->points[2].x;
					item->vertices[0].y = quad->points[0].y;
					item->vertices[1].y = quad->points[1].y;
					item->vertices[2].y = quad->points[3].y;
					item->vertices[3].y = quad->points[2].y;
					item->vertices[0].u = quad->texCoords[0].u;
					item->vertices[1].u = quad->texCoords[1].u;
					item->vertices[2].u = quad->texCoords[3].u;
					item->vertices[3].u = quad->texCoords[2].u;
					item->vertices[0].v = quad->texCoords[0].v;
					item->vertices[1].v = quad->texCoords[1].v;
					item->vertices[2].v = quad->texCoords[3].v;
					item->vertices[3].v = quad->texCoords[2].v;

					blue <<= 13;
					item->vertices[3].blue = blue;
					item->vertices[2].blue = blue;
					item->vertices[1].blue = blue;
					item->vertices[0].blue = blue;
					green <<= 13;
					item->vertices[3].green = green;
					item->vertices[2].green = green;
					item->vertices[1].green = green;
					item->vertices[0].green = green;
					red <<= 13;
					item->vertices[3].red = red;
					item->vertices[2].red = red;
					item->vertices[1].red = red;
					item->vertices[0].red = red;

					item->textureIndex = (uint8_t)quad->drawSlot;
					item->renderFlags =
						g_texturedQuadRenderFlags | SoftwareRenderer::SOFTWARE_RENDER_COLOUR_OFFSET | SoftwareRenderer::SOFTWARE_RENDER_HIGH_PRIORITY;
					item->next = SoftwareRenderer::g_softwareRenderBuckets[quad->depth];
					SoftwareRenderer::g_softwareRenderBuckets[quad->depth] = item;
				}
				break;
			default:
				break;
		}

		return 1;
	}

	// FUNCTION: TOY2 0x00497FE0 [MATCHED]
	void SetVertexBufferAllocation(int16_t slot, int32_t allocate)
	{
		int16_t slotNumber = slot;
		if (slotNumber >= 0x40)
			return;

		if (allocate)
		{
			if (g_drawBufferStorage.Vertice[slotNumber] == NULL)
			{
				g_drawBufferStorage.Vertice[slotNumber] = malloc(sizeof(Nu3D::VertexTL) * 500);
				Logger::Log("MEM : Vertex buffer for tpage %d created.\n", slotNumber);
			}

			if (slotNumber < 0x20 && g_transparentDrawBufferStorage.Vertice[slotNumber] == NULL)
			{
				int32_t vertexCount = slotNumber == 14 ? 1000 : 500;
				g_transparentDrawBufferStorage.Vertice[slotNumber] = malloc(sizeof(Nu3D::VertexTL) * vertexCount);
				Logger::Log("MEM : Trans vertex buffer for tpage %d created.\n", slotNumber);
			}
		}
		else
		{
			if (g_drawBufferStorage.Vertice[slotNumber] != NULL)
			{
				free(g_drawBufferStorage.Vertice[slotNumber]);
				Logger::Log("MEM : Vertex buffer for tpage %d destroyed.\n", slotNumber);
			}

			if (slotNumber < 0x20 && g_transparentDrawBufferStorage.Vertice[slotNumber] != NULL)
			{
				free(g_transparentDrawBufferStorage.Vertice[slotNumber]);
				Logger::Log("MEM : Trans vertex buffer for tpage %d destroyed.\n", slotNumber);
			}

			g_drawBufferStorage.Vertice[slotNumber] = NULL;
			g_transparentDrawBufferStorage.Vertice[slotNumber] = NULL;
		}
	}

	// FUNCTION: TOY2 0x004907E0 [MATCHED]
	int16_t DrawSlots()
	{
		switch (g_renderMode)
		{
			case 1:
				if (SoftwareRenderer::g_skipOddSoftwareFrames != 1 || (Renderer::g_frameDelta & 1) == 0)
				{
					SoftwareRenderer::RenderSoftwareFrame(SoftwareRenderer::g_displayMaxX, 0);
				}
				SoftwareRenderer::g_softwareRenderItemCount = 0;
				Nu3D::MemSet32Util(SoftwareRenderer::g_softwareRenderBuckets, 0x1000, 0);
				break;
			case 2: {
				int16_t i;
				for (i = 0; i < 0x40; i++)
				{
					FlushDrawBufferSlot(i);
				}
				for (i = 0; i < 0x40; i++)
				{
					FlushTransparentDrawBufferSlot(i);
				}
				return 1;
			}
			default:
				break;
		}
		return 1;
	}

	// FUNCTION: TOY2 0x00490470 [MATCHED]
	int16_t FlushDrawBufferSlot(int16_t slot)
	{
		Renderer::InitRenderState(0);

		LPDIRECT3DDEVICE3 d3dDevice = DrawingDevice::GetD3DDevice();
		int32_t slotIndex = slot;

		if (Toy2::drawb->VerticeCount[slotIndex] != 0)
		{
			char textureName[15] = "LOADTEXT_tex00";
			textureName[12] = (char)('0' + slotIndex / 10);
			textureName[13] = (char)('0' + slotIndex % 10);

			Nu3D::BmpDataNode* bmpDataNode = Nu3D::GetBmpDataNodeByName_T(textureName);
			d3dDevice->SetTexture(0, Nu3D::GetTexture(bmpDataNode));

			HRESULT error = d3dDevice->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,
				D3DFVF_0x1C4,
				Toy2::drawb->Vertice[slotIndex],
				Toy2::drawb->VerticeCount[slotIndex],
				Toy2::drawb->Index[slotIndex],
				Toy2::drawb->IndexCount[slotIndex],
				8);

			if (error < 0)
			{
				Logger::LogDDError(
					"dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, ( 0x004 | 0x040 | 0x080 | 0x100 ), (LPVOID) (drawb->Vertice[i]), "
					"drawb->VerticeCount[i], (LPWORD) & (drawb->Index[i]), drawb->IndexCount[i], 0x00000008l)",
					error);
			}

			Toy2::g_currentDrawSlot = slot;
			g_vertexCount += Toy2::drawb->VerticeCount[slotIndex];
		}

		Toy2::drawb->IndexCount[slotIndex] = 0;
		Toy2::drawb->VerticeCount[slotIndex] = 0;

		return 1;
	}

	// FUNCTION: TOY2 0x004905C0 [MATCHED]
	int16_t FlushTransparentDrawBufferSlot(int16_t slot)
	{
		LPDIRECT3DDEVICE3 d3dDevice = DrawingDevice::GetD3DDevice();
		Renderer::InitRenderState(0x400);

		if (slot < 0x20)
		{
			int32_t slotIndex = slot;

			if (Toy2::drawtranb->VerticeCount[slotIndex] != 0)
			{
				char textureName[15] = "LOADTEXT_tex00";
				textureName[12] = (char)('0' + slotIndex / 10);
				textureName[13] = (char)('0' + slotIndex % 10);

				Nu3D::BmpDataNode* bmpDataNode = Nu3D::GetBmpDataNodeByName_T(textureName);
				d3dDevice->SetTexture(0, Nu3D::GetTexture(bmpDataNode));

				HRESULT error = d3dDevice->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,
					D3DFVF_0x1C4,
					Toy2::drawtranb->Vertice[slotIndex],
					Toy2::drawtranb->VerticeCount[slotIndex],
					Toy2::drawtranb->Index[slotIndex],
					Toy2::drawtranb->IndexCount[slotIndex],
					8);

				if (error < 0)
				{
					Logger::LogDDError(
						"dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, ( 0x004 | 0x040 | 0x080 | 0x100 ), (LPVOID) (drawtranb->Vertice[i]), "
						"drawtranb->VerticeCount[i], (LPWORD) & (drawtranb->Index[i]), drawtranb->IndexCount[i], 0x00000008l)",
						error);
				}

				Toy2::g_currentDrawSlot = slot;
				g_vertexCount += Toy2::drawtranb->VerticeCount[slotIndex];
			}
		}

		Renderer::InitRenderState(0);

		Toy2::drawtranb->IndexCount[slot] = 0;
		Toy2::drawtranb->VerticeCount[slot] = 0;

		return 1;
	}

	// FUNCTION: TOY2 0x00490D10 [MATCHED]
	int16_t AppendClippedVertexPair(Vector3I16* pointA, Vector3I16* pointB, float red, float green, float blue)
	{
		if (pointA->x < Toy2::g_screenClipLeft)
			pointA->x = Toy2::g_screenClipLeft;
		if (pointB->x < Toy2::g_screenClipLeft)
			pointB->x = Toy2::g_screenClipLeft;
		if (pointA->x > Toy2::g_screenClipRight)
			pointA->x = Toy2::g_screenClipRight;
		if (pointB->x > Toy2::g_screenClipRight)
			pointB->x = Toy2::g_screenClipRight;

		if (pointA->y < Toy2::g_screenClipTop)
			pointA->y = Toy2::g_screenClipTop;
		if (pointB->y < Toy2::g_screenClipTop)
			pointB->y = Toy2::g_screenClipTop;
		if (pointA->y > Toy2::g_screenClipBottom)
			pointA->y = Toy2::g_screenClipBottom;
		if (pointB->y > Toy2::g_screenClipBottom)
			pointB->y = Toy2::g_screenClipBottom;

		if (Toy2::drawb->VerticePoolCount < 0x3FFE)
		{
			Nu3D::VertexTL* vertex = &Toy2::drawb->VerticePool[Toy2::drawb->VerticePoolCount++];
			vertex->position.x = pointA->x;
			vertex->position.y = pointA->y;
			vertex->position.z = pointA->z;

			uint32_t diffuse = 0xFF000000 | ((int32_t)(red * 255.0f) << 16) | ((int32_t)(green * 255.0f) << 8) | (int32_t)(blue * 255.0f);
			vertex->diffuse.value = diffuse;
			vertex->specular.value = 0;
			vertex->uv.x = 0;

			vertex = &Toy2::drawb->VerticePool[Toy2::drawb->VerticePoolCount++];
			vertex->position.x = pointB->x;
			vertex->position.y = pointB->y;
			vertex->position.z = pointB->z;
			vertex->diffuse.value = diffuse;
			vertex->specular.value = 0;
			vertex->uv.x = 0;
		}

		return 1;
	}

	// FUNCTION: TOY2 0x00490EB0 [MATCHED]
	float CalculateProjectedDepth(int16_t depth)
	{
		float depthValue = depth;
		return (depthValue - Nu3D::Camera::g_currentCamera->nearClip) * Nu3D::Camera::g_currentCamera->farClip
			/ ((Nu3D::Camera::g_currentCamera->farClip - Nu3D::Camera::g_currentCamera->nearClip) * depthValue);
	}

}
