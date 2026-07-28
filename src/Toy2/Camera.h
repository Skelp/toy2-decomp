#pragma once

#include "Numerics.h"
#include "Toy2/Buzz.h"

namespace Toy2
{
	namespace Camera
	{
		// Gameplay camera controller state. Fixed-point positions and 12-bit
		// angles drive the view transform that UpdateActiveTransform blends
		// into the render camera. Fields beyond the angles are confirmed by
		// InitGameplayCamera zeroing the full 0x34-byte struct; their roles
		// are reconstructed as the cluster is recovered.
		struct GameplayCamera
		{
			Vector3I pos; // 0x00 — current camera position (fixed-point)
			Vector3I lookAt; // 0x0C — look-at position (fixed-point)
			Vector3I target; // 0x18 — position the camera moves toward (fixed-point)
			Angles angles; // 0x24 — pitch and yaw (12-bit fixed-point angles)
			uint16_t roll; // 0x28 — roll angle (12-bit fixed-point)
			uint16_t data[5]; // 0x2A — remaining camera state
		};

		STATIC_ASSERT(sizeof(GameplayCamera) == 0x34);

		extern GameplayCamera g_gameplayCamera;

		void SmoothToTarget(GameplayCamera* camera);
		int32_t UpdateRocketBoots(Buzz::Toy2BuzzActor* buzz, Buzz::MovementRates* movementRates);
	}
} // namespace Toy2

namespace Camera
{
	int32_t CalculateMaxTurnAngle(uint16_t directionInputState);
}
