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

namespace Renderer
{
	namespace Particles
	{}

	// FUNCTION: TOY2 0x0049B990 [MATCHED]
	void DrawFormattedText(int32_t screenX, int32_t screenY, const char* format, ...)
	{
		char text[512];
		va_list arguments;

		va_start(arguments, format);
		vsprintf(text, format, arguments);
		DrawBitmapText(text, screenY, screenX, 0xFF, 0xFF, 0xFF, 0x60);
	}

	namespace Beam
	{
		// GLOBAL: TOY2 0x004F72D4
		int32_t g_queueHead;

		// GLOBAL: TOY2 0x0054F640
		Command g_commands[100];

	}

	namespace LensFlare
	{
		// GLOBAL: TOY2 0x004DC054
		const float k_positionScale = 0.03125f;

		// GLOBAL: TOY2 0x004DC084
		const float k_depthOffset = 50.0f;

		// GLOBAL: TOY2 0x004DC088
		const float k_depthScale = 47950.0f;

		// GLOBAL: TOY2 0x004F72D8
		const Element g_elements[17] = {
			{ 5, 72, 48, 96, 96, 96 },
			{ 0, 0, 0, 0, 0, 0 },
			{ 0, 0, 0, 0, 0, 0 },
			{ 0, 0, 0, 0, 0, 0 },
			{ 0, 0, 0, 0, 0, 0 },
			{ 7, 42, 28, 4, 4, 56 },
			{ 4, 36, 24, 24, 24, 72 },
			{ 4, 24, 16, 64, 24, 24 },
			{ 0, 0, 0, 0, 0, 0 },
			{ 4, 6, 4, 64, 64, 64 },
			{ 4, 12, 8, 64, 64, 64 },
			{ 4, 36, 24, 64, 64, 24 },
			{ 7, 66, 44, 48, 16, 16 },
			{ 4, 36, 24, 32, 32, 72 },
			{ 0, 0, 0, 0, 0, 0 },
			{ 0, 0, 0, 0, 0, 0 },
			{ 7, 96, 64, 12, 32, 12 },
		};

		// GLOBAL: TOY2 0x0054DD50
		int32_t g_slotCounts[2];

		// GLOBAL: TOY2 0x0054E050
		int16_t g_bufferActive[2];

		// GLOBAL: TOY2 0x005543E0
		Slot g_slots[2][8];

		// GLOBAL: TOY2 0x00557710
		int32_t g_bufferIndex;

		// GLOBAL: TOY2 0x00554DC8
		RegisteredLight g_registeredLights[8];

		// GLOBAL: TOY2 0x00559C64
		int32_t g_registeredLightCount;

	}

	// GLOBAL: TOY2 0x00508D28
	ViewportPreset g_viewportPresets[] = {
		{ 3000.0f, 3500.0f, 50.0f, 55.0f, 10000.0f, 0.0f },
		{ 5000.0f, 6500.0f, 40.0f, 60.0f, 10000.0f, 0.0f },
		{ 10000.0f, 12000.0f, 40.0f, 60.0f, 12000.0f, 1000.0f },
	};

	// GLOBAL: TOY2 0x00884484
	int32_t g_isSoftwareRendering;

	// GLOBAL: TOY2 0x00508218
	int32_t g_textureBlitsEnabled = 1;

	// GLOBAL: TOY2 0x0052F2D4
	int32_t g_frameDelta;

	// GLOBAL: TOY2 0x00E4D968
	LPDIRECT3DDEVICE3 g_drawDeviceD3DDevice;

	// GLOBAL: TOY2 0x00884AB8
	int32_t g_vertexColorModBlue;

	// GLOBAL: TOY2 0x00884ABC
	int32_t g_vertexColorModGreen;

	// GLOBAL: TOY2 0x0088C7C8
	int32_t g_vertexColorModRed;

	// GLOBAL: TOY2 0x0094FCD8
	int32_t g_additionalRenderFlags;

	// GLOBAL: TOY2 0x00508718
	int32_t g_primitiveRenderFlags = 5;

	// GLOBAL: TOY2 0x0050871C
	int32_t g_hardwareTransparencyEnabled = 1;

	// GLOBAL: TOY2 0x005088B0
	float g_primaryRenderDistanceSquared = 144000000.0f;

	// GLOBAL: TOY2 0x005088B4
	float g_secondaryRenderDistanceSquared = 1000000.0f;

	// GLOBAL: TOY2 0x009F2F24
	int32_t g_instanceDataFreeCount;

	// GLOBAL: TOY2 0x0094FCD0
	int32_t g_renderEntryFreeCount;

	// GLOBAL: TOY2 0x00508728
	int32_t g_primitiveBufferFreeCount = 3000;

	// GLOBAL: TOY2 0x0095B860
	void* g_renderBuckets[1024];

	// GLOBAL: TOY2 0x008F7E90
	SortedPrimitive g_primitiveBuffer[3000];

	// FUNCTION: TOY2 0x004BC410 [MATCHED]
	void SetRenderDistance(float primaryDistance, float secondaryDistance)
	{
		g_primaryRenderDistanceSquared = primaryDistance * primaryDistance;
		g_secondaryRenderDistanceSquared = secondaryDistance * secondaryDistance;
	}

	// FUNCTION: TOY2 0x004CDD10 [MATCHED]
	void SetViewportPresetByDetail(int32_t detail)
	{
		if (detail <= 2)
		{
			if (detail < 0)
				detail = 0;
		}
		else
		{
			detail = 2;
		}

		SetRenderDistance(g_viewportPresets[detail].primaryRenderDistance, g_viewportPresets[detail].secondaryRenderDistance);
		Nu3D::Scene::g_secondaryPortalNearClip = g_viewportPresets[detail].secondaryPortalNearClip;
		Nu3D::Scene::g_primaryFogFarClip = g_viewportPresets[detail].primaryFogFarClip;
		Nu3D::Scene::g_primaryNearClip = g_viewportPresets[detail].primaryNearClip;
		Nu3D::Scene::g_secondaryNearClip = g_viewportPresets[detail].secondaryNearClip;
	}

	// FUNCTION: TOY2 0x004CDD80 [MATCHED]
	void SetViewportPreset() { SetViewportPresetByDetail(Toy2::g_toyCfgData.detail); }

	// GLOBAL: TOY2 0x0094FCD4
	float g_lodFactor;

	// GLOBAL: TOY2 0x009F5FF0
	int32_t g_drawingTransparentBuckets;

	// GLOBAL: TOY2 0x009F5FF8
	int32_t g_vertexLightingEnabled;

	// FUNCTION: TOY2 0x004B9A60 [MATCHED]
	int32_t EnableVertexLighting(int32_t enable)
	{
		int32_t previousValue = g_vertexLightingEnabled;
		g_vertexLightingEnabled = enable;
		return previousValue;
	}

	// GLOBAL: TOY2 0x009F6000
	int32_t g_useVertexColorMod;

	// GLOBAL: TOY2 0x009F5FD8
	float g_materialHorzOffset;

	// GLOBAL: TOY2 0x009F5FDC
	float g_materialVertOffset;

	// GLOBAL: TOY2 0x009F2EA0
	Nu3D::Sprite g_instanceSpriteTemplate;

	// GLOBAL: TOY2 0x009F5FB0
	Nu3D::Patch::PatchVertices g_FVF_14C_Buffer_2;

	// GLOBAL: TOY2 0x009F5FC4
	Nu3D::Patch::PatchVertices g_FVF_14C_Buffer_1;

	// GLOBAL: TOY2 0x009F2F28
	Nu3D::Patch::PatchVertices g_FVF_152_Buffer;

	// GLOBAL: TOY2 0x009F5FFC
	int32_t g_drawTriangleWireframes;

	// GLOBAL: TOY2 0x00E4D8FC
	int32_t g_submittedVertexCount;

	// GLOBAL: TOY2 0x00E4D944
	int32_t g_submittedTriangleCount;

	// GLOBAL: TOY2 0x00E4D94C
	int32_t g_submittedPrimitiveCount;

	// GLOBAL: TOY2 0x00E4D920
	int32_t g_renderStateCache[8];

	// GLOBAL: TOY2 0x00E4D8F8
	int32_t g_maxSimultaneousTextures;

	// GLOBAL: TOY2 0x00508708
	int32_t g_srcBlendMode = 5;

	// GLOBAL: TOY2 0x0050870C
	int32_t g_destBlendMode = 2;

	// GLOBAL: TOY2 0x00508710
	int32_t g_alphaBlendSrc = 1;

	// GLOBAL: TOY2 0x00508714
	int32_t g_alphaBlendDest = 4;

	// GLOBAL: TOY2 0x005084F4
	float g_gammaCorrection = 1.0;

	// GLOBAL: TOY2 0x005087F4
	int32_t g_drawMaterialBuckets = 1;

	// GLOBAL: TOY2 0x005087F8
	int32_t g_drawTransparentBuckets = 1;

	// GLOBAL: TOY2 0x004F7414
	float g_virtualScreenWidth = 512.0;

	// GLOBAL: TOY2 0x004F7418
	float g_virtualScreenHeight = 256.0;

	// GLOBAL: TOY2 0x00A4CC90
	Nu3D::Material* g_boundMaterial;

	// GLOBAL: TOY2 0x00AAD778
	int32_t g_boundTextureIndices[8];

	// GLOBAL: TOY2 0x00830C50
	float g_parallaxCurHorizScroll;

	// GLOBAL: TOY2 0x00E4D96C
	int32_t g_deviceBlendShadeCapsCpy;
}

namespace DrawingAPI
{
	// GLOBAL: TOY2 0x00508724
	Device_DrawIndexedPrimitive* DrawIndexedPrimitive;

	// GLOBAL: TOY2 0x00508720
	Device_DrawIndexedPrimitiveVB* DrawIndexedPrimitiveVB;

	// GLOBAL: TOY2 0x005084FC
	Device_ReleaseVertexBuffer* ReleaseVertexBuffer = HardwareDevice::ReleaseVertexBuffer;

	// GLOBAL: TOY2 0x00508500
	Device_CreateVertexBuffer* CreateVertexBuffer = HardwareDevice::CreateVertexBuffer;

	// GLOBAL: TOY2 0x00508504
	Device_LockVertexBuffer* LockVertexBuffer = HardwareDevice::LockVertexBuffer;

	// GLOBAL: TOY2 0x00508508
	Device_UnlockVertexBuffer* UnlockVertexBuffer;

	// GLOBAL: TOY2 0x0050850C
	Device_OptimizeVertexBuffer* OptimizeVertexBuffer;

	// GLOBAL: TOY2 0x00508510
	Device_ProcessVerticesOnBuffer* ProcessVerticesOnBuffer;

}

namespace Renderer
{
	// FUNCTION: TOY2 0x004B62C0 [MATCHED]
	void ResetRenderPools()
	{
		memset(Nu3D::g_spriteBuckets, 0, sizeof(Nu3D::g_spriteBuckets));
		memset(g_renderBuckets, 0, sizeof(g_renderBuckets));
		g_instanceDataFreeCount = 1000;
		g_primitiveBufferFreeCount = 3000;
		g_renderEntryFreeCount = 3000;
		Renderer::Sprite::g_spriteBuffer3DCount = 2000;
	}

	// FUNCTION: TOY2 0x00453CD0 [MATCHED]
	void SetVirtualRatioTo54()
	{
		g_virtualScreenWidth = 320.0;
		g_virtualScreenHeight = 256.0;
	}

	// FUNCTION: TOY2 0x004C2080 [PROVISIONAL]
	int32_t ConvertRGBATo16Bit(RGBA color)
	{
		if (SoftwareRenderer::g_pixelFormatMode == 0)
		{
			uint16_t red = color.r >> 3;
			uint16_t green = (color.g & 0xF8) << 2;
			uint16_t blue = (color.b & 0xF8) << 7;
			return green + blue + red;
		}

		uint16_t red = color.r >> 3;
		uint16_t green = (color.g & 0xF8) << 3;
		uint16_t blue = (color.b & 0xF8) << 8;
		return green + blue + red;
	}

	// FUNCTION: TOY2 0x004CE5B0 [MATCHED]
	void ShowBlackFrames()
	{
		RGBA black;
		black.value = 0;
		ClearScreen(black, 3);
		if (BeginScene())
		{
			EndScene(1);
		}
		ClearScreen(black, 3);
		if (BeginScene())
		{
			EndScene(1);
		}
	}

	// FUNCTION: TOY2 0x004AFD30 [PROVISIONAL]
	void BlitBitmapWithWrapping(
		Nu3D::BmpDataNode* bitmap, int32_t sourceX, int32_t sourceY, int32_t width, int32_t height, int32_t wrapX, int32_t wrapY, int32_t destX, int32_t destY)
	{
		if (g_textureBlitsEnabled == 0)
		{
			return;
		}

		DDBLTFX blitEffects;
		blitEffects.dwSize = sizeof(DDBLTFX);
		blitEffects.dwROP = SRCCOPY;
		LPDIRECTDRAWSURFACE4 surface = bitmap->surface;

		RECT sourceRect;
		RECT destRect;
		if (wrapY == 0)
		{
			int32_t sourceRight = sourceX + width;
			int32_t sourceBottom = sourceY + height;
			int32_t destRight = destX + width;
			int32_t destBottom = destY + height;

			sourceRect.left = sourceX + wrapX;
			sourceRect.top = sourceY;
			sourceRect.right = sourceRight - wrapX;
			sourceRect.bottom = sourceBottom;

			destRect.left = destX;
			destRect.top = destY;
			destRect.right = destRight - wrapX;
			destRect.bottom = destBottom;
			surface->Blt(&destRect, surface, &sourceRect, DDBLT_ROP | DDBLT_WAIT, &blitEffects);

			if (wrapX != 0)
			{
				sourceRect.left = sourceX;
				sourceRect.right = sourceX + wrapX;
				destRect.left = destRight - wrapX;
				destRect.right = destRight;
				surface->Blt(&destRect, surface, &sourceRect, DDBLT_ROP | DDBLT_WAIT, &blitEffects);
			}
		}
		else
		{
			int32_t sourceRight = sourceX + width;
			int32_t sourceBottom = sourceY + height;
			int32_t destRight = destX + width;
			int32_t destBottom = destY + height;

			sourceRect.left = sourceX;
			sourceRect.top = sourceY + wrapY;
			sourceRect.right = sourceRight;
			sourceRect.bottom = sourceBottom;

			destRect.left = destX;
			destRect.top = destY;
			destRect.right = destRight;
			destRect.bottom = destBottom - wrapY;
			surface->Blt(&destRect, surface, &sourceRect, DDBLT_ROP | DDBLT_WAIT, &blitEffects);

			sourceRect.top = sourceY;
			sourceRect.bottom = sourceY + wrapY;
			destRect.top = destBottom - wrapY;
			destRect.bottom = destBottom;
			surface->Blt(&destRect, surface, &sourceRect, DDBLT_ROP | DDBLT_WAIT, &blitEffects);
		}
	}

	// FUNCTION: TOY2 0x004CE510 [MATCHED]
	void BlitTextureByIndex(
		uint32_t textureIndex, int32_t destX, int32_t destY, int32_t width, int32_t height, int32_t wrapX, int32_t wrapY, int32_t sourceX, int32_t sourceY)
	{
		if ((Toy2::g_toyCfgData.flags & 4) != 0)
		{
			uint32_t textureDataIndex = NGNLoader::GetTextureDataIndex(textureIndex);
			if (textureDataIndex != 0)
			{
				NGNLoader::NGNTextureData* textureData = NGNLoader::GetTextureDataByIndex(textureDataIndex);
				BlitBitmapWithWrapping(textureData->bmpDataNode, sourceX, sourceY, width, height, wrapX, wrapY, destX, destY);
			}
		}
	}

	// FUNCTION: TOY2 0x00401B60 [MATCHED]
	void DrawBlackBorderBox(int32_t xPos, int32_t yPos, int32_t width, int32_t height, uint32_t red, uint32_t green, uint32_t blue)
	{
		Sprite::DrawScaled(xPos, yPos, 6, 1, 0, 0, 0, 0x60, 0x2000, height);
		Sprite::DrawScaled((width >> 12) + xPos - 2, yPos, 6, 1, 0, 0, 0, 0x60, 0x2000, height);
		Sprite::DrawScaled(xPos, yPos, 6, 1, 0, 0, 0, 0x60, width, 0x1000);
		Sprite::DrawScaled(xPos, (height >> 12) + yPos - 1, 6, 1, 0, 0, 0, 0x60, width, 0x1000);
		Sprite::DrawScaled(xPos + 2, yPos + 1, 6, 1, red, green, blue, 0, width - 0x4000, height - 0x2000);
	}

	// FUNCTION: TOY2 0x00401FB0 [PROVISIONAL]
	void DrawString(int32_t yPos, const char* text, uint32_t red, uint32_t green, uint32_t blue, int32_t fullWidthLayout)
	{
		int32_t centerX = fullWidthLayout != 0 ? 256 : 160;
		int32_t textLength = 0;
		while (text[textLength] != '\0')
			textLength++;

		int32_t xPos = centerX - textLength * 4;
		if (textLength > 0)
		{
			int32_t remaining = textLength;
			do
			{
				DrawChar(xPos, yPos, *text++, red, green, blue, fullWidthLayout);
				xPos += 8;
			} while (--remaining != 0);
		}
	}

}
namespace Nu3D
{
	using namespace Renderer;

}
namespace Renderer
{

	// FUNCTION: TOY2 0x004B8400 [MATCHED]
	void FlushMaterialBuckets()
	{
		UnbindMaterial();
		Nu3D::Material* material = Nu3D::Material::GetHead();
		while (material != 0)
		{
			Nu3D::Sprite* command = reinterpret_cast<Nu3D::Sprite*>(material->renderEntryHead);
			if (command != 0)
			{
				g_materialHorzOffset = material->horzOffset;
				g_materialVertOffset = material->vertOffset;
				BindMaterial(material, 0);
				Sprite::DispatchCommand(command);
			}

			material->renderEntryHead = 0;
			material = material->next;
		}
	}

	// FUNCTION: TOY2 0x004B6A90 [MATCHED]
	void FlushTransparentBuckets()
	{
		g_drawingTransparentBuckets = g_isSoftwareRendering != 0 ? 0 : g_hardwareTransparencyEnabled;

		Nu3D::Sprite** bucket = &Nu3D::g_spriteBuckets[255];
		do
		{
			Sprite::DispatchCommand(*bucket);
			bucket--;
		} while (reinterpret_cast<int32_t>(bucket) >= reinterpret_cast<int32_t>(Nu3D::g_spriteBuckets));

		g_drawingTransparentBuckets = 0;
	}

	// FUNCTION: TOY2 0x004B6A50 [MATCHED]
	void FlushRenderQueues()
	{
		if (g_drawMaterialBuckets)
			FlushMaterialBuckets();

		if (g_drawTransparentBuckets)
			FlushTransparentBuckets();

		FlushPrimitives();
		ResetRenderPools();

		if (g_isSoftwareRendering)
			SoftwareRenderer::FlushSortedRenderCommandsAndReset();
	}

	// FUNCTION: TOY2 0x004B8BF0 [MATCHED]
	RGBA ModulateColorByAlpha(RGBA color, int32_t flags)
	{
		uint8_t red;
		uint8_t green;
		uint8_t blue;

		if (((flags & 0x4000) != 0 || flags == 0x20000000) && g_srcBlendMode == 2)
		{
			red = (color.a * color.r) >> 8;
			color.r = red;

			green = (color.a * color.g) >> 8;
			color.g = green;

			blue = (color.a * color.b) >> 8;
			color.b = blue;
		}
		else
		{
			red = color.r;
			green = color.g;
			blue = color.b;
		}

		if (g_alphaBlendDest != 6 || flags != 0x40000000)
			return color;

		color.a = green;

		if (red == green && red == blue)
		{
			color.b = 0;
			color.g = 0;
			color.r = 0;
			return color;
		}

		color.r = 255 - red;
		color.g = 255 - green;
		color.b = 255 - blue;

		return color;
	}

	// FUNCTION: TOY2 0x004C27D0 [MATCHED]
	void BindMaterial(Nu3D::Material* material, int32_t force)
	{
		if (g_boundMaterial != material || force != 0)
		{
			g_boundMaterial = material;

			if (material != 0)
			{
				if (g_isSoftwareRendering == 0)
					DrawingDevice::SetLightState(D3DLIGHTSTATE_MATERIAL, material->d3dMaterialHandle);

				for (int32_t stage = 0; stage < g_maxSimultaneousTextures; ++stage)
				{
					if (g_boundTextureIndices[stage] != material->texDataIndex)
					{
						g_boundTextureIndices[stage] = material->texDataIndex;
						DrawingDevice::BindTexWithStage(material->texDataIndex, stage);
					}

					material = material->nextPass;

					if (material == 0)
						return;
				}
			}
			else
			{
				for (int32_t stage = 0; stage < g_maxSimultaneousTextures; ++stage)
					g_boundTextureIndices[stage] = -1;
			}
		}
	}

	// FUNCTION: TOY2 0x004B8450 [MATCHED]
	void UnbindMaterial() { BindMaterial(0, 0); }

	// FUNCTION: TOY2 0x004C2870 [MATCHED]
	void BindTexture(int32_t texIndex)
	{
		g_boundMaterial = 0;

		if (g_boundTextureIndices[0] != texIndex)
		{
			g_boundTextureIndices[0] = texIndex;
			DrawingDevice::BindTexWithStage(texIndex, 0);
		}

		for (int32_t idx = 1; idx < g_maxSimultaneousTextures; ++idx)
		{
			if (g_boundTextureIndices[0] != texIndex)
			{
				g_boundTextureIndices[0] = -1;
				DrawingDevice::BindTexWithStage(-1, idx);
			}
		}
	}

}