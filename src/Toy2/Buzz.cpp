#include "Toy2/Buzz.h"
#include "Toy2/Toy2.h"
#include "Nu3D/Link.h"
#include "Nu3D/Particles.h"

namespace Toy2
{
	// GLOBAL: TOY2 0x0053C5E0
	int32_t g_rocketBootsTimer;

	// GLOBAL: TOY2 0x00882924
	Buzz::GadgetPickup* g_activeRocketBootsPickup;

	// GLOBAL: TOY2 0x00882954
	int32_t g_savedRocketBootsPickupState;

	// STUB: TOY2 0x00433ED0
	void ResetBuzzState() {}

	// FUNCTION: TOY2 0x004A4B70 [MATCHED]
	void ActivateGravityBoots()
	{
		ResetBuzzState();
		g_gravityBootsTimer = 600;
		g_gravityBootsHoverHeight = 0x2000;
	}

	namespace Buzz
	{
		// STUB: TOY2 0x00414110
		void Respawn() {}

		// FUNCTION: TOY2 0x004A4B90 [MATCHED]
		void ResetGravityBoots()
		{
			if (g_gravityBootsTimer != 0)
			{
				g_gravityBootsTimer = 0;
			}
		}

		// FUNCTION: TOY2 0x004A4E60 [MATCHED]
		void DeactivateRocketBoots()
		{
			if (g_rocketBootsTimer != 0)
			{
				g_rocketBootsTimer = 0;
				for (int32_t particleIndex = 0; particleIndex < 64; particleIndex++)
				{
					Nu3D::Particles::ParticleInstance& particle = Nu3D::Particles::g_particleInstances[particleIndex];
					if (particle.typeId == 0x2F)
					{
						particle.lifetime = 1;
					}
				}

				Nu3D::Link::SetScaleFromFixedOffsets(g_activeRocketBootsPickup->linkId, 0x1000, 0x1000, 0x1000);
				g_activeRocketBootsPickup->state = g_savedRocketBootsPickupState;
			}
		}

		// FUNCTION: TOY2 0x004A5340 [MATCHED]
		void CancelGrapple()
		{
			if (g_grappleState != 0)
			{
				if (g_grappleCharges > 0)
				{
					g_grappleCharges--;
				}
				g_grappleState = 0;
			}
		}
	}
}
