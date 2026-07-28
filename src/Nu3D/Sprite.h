#pragma once

#include "Common.h"
#include "Numerics.h"
#include "Nu3D/Viewport.h"
#include "Renderer/RenderType.h"
#include <stddef.h>
#include <directx6/ddraw.h>
#include <directx6/d3d.h>

namespace Nu3D
{
	struct Sprite
	{
		Sprite* next;
		Renderer::RenderType type;
		float distanceSquared;
		Vector3F position;
		Vector3F triVerts[3];
		int32_t trigIndex;
		float width;
		float height;
		Vector2F uvBottomLeft;
		Vector2F uvTopLeft;
		Vector2F uvBottomRight;
		Vector2F uvTopRight;
		int32_t textureIndex;
		RGBA color;
		int32_t renderFlags;
		Viewport::ViewportRect viewportRect;

		static void InsertIntoBucket(Sprite* sprite);
	};

	enum InstanceRenderModeFlags
	{
		INSTANCE_RENDER_VERTEX_LIGHTING = 0x1,
		INSTANCE_RENDER_VERTEX_COLOR_MODULATION = 0x2,
	};

	struct InstanceData
	{
		struct TextureProjectionData
		{
			float scale;
			int32_t reserved0[4];
			Vector3F referencePoint;
			int32_t reserved1[9];
			D3DMATRIX matrix;
		};

		D3DMATRIX matrices[4];
		int32_t renderFlags;
		float lodFactor;
		float horzOffset;
		float vertOffset;
		int32_t renderModeFlags;
		int32_t unkInt6;
		RGB32 vertexModColor;
		Viewport::ViewportRect clipRect;
		union
		{
			Sprite sprite;
			TextureProjectionData textureProjection;
		};

		static InstanceData* AllocFromMatrix(const D3DMATRIX* matrix, int32_t renderFlags);
		static InstanceData* AllocFromNodeMatrices(const D3DMATRIX* matrices, int32_t* nodeIndices, int32_t count, int32_t* flags, int32_t renderFlags);
	};

	extern Sprite* g_spriteBuckets[256];
	extern int32_t g_maxBucketDepth;

	STATIC_ASSERT(sizeof(InstanceData) == 0x1B8);
	STATIC_ASSERT(sizeof(InstanceData::TextureProjectionData) == 0x84);
	STATIC_ASSERT(offsetof(InstanceData::TextureProjectionData, referencePoint) == 0x14);
	STATIC_ASSERT(offsetof(InstanceData::TextureProjectionData, matrix) == 0x44);
	STATIC_ASSERT(offsetof(InstanceData, textureProjection) == 0x134);
	STATIC_ASSERT(sizeof(Sprite) == 0x84);
}
