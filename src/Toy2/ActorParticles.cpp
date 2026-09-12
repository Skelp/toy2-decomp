#include "Toy2/Actor.h"
#include "Toy2/Buzz.h"
#include "Toy2/Camera.h"
#include "Toy2/Lighting.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Random.h"

// The particle bursts an actor emits. SpawnCollectSparkle marks a pickup the
// player took, HitType1Particles marks a hit, and the two burst ring functions
// throw a ring of particles from a point or from an actor's origin.
//
// Retail run 0x00410410-0x004106C0, between the Toy2/Buzz.cpp and
// Nu3D/Particles.cpp runs.

namespace Toy2
{
	namespace Particles
	{
		// FUNCTION: TOY2 0x00410410 [PROVISIONAL]
		void SpawnCollectSparkle(int32_t x, int32_t y, int32_t z, int32_t particleSpread)
		{
			particleSpread *= 2;
			for (int32_t particleIndex = 0; particleIndex < 5; particleIndex++)
			{
				int32_t positionOffset = particleSpread * ((*g_randDatBufferPtr & 0x3F) - 0x20);
				g_randDatBufferPtr += 3;
				Nu3D::Particles::ParticleInstance* particle =
					Nu3D::Particles::SpawnFromPreset(x + positionOffset, y + positionOffset, z + positionOffset, 0x29, 0xF);
				particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
				particle->lifetime = (*g_randDatBufferPtr++ & 0xF) * 2 + 0x18;
			}

			Lighting::SpawnLight(x, y, z, 0x604000, 0x10, x);
		}

	}
	namespace Actor
	{
		// FUNCTION: TOY2 0x004104D0 [MATCHED]
		void HitType1Particles(int32_t x, int32_t y, int32_t z)
		{
			for (int32_t count = 3; count != 0; count--)
			{
				Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(x, y, z, 0x5E, 9);
				particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
				particle->lifetime = (*g_randDatBufferPtr++ & 0xF) * 2 + 0x18;
			}
		}

	}
	namespace Particles
	{
		// FUNCTION: TOY2 0x00410540 [PROVISIONAL]
		void SpawnBurstRingAtPoint(Actor::Toy2Actor* actor, uint8_t red, uint8_t green, uint8_t blue, int32_t verticalOffset)
		{
			int32_t cameraSine = Numerics::g_sinCosLUT[(int16_t)Camera::g_renderCameraTransform.rotation.euler.angles.yaw & 0xFFF];
			int32_t cameraCosine = Numerics::g_sinCosLUT[((int16_t)Camera::g_renderCameraTransform.rotation.euler.angles.yaw + 0x400) & 0xFFF];

			for (int32_t ringAngle = 0; ringAngle < 0x1000; ringAngle += 0x200)
			{
				int32_t radiusScale = Numerics::g_sinCosLUT[ringAngle] / 4;
				int32_t offsetX = radiusScale * cameraCosine >> 14;
				int32_t offsetZ = -(radiusScale * cameraSine) >> 14;
				int32_t offsetY = Numerics::g_sinCosLUT[(ringAngle + 0x400) & 0xFFF] / 4;

				Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnInstance(actor->pos.x + offsetX,
					actor->pos.y - verticalOffset + offsetY,
					actor->pos.z + offsetZ,
					offsetX >> 2,
					(offsetY >> 2) - 0x400,
					offsetZ >> 2,
					0x40,
					ringAngle,
					*g_randDatBufferPtr++ - 0x80,
					0x7D);
				particle->lifetime = (*g_randDatBufferPtr++ & 0x3F) + 0x50;
				particle->colourR = red;
				particle->colourG = green;
				particle->colourB = blue;
			}

			Nu3D::Particles::SpawnInstance(actor->pos.x, actor->pos.y - verticalOffset, actor->pos.z, 0, -0x400, 0, 0x40, 0, 0, 0x7E);
			Nu3D::Particles::SpawnInstance(actor->pos.x, actor->pos.y - verticalOffset, actor->pos.z, 0, -0x400, 0, 0x40, 0, 0, 0x7F);
		}

		// FUNCTION: TOY2 0x004106C0 [PROVISIONAL]
		void SpawnBurstRingAtActor(Actor::Toy2Actor* actor, uint8_t red, uint8_t green, uint8_t blue, int32_t verticalOffset)
		{
			int32_t cameraSine = Numerics::g_sinCosLUT[(int16_t)Camera::g_renderCameraTransform.rotation.euler.angles.yaw & 0xFFF];
			int32_t cameraCosine = Numerics::g_sinCosLUT[((int16_t)Camera::g_renderCameraTransform.rotation.euler.angles.yaw + 0x400) & 0xFFF];

			int16_t* radiusSample = Numerics::g_sinCosLUT;
			int32_t ringAngle = 0;
			do
			{
				int32_t radiusScale = *radiusSample / 4;
				int32_t offsetX = radiusScale * cameraCosine >> 14;
				int32_t offsetZ = -(radiusScale * cameraSine) >> 14;
				int32_t offsetY = Numerics::g_sinCosLUT[(ringAngle + 0x400) & 0xFFF] / 4;

				Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnInstance(actor->pos.x + offsetX,
					actor->pos.y + offsetY - verticalOffset,
					actor->pos.z + offsetZ,
					offsetX >> 3,
					(offsetY >> 3) - 0x400,
					offsetZ >> 3,
					0x40,
					ringAngle,
					*g_randDatBufferPtr++ - 0x80,
					0x7D);
				particle->lifetime = (*g_randDatBufferPtr++ & 0x1F) + 0x32;
				particle->colourR = red;
				particle->colourG = green;
				particle->colourB = blue;

				radiusSample += 0x333;
				ringAngle += 0x333;
			} while (radiusSample < &Numerics::g_sinCosLUT[0xFFF]);

			Nu3D::Particles::SpawnInstance(actor->pos.x, actor->pos.y - verticalOffset, actor->pos.z, 0, -0x400, 0, 0x40, 0, 0, 0x7E);
			Nu3D::Particles::SpawnInstance(actor->pos.x, actor->pos.y - verticalOffset, actor->pos.z, 0, -0x400, 0, 0x40, 0, 0, 0x7F);
		}

	}
}
