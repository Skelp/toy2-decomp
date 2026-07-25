#pragma once

#include "Common.h"
#include "Numerics.h"
#include "Nu3D/Patch.h"

namespace Renderer
{
	namespace Vertices
	{
		void ApplyOffset(Nu3D::Patch::PatchVertices* vertices, float du, float dv);
	}
}
