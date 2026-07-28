#include "Toy2/Collectables.h"
#include "Toy2/Camera.h"
#include "Nu3D/Link.h"

namespace Toy2
{
	namespace Collectables
	{
		// GLOBAL: TOY2 0x00830CCC
		TokenState g_tokenStates[5];

		// STUB: TOY2 0x00447DB0
		void BuildPickupTable() {}

		// FUNCTION: TOY2 0x004A0DB0
		void Activate(int32_t tokenIndex, int32_t skipCutscene)
		{
			if (g_tokenStates[tokenIndex].active != 0)
				return;

			g_tokenStates[tokenIndex].active = 1;
			int32_t* verticalPosition = g_tokenStates[tokenIndex].verticalPosition;
			Vector3I position;
			Nu3D::Link::GetCurrentPosFixed(g_tokenStates[tokenIndex].linkId, &position);
			*verticalPosition = position.y >> 5;

			if (skipCutscene == 0)
			{
				g_tokenStates[tokenIndex].timer = 0x84;
				Camera::BeginScriptedCutsceneAtPoint(&position, 0xB4, 0x10);
				if (Camera::g_scriptedCameraState != 0)
					Camera::g_scriptedCameraState = 5;
			}
			else
			{
				Nu3D::Link::SetScaleFromFixedOffsets(g_tokenStates[tokenIndex].linkId, 0x1000, 0x1000, 0x1000);
			}
		}

		// FUNCTION: TOY2 0x004CD110 [MATCHED]
		int32_t ShowTokenSparkle(int32_t linkId) { return 1; }
	}
}
