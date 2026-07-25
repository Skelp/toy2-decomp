#pragma once

#include "Common.h"
#include "Numerics.h"
#include "Nu3D/Patch.h"

namespace Renderer
{
	namespace Vertices
	{
		void ApplyOffset(Nu3D::Patch::PatchVertices* vertices, float du, float dv);
		void ModuleColor(Nu3D::Patch::PatchVertices* vertices, int32_t red, int32_t green, int32_t blue);
	}
}
