#include "Renderer/Renderer.h"
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
