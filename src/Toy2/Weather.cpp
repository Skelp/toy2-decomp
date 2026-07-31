#include "Toy2/Weather.h"

#include "Nu3D/Camera.h"
#include "Random.h"
#include "Renderer/Renderer.h"
#include "Toy2/Buzz.h"
#include "Toy2/Levels.h"

namespace Toy2
{
	namespace Sector
	{
		// GLOBAL: TOY2 0x00547EE0
		Vector3I16 g_viewRotation;

		// GLOBAL: TOY2 0x0054DEA0
		int32_t g_activeSectorIndex;

		// GLOBAL: TOY2 0x005D2A8C
		int32_t g_currentSectorIndex;
	}

	namespace Weather
	{
		// GLOBAL: TOY2 0x00557708
		int32_t g_spawnAccumulator;

		// GLOBAL: TOY2 0x00557AA4
		int32_t g_precipitationSpriteSheetIndex;

		// GLOBAL: TOY2 0x005550A0
		int32_t g_inactiveParticleIndices[8];

		// GLOBAL: TOY2 0x0054F08C
		PrecipitationParticle* g_precipitationParticles;

		// FUNCTION: TOY2 0x0044ED60 [MATCHED]
		void Init()
		{
			g_precipitationParticles = (PrecipitationParticle*)Levels::g_levelLoadArena;
			Levels::g_levelLoadArena += sizeof(PrecipitationParticle) * 64;

			for (int32_t i = 0; i < 64; i++)
			{
				g_precipitationParticles[i].terminalY = 0;
			}
		}

		// FUNCTION: TOY2 0x0044ED90 [PROVISIONAL]
		void StepPrecipitation(int32_t spawnRate, int32_t fallSpeed, int32_t spriteSheetIndex)
		{
			if (g_precipitationParticles == 0)
				return;

			if (g_buzzActor.velocity.vertical > 0x400)
				fallSpeed += g_buzzActor.velocity.vertical * 2;

			int32_t inactiveCount = 0;
			int32_t* inactiveIndex = g_inactiveParticleIndices;
			for (int32_t i = 0; i < 64; i++)
			{
				if (g_precipitationParticles[i].terminalY != 0)
				{
					g_precipitationParticles[i].position.y += Renderer::g_frameDelta * fallSpeed;
					if (g_precipitationParticles[i].position.y > g_precipitationParticles[i].terminalY)
						g_precipitationParticles[i].terminalY = 0;
				}
				else
				{
					if (inactiveIndex < g_inactiveParticleIndices + 8)
					{
						*inactiveIndex = i;
						inactiveCount++;
						inactiveIndex++;
					}
				}
			}

			g_precipitationSpriteSheetIndex = spriteSheetIndex;
			g_spawnAccumulator += Renderer::g_frameDelta * spawnRate;
			fallSpeed = inactiveCount;

			while (g_spawnAccumulator > 0xFF)
			{
				if (fallSpeed <= 0)
					return;

				fallSpeed--;
				inactiveIndex--;
				int32_t angleOffset = *g_randDatBufferPtr++ * 8 - 0x400;
				int32_t radius = Numerics::g_sinCosLUT[(Sector::g_viewRotation.x * 3 + 0x400) & 0xFFF] / 0x200;
				radius += *g_randDatBufferPtr++;

				g_precipitationParticles[*inactiveIndex].position.x = (*g_randDatBufferPtr++ - 0x80) * 0x20
					- (Numerics::g_sinCosLUT[(Sector::g_viewRotation.y + angleOffset) & 0xFFF] * radius >> 5) + Nu3D::Camera::g_fixedViewPosition.x;
				g_precipitationParticles[*inactiveIndex].position.y = Nu3D::Camera::g_fixedViewPosition.y - 0x14000;
				g_precipitationParticles[*inactiveIndex].position.z = (*g_randDatBufferPtr++ - 0x80) * 0x20 + Nu3D::Camera::g_fixedViewPosition.z
					+ (Numerics::g_sinCosLUT[(Sector::g_viewRotation.y + angleOffset + 0x400) & 0xFFF] * radius >> 5);

				if (Sector::g_activeSectorIndex != 3)
					g_precipitationParticles[*inactiveIndex].terminalY = 0;
				else
					g_precipitationParticles[*inactiveIndex].terminalY = 0xF80C;

				if (g_precipitationParticles[*inactiveIndex].terminalY > g_precipitationParticles[*inactiveIndex].position.y + 0x24000)
					g_precipitationParticles[*inactiveIndex].terminalY = g_precipitationParticles[*inactiveIndex].position.y + 0x24000;

				if (g_precipitationParticles[*inactiveIndex].terminalY == 0)
					g_precipitationParticles[*inactiveIndex].terminalY--;
				else if (g_precipitationParticles[*inactiveIndex].terminalY == (int32_t)0x80000000)
					g_precipitationParticles[*inactiveIndex].terminalY = 0;

				g_spawnAccumulator -= 0x100;
			}
		}
	}
}
