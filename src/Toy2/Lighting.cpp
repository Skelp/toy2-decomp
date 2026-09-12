#include "Toy2/Actor.h"
#include "Toy2/BuzzLightPreset.h"
#include "Toy2/Lighting.h"
#include "Toy2/Animation.h"
#include "Toy2/Buzz.h"
#include "Toy2/Camera.h"
#include "Toy2/Collectables.h"
#include "Toy2/Collision.h"
#include "Toy2/Dialogue.h"
#include "Toy2/Gadget.h"
#include "Toy2/Levels.h"
#include "Toy2/Toy2.h"
#include "AudioManager/AudioManager.h"
#include "CharacterLoader.h"
#include "InputManager.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Random.h"
#include "Renderer/Renderer.h"
#include "SaveManager.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "Toy2/Toy2Internal.h"

// The lights of the actors and of Buzz: retail holds 0x0049EE50 through
// 0x0049F350 as one object, in the order of this file.
namespace Toy2
{
	namespace Lighting
	{
		// FUNCTION: TOY2 0x0049EE50 [PROVISIONAL]
		void SpawnLight(int32_t x, int32_t y, int32_t z, int32_t colour, int32_t lifetime, int32_t sourceId)
		{
			int32_t shortestLifetime = INT_MAX;
			int32_t lightIndex = x;
			int32_t candidateIndex = 2;
			DynamicLight* candidate = &g_lightingState.dynamicLights[candidateIndex];
			do
			{
				if (candidate->lifetime < shortestLifetime)
				{
					shortestLifetime = candidate->lifetime;
					lightIndex = candidateIndex;
				}
				candidate++;
				candidateIndex++;
			} while (candidate < g_lightingState.dynamicLights + 6);

			g_lightingState.dynamicLights[lightIndex].position.x = x;
			g_lightingState.dynamicLights[lightIndex].position.y = y;
			g_lightingState.dynamicLights[lightIndex].position.z = z;
			g_lightingState.dynamicLights[lightIndex].colour.b = (uint8_t)colour;
			g_lightingState.dynamicLights[lightIndex].colour.g = (uint8_t)(colour >> 8);
			g_lightingState.dynamicLights[lightIndex].colour.r = (uint8_t)(colour >> 16);
			g_lightingState.dynamicLights[lightIndex].sourceId = sourceId;
			g_lightingState.dynamicLights[lightIndex].lifetime = lifetime;
		}

		// FUNCTION: TOY2 0x0049EEE0 [PROVISIONAL]
		void UpdateBuzzLight()
		{
			int32_t buzzLightY = g_buzzActor.posAngles.pos.y - 0x2000;
			g_lightingState.dynamicLights[0].position.x = g_buzzActor.posAngles.pos.x + g_buzzLightPresets[g_levelFileIndex - 1].positionOffset.x * 0x10;
			g_lightingState.dynamicLights[0].position.z = g_buzzActor.posAngles.pos.z + g_buzzLightPresets[g_levelFileIndex - 1].positionOffset.z * 0x10;
			g_lightingState.dynamicLights[0].position.y = buzzLightY + g_buzzLightPresets[g_levelFileIndex - 1].positionOffset.y * 0x10;

			int32_t nearestLightIndex;
			int32_t nearestDistanceSquared;
			if (g_lightingState.dynamicLights[1].lifetime != 0)
			{
				int32_t offsetZ = (g_lightingState.dynamicLights[1].position.z - g_buzzActor.posAngles.pos.z) >> 8;
				int32_t offsetY = (g_lightingState.dynamicLights[1].position.y - buzzLightY) >> 8;
				int32_t offsetX = (g_lightingState.dynamicLights[1].position.x - g_buzzActor.posAngles.pos.x) >> 8;
				nearestDistanceSquared = offsetX * offsetX + offsetY * offsetY + offsetZ * offsetZ;
				nearestLightIndex = 1;
			}
			else
			{
				nearestDistanceSquared = INT_MAX;
				nearestLightIndex = 0;
			}

			for (int32_t lightIndex = 2; lightIndex < 6; lightIndex++)
			{
				DynamicLight& light = g_lightingState.dynamicLights[lightIndex];
				if (light.lifetime > 0)
				{
					light.lifetime -= Renderer::g_frameDelta;
					if (light.lifetime <= 0)
					{
						light.lifetime = 0;
					}
					else
					{
						int32_t offsetZ = (light.position.z - g_buzzActor.posAngles.pos.z) >> 8;
						int32_t offsetY = (light.position.y - buzzLightY) >> 8;
						int32_t offsetX = (light.position.x - g_buzzActor.posAngles.pos.x) >> 8;
						int32_t distanceSquared = offsetX * offsetX + offsetY * offsetY + offsetZ * offsetZ;
						if (distanceSquared > 0x8000)
						{
							light.lifetime = 0;
						}
						else if (distanceSquared < nearestDistanceSquared)
						{
							nearestDistanceSquared = distanceSquared;
							nearestLightIndex = lightIndex;
						}
					}
				}
			}

			int32_t blendTimer;
			if (g_lightingState.dynamicLights[g_selectedLightIndex].sourceId != g_lightingState.dynamicLights[nearestLightIndex].sourceId)
			{
				blendTimer = g_lightBlendTimer;
				g_lightingState.blendedPosition.x = g_lightingState.dynamicLights[g_selectedLightIndex].position.x
					+ (g_lightingState.blendedPosition.x - g_lightingState.dynamicLights[g_selectedLightIndex].position.x) * blendTimer / 0x40;
				g_lightingState.blendedPosition.y = g_lightingState.dynamicLights[g_selectedLightIndex].position.y
					+ (g_lightingState.blendedPosition.y - g_lightingState.dynamicLights[g_selectedLightIndex].position.y) * blendTimer / 0x40;
				g_lightingState.blendedPosition.z = g_lightingState.dynamicLights[g_selectedLightIndex].position.z
					+ (g_lightingState.blendedPosition.z - g_lightingState.dynamicLights[g_selectedLightIndex].position.z) * blendTimer / 0x40;
				g_lightingState.blendedColour.r = (uint8_t)(g_lightingState.dynamicLights[g_selectedLightIndex].colour.r
					+ (g_lightingState.blendedColour.r - g_lightingState.dynamicLights[g_selectedLightIndex].colour.r) * blendTimer / 0x40);
				g_lightingState.blendedColour.g = (uint8_t)(g_lightingState.dynamicLights[g_selectedLightIndex].colour.g
					+ (g_lightingState.blendedColour.g - g_lightingState.dynamicLights[g_selectedLightIndex].colour.g) * blendTimer / 0x40);
				g_lightingState.blendedColour.b = (uint8_t)(g_lightingState.dynamicLights[g_selectedLightIndex].colour.b
					+ (g_lightingState.blendedColour.b - g_lightingState.dynamicLights[g_selectedLightIndex].colour.b) * blendTimer / 0x40);

				g_selectedLightIndex = nearestLightIndex;
				if (nearestLightIndex <= 1)
				{
					blendTimer = 0x40;
				}
				else
				{
					g_lightBlendTimer = 0;
					blendTimer = 0;
				}
			}
			else
			{
				blendTimer = g_lightBlendTimer;
			}

			int32_t lightX;
			int32_t lightY;
			int32_t lightZ;
			int32_t colourR;
			int32_t colourG;
			int32_t colourB;
			if (blendTimer == 0)
			{
				colourR = g_lightingState.dynamicLights[g_selectedLightIndex].colour.r;
				lightX = g_lightingState.dynamicLights[g_selectedLightIndex].position.x;
				lightY = g_lightingState.dynamicLights[g_selectedLightIndex].position.y;
				lightZ = g_lightingState.dynamicLights[g_selectedLightIndex].position.z;
				colourG = g_lightingState.dynamicLights[g_selectedLightIndex].colour.g;
				colourB = g_lightingState.dynamicLights[g_selectedLightIndex].colour.b;
			}
			else
			{
				lightX = g_lightingState.dynamicLights[g_selectedLightIndex].position.x
					+ (g_lightingState.blendedPosition.x - g_lightingState.dynamicLights[g_selectedLightIndex].position.x) * blendTimer / 0x40;
				lightY = g_lightingState.dynamicLights[g_selectedLightIndex].position.y
					+ (g_lightingState.blendedPosition.y - g_lightingState.dynamicLights[g_selectedLightIndex].position.y) * blendTimer / 0x40;
				lightZ = g_lightingState.dynamicLights[g_selectedLightIndex].position.z
					+ (g_lightingState.blendedPosition.z - g_lightingState.dynamicLights[g_selectedLightIndex].position.z) * blendTimer / 0x40;
				colourR = g_lightingState.dynamicLights[g_selectedLightIndex].colour.r
					+ (g_lightingState.blendedColour.r - g_lightingState.dynamicLights[g_selectedLightIndex].colour.r) * blendTimer / 0x40;
				colourG = g_lightingState.dynamicLights[g_selectedLightIndex].colour.g
					+ (g_lightingState.blendedColour.g - g_lightingState.dynamicLights[g_selectedLightIndex].colour.g) * blendTimer / 0x40;
				colourB = g_lightingState.dynamicLights[g_selectedLightIndex].colour.b
					+ (g_lightingState.blendedColour.b - g_lightingState.dynamicLights[g_selectedLightIndex].colour.b) * blendTimer / 0x40;

				blendTimer -= Renderer::g_frameDelta;
				g_lightBlendTimer = blendTimer;
				if (blendTimer <= 0)
				{
					g_lightBlendTimer = 0;
				}
			}

			int32_t directionY = (lightY - buzzLightY) >> 8;
			int32_t directionZ = (lightZ - g_buzzActor.posAngles.pos.z) >> 8;
			int32_t directionX = (lightX - g_buzzActor.posAngles.pos.x) >> 8;
			int32_t distanceSquared = directionX * directionX + directionY * directionY + directionZ * directionZ;
			if (distanceSquared > 0x10000)
			{
				distanceSquared = 0x10000;
			}

			g_buzzActor.lightDirection.x = (int16_t)directionX;
			g_buzzActor.lightDirection.y = (int16_t)directionY;
			g_buzzActor.lightDirection.z = (int16_t)directionZ;

			int32_t intensity = 0x10000 - distanceSquared;
			g_buzzActor.color.value = (((intensity * colourB >> 16) * 0x100 + (intensity * colourG >> 16)) * 0x100) + (intensity * colourR >> 16);
		}

		// FUNCTION: TOY2 0x0049F350 [PROVISIONAL]
		void InitBuzzLight()
		{
			memset(&g_lightingState, 0, sizeof(g_lightingState));

			g_lightBlendTimer = 0;
			int32_t levelFileIndex = g_levelFileIndex;
			g_selectedLightIndex = 0;
			g_lightingState.dynamicLights[0].sourceId = 0;
			g_buzzActor.lightDistance = 0x960;

			g_lightingState.dynamicLights[0].colour.r = (uint8_t)(g_buzzLightPresets[levelFileIndex - 1].colour >> 16);
			g_lightingState.dynamicLights[0].colour.g = (uint8_t)(g_buzzLightPresets[levelFileIndex - 1].colour >> 8);
			g_lightingState.dynamicLights[0].colour.b = (uint8_t)g_buzzLightPresets[levelFileIndex - 1].colour;

			UpdateBuzzLight();
		}
	}
}
