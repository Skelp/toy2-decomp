#ifndef SOFTWARERENDERERINTERNAL_H
#define SOFTWARERENDERERINTERNAL_H

#include "SoftwareRenderer.h"
#include "Nu3D/Nu3D.h"

// Types that the software renderer keeps private to itself but that more than
// one of its translation units needs. The public header forward declares them.
namespace SoftwareRenderer
{
	// The locked back buffer of the software device, and the level index that
	// selects a palette. Every rasterizer band writes through the first and reads
	// the second, so they stay private to the renderer.
	extern void* g_lockedBackBuffer;
	extern int32_t g_levelFileIndex;

	// A queued render command for the software rasterizer. QueueRenderCommand enqueues
	// transformed vertices (3 for a triangle, 4 for a quad when vertexCount is
	// 4) and RasterizeRenderCommand dequeues and rasterizes one. Stride 0x9C, capacity 1024
	// (g_renderCommandCount is the live count). The same struct is reused by the sorted
	// path: FlushSortedRenderCommands walks the sorted depth buckets and threads nodes
	// through the next pointer (+0x8C), calling RasterizeSortedRenderCommand per node. The
	// metadata at +0x80 is only partially understood; refine the names when
	// QueueRenderCommand and RasterizeRenderCommand are reconstructed.
	struct RenderCommand
	{
		Nu3D::VertexTL vertices[4]; // +0x00
		uint32_t* texData; // +0x80 (NULL selects an untextured rasterizer)
		int32_t vertexCount; // +0x84 (3 = triangle, 4 = quad)
		int32_t renderState; // +0x88 (Renderer::RENDER_* alpha flags)
		RenderCommand* next; // +0x8C (sorted-path list link)
		int32_t reserved90; // +0x90
		// +0x94: nonzero selects alternate span rasterizers. RasterizeRenderCommand ignores it;
		// RasterizeSortedRenderCommand branches on it, which is what gives it this role.
		int32_t useAlternateSpans;
		int32_t reserved98; // +0x98
	};
	STATIC_ASSERT(sizeof(RenderCommand) == 0x9c);
	STATIC_ASSERT(offsetof(RenderCommand, texData) == 0x80);
	STATIC_ASSERT(offsetof(RenderCommand, next) == 0x8c);
	STATIC_ASSERT(offsetof(RenderCommand, useAlternateSpans) == 0x94);
}

#endif
