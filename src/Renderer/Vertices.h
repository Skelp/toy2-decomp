#pragma once

#include "Common.h"
#include "Numerics.h"
#include "Nu3D/Patch.h"
#include "Nu3D/Sprite.h"

namespace Renderer
{
	namespace Vertices
	{
		void ApplyOffset(Nu3D::Patch::PatchVertices* vertices, float du, float dv);
		void ProjectToScreen(Nu3D::Patch::PatchVertices* vertices, D3DMATRIX* transforms, int32_t transformCount);
		void ProjectCustomTextureCoordinates(
			Nu3D::Patch::PatchVertices* vertices, D3DMATRIX* transform, const Nu3D::InstanceData::TextureProjectionData* projection);
		void ProjectTex14Coordinates(Nu3D::Patch::PatchVertices* vertices, D3DMATRIX* transform);
		void ModuleColor(Nu3D::Patch::PatchVertices* vertices, int32_t red, int32_t green, int32_t blue);
	}
}
