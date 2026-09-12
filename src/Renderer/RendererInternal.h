#ifndef RENDERERINTERNAL_H
#define RENDERERINTERNAL_H

#include "Renderer/Renderer.h"
#include "Nu3D/Sprite.h"

// Render state that Renderer.cpp defines and the units split out of it read.
// None of it belongs in the public header: no caller outside the renderer sets
// a pool counter or a vertex colour modulation channel.
namespace Renderer
{
	extern int32_t g_vertexColorModRed;
	extern int32_t g_vertexColorModGreen;
	extern int32_t g_vertexColorModBlue;
	extern int32_t g_useVertexColorMod;
	extern int32_t g_vertexLightingEnabled;
	extern int32_t g_primitiveRenderFlags;
	extern int32_t g_instanceDataFreeCount;
	extern int32_t g_renderEntryFreeCount;
	extern int32_t g_maxSimultaneousTextures;
	extern float g_lodFactor;
	extern Nu3D::Sprite g_instanceSpriteTemplate;
}

#endif
