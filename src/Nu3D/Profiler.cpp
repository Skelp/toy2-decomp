#include "Nu3D/Nu3D.h"

namespace Nu3D
{
	// FUNCTION: TOY2 0x004B9D40 [MATCHED]
	void SetTimingGraphVertex(VertexTL* vertex, float x, float y, uint32_t color)
	{
		vertex->position.x = x;
		vertex->position.y = y;
		vertex->position.z = 0.0f;
		vertex->uv.y = 0.0f;
		vertex->uv.x = 0.0f;
		vertex->rhw = 0.5f;
		vertex->specular.value = color;
		vertex->diffuse.value = color;
	}
}
