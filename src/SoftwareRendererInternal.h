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

	// One scanline of a polygon that the rasterizer bands fill. The band that
	// walks the edges writes the left and right ends here, and the row loop reads
	// them back. ClearScanlineFlags empties the table between polygons.
	struct ScanlineScratch
	{
		union
		{
			int32_t populated;
			uint8_t populatedByte;
		};
		int32_t leftXFixed;
		int32_t leftInterpolants[5];
		int32_t rightXFixed;
		int32_t rightInterpolants[11];
	};
	STATIC_ASSERT(sizeof(ScanlineScratch) == 0x4c);
	STATIC_ASSERT(offsetof(ScanlineScratch, leftXFixed) == 0x4);
	STATIC_ASSERT(offsetof(ScanlineScratch, rightXFixed) == 0x1c);

	// One rasterizer set. The pixel format picks the table, and the render state
	// of an item picks the entry. RenderSoftwareFrame walks the buckets and calls
	// through the selected table, so both the band that fills the tables and the
	// band that reads them need the record.
	typedef void (*SoftwareRenderCallback)(SoftwareRenderItem* item);
	typedef void (*SoftwareSolidQuadCallback)(const PointI* point0, const PointI* point1, const PointI* point2, const PointI* point3, uint32_t colourPair);

	struct SoftwareRenderDispatchTable
	{
		SoftwareRenderCallback highPriority;
		SoftwareSolidQuadCallback solidQuad;
		SoftwareRenderCallback additive;
		SoftwareRenderCallback subtractive;
		SoftwareRenderCallback colourOffset[4];
		SoftwareRenderCallback defaultCallback[4];
		SoftwareRenderCallback flag80;
	};
	STATIC_ASSERT(sizeof(SoftwareRenderDispatchTable) == 0x34);

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

	// The rasterizer entry points of every pixel format. Retail holds one object for
	// each format, and the three dispatch tables of SoftwareRenderer.cpp name all of
	// them, so the file that holds the tables needs every format's names.
	void RasterizeTexturedRect555(SoftwareRenderItem* item);
	void RasterizeTexturedPolygon555(SoftwareRenderItem* item);
	void RasterizeAdditiveTexturedPolygon555(SoftwareRenderItem* item);
	void RasterizeSubtractiveTexturedPolygon555(SoftwareRenderItem* item);
	void RasterizeBlend25TexturedPolygon555(SoftwareRenderItem* item);
	void RasterizeBlend50TexturedPolygon555(SoftwareRenderItem* item);
	void RasterizeBlend75TexturedPolygon555(SoftwareRenderItem* item);
	void UnkRenderAPI5(SoftwareRenderItem* item);
	void UnkRenderAPI6(SoftwareRenderItem* item);
	void UnkRenderAPI7(SoftwareRenderItem* item);
	void UnkRenderAPI8(SoftwareRenderItem* item);
	void UnkRenderAPI13(SoftwareRenderItem* item);

	void RasterizeTexturedRect565(SoftwareRenderItem* item);
	void RasterizeTexturedPolygon565(SoftwareRenderItem* item);
	void RasterizeAdditiveTexturedPolygon565(SoftwareRenderItem* item);
	void RasterizeSubtractiveTexturedPolygon565(SoftwareRenderItem* item);
	void RasterizeBlend25TexturedPolygon565(SoftwareRenderItem* item);
	void RasterizeBlend50TexturedPolygon565(SoftwareRenderItem* item);
	void RasterizeSolidQuad16(const PointI* point0, const PointI* point1, const PointI* point2, const PointI* point3, uint32_t colourPair);
	void UnkRenderAPI17(SoftwareRenderItem* item);
	void UnkRenderAPI18(SoftwareRenderItem* item);
	void UnkRenderAPI19(SoftwareRenderItem* item);
	void UnkRenderAPI20(SoftwareRenderItem* item);
	void UnkRenderAPI24(SoftwareRenderItem* item);
	void UnkRenderAPI25(SoftwareRenderItem* item);

	void RasterizeTexturedRect8(SoftwareRenderItem* item);
	void RasterizeTexturedPolygon8(SoftwareRenderItem* item);
	void RasterizeAdditiveTexturedPolygon8(SoftwareRenderItem* item);
	void RasterizeSubtractiveTexturedPolygon8(SoftwareRenderItem* item);
	void RasterizeSolidQuad8(const PointI* point0, const PointI* point1, const PointI* point2, const PointI* point3, uint32_t colourPair);
	void UnkRenderAPI30(SoftwareRenderItem* item);
	void UnkRenderAPI31(SoftwareRenderItem* item);
	void UnkRenderAPI33(SoftwareRenderItem* item);
	void UnkRenderAPI34(SoftwareRenderItem* item);
}

#endif
