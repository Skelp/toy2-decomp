#include "Renderer/Renderer.h"
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
#include "Renderer/Sprite.h"
#include "Toy2/Toy2.h"
#include "Renderer/Glue.h"
#include "Toy2/D3DApp.h"
#include "Logger.h"

// Unidentified empty no-op (single RET) called once from Renderer::Cleanup
// between the primitive-list teardown and the texture/light release. Its
// address sits between Nu3D::Material::Init and Nu3D::Light::Destroy; the
// retail body is empty, so it is a stubbed/placeholder cleanup step.
// STUB: TOY2 0x004C2990
namespace Nu3D
{
	void UnkFunc0() {}
} // namespace Nu3D

namespace Renderer
{
	// GLOBAL: TOY2 0x00884484
	int32_t g_isSoftwareRendering;

	// GLOBAL: TOY2 0x0052F2D4
	int32_t g_frameDelta;

	// GLOBAL: TOY2 0x00884488
	int32_t g_rendererValid;

	// GLOBAL: TOY2 0x00E4D964
	LPDIRECT3D3 g_drawDeviceD3D;

	// GLOBAL: TOY2 0x00E4D968
	LPDIRECT3DDEVICE3 g_drawDeviceD3DDevice;

	// GLOBAL: TOY2 0x0088448C
	int32_t g_fogEnabled;

	// GLOBAL: TOY2 0x00508514
	float g_fogStart = 1.0;

	// GLOBAL: TOY2 0x00508518
	float g_fogEnd = 100.0;

	// GLOBAL: TOY2 0x00884478
	DWORD g_fogColor;

	// GLOBAL: TOY2 0x00E4D95C
	int32_t g_deviceBlendShadeCaps;

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

	// GLOBAL: TOY2 0x005088B0
	float g_primaryRenderDistanceSquared = 144000000.0f;

	// GLOBAL: TOY2 0x005088B4
	float g_secondaryRenderDistanceSquared = 1000000.0f;

	// GLOBAL: TOY2 0x009F2F24
	int32_t g_instanceDataFreeCount;

	// GLOBAL: TOY2 0x0088C7D0
	Nu3D::InstanceData g_instanceDataPool[1000];

	// GLOBAL: TOY2 0x0094FCD0
	int32_t g_renderEntryFreeCount;

	// GLOBAL: TOY2 0x00508728
	int32_t g_primitiveBufferFreeCount = 3000;

	// GLOBAL: TOY2 0x0095C860
	RenderEntry g_renderEntryPool[3000];

	// GLOBAL: TOY2 0x0095B860
	void* g_renderBuckets[1024];

	// GLOBAL: TOY2 0x008F7E90
	SortedPrimitive g_primitiveBuffer[3000];

	// FUNCTION: TOY2 0x004B92B0 [MATCHED]
	int32_t SetAdditionalRenderFlags(int32_t flags)
	{
		int32_t previousFlags = g_additionalRenderFlags;
		g_additionalRenderFlags = flags;
		return previousFlags;
	}

	// FUNCTION: TOY2 0x004B5CE0 [MATCHED]
	int32_t Set508718(int32_t value)
	{
		int32_t previousValue = g_primitiveRenderFlags;
		g_primitiveRenderFlags = value;
		return previousValue;
	}

	// FUNCTION: TOY2 0x004BC410 [MATCHED]
	void SetRenderDistance(float primaryDistance, float secondaryDistance)
	{
		g_primaryRenderDistanceSquared = primaryDistance * primaryDistance;
		g_secondaryRenderDistanceSquared = secondaryDistance * secondaryDistance;
	}

	// GLOBAL: TOY2 0x0094FCD4
	float g_lodFactor;

	// GLOBAL: TOY2 0x009F5FE0
	Nu3D::Material* g_whiteMaterial;

	// GLOBAL: TOY2 0x009F5FF0
	int32_t g_unk9F5FF0;

	// GLOBAL: TOY2 0x009F5FF8
	int32_t g_unk9F5FF8;

	// FUNCTION: TOY2 0x004B9A60 [MATCHED]
	int32_t Set9F5FF8(int32_t value)
	{
		int32_t previousValue = g_unk9F5FF8;
		g_unk9F5FF8 = value;
		return previousValue;
	}

	// GLOBAL: TOY2 0x009F6000
	int32_t g_useVertexColorMod;

	// FUNCTION: TOY2 0x004B92E0 [MATCHED]
	void SetVertexColorModulation(int32_t red, int32_t green, int32_t blue)
	{
		g_vertexColorModRed = red;
		g_vertexColorModGreen = green;
		g_vertexColorModBlue = blue;
	}

	// FUNCTION: TOY2 0x004B9300 [MATCHED]
	int32_t EnableVertexColorModulation(int32_t enable)
	{
		int32_t previousValue = g_useVertexColorMod;
		g_useVertexColorMod = enable;
		return previousValue;
	}

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

	// GLOBAL: TOY2 0x00E4D920
	int32_t g_renderStateCache[8];

	// GLOBAL: TOY2 0x00E4D8F8
	int32_t g_maxSimultaneousTextures;

	// GLOBAL: TOY2 0x00508704
	int32_t g_maxSimultaneousTexturesMax = 1;

	// GLOBAL: TOY2 0x00508708
	int32_t g_srcBlendMode = 5;

	// GLOBAL: TOY2 0x0050870C
	int32_t g_destBlendMode = 2;

	// GLOBAL: TOY2 0x00508710
	int32_t g_alphaBlendSrc = 1;

	// GLOBAL: TOY2 0x00508714
	int32_t g_alphaBlendDest = 4;

	// GLOBAL: TOY2 0x00830C54
	int32_t g_lastFrameTimestamp;

	// GLOBAL: TOY2 0x00830C58
	int32_t g_frameStabilityCounter;

	// GLOBAL: TOY2 0x00500AA4
	int32_t g_maxSpeedMultiplier = 4;

	// GLOBAL: TOY2 0x00500AAC
	int32_t g_startupDelayFrames = 120;

	// GLOBAL: TOY2 0x00500AA8
	int32_t g_targetSpeedMultiplier = 1;

	// GLOBAL: TOY2 0x0050851C
	uint8_t g_gammaLUT[256] = {
		// clang-format off
		0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,  15,
		16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,  30,  31,
		32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,
		48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,
		64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,  76,  77,  78,  79,
		80,  81,  82,  83,  84,  85,  86,  87,  88,  89,  90,  91,  92,  93,  94,  95,
		96,  97,  98,  99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111,
		112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127,
		128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143,
		144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159,
		160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175,
		176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191,
		192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207,
		208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223,
		224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239,
		240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255,
		// clang-format on
	};

	// GLOBAL: TOY2 0x005084F4
	float g_gammaCorrection = 1.0;

	// GLOBAL: TOY2 0x005084F8
	int32_t g_gammaFixedPoint = 0x10000;

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

	// GLOBAL: TOY2 0x00500A54
	int32_t g_drawParallaxTexture = 1;

	// GLOBAL: TOY2 0x0072EF90
	float g_parallaxHorizOffset;

	// GLOBAL: TOY2 0x00830C50
	float g_parallaxCurHorizScroll;

	// GLOBAL: TOY2 0x00731CD0
	float g_parallaxTexHeightRatio;

	// GLOBAL: TOY2 0x00731CD4
	float g_parallaxTexWidthRatio;

	// GLOBAL: TOY2 0x00731D68
	RGBA g_parallaxTexFirstPixel;

	// GLOBAL: TOY2 0x00731CC8
	RGBA g_parallaxTexLastPixel;

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
	Device_ReleaseVertexBuffer* ReleaseVertexBuffer;

	// GLOBAL: TOY2 0x00508500
	Device_CreateVertexBuffer* CreateVertexBuffer;

	// GLOBAL: TOY2 0x00508504
	Device_LockVertexBuffer* LockVertexBuffer;

	// GLOBAL: TOY2 0x00508508
	Device_UnlockVertexBuffer* UnlockVertexBuffer;

	// GLOBAL: TOY2 0x0050850C
	Device_OptimizeVertexBuffer* OptimizeVertexBuffer;

	// GLOBAL: TOY2 0x00508510
	Device_ProcessVerticesOnBuffer* ProcessVerticesOnBuffer;

	// FUNCTION: TOY2 0x004B2BF0 [MATCHED]
	void SetVertexAPIs(int32_t isSoftwareRendering)
	{
		if (isSoftwareRendering)
		{
			ReleaseVertexBuffer = SoftwareDevice::ReleaseVertexBuffer;
			CreateVertexBuffer = SoftwareDevice::CreateVertexBuffer;
			LockVertexBuffer = SoftwareDevice::LockVertexBuffer;
			UnlockVertexBuffer = SoftwareDevice::UnlockVertexBuffer;
			OptimizeVertexBuffer = SoftwareDevice::OptimizeVertexBuffer;
			ProcessVerticesOnBuffer = SoftwareDevice::ProcessVerticesOnBuffer;
		}
		else
		{
			ReleaseVertexBuffer = HardwareDevice::ReleaseVertexBuffer;
			CreateVertexBuffer = HardwareDevice::CreateVertexBuffer;
			LockVertexBuffer = HardwareDevice::LockVertexBuffer;
			UnlockVertexBuffer = HardwareDevice::UnlockVertexBuffer;
			OptimizeVertexBuffer = HardwareDevice::OptimizeVertexBuffer;
			ProcessVerticesOnBuffer = HardwareDevice::ProcessVerticesOnBuffer;
		}
	}
}

namespace Renderer
{
	// FUNCTION: TOY2 0x004B2CE0 [MATCHED]
	void DisableFog() { g_fogEnabled = 0; }

	// FUNCTION: TOY2 0x004B2CF0
	void ConfigureFog(float start, float end, RGBA color)
	{
		g_fogEnabled = 1;
		g_fogStart = start;
		g_fogEnd = end;
		g_fogColor = ApplyGammaCorrection(color).value;
	}

	// FUNCTION: TOY2 0x004B2D20
	void SetFogEnable(int32_t enable)
	{
		if (g_fogEnabled)
		{
			if (enable)
				DrawingDevice::SetRenderState(D3DRENDERSTATE_FOGENABLE, 1);
			else
				DrawingDevice::SetRenderState(D3DRENDERSTATE_FOGENABLE, 0);
		}
	}

	// FUNCTION: TOY2 0x004B6320
	void SetRenderState(int32_t newStateFlags)
	{
		if (g_isSoftwareRendering)
		{
			g_renderStateCache[0] = newStateFlags;
		}
		else
		{
			uint32_t flagsToDisable = g_renderStateCache[0] & (newStateFlags ^ g_renderStateCache[0]);

			if (flagsToDisable)
			{
				if ((flagsToDisable & RENDER_ALPHA_DEFAULT) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 0);

				if ((flagsToDisable & RENDER_ALPHA_CUSTOM) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 0);

				if ((flagsToDisable & RENDER_ALPHA_ALT) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 0);

				if ((flagsToDisable & RENDER_ALPHA_TEX_MODULATE) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 0);

				if ((flagsToDisable & RENDER_ALPHA_TEX_MOD_CUSTOM) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 0);

				if ((flagsToDisable & RENDER_ALPHA_TEX_MOD_ALT) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 0);

				if ((flagsToDisable & RENDER_Z) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ZENABLE, 1);

				if ((flagsToDisable & RENDER_DISABLE_PERSPECTIVE) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_TEXTUREPERSPECTIVE, 1);

				if ((flagsToDisable & RENDER_BILINEAR_FILTER) != 0)
				{
					DrawingDevice::SetRenderState(D3DRENDERSTATE_TEXTUREMAG, 1);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_TEXTUREMIN, 1);
				}

				if ((flagsToDisable & RENDER_ZWRITE) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, 1);

				if ((flagsToDisable & RENDER_COLOR_VERTEX) != 0)
					DrawingDevice::SetLightState(D3DLIGHTSTATE_COLORVERTEX, 0);

				if ((flagsToDisable & RENDER_ZBIAS_1) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ZBIAS, 0);

				if ((flagsToDisable & RENDER_ZBIAS_2) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ZBIAS, 0);
			}

			uint32_t flagsToEnable = newStateFlags & (newStateFlags ^ g_renderStateCache[0]);

			if (flagsToEnable)
			{
				if ((flagsToEnable & RENDER_Z) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ZENABLE, 0);

				if ((flagsToEnable & RENDER_ZWRITE) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, 0);

				if ((flagsToEnable & RENDER_DISABLE_PERSPECTIVE) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_TEXTUREPERSPECTIVE, 0);

				if ((flagsToEnable & RENDER_CULL_BACK) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_CULLMODE, 2);

				if ((flagsToEnable & RENDER_CULL_FRONT) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_CULLMODE, 3);

				if ((flagsToEnable & RENDER_CULL_NONE) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_CULLMODE, 1);

				if ((flagsToEnable & RENDER_ALPHA_DEFAULT) != 0)
				{
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_SRCBLEND, 5);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_DESTBLEND, 6);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_TEXTUREMAPBLEND, 4);
				}

				if ((flagsToEnable & RENDER_ALPHA_CUSTOM) != 0)
				{
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_SRCBLEND, g_srcBlendMode);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_DESTBLEND, g_destBlendMode);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_TEXTUREMAPBLEND, 4);
				}

				if ((flagsToEnable & RENDER_ALPHA_ALT) != 0)
				{
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_SRCBLEND, g_alphaBlendSrc);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_DESTBLEND, g_alphaBlendDest);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_TEXTUREMAPBLEND, 4);
				}

				if ((flagsToEnable & RENDER_ALPHA_TEX_MODULATE) != 0)
				{
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_SRCBLEND, 5);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_DESTBLEND, 6);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_TEXTUREMAPBLEND, 3);
				}

				if ((flagsToEnable & RENDER_ALPHA_TEX_MOD_CUSTOM) != 0)
				{
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_SRCBLEND, g_srcBlendMode);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_DESTBLEND, g_destBlendMode);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_TEXTUREMAPBLEND, 3);
				}

				if ((flagsToEnable & RENDER_ALPHA_TEX_MOD_ALT) != 0)
				{
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_SRCBLEND, g_alphaBlendSrc);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_DESTBLEND, g_alphaBlendDest);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_TEXTUREMAPBLEND, 3);
				}

				if ((flagsToEnable & RENDER_BILINEAR_FILTER) != 0)
				{
					DrawingDevice::SetRenderState(D3DRENDERSTATE_TEXTUREMAG, 2);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_TEXTUREMIN, 2);
				}

				if ((flagsToEnable & RENDER_COLOR_VERTEX) != 0)
				{
					DrawingDevice::SetLightState(D3DLIGHTSTATE_COLORVERTEX, 1);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_SHADEMODE, 2);
				}

				if ((flagsToEnable & RENDER_ZBIAS_1) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ZBIAS, 1);

				if ((flagsToEnable & RENDER_ZBIAS_2) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ZBIAS, 2);
			}

			g_renderStateCache[0] = newStateFlags;
		}
	}

	// FUNCTION: TOY2 0x004B6660
	void SetTextureStageState(int32_t newState, DWORD textureStage)
	{
		uint32_t stateFlagsToDisable = g_renderStateCache[textureStage + 1] & (newState ^ g_renderStateCache[textureStage + 1]);

		if (stateFlagsToDisable)
		{
			if ((stateFlagsToDisable & (RENDER_TEXTURE_WRAP_UV | RENDER_TEXTURE_CLAMP_U)) != 0)
				DrawingDevice::SetTextureStageState(textureStage, D3DTSS_ADDRESS, 1);

			if ((stateFlagsToDisable & (RENDER_COLOR_MODULATE | RENDER_COLOR_BLEND_FACTOR)) != 0)
				DrawingDevice::SetTextureStageState(textureStage, D3DTSS_COLOROP, 1);
		}

		uint32_t flagsToEnable = newState & (newState ^ g_renderStateCache[textureStage + 1]);

		if (flagsToEnable)
		{
			if ((flagsToEnable & RENDER_TEXTURE_WRAP_UV) != 0)
				DrawingDevice::SetTextureStageState(textureStage, D3DTSS_ADDRESS, 3);

			if ((flagsToEnable & RENDER_TEXTURE_CLAMP_U) != 0)
			{
				DrawingDevice::SetTextureStageState(textureStage, D3DTSS_ADDRESS, 2);
				DrawingDevice::SetTextureStageState(textureStage, D3DTSS_ADDRESS, 4);
			}

			if ((flagsToEnable & RENDER_COLOR_MODULATE) != 0)
			{
				DrawingDevice::SetTextureStageState(textureStage, D3DTSS_COLOROP, 4);
				DrawingDevice::SetTextureStageState(textureStage, D3DTSS_COLORARG1, 2);
				DrawingDevice::SetTextureStageState(textureStage, D3DTSS_COLORARG2, 1);
			}

			if ((flagsToEnable & RENDER_COLOR_BLEND_FACTOR) != 0)
			{
				DrawingDevice::SetRenderState(D3DRENDERSTATE_TEXTUREFACTOR, 0x80FFFFFF);
				DrawingDevice::SetTextureStageState(textureStage, D3DTSS_COLOROP, 14);
				DrawingDevice::SetTextureStageState(textureStage, D3DTSS_COLORARG1, 2);
				DrawingDevice::SetTextureStageState(textureStage, D3DTSS_COLORARG2, 1);
			}
		}

		g_renderStateCache[textureStage + 1] = newState;
	}

	// FUNCTION: TOY2 0x004B6760
	int32_t SetupMaterialRenderState(Nu3D::Material* material, int32_t stateFlags)
	{
		int32_t textureStage = 0;
		while (textureStage < g_maxSimultaneousTextures)
		{
			int32_t newStateFlags = stateFlags;
			if (material != 0)
			{
				int32_t metadata = material->metadata;
				if (metadata & 0x2)
					newStateFlags |= RENDER_ALPHA_DEFAULT | RENDER_ZWRITE;
				if (metadata & 0x10)
					newStateFlags |= RENDER_ZWRITE | RENDER_ALPHA_CUSTOM;
				if (metadata & 0x20)
					newStateFlags |= RENDER_ZWRITE | RENDER_ALPHA_ALT;
				if (metadata & 0x200)
					newStateFlags |= RENDER_ZWRITE | RENDER_ALPHA_TEX_MODULATE;
				if (metadata & 0x400)
					newStateFlags |= RENDER_ZWRITE | RENDER_ALPHA_TEX_MOD_CUSTOM;
				if (metadata & 0x800)
					newStateFlags |= RENDER_ZWRITE | RENDER_ALPHA_TEX_MOD_ALT;
				if (metadata & 0x8)
					newStateFlags = (newStateFlags & ~(RENDER_CULL_FRONT | RENDER_CULL_BACK)) | RENDER_CULL_NONE;
				if (material == NGNLoader::g_tex14Materials[0] || material == NGNLoader::g_tex14Materials[1] || material == NGNLoader::g_tex14Materials[2])
					newStateFlags &= ~(RENDER_TEXTURE_WRAP_UV | RENDER_TEXTURE_CLAMP_U);
				if (textureStage != 0)
				{
					if (metadata & 0x40000)
						newStateFlags |= RENDER_COLOR_MODULATE;
					if (metadata & 0x80000)
						newStateFlags |= RENDER_COLOR_BLEND_FACTOR;
				}
			}
			if (textureStage == 0)
			{
				newStateFlags |= RENDER_COLOR_MODULATE;
				SetRenderState(newStateFlags);
			}
			SetTextureStageState(newStateFlags, textureStage);
			if (material != 0)
				material = material->nextPass;
			textureStage++;
		}

		return textureStage;
	}

	// FUNCTION: TOY2 0x004B6850 [MATCHED]
	void InitRenderState(int32_t newStage)
	{
		SetRenderState(newStage | RENDER_COLOR_MODULATE);
		SetTextureStageState(newStage | RENDER_COLOR_MODULATE, 0);

		for (int32_t texture = 1; texture < g_maxSimultaneousTextures; ++texture)
			SetTextureStageState(0, texture);
	}

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

	// FUNCTION: TOY2 0x004B9710
	void InitResources()
	{
		g_vertexColorModBlue = 4096;
		g_vertexColorModGreen = 4096;
		g_vertexColorModRed = 4096;
		g_additionalRenderFlags = 0;
		g_lodFactor = 1.0;

		RGBColor whiteColor;
		whiteColor.b = 1.0;
		whiteColor.g = 1.0;
		whiteColor.r = 1.0;

		g_whiteMaterial = Nu3D::Material::CreateFromColor(&whiteColor);

		g_unk9F5FF8 = 0;
		g_useVertexColorMod = 0;

		SoftwareRenderer::g_viewportRect = 0;

		Set508718(5);

		g_FVF_14C_Buffer_2.format = D3DFVF_0x1C4;
		g_FVF_14C_Buffer_2.vertexCount = 1000;
		g_FVF_14C_Buffer_2.vertexBuffer = 0;
		g_FVF_14C_Buffer_2.data.verticesTL = (Nu3D::VertexTL*)malloc(sizeof(Nu3D::VertexTL) * 1000);

		Nu3D::Patch::PatchVertices::CreateVertexBuffer(&g_FVF_14C_Buffer_2, 12);

		g_FVF_14C_Buffer_1.format = D3DFVF_0x1C4;
		g_FVF_14C_Buffer_1.vertexCount = 1000;
		g_FVF_14C_Buffer_1.vertexBuffer = 0;
		g_FVF_14C_Buffer_1.data.verticesTL = (Nu3D::VertexTL*)malloc(sizeof(Nu3D::VertexTL) * 1000);

		Nu3D::Patch::PatchVertices::CreateVertexBuffer(&g_FVF_14C_Buffer_1, 2);

		g_FVF_152_Buffer.format = D3DFVF_0x152;
		g_FVF_152_Buffer.vertexCount = 1000;
		g_FVF_152_Buffer.vertexBuffer = 0;
		g_FVF_152_Buffer.data.vertices = (Nu3D::Vertex*)malloc(sizeof(Nu3D::Vertex) * 1000);

		Nu3D::Patch::PatchVertices::CreateVertexBuffer(&g_FVF_152_Buffer, 14);

		D3DDEVICEDESC outSurfaceDesc;
		memcpy(&outSurfaceDesc, DrawingDevice::CopySurfaceDesc(&outSurfaceDesc), sizeof(outSurfaceDesc));

		memset(g_renderStateCache, 0, sizeof(g_renderStateCache));

		g_maxSimultaneousTextures = outSurfaceDesc.wMaxSimultaneousTextures;

		if (outSurfaceDesc.wMaxSimultaneousTextures >= g_maxSimultaneousTexturesMax)
			g_maxSimultaneousTextures = g_maxSimultaneousTexturesMax;

		if ((outSurfaceDesc.dpcTriCaps.dwShadeCaps & 0x4000) != 0)
		{
			if ((outSurfaceDesc.dpcTriCaps.dwDestBlendCaps & 2) == 0)
				g_destBlendMode = 6;

			if ((outSurfaceDesc.dpcTriCaps.dwDestBlendCaps & 8) == 0)
			{
				g_alphaBlendSrc = 5;
				g_alphaBlendDest = 6;
			}
		}
		else
		{
			g_srcBlendMode = 2;
			g_destBlendMode = 2;

			if ((outSurfaceDesc.dpcTriCaps.dwDestBlendCaps & 8) == 0)
			{
				g_alphaBlendSrc = 5;
				g_alphaBlendDest = 6;
			}
		}

		InitRenderState(3078);
		InitRenderState(4752);

		DrawingAPI::SetVertexAPIs(g_isSoftwareRendering);

		if (g_isSoftwareRendering == 1)
		{
			DrawingAPI::DrawIndexedPrimitiveVB = SoftwareDevice::DrawIndexedPrimitiveVB;
			DrawingAPI::DrawIndexedPrimitive = SoftwareDevice::DrawIndexedPrimitive;
		}
		else
		{
			DrawingAPI::DrawIndexedPrimitiveVB = HardwareDevice::DrawIndexedPrimitiveVB;
			DrawingAPI::DrawIndexedPrimitive = HardwareDevice::DrawIndexedPrimitive;
		}

		ResetRenderPools();
		Renderer::Sprite::ResetQueue();

		SoftwareRenderer::InitialisePrimarySurface();

		DECOMP_PRINT(("Finished Renderer::InitResources\n"));
	}

	// FUNCTION: TOY2 0x004B37F0
	void Cleanup()
	{
		while (Nu3D::g_patchListHead != NULL)
		{
			Nu3D::g_patchListHead->listNext = NULL;
			Nu3D::Patch::Destroy(Nu3D::g_patchListHead);
		}
		while (Nu3D::g_primListHead != NULL)
		{
			Nu3D::g_primListHead->listNext = NULL;
			Nu3D::Primitive::Destroy(Nu3D::g_primListHead);
		}
		Nu3D::UnkFunc0();
		NGNLoader::ReleaseAllTextures();
		Nu3D::Light::DestroyAllLights();
		g_rendererValid = 0;
	}

	// FUNCTION: TOY2 0x004B3630
	void Init()
	{
		if (g_rendererValid)
			Cleanup();

		g_drawDeviceD3D = DrawingDevice::GetD3D();
		g_drawDeviceD3DDevice = DrawingDevice::GetD3DDevice();

		Nu3D::g_primListHead = 0;
		Nu3D::g_patchListHead = 0;

		Nu3D::SetIsSoftwareRendering(g_isSoftwareRendering);
		Nu3D::SetMinTexSize(g_isSoftwareRendering != 0 ? 256 : 0);

		Nu3D::Viewport::Init();
		Nu3D::Light::InitPool(16);
		NGNLoader::Init();
		Nu3D::Material::Init();

		InitResources();
		g_rendererValid = 1;
		Nu3D::Font::BuildFontTextures();
		DisableFog();

		g_deviceBlendShadeCaps = 0;

		D3DDEVICEDESC outSurfaceDesc;
		memcpy(&outSurfaceDesc, DrawingDevice::CopySurfaceDesc(&outSurfaceDesc), sizeof(outSurfaceDesc));

		if ((outSurfaceDesc.dpcTriCaps.dwShadeCaps & D3DPSHADECAPS_ALPHAGOURAUDBLEND) != 0)
			g_deviceBlendShadeCaps |= 1;

		if ((outSurfaceDesc.dpcTriCaps.dwDestBlendCaps & D3DPBLENDCAPS_ONE) != 0)
			g_deviceBlendShadeCaps |= 2;

		if ((outSurfaceDesc.dpcTriCaps.dwDestBlendCaps & D3DPBLENDCAPS_INVSRCCOLOR) != 0)
			g_deviceBlendShadeCaps |= 4;

		if ((outSurfaceDesc.dpcTriCaps.dwTextureBlendCaps & D3DPTEXTURECAPS_TRANSPARENCY) != 0)
			g_deviceBlendShadeCaps |= 8;
	}

	// FUNCTION: TOY2 0x004B3860 [MATCHED]
	void SetIsSoftwareRendering(int32_t value) { g_isSoftwareRendering = value; }

	// FUNCTION: TOY2 0x00453CD0 [MATCHED]
	void SetVirtualRatioTo54()
	{
		g_virtualScreenWidth = 320.0;
		g_virtualScreenHeight = 256.0;
	}

	// FUNCTION: TOY2 0x00490860 [MATCHED]
	void DoFrameDelay(int32_t isGameplayFrame)
	{
		// This method does the FPS delay for each frame, it is the source of all pain
		int32_t hrt = Nu3D::GetHighResolutionTime();
		int32_t elapsedMs = hrt - g_lastFrameTimestamp;

		if (Toy2::g_demoMode)
		{
			g_frameDelta = 2;

			if (elapsedMs < 33)
			{
				Nu3D::PrecisionSleep(33 - elapsedMs);
				g_lastFrameTimestamp = Nu3D::GetHighResolutionTime();
				return;
			}

			goto LBL_UPDATE_TIMESTAMP;
		}

		{
			int32_t calculatedMultiplier = 60 * elapsedMs / 1000;
			g_frameDelta = calculatedMultiplier;

			if (1000 * calculatedMultiplier / 60 != elapsedMs)
				g_frameDelta = ++calculatedMultiplier;

			int32_t minMultiplier;

			if (calculatedMultiplier < 1)
				minMultiplier = 1;
			else
				minMultiplier = calculatedMultiplier;

			if (g_maxSpeedMultiplier < minMultiplier)
			{
				calculatedMultiplier = g_maxSpeedMultiplier;
			}
			else
			{
				if (calculatedMultiplier >= 1)
					goto LBL_STARTUP_DELAY;

				calculatedMultiplier = 1;
			}

			g_frameDelta = calculatedMultiplier;

		LBL_STARTUP_DELAY:

			int32_t delayFrames = g_startupDelayFrames;
			if (g_startupDelayFrames > 0)
			{
				delayFrames = g_startupDelayFrames - calculatedMultiplier;
				g_startupDelayFrames -= calculatedMultiplier;
			}

			if (isGameplayFrame)
			{
				if (delayFrames > 0)
					goto LBL_WAIT_NEXT_FRAME;

				int32_t stabilityCounter = g_frameStabilityCounter;
				int32_t targetSpeedMultiplier = g_targetSpeedMultiplier;

				if (g_frameStabilityCounter > 0)
				{
					stabilityCounter = g_frameStabilityCounter - targetSpeedMultiplier;
					g_frameStabilityCounter -= targetSpeedMultiplier;
				}

				if (calculatedMultiplier < targetSpeedMultiplier)
				{
					if (stabilityCounter > 0)
					{
						calculatedMultiplier = targetSpeedMultiplier;
						targetSpeedMultiplier = calculatedMultiplier;
						g_frameDelta = calculatedMultiplier;
					}
				}
				else
				{
					g_frameStabilityCounter = 60;
				}

				g_targetSpeedMultiplier = calculatedMultiplier;
			}
			else
			{
				g_targetSpeedMultiplier = 1;
				g_frameStabilityCounter = 0;
				g_startupDelayFrames = 120;
			}

		LBL_WAIT_NEXT_FRAME:

			int32_t sleepTimeMs = 1000 * calculatedMultiplier / 60 - elapsedMs;

			if (sleepTimeMs > 0)
				Nu3D::PrecisionSleep(sleepTimeMs);
		}

	LBL_UPDATE_TIMESTAMP:

		g_lastFrameTimestamp = Nu3D::GetHighResolutionTime();
	}

	// FUNCTION: TOY2 0x004C2080
	int32_t ConvertRGBATo16Bit(RGBA color) { return 0; }

	// FUNCTION: TOY2 0x004B37B0
	RGBA ApplyGammaCorrection(RGBA color)
	{
		color.b = g_gammaLUT[color.b];
		color.g = g_gammaLUT[color.g];
		color.r = g_gammaLUT[color.r];

		return color;
	}

	// FUNCTION: TOY2 0x004B2C80
	void ClearScreen(RGBA clearColor, int32_t clearFlags)
	{
		RGBA color = ApplyGammaCorrection(clearColor);

		if (g_isSoftwareRendering)
		{
			uint16_t clearColor = Renderer::ConvertRGBATo16Bit(color);
			SoftwareRenderer::g_softwareClearColor = (clearColor << 16) | clearColor;
		}
		else
		{
			DrawingDevice::ClearScreen(clearFlags, color.value);
		}
	}

	// FUNCTION: TOY2 0x004B2D80
	void ApplyFogSettings()
	{
		if (g_fogEnabled)
		{
			DrawingDevice::SetRenderState(D3DRENDERSTATE_FOGENABLE, 1);
			DrawingDevice::SetRenderState(D3DRENDERSTATE_FOGCOLOR, g_fogColor & 0xFFFFFF);

			DrawingDevice::SetLightState(D3DLIGHTSTATE_FOGMODE, 3);
			DrawingDevice::SetLightState(D3DLIGHTSTATE_FOGSTART, g_fogStart);
			DrawingDevice::SetLightState(D3DLIGHTSTATE_FOGEND, g_fogEnd);
		}
		else
		{
			DrawingDevice::SetRenderState(D3DRENDERSTATE_FOGENABLE, 0);
		}
	}

	// FUNCTION: TOY2 0x004B2D50 [MATCHED]
	int32_t BeginScene()
	{
		if (! DrawingDevice::BeginScene())
		{
			Renderer::ApplyFogSettings();
			Nu3D::Viewport::Reset();
			Nu3D::Viewport::SetViewClipRect();

			if (g_isSoftwareRendering)
				SoftwareRenderer::UnkFunc32();

			return 1;
		}

		return 0;
	}

	// FUNCTION: TOY2 0x004B2DE0
	void EndScene(int32_t presentFrame)
	{
		DrawingDevice::EndScene();

		if (presentFrame)
		{
			if (g_isSoftwareRendering)
			{
				SoftwareRenderer::PresentFrame();
			}
			else if (DrawingDevice::PresentFrame() == DDERR_SURFACELOST)
			{
				DrawingDevice::CD3DFramework* device = DrawingDevice::g_drawingDevice;

				LPDIRECTDRAWSURFACE4 frontBuffer = DrawingDevice::g_drawingDevice->m_pddsFrontBuffer;

				if (frontBuffer && frontBuffer->IsLost())
					device->m_pddsFrontBuffer->Restore();

				LPDIRECTDRAWSURFACE4 backBuffer = device->m_pddsBackBuffer;

				if (backBuffer && backBuffer->IsLost())
					device->m_pddsBackBuffer->Restore();

				LPDIRECTDRAWSURFACE4 zBuffer = device->m_pddsZBuffer;

				if (zBuffer)
				{
					if (zBuffer->IsLost())
						device->m_pddsZBuffer->Restore();
				}
			}
		}
	}

	// FUNCTION: TOY2 0x00401B60
	void DrawBlackBorderBox(int32_t xPos, int32_t yPos, int32_t width, int32_t height, uint32_t red, uint32_t green, uint32_t blue)
	{
		Sprite::DrawScaled(xPos, yPos, 6, 1, 0, 0, 0, 0x60, 0x2000, height);
		Sprite::DrawScaled((width >> 12) + xPos - 2, yPos, 6, 1, 0, 0, 0, 0x60, 0x2000, height);
		Sprite::DrawScaled(xPos, yPos, 6, 1, 0, 0, 0, 0x60, width, 0x1000);
		Sprite::DrawScaled(xPos, (height >> 12) + yPos - 1, 6, 1, 0, 0, 0, 0x60, width, 0x1000);
		Sprite::DrawScaled(xPos + 2, yPos + 1, 6, 1, red, green, blue, 0, width - 0x4000, height - 0x2000);
	}

	// FUNCTION: TOY2 0x0049B580
	void DrawMainMenuText(int16_t yPos, char* text, int32_t fadeAlpha)
	{
		char* charPtr = text;
		int32_t strLength = 0;

		if (*text)
			while (text[++strLength]) {};

		int16_t xPos = 160 - 6 * strLength;

		if (strLength > 0)
		{
			int32_t remaining = strLength;

			do
			{
				char currentChar = *charPtr++;

				if (currentChar != ' ')
				{
					uint8_t tileIndex;

					if (currentChar == '\'')
						tileIndex = 47;
					else
						tileIndex = currentChar - 97;

					if (fadeAlpha == 128)
						Sprite::DrawScaled(xPos, yPos, 50, tileIndex, 128, 128, 128, 255, 2048, 2048);
					else
						Sprite::DrawScaled(xPos, yPos, 50, tileIndex, 255, 255, 255, (fadeAlpha << 9) + 96, 2048, 2048);
				}

				xPos += 12;
				--remaining;

			} while (remaining);
		}
	}

	// FUNCTION: TOY2 0x0044DD80
	void DrawTintOverlay()
	{
		Vector2F uvTopLeft;
		Vector2F uvBottomRight;
		bool greenAtOrBelowMid;

		if (Nu3D::Camera::g_cameraTintBlue != 128)
		{
			if (Nu3D::Camera::g_cameraTintBlue > 128)
				goto LBL_BRIGHTEN;

			greenAtOrBelowMid = Nu3D::Camera::g_cameraTintGreen <= 128;

			if (greenAtOrBelowMid)
				goto LBL_DARKEN;

		LBL_BRIGHTEN:

			int32_t brightenGreen = 2 * Nu3D::Camera::g_cameraTintGreen - 256;
			int32_t brightenBlue = 2 * Nu3D::Camera::g_cameraTintBlue - 256;
			int32_t brightenRed = 2 * Nu3D::Camera::g_cameraTintRed - 256;

			if (brightenBlue <= 255)
			{
				if (brightenBlue < 0)
					brightenBlue = 0;
			}
			else
			{
				brightenBlue = -1;
			}

			if (brightenGreen <= 255)
			{
				if (brightenGreen < 0)
					brightenGreen = 0;
			}
			else
			{
				brightenGreen = -1;
			}

			if (brightenRed <= 255)
			{
				if (brightenRed < 0)
					brightenRed = 0;
			}
			else
			{
				brightenRed = -1;
			}

			RGBA brightenColor;
			brightenColor.b = brightenBlue;
			brightenColor.g = brightenGreen;
			brightenColor.a = -1 - brightenGreen;
			brightenColor.r = brightenRed;

			uvTopLeft.x = 0.0;
			uvTopLeft.y = 0.0;

			uvBottomRight.x = 1.0;
			uvBottomRight.y = 1.0;

			Sprite::Queue2DSprite(0.0, 0.0, 1.0, 1.0, &uvTopLeft, &uvBottomRight, 0, brightenColor, RENDER_PRESET_COLOR_OVERLAY);

			if (D3DApp::g_renderMode == RENDERMODE_SOFTWARE && SoftwareRenderer::g_bitsPerPixel == 8)
				SoftwareRenderer::UnkFunc7();

			return;
		}

		greenAtOrBelowMid = Nu3D::Camera::g_cameraTintGreen <= 128;

		if (Nu3D::Camera::g_cameraTintGreen != 128)
		{
			if (greenAtOrBelowMid)
				goto LBL_DARKEN;

			goto LBL_BRIGHTEN;
		}

		if (Nu3D::Camera::g_cameraTintRed == 128)
			return;

	LBL_DARKEN:

		if (Nu3D::Camera::g_cameraTintRed > 128)
			goto LBL_BRIGHTEN;

		int32_t darkenBlue = 2 * (128 - Nu3D::Camera::g_cameraTintBlue);
		int32_t darkenGreen = 2 * (128 - Nu3D::Camera::g_cameraTintGreen);
		int32_t darkenRed = 2 * (128 - Nu3D::Camera::g_cameraTintRed);

		if (darkenBlue <= 255)
		{
			if (((128 - Nu3D::Camera::g_cameraTintBlue) & 0x40000000) != 0)
				darkenBlue = 0;
		}
		else
		{
			darkenBlue = -1;
		}

		if (darkenGreen <= 255)
		{
			if (((128 - Nu3D::Camera::g_cameraTintGreen) & 0x40000000) != 0)
				darkenGreen = 0;
		}
		else
		{
			darkenGreen = -1;
		}

		if (darkenRed <= 255)
		{
			if (((128 - Nu3D::Camera::g_cameraTintRed) & 0x40000000) != 0)
				darkenRed = 0;
		}
		else
		{
			darkenRed = -1;
		}

		uvTopLeft.x = 0.0;
		uvTopLeft.y = 0.0;

		uvBottomRight.x = 1.0;
		uvBottomRight.y = 1.0;

		RGBA darkenColor;
		darkenColor.a = -1 - darkenGreen;
		darkenColor.b = darkenBlue;
		darkenColor.g = darkenGreen;
		darkenColor.r = darkenRed;

		int32_t texDataIndex;

		if (darkenBlue == darkenGreen && darkenBlue == darkenRed)
		{
			texDataIndex = NGNLoader::GetTextureDataIndex(14);

			if (! texDataIndex)
			{
				texDataIndex = NGNLoader::GetTextureDataIndex(36);

				if (! texDataIndex)
					texDataIndex = NGNLoader::GetTextureDataIndex(37);
			}
		}
		else
		{
			texDataIndex = 0;
		}

		Sprite::Queue2DSprite(0.0, 0.0, 1.0, 1.0, &uvTopLeft, &uvBottomRight, texDataIndex, darkenColor, RENDER_PRESET_FADE_OVERLAY);

		if (D3DApp::g_renderMode == RENDERMODE_SOFTWARE && SoftwareRenderer::g_bitsPerPixel == 8)
			SoftwareRenderer::UnkFunc7();
	}

	// FUNCTION: TOY2 0x0048F3E0 [MATCHED]
	void ResetParallax()
	{
		g_parallaxCurHorizScroll = 0.0;
		g_parallaxHorizOffset = 0.0;
		g_parallaxTexHeightRatio = 1.0;
		g_parallaxTexWidthRatio = 1.0;
	}

	// FUNCTION: TOY2 0x004B3870 [MATCHED]
	int32_t GetIsSoftwareRendering() { return g_isSoftwareRendering; }

	// FUNCTION: TOY2 0x004B3740 [MATCHED]
	float BuildGammaCorrectionLUT(float gammaCorrection)
	{
		float result = g_gammaCorrection;

		g_gammaCorrection = gammaCorrection;
		g_gammaFixedPoint = (gammaCorrection * 65536.0f);

		int32_t curGammaIdx = 0;
		int32_t count = 0;
		int32_t step = g_gammaFixedPoint;

		do
		{
			int32_t outputValue;

			if ((int32_t)(count & 0xFFFF0000) > 0xFF0000)
				outputValue = 0xFF;
			else
				outputValue = count >> 16;

			g_gammaLUT[curGammaIdx++] = outputValue;
			count += step;

		} while (curGammaIdx < 256);

		return result;
	}

	// FUNCTION: TOY2 0x004B2CC0 [MATCHED]
	void GetBlendShadeCaps(int32_t* capsOut)
	{
		if (capsOut)
			*capsOut = Renderer::g_deviceBlendShadeCaps;
	}

	// FUNCTION: TOY2 0x0048F410
	void RenderParallaxBackground(int32_t forceRender)
	{
		if (g_drawParallaxTexture)
		{
			if (Toy2::g_hasStaticBackdrop)
			{
				g_parallaxHorizOffset = 0.0;
				g_parallaxCurHorizScroll = 0.0;
				g_parallaxTexHeightRatio = 1.0;
				g_parallaxTexWidthRatio = 1.0;
			}
			else if (! Toy2::g_hasBackdrop)
			{
				return;
			}

			if (forceRender || GetIsSoftwareRendering() || (Glue::SetBackdrop(Toy2::g_nextBackdropId), ! Glue::BackdropBltFast()))
			{
				int32_t texIndex = NGNLoader::GetTextureDataIndex(Toy2::g_nextBackdropId);

				if (texIndex)
				{
					Vector2F uvMax;
					Vector2F uvMin;

					uvMin.y = 0.0;
					uvMin.x = 0.0;

					uvMax.y = 1.0;
					uvMax.x = 1.0;

					if (g_parallaxHorizOffset > 0.0)
					{
						float topFillHeight = 1.0 / g_virtualScreenHeight + g_parallaxHorizOffset;
						Sprite::Queue2DSprite(0.0, 0.0, 1.0, topFillHeight, &uvMin, &uvMax, 0, g_parallaxTexLastPixel, RENDER_PARALLAX_BG);
					}

					double verticalExtent = g_parallaxTexHeightRatio + g_parallaxHorizOffset;

					if (verticalExtent < 1.0)
					{
						float bottomFillHeight = 1.0 - g_parallaxHorizOffset - g_parallaxTexHeightRatio + 1.0 / g_virtualScreenHeight;
						float verticalEnd = verticalExtent;

						Sprite::Queue2DSprite(0.0, verticalEnd, 1.0, bottomFillHeight, &uvMin, &uvMax, 0, g_parallaxTexFirstPixel, RENDER_PARALLAX_BG);
					}

					double nextHorizontalPos;

					do
					{
						RGBA color;
						color.value = -1;

						Sprite::Queue2DSprite(g_parallaxCurHorizScroll,
							g_parallaxHorizOffset,
							g_parallaxTexWidthRatio,
							g_parallaxTexHeightRatio,
							&uvMin,
							&uvMax,
							texIndex,
							color,
							RENDER_PARALLAX_BG);

						nextHorizontalPos = g_parallaxTexWidthRatio + g_parallaxCurHorizScroll;

						g_parallaxCurHorizScroll = nextHorizontalPos;

					} while (nextHorizontalPos < 1.0);
				}
			}
		}
	}

	// FUNCTION: TOY2 0x004B8490 [MATCHED]
	void RenderPrimitive(Nu3D::Primitive* primitive, const D3DMATRIX* transform, int32_t renderFlags)
	{
		if (0.0f != primitive->originRadius)
		{
			renderFlags |= g_additionalRenderFlags;
			Nu3D::InstanceData* instanceData = Nu3D::InstanceData::AllocFromMatrix(transform, renderFlags);
			if (instanceData)
			{
				for (; primitive; primitive = primitive->listNext)
					ProcessPrimitive(instanceData, primitive);
			}
		}
	}

}

namespace Nu3D
{
	using namespace Renderer;

	// FUNCTION: TOY2 0x004B84E0
	InstanceData* InstanceData::AllocFromMatrix(const D3DMATRIX* matrix, int32_t renderFlags)
	{
		if (! g_instanceDataFreeCount)
			return 0;

		Nu3D::InstanceData* instanceData = &g_instanceDataPool[--g_instanceDataFreeCount];
		memcpy(instanceData->matrices, matrix, sizeof(D3DMATRIX));
		instanceData->renderFlags = renderFlags;
		instanceData->lodFactor = g_lodFactor;
		instanceData->horzOffset = g_materialHorzOffset;
		instanceData->vertOffset = g_materialVertOffset;
		instanceData->renderModeFlags = g_unk9F5FF8 != 0;

		if (g_useVertexColorMod)
		{
			instanceData->renderModeFlags |= 2;
			instanceData->vertexModColor.r = g_vertexColorModRed;
			instanceData->vertexModColor.g = g_vertexColorModGreen;
			instanceData->vertexModColor.b = g_vertexColorModBlue;
		}

		Nu3D::Viewport::GetViewClipRect(&instanceData->clipRect);
		instanceData->unkInt6 = g_primitiveRenderFlags;
		instanceData->sprite = g_instanceSpriteTemplate;
		return instanceData;
	}

	// FUNCTION: TOY2 0x004B8840
	InstanceData* InstanceData::AllocFromNodeMatrices(const D3DMATRIX* matrices, int32_t* nodeIndices, int32_t count, int32_t* flags, int32_t renderFlags)
	{
		if (! g_instanceDataFreeCount)
			return 0;

		--g_instanceDataFreeCount;
		D3DMATRIX* dest = g_instanceDataPool[g_instanceDataFreeCount].matrices;

		while (count != 0)
		{
			if (flags && (flags[*nodeIndices] & 1))
			{
				++g_instanceDataFreeCount;
				return 0;
			}

			memcpy(dest, &matrices[*nodeIndices], sizeof(D3DMATRIX));
			dest++;
			nodeIndices++;
			count--;
		}

		g_instanceDataPool[g_instanceDataFreeCount].renderFlags = renderFlags;
		g_instanceDataPool[g_instanceDataFreeCount].lodFactor = g_lodFactor;
		g_instanceDataPool[g_instanceDataFreeCount].horzOffset = g_materialHorzOffset;
		g_instanceDataPool[g_instanceDataFreeCount].vertOffset = g_materialVertOffset;
		g_instanceDataPool[g_instanceDataFreeCount].renderModeFlags = g_unk9F5FF8 != 0;
		Nu3D::Viewport::GetViewClipRect(&g_instanceDataPool[g_instanceDataFreeCount].clipRect);
		g_instanceDataPool[g_instanceDataFreeCount].unkInt6 = g_primitiveRenderFlags;
		return &g_instanceDataPool[g_instanceDataFreeCount];
	}
}

namespace Renderer
{

	// FUNCTION: TOY2 0x004B85E0 [MATCHED]
	void ProcessPrimitive(Nu3D::InstanceData* instanceData, Nu3D::Primitive* primitive)
	{
		Nu3D::Material* material = Nu3D::Material::GetFreeByIndex(primitive->materialIndex);
		for (;;)
		{
			if (! material)
				return;

			RenderEntry::AllocObj(material, primitive, instanceData);
			if ((material->metadata & 4) != 0)
				RenderEntry::AllocObj(NGNLoader::g_tex14Materials[0], primitive, instanceData);
			if ((material->metadata & 0x40) != 0)
				RenderEntry::AllocObj(NGNLoader::g_tex14Materials[1], primitive, instanceData);
			if ((material->metadata & 0x80) != 0)
				RenderEntry::AllocObj(NGNLoader::g_tex14Materials[2], primitive, instanceData);

			for (int32_t textureStage = 0; textureStage < g_maxSimultaneousTextures; ++textureStage)
			{
				if (! material)
					return;
				material = material->nextPass;
			}
		}
	}

	// FUNCTION: TOY2 0x004B8670 [MATCHED]
	RenderEntry* RenderEntry::AllocObj(Nu3D::Material* material, Nu3D::Primitive* primitive, Nu3D::InstanceData* instanceData)
	{
		if (g_renderEntryFreeCount)
		{
			RenderEntry* entry = &g_renderEntryPool[--g_renderEntryFreeCount];
			entry->primitive = primitive;
			entry->instanceData = instanceData;
			entry->material = material;

			if (instanceData->lodFactor == 1.0f && (material->metadata & 0xE33) == 0)
			{
				entry->type = RENDER_TYPE7;
				entry->next = material->renderEntryHead;
				material->renderEntryHead = entry;
			}
			else
			{
				entry->type = RENDER_TYPE9;
				InsertIntoBucket(entry);
			}
			return entry;
		}

		Logger::DebugLog("rndrentryAllocObj - out of rndrentries");
		return 0;
	}

	// FUNCTION: TOY2 0x004B86F0
	void RenderEntry::InsertIntoBucket(RenderEntry* entry)
	{
		Vector3F cameraPosition;
		Vector3F instancePosition;
		Nu3D::Math::GetPositionVector(&Nu3D::Camera::g_activeCamera.transform, &cameraPosition);
		Nu3D::Math::GetPositionVector(&entry->instanceData->matrices[0], &instancePosition);
		Nu3D::Math::VertexSubtract(&cameraPosition, &cameraPosition, &instancePosition);

		float distanceSquared = cameraPosition.x * cameraPosition.x + cameraPosition.y * cameraPosition.y + cameraPosition.z * cameraPosition.z;
		if (distanceSquared < 1.0f)
			distanceSquared = 1.0f;
		entry->distanceSquared = distanceSquared;

		union
		{
			float value;
			uint32_t bits;
		} distance;
		distance.value = distanceSquared;
		int32_t bucketIndex = (distance.bits >> 20) - 0x3F8;
		if (bucketIndex > 255)
			return;

		int32_t depth = 0;
		RenderEntry* previous = 0;
		RenderEntry* current = (RenderEntry*)Nu3D::g_spriteBuckets[bucketIndex];
		while (current && distanceSquared < current->distanceSquared)
		{
			++depth;
			previous = current;
			current = current->next;
		}

		entry->next = current;
		if (previous)
			previous->next = entry;
		else
			Nu3D::g_spriteBuckets[bucketIndex] = (Nu3D::Sprite*)entry;

		if (depth >= Nu3D::g_maxBucketDepth)
			Nu3D::g_maxBucketDepth = depth;
	}

	// FUNCTION: TOY2 0x004B87F0
	void RenderPatchList(Nu3D::Patch* patch, const D3DMATRIX* matrices, int32_t* flags, int32_t renderFlags)
	{
		renderFlags |= g_additionalRenderFlags;

		while (patch != 0)
		{
			Nu3D::InstanceData* instanceData =
				Nu3D::InstanceData::AllocFromNodeMatrices(matrices, patch->controlPointIndices, patch->controlPointCount, flags, renderFlags);

			if (instanceData != 0)
			{
				ProcessPatch(instanceData, patch);
			}

			patch = patch->listNext;
		}
	}

	// FUNCTION: TOY2 0x004B8940
	void ProcessPatch(Nu3D::InstanceData* instanceData, Nu3D::Patch* patch)
	{
		Nu3D::Material* material = Nu3D::Material::GetFreeByIndex(patch->materialId);
		RenderEntry::AllocPatch(material, (Nu3D::Primitive*)patch, instanceData);

		if ((material->metadata & 4) != 0)
		{
			RenderEntry::AllocPatch(NGNLoader::g_tex14Materials[0], (Nu3D::Primitive*)patch, instanceData);
		}

		if ((material->metadata & 0x40) != 0)
		{
			RenderEntry::AllocPatch(NGNLoader::g_tex14Materials[1], (Nu3D::Primitive*)patch, instanceData);
		}

		if ((material->metadata & 0x80) != 0)
		{
			RenderEntry::AllocPatch(NGNLoader::g_tex14Materials[2], (Nu3D::Primitive*)patch, instanceData);
		}
	}

	// FUNCTION: TOY2 0x004B89B0
	RenderEntry* RenderEntry::AllocPatch(Nu3D::Material* material, Nu3D::Primitive* primitive, Nu3D::InstanceData* instanceData)
	{
		if (g_renderEntryFreeCount)
		{
			RenderEntry* entry = &g_renderEntryPool[--g_renderEntryFreeCount];
			entry->primitive = primitive;
			entry->instanceData = instanceData;
			entry->material = material;

			if (instanceData->lodFactor == 1.0f && (material->metadata & 0xE33) == 0)
			{
				entry->type = RENDER_TYPE6;
				entry->next = material->renderEntryHead;
				material->renderEntryHead = entry;
				return entry;
			}

			entry->type = RENDER_TYPE8;
			InsertIntoBucket(entry);
			return entry;
		}

		Logger::DebugLog("rndrentryAllocPatch - out of rndrentries");
		return 0;
	}

	// FUNCTION: TOY2 0x004B8400
	void FlushMaterialBuckets() {}

	// FUNCTION: TOY2 0x004B6A90
	void FlushTransparentBuckets() {}

	// FUNCTION: TOY2 0x004B5CF0
	void FlushPrimitives() {}

	// FUNCTION: TOY2 0x004B5E20
	void DrawPrimitive(void* vertices, DWORD vertexCount)
	{
		if (vertexCount != 0)
			DrawingDevice::DrawPrimitive(D3DPT_TRIANGLELIST, D3DFVF_0x1C4, vertices, vertexCount, 0x10);
	}

	// FUNCTION: TOY2 0x004B6A50
	void FlushRenderQueues()
	{
		if (g_drawMaterialBuckets)
			FlushMaterialBuckets();

		if (g_drawTransparentBuckets)
			FlushTransparentBuckets();

		FlushPrimitives();
		ResetRenderPools();

		if (g_isSoftwareRendering)
		{
			SoftwareRenderer::UnkFunc33();
			SoftwareRenderer::UnkFunc32();
		}
	}

	// FUNCTION: TOY2 0x004B8BF0
	RGBA ModulateColorByAlpha(RGBA color, int32_t flags)
	{
		uint8_t blue;
		uint8_t green;
		uint8_t red;

		if (((flags & 0x4000) != 0 || flags == 0x20000000) && g_srcBlendMode == 2)
		{
			blue = (color.a * color.b) >> 8;
			red = (color.a * color.r) >> 8;
			green = (color.a * color.g) >> 8;

			color.b = blue;
			color.r = red;
			color.g = green;
		}
		else
		{
			blue = color.b;
			green = color.g;
			red = color.r;
		}

		if (g_alphaBlendDest != 6 || flags != 0x40000000)
			return color;

		color.a = green;

		if (blue == green && blue == red)
		{
			color.b = 0;
			color.g = 0;
			color.r = 0;
			return color;
		}

		color.b = 255 - blue;
		color.g = 255 - green;
		color.r = 255 - red;

		return color;
	}

	// FUNCTION: TOY2 0x004C27D0
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

	// FUNCTION: TOY2 0x004B8450
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

	// FUNCTION: TOY2 0x004B9600 [MATCHED]
	void DrawSingleTexturedTriangle(Nu3D::VertexTL* vertices, int32_t texIndex, int32_t renderFlags)
	{
		InitRenderState(renderFlags);
		BindTexture(texIndex);
		DrawingDevice::DrawPrimitive(D3DPT_TRIANGLELIST, D3DFVF_0x1C4, vertices, 3, 0x10);
	}
}

namespace DevDraw
{
	// GLOBAL: TOY2 0x00732FBC
	int32_t g_vertexCount;

	// FUNCTION: TOY2 0x004907E0
	int16_t DrawSlots()
	{
		switch (D3DApp::g_renderMode)
		{
			case 1:
				if (SoftwareRenderer::g_unk559C40 != 1 || (Renderer::g_frameDelta & 1) == 0)
				{
					SoftwareRenderer::UnkFunc8(SoftwareRenderer::g_unk839278, 0);
				}
				SoftwareRenderer::g_unk839280 = 0;
				Nu3D::MemSet32Util(SoftwareRenderer::g_unk504D34, 0x1000, 0);
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

	// Flushes one slot of the opaque indexed draw buffer: binds the slot's
	// texture, issues an indexed triangle-list DrawIndexedPrimitive,
	// accumulates the vertex count, and resets the slot's vertex and index
	// counts. The texture name is built as "LOADTEXT_texXX" where XX is the
	// zero-padded slot index.
	//
	// The slot parameter stays in a 16-bit register (for the current-slot word
	// store) while a sign-extended copy drives the array indexing. The draw
	// buffer pointer is re-read from the global before each use (the string
	// building and the COM calls clobber the holding register), so no local
	// caches it.
	// FUNCTION: TOY2 0x00490470
	int16_t FlushDrawBufferSlot(int16_t slot)
	{
		Renderer::InitRenderState(0);

		LPDIRECT3DDEVICE3 d3dDevice = DrawingDevice::GetD3DDevice();
		int32_t slotIndex = slot;

		if (Toy2::g_drawBuffer->VerticeCount[slotIndex] != 0)
		{
			char textureName[15] = "LOADTEXT_tex00";
			textureName[12] = (char)('0' + slotIndex / 10);
			textureName[13] = (char)('0' + slotIndex % 10);

			Nu3D::BmpDataNode* bmpDataNode = Nu3D::GetBmpDataNodeByName_T(textureName);
			d3dDevice->SetTexture(0, Nu3D::GetTexture(bmpDataNode));

			HRESULT error = d3dDevice->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,
				D3DFVF_0x1C4,
				Toy2::g_drawBuffer->Vertice[slotIndex],
				Toy2::g_drawBuffer->VerticeCount[slotIndex],
				Toy2::g_drawBuffer->Index[slotIndex],
				Toy2::g_drawBuffer->IndexCount[slotIndex],
				8);

			if (error < 0)
			{
				Logger::LogDDError(
					"dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, ( 0x004 | 0x040 | 0x080 | 0x100 ), (LPVOID) (drawb->Vertice[i]), "
					"drawb->VerticeCount[i], (LPWORD) & (drawb->Index[i]), drawb->IndexCount[i], 0x00000008l)",
					error);
			}

			Toy2::g_currentDrawSlot = slot;
			g_vertexCount += Toy2::g_drawBuffer->VerticeCount[slotIndex];
		}

		Toy2::g_drawBuffer->IndexCount[slotIndex] = 0;
		Toy2::g_drawBuffer->VerticeCount[slotIndex] = 0;

		return 1;
	}

	// Flushes one slot of the transparent indexed draw buffer: binds the
	// slot's texture, issues an indexed triangle-list DrawIndexedPrimitive,
	// accumulates the vertex count, resets the render state, and clears the
	// slot's vertex and index counts. The texture name is built as
	// "LOADTEXT_texXX" where XX is the zero-padded slot index.
	//
	// Only slots 0..31 are valid (the transparent pool is half the opaque
	// pool's 64 slots).
	//
	// The slot parameter stays in a 16-bit register (BX) while a sign-extended
	// copy (slotIndex) drives the array indexing. The draw buffer pointer is
	// re-read from the global before each use (no local cache, so the reloads
	// match retail). The render state is set to 0x400 before the draw and
	// reset to 0 after it.
	// FUNCTION: TOY2 0x004905C0
	int16_t FlushTransparentDrawBufferSlot(int16_t slot)
	{
		LPDIRECT3DDEVICE3 d3dDevice = DrawingDevice::GetD3DDevice();
		Renderer::InitRenderState(0x400);

		if (slot < 0x20)
		{
			int32_t slotIndex = slot;

			if (Toy2::g_transparentDrawBuffer->VerticeCount[slotIndex] != 0)
			{
				char textureName[15] = "LOADTEXT_tex00";
				textureName[12] = (char)('0' + slotIndex / 10);
				textureName[13] = (char)('0' + slotIndex % 10);

				Nu3D::BmpDataNode* bmpDataNode = Nu3D::GetBmpDataNodeByName_T(textureName);
				d3dDevice->SetTexture(0, Nu3D::GetTexture(bmpDataNode));

				HRESULT error = d3dDevice->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,
					D3DFVF_0x1C4,
					Toy2::g_transparentDrawBuffer->Vertice[slotIndex],
					Toy2::g_transparentDrawBuffer->VerticeCount[slotIndex],
					Toy2::g_transparentDrawBuffer->Index[slotIndex],
					Toy2::g_transparentDrawBuffer->IndexCount[slotIndex],
					8);

				if (error < 0)
				{
					Logger::LogDDError(
						"dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, ( 0x004 | 0x040 | 0x080 | 0x100 ), (LPVOID) (drawtranb->Vertice[i]), "
						"drawtranb->VerticeCount[i], (LPWORD) & (drawtranb->Index[i]), drawtranb->IndexCount[i], 0x00000008l)",
						error);
				}

				Toy2::g_currentDrawSlot = slot;
				g_vertexCount += Toy2::g_transparentDrawBuffer->VerticeCount[slotIndex];
			}
		}

		Renderer::InitRenderState(0);

		Toy2::g_transparentDrawBuffer->IndexCount[slot] = 0;
		Toy2::g_transparentDrawBuffer->VerticeCount[slot] = 0;

		return 1;
	}

}
