#include "Renderer/Renderer.h"
#include "Renderer/RendererInternal.h"
#include "DrawingDevice.h"
#include "Nu3D/Math.h"
#include "Nu3D/Sprite.h"
#include "SoftwareRenderer.h"

// The per-draw modifiers a caller sets before it queues geometry: the extra
// render flags, the vertex colour modulation, and the sprite template an
// instance inherits. DrawSingleTexturedTriangle is the one immediate draw that
// uses them without a render entry.
//
// Retail run 0x004B92B0-0x004B9600, between the Renderer/Sprite.cpp and
// NGNLoader.cpp runs.

namespace Renderer
{
	// FUNCTION: TOY2 0x004B92B0 [MATCHED]
	int32_t SetAdditionalRenderFlags(int32_t flags)
	{
		int32_t previousFlags = g_additionalRenderFlags;
		g_additionalRenderFlags = flags;
		return previousFlags;
	}

	// FUNCTION: TOY2 0x004B92C0 [MATCHED]
	void SetInstanceSpriteTemplate(const Nu3D::Sprite* sprite) { g_instanceSpriteTemplate = *sprite; }

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

	// FUNCTION: TOY2 0x004B9600 [MATCHED]
	void DrawSingleTexturedTriangle(Nu3D::VertexTL* vertices, int32_t texIndex, int32_t renderFlags)
	{
		InitRenderState(renderFlags);
		BindTexture(texIndex);
		DrawingDevice::DrawPrimitive(D3DPT_TRIANGLELIST, D3DFVF_0x1C4, vertices, 3, 0x10);
	}

}
