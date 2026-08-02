#pragma once

#include <directx6/d3d.h>

namespace Nu3D
{
	struct Quaternion
	{
		float x;
		float y;
		float z;
		float w;
	};

	namespace Math
	{
		void MatrixToQuaternion(D3DMATRIX* matrix, Quaternion* quaternion);
	}
}
