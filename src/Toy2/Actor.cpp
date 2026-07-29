#include "Toy2/Actor.h"
#include "Toy2/Animation.h"
#include "Toy2/Buzz.h"
#include "Toy2/Camera.h"
#include "Toy2/Collectables.h"
#include "Toy2/Collision.h"
#include "Toy2/Levels.h"
#include "Toy2/Toy2.h"
#include "AudioManager/AudioManager.h"
#include "CharacterLoader.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Random.h"
#include "Renderer/Renderer.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

namespace Toy2
{
	extern int32_t g_hudActorAnimationFrame;

	namespace Dialogue
	{
		struct CutsceneScript
		{
			int32_t selectRecordCommand;
			int32_t recordType;
			int32_t moveBuzzCommand;
			int32_t buzzActorIndex;
			int32_t buzzPathPoint;
			int32_t moveActorCommand;
			int32_t actorIndex;
			int32_t actorPathPoint;
			int32_t faceBuzzCommand;
			int32_t facingBuzzActorIndex;
			int32_t buzzFacingAngle;
			int32_t faceActorCommand;
			int32_t facingActorIndex;
			int32_t actorFacingAngle;
		};

		struct DialogueRecords
		{
			uint16_t recordCount;
			uint16_t recordType;
			Vector3I buzzPosition;
			Vector3I actorPosition;
		};

		union SubtitleCells
		{
			uint16_t characters[72];
			uint32_t pairs[36];
		};

		STATIC_ASSERT(sizeof(DialogueRecords) == 0x1C);
		STATIC_ASSERT(offsetof(DialogueRecords, buzzPosition) == 0x4);
		STATIC_ASSERT(offsetof(DialogueRecords, actorPosition) == 0x10);

		// GLOBAL: TOY2 0x004DF6CC
		CutsceneScript g_dialogueCutsceneScript = { 0, 0, 1, -1, 0, 1, 0, 1, 2, -1, 0, 2, 0, 0 };

		// GLOBAL: TOY2 0x0050A1FC
		SubtitleCells g_subtitleCells;

		// GLOBAL: TOY2 0x0050A298
		char g_wrappedSubtitleText[540];

		// GLOBAL: TOY2 0x0050A530
		char* g_subtitleTextCursor;

		// GLOBAL: TOY2 0x0050A518
		int32_t g_subtitleActive;

		// GLOBAL: TOY2 0x0050A4C4
		int32_t g_subtitleColumn;

		// GLOBAL: TOY2 0x0050A114
		int32_t g_subtitleRowStart;

		// GLOBAL: TOY2 0x0050A4D8
		int32_t g_subtitleVisibleStart;

		// GLOBAL: TOY2 0x0050A110
		int32_t g_subtitleColour;

		// GLOBAL: TOY2 0x0050A4CC
		int32_t g_subtitleCharacterDelay;

		// GLOBAL: TOY2 0x0050A4D4
		int32_t g_subtitleCharacterStyle;

		// GLOBAL: TOY2 0x0050A4EC
		int32_t g_subtitleBoxScale;

		// GLOBAL: TOY2 0x0050A4C0
		int32_t g_subtitlePageState;

		// FUNCTION: TOY2 0x00401A00 [PROVISIONAL]
		void WrapSubtitleText(char* subtitle)
		{
			char* destination = g_wrappedSubtitleText;
			memset(g_wrappedSubtitleText, ' ', sizeof(g_wrappedSubtitleText));

			for (;;)
			{
				int32_t visibleCharacters = 0;
				char* scan = subtitle;
				char* nextLine;
				int32_t padding;
				for (;;)
				{
					char character = *scan;
					if (character == '\0')
					{
						nextLine = scan;
						padding = 0;
						break;
					}
					scan++;
					if (character != '^')
						visibleCharacters++;
					if (visibleCharacters < 36)
						continue;

					if (*scan == ' ')
					{
						nextLine = scan + 1;
						padding = 0;
						break;
					}
					if (*scan == '\0')
					{
						nextLine = scan;
						padding = 0;
						break;
					}

					char* wordStart = scan - 1;
					while (*wordStart != ' ')
					{
						if (*wordStart != '^')
							visibleCharacters--;
						wordStart--;
					}
					nextLine = wordStart + 1;
					while (*wordStart == ' ')
					{
						wordStart--;
						visibleCharacters--;
					}
					padding = 35 - visibleCharacters;
					break;
				}

				int32_t copyWidth = 36 - padding;
				if (copyWidth > 0)
				{
					int32_t copiedVisibleCharacters = 0;
					do
					{
						uint32_t character = (uint8_t)*subtitle++;
						if (character == '^')
							copiedVisibleCharacters--;
						*destination++ = (char)character;
						if (character == '\0')
							return;
						copiedVisibleCharacters++;
					} while (copiedVisibleCharacters < copyWidth);
				}

				subtitle = nextLine;
				if (padding > 0)
				{
					memset(destination, ' ', padding);
					destination += padding;
				}
			}
		}

		// FUNCTION: TOY2 0x004027F0 [TOOL]
		void Begin(int32_t actorIndex, int32_t recordType, char* subtitle, int32_t buzzFacingAngle, int32_t actorFacingAngle, int32_t rewardTokenIndex)
		{
			if (buzzFacingAngle == -1)
			{
				DialogueRecords* dialogueRecords = reinterpret_cast<DialogueRecords*>(Levels::g_recordData[recordType]);
				int32_t deltaX = (dialogueRecords->buzzPosition.x << 5) - (dialogueRecords->actorPosition.x << 5);
				int32_t deltaZ = (dialogueRecords->buzzPosition.z << 5) - (dialogueRecords->actorPosition.z << 5);
				actorFacingAngle = Nu3D::Math::CartesianToFixedAngle(deltaX, deltaZ) & 0xFFF;
				buzzFacingAngle = (actorFacingAngle - 0x800) & 0xFFF;
			}

			g_dialogueCutsceneScript.recordType = recordType;
			g_dialogueCutsceneScript.actorIndex = actorIndex;
			g_dialogueCutsceneScript.facingActorIndex = actorIndex;
			g_dialogueCutsceneScript.buzzFacingAngle = buzzFacingAngle;
			g_dialogueCutsceneScript.actorFacingAngle = actorFacingAngle;

			if (Nu3D::Camera::g_viewHistoryInitialized != 0)
				return;

			if (Camera::g_scriptedCameraState != 0)
			{
				if (Camera::g_cameraMarkerParticle != (Nu3D::Particles::ParticleInstance*)-1)
				{
					Camera::g_cameraMarkerParticle->lifetime = 1;
					Camera::g_cameraMarkerParticle = (Nu3D::Particles::ParticleInstance*)-1;
				}
				if (Camera::g_targetMarkerParticle != (Nu3D::Particles::ParticleInstance*)-1)
				{
					Camera::g_targetMarkerParticle->lifetime = 1;
					Camera::g_targetMarkerParticle = (Nu3D::Particles::ParticleInstance*)-1;
				}

				g_buzzActor.actorFlags |= 1;
				int32_t cameraY = g_buzzActor.posAngles.pos.y - 0x3000;
				Camera::g_gameplayCamera.roll = g_buzzActor.posAngles.angles.yaw;
				g_buzzActor.facingAngle = g_buzzActor.posAngles.angles.yaw;
				Camera::g_gameplayCamera.angles.yaw = 0x4B0;
				Camera::g_gameplayCamera.pos.y = cameraY;
				Camera::g_gameplayCamera.target.visorAimAngles.pitch = 0;
				Camera::g_gameplayCamera.lookAt.x = Camera::g_gameplayCamera.pos.x;
				Camera::g_gameplayCamera.modeTransitionState = 0;
				Camera::g_scriptedCameraState = 0;

				Nu3D::Link::SetScaleFromFixedOffsets(0x2D, 0, 0, 0);
				Nu3D::Link::SetScaleFromFixedOffsets(0x2E, 0, 0, 0);
				Nu3D::Link::SetScaleFromFixedOffsets(0x2F, 0, 0, 0);
			}

			if (rewardTokenIndex >= 0)
				Nu3D::Camera::g_viewHistoryInitialized = rewardTokenIndex + 10;
			else
				Nu3D::Camera::g_viewHistoryInitialized = 1;
			g_gameplayStateFlags |= 1;
			Camera::g_cutsceneElapsedTime = 0;
			Camera::g_cutsceneCommandCursor = reinterpret_cast<const int32_t*>(&g_dialogueCutsceneScript);
			Camera::g_cutsceneWaitTimer = 0;
			Camera::g_cutsceneSegmentProgress = 0;
			Camera::g_cutsceneMoveSpeed = 0;
			Camera::g_nextCutsceneMoveSpeed = 0;
			Camera::g_cutsceneSegmentDuration = 0;
			Camera::g_cutsceneFocusPathPoint = 0;
			Camera::g_cutsceneCameraPathPoint = 0;
			Camera::g_cutsceneRecordType = 0;

			if (subtitle != 0)
			{
				WrapSubtitleText(subtitle);
				g_subtitleActive = 1;
				for (int32_t i = 0; i < 36; i++)
					g_subtitleCells.pairs[i] = 0x00200020;
				g_subtitleTextCursor = g_wrappedSubtitleText;
				g_subtitleColumn = 0;
				g_subtitleRowStart = 0;
				g_subtitleVisibleStart = -72;
				g_subtitleColour = 0x00808080;
				g_subtitleCharacterDelay = 2;
				g_subtitleCharacterStyle = 0;
				g_subtitleBoxScale = 0;
				g_subtitlePageState = 0;
				AudioManager::PlaySoundEffect(0x1D, 0);
			}
			else
			{
				g_subtitleActive = 0;
			}
		}
	}

	namespace Game
	{
		void InitActor(Actor::Toy2Actor* actor, int32_t param);
	}

	namespace Lighting
	{
		struct DynamicLight
		{
			Vector3I position;
			int32_t lifetime;
			int32_t sourceId;
			RGBColor3B colour;
		};

		struct LightingState
		{
			DynamicLight dynamicLights[6];
			Vector3I blendedPosition;
			int32_t reservedBlendState[2];
			RGBColor3B blendedColour;
			uint8_t reserved[0x18];
		};

		struct BuzzLightPreset
		{
			Vector3I positionOffset;
			int32_t reserved;
			int32_t colour;
		};

		extern LightingState g_lightingState;
		void SpawnLight(int32_t x, int32_t y, int32_t z, int32_t colour, int32_t lifetime, int32_t sourceId);
		void UpdateBuzzLight();

		STATIC_ASSERT(sizeof(DynamicLight) == 0x18);
		STATIC_ASSERT(offsetof(DynamicLight, lifetime) == 0xC);
		STATIC_ASSERT(offsetof(DynamicLight, sourceId) == 0x10);
		STATIC_ASSERT(offsetof(DynamicLight, colour) == 0x14);
		STATIC_ASSERT(sizeof(LightingState) == 0xC0);
		STATIC_ASSERT(offsetof(LightingState, blendedPosition) == 0x90);
		STATIC_ASSERT(offsetof(LightingState, blendedColour) == 0xA4);
		STATIC_ASSERT(sizeof(BuzzLightPreset) == 0x14);
	}

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

		// FUNCTION: TOY2 0x00410540 [PROVISIONAL]
		void SpawnBurstRingAtPoint(Actor::Toy2Actor* actor, uint8_t red, uint8_t green, uint8_t blue, int32_t verticalOffset)
		{
			int32_t cameraSine = Numerics::g_sinCosLUT[(int16_t)Camera::g_renderCameraTransform.angles.yaw & 0xFFF];
			int32_t cameraCosine = Numerics::g_sinCosLUT[((int16_t)Camera::g_renderCameraTransform.angles.yaw + 0x400) & 0xFFF];

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
			int32_t cameraSine = Numerics::g_sinCosLUT[(int16_t)Camera::g_renderCameraTransform.angles.yaw & 0xFFF];
			int32_t cameraCosine = Numerics::g_sinCosLUT[((int16_t)Camera::g_renderCameraTransform.angles.yaw + 0x400) & 0xFFF];

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

	namespace Lighting
	{
		// GLOBAL: TOY2 0x0050387C
		BuzzLightPreset g_buzzLightPresets[16] = {
			{ { 0x40001000, 0x20008000, 0x20004000 }, 0x20008000, (int32_t)0x80004000 },
			{ { 0x400, -0x400, 0x400 }, 0x960, 0x908060 },
			{ { 0x400, -0x400, 0x400 }, 0x960, 0xA02000 },
			{ { 0, -0x400, 0 }, 0x960, 0xC0C000 },
			{ { 0x200, -0x400, 0x200 }, 0x960, 0x204080 },
			{ { 0x200, -0x400, 0x200 }, 0x960, 0x204060 },
			{ { 0x400, -0x400, 0x400 }, 0x960, 0xA02000 },
			{ { 0x400, -0x400, 0x400 }, 0x960, 0x608090 },
			{ { 0x400, -0x400, 0x400 }, 0x960, 0x608090 },
			{ { 0x400, -0x400, 0x400 }, 0x960, 0x608090 },
			{ { 0x400, -0x400, 0x400 }, 0x960, 0x204080 },
			{ { 0, -0x400, -0x400 }, 0x960, 0x204080 },
			{ { 0x400, -0x400, 0x400 }, 0x960, 0x204080 },
			{ { 0x400, -0x400, 0x400 }, 0x960, 0x908060 },
			{ { 0x200, -0x400, 0x200 }, 0x960, 0x204080 },
			{ { 0x400, -0x400, 0x400 }, 0x960, 0x406080 },
		};

		// GLOBAL: TOY2 0x00830D60
		LightingState g_lightingState;

		// GLOBAL: TOY2 0x00830D3C
		int32_t g_selectedLightIndex;

		// GLOBAL: TOY2 0x00830D54
		int32_t g_lightBlendTimer;

		// FUNCTION: TOY2 0x0049EE50 [PROVISIONAL]
		void SpawnLight(int32_t x, int32_t y, int32_t z, int32_t colour, int32_t lifetime, int32_t sourceId)
		{
			int32_t shortestLifetime = INT_MAX;
			int32_t lightIndex = x;
			for (int32_t candidateIndex = 2; candidateIndex < 6; candidateIndex++)
			{
				if (g_lightingState.dynamicLights[candidateIndex].lifetime < shortestLifetime)
				{
					shortestLifetime = g_lightingState.dynamicLights[candidateIndex].lifetime;
					lightIndex = candidateIndex;
				}
			}

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
			g_lightingState.dynamicLights[0].position.x = g_buzzActor.posAngles.pos.x + g_buzzLightPresets[g_levelFileIndex].positionOffset.x * 0x10;
			g_lightingState.dynamicLights[0].position.z = g_buzzActor.posAngles.pos.z + g_buzzLightPresets[g_levelFileIndex].positionOffset.z * 0x10;
			g_lightingState.dynamicLights[0].position.y = buzzLightY + g_buzzLightPresets[g_levelFileIndex].positionOffset.y * 0x10;

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

			g_lightingState.dynamicLights[0].colour.r = (uint8_t)(g_buzzLightPresets[levelFileIndex].colour >> 16);
			g_lightingState.dynamicLights[0].colour.g = (uint8_t)(g_buzzLightPresets[levelFileIndex].colour >> 8);
			g_lightingState.dynamicLights[0].colour.b = (uint8_t)g_buzzLightPresets[levelFileIndex].colour;

			UpdateBuzzLight();
		}
	}

	namespace Actor
	{
		// STUB: TOY2 0x004076F0
		void UpdateAIMovement(Toy2Actor* actor) {}

		enum DamageActorFlags
		{
			ACTOR_FLAG_BALLISTIC_MOTION = 0x400,
			ACTOR_FLAGS_CLEARED_ON_DEFEAT = 0x19C,
		};

		struct DamageResponse
		{
			int16_t responseType;
			int16_t cooldown;
			int16_t phaseDamage;
		};

		// GLOBAL: TOY2 0x004E066C
		DamageResponse g_damageResponses[] = {
			{ 0, 0, 0 },
			{ 1, 30, 2 },
			{ 2, 4, 1 },
			{ 2, 4, 4 },
			{ 3, 4, 2 },
			{ 1, 30, 2 },
		};

		// GLOBAL: TOY2 0x004DF7C8
		uint16_t g_defeatMovementScript[] = { 0x16, 0xFFE0, 0xFFE0, 4, 0xFFFF, 1 };

		// GLOBAL: TOY2 0x004DF728
		uint16_t g_movementControl0Script[] = { 0x000D, 0x0000, 0x0002, 0x0016, 0xFFE0, 0xFFE0, 0x0004, 0xFFFF, 0x0001, 0x0000 };

		// GLOBAL: TOY2 0x004DF73C
		uint16_t g_movementControl1Script[] = {
			0x0016,
			0xFFE0,
			0xFFE0,
			0x000D,
			0x0000,
			0x0002,
			0x0011,
			0x0008,
			0x0021,
			0x0001,
			0x003C,
			0x0004,
			0x0005,
			0x0000,
			0x0019,
			0x0001,
			0x0001,
			0x000A,
			0x0004,
			0x000D,
			0x0001,
			0x0004,
			0x0001,
			0x0016,
			0x0004,
			0x000D,
			0x0001,
			0x0005,
			0x0018,
			0xF800,
			0x0017,
			0x000E,
			0x0007,
			0x000D,
			0x0001,
			0x0006,
			0x0001,
			0x000C,
			0x0004,
			0xFFFF,
			0x0025,
			0x001B,
			0x0019,
			0x0001,
			0x0001,
			0x000A,
			0x0004,
			0x000D,
			0x0001,
			0x0004,
			0x0001,
			0x0016,
			0x0004,
			0x000D,
			0x0001,
			0x0001,
			0x0018,
			0xF800,
			0x0017,
			0x000E,
			0x0007,
			0x000D,
			0x0001,
			0x0006,
			0x0001,
			0x000C,
			0x0004,
			0xFFFF,
			0x0041,
			0x0000,
		};

		// GLOBAL: TOY2 0x004E02C4
		uint16_t* g_movementDataByControl[2] = { g_movementControl0Script, g_movementControl1Script };

		// GLOBAL: TOY2 0x004E02CC
		uint16_t* g_defeatMovementData = g_defeatMovementScript;

		STATIC_ASSERT(sizeof(DamageResponse) == 6);

		// FUNCTION: TOY2 0x00408A60 [MATCHED]
		void HandleDamage(Toy2Actor* actor, int32_t attackAngle, int32_t damageType)
		{
			g_lastKilledActor = (Toy2Actor*)-1;
			if (g_damageResponses[damageType].responseType != 2 || (actor->creatureRam->defenseMode & 1) != 0)
			{
				if (actor->creatureRam->latSpeedTarget != 0xFF && g_levelFileIndex != 6)
				{
					actor->velX = Numerics::g_sinCosLUT[attackAngle & 0xFFF] / 32;
					actor->velForward = Numerics::g_sinCosLUT[(attackAngle + 0x400) & 0xFFF] / 32;
					actor->actorFlags &= ~ACTOR_FLAG_BALLISTIC_MOTION;
				}

				if (actor->damageCooldownTimer == 0)
				{
					actor->damageCooldownTimer = g_damageResponses[damageType].cooldown;
					if (g_damageResponses[damageType].responseType != 0)
					{
						if (g_damageResponses[damageType].responseType == 1)
						{
							if (damageType == 1)
							{
								HitType1Particles(g_buzzActor.posAngles.pos.x + Numerics::g_sinCosLUT[attackAngle] / 2,
									g_buzzActor.posAngles.pos.y - 0x2C00,
									g_buzzActor.posAngles.pos.z + Numerics::g_sinCosLUT[(attackAngle + 0x400) & 0xFFF] / 2);
							}
							else
							{
								HitType1Particles(g_buzzActor.posAngles.pos.x, g_buzzActor.posAngles.pos.y, g_buzzActor.posAngles.pos.z);
							}
						}

						if (actor->actorPhase < 100)
						{
							actor->actorPhase -= g_damageResponses[damageType].phaseDamage;
							AudioManager::PlaySoundEffect(9, &actor->pos);
						}
						if (actor->actorPhase <= 0)
						{
							actor->actorFlags &= ~ACTOR_FLAGS_CLEARED_ON_DEFEAT;
							actor->actorPhase = 999;
							actor->movementCommandTimer = 0;
							actor->movementData = g_defeatMovementData;
							Kill(actor, KILL_EFFECTS);
						}
					}
				}
			}
		}

		// FUNCTION: TOY2 0x00405D20 [PROVISIONAL]
		void Kill(Toy2Actor* actor, uint8_t killFlags)
		{
			if ((killFlags & KILL_EFFECTS) != 0)
			{
				if ((actor->actorFlags & ACTOR_FLAG_BOSS) == 0)
				{
					Nu3D::Particles::ParticleInstance* marker =
						Nu3D::Particles::SpawnInstance(actor->pos.x, actor->pos.y - 0x1000, actor->pos.z, 0, -0x800, 0, 0x80, 0, 0, 0x3D);
					marker->groundHeightY = Nu3D::Collision::GetGroundHeight(&marker->groundProbe, 0);
					if (marker->groundHeightY == INT_MIN)
					{
						marker->groundHeightY = marker->pos.y;
					}
				}

				actor->actorFlags |= ACTOR_FLAG_BOSS;
				actor->reservedArea[1] = 0xE0;
				actor->reservedArea[2] = 0xE0;
				actor->velX = 0;
				actor->velForward = 0;
				actor->gravityVel = -0x400;

				int32_t effectCount = 0;
				switch (actor->creatureId)
				{
					case 3:
					case 0xE:
						actor->primaryAnimIdx = 2;
						actor->animationFrameSequence = g_animationFrameSequences[1];
						effectCount = 3;
						actor->hitpoints = -0x5E;
						actor->animationFramePosition = *actor->animationFrameSequence << 16;
						break;
					case 4:
						actor->primaryAnimIdx = 2;
						actor->animationFrameSequence = g_animationFrameSequences[7];
						effectCount = 3;
						actor->hitpoints = -0x3E;
						actor->animationFramePosition = *actor->animationFrameSequence << 16;
						break;
					case 0xF:
						actor->hitpoints = -1;
						Particles::SpawnBurstRingAtPoint(actor, 0x20, 0x20, 0x20, 0x2000);
						AudioManager::PlaySoundEffect(-2, &actor->pos);
						break;
					case 0x10:
					case 0x19:
						actor->hitpoints = -1;
						effectCount = 6;
						AudioManager::PlaySoundEffect(-2, &actor->pos);
						break;
					case 0x14:
						actor->primaryAnimIdx = 1;
						actor->animationFrameSequence = g_animationFrameSequences[0x14];
						effectCount = 3;
						actor->hitpoints = -0x46;
						actor->animationFramePosition = *actor->animationFrameSequence << 16;
						break;
					case 0x15:
						Particles::SpawnBurstRingAtActor(actor, 0x60, 0x70, 0x80, 0x3000);
						actor->hitpoints = -8;
						break;
					case 0x16:
						actor->hitpoints = -1;
						Particles::SpawnBurstRingAtPoint(actor, 0x80, 0, 0, 0x5000);
						AudioManager::PlaySoundEffect(-2, &actor->pos);
						break;
					case 0x18:
						actor->gravityVel = -0xB00;
						actor->primaryAnimIdx = 2;
						actor->animationFrameSequence = g_animationFrameSequences[1];
						effectCount = 3;
						actor->hitpoints = -0x5E;
						actor->animationFramePosition = *actor->animationFrameSequence << 16;
						actor->respawnDelay = 10000;
						break;
					case 0x1B:
						actor->hitpoints = -1;
						effectCount = 4;
						break;
					case 0x1F:
						actor->hitpoints = -1;
						Particles::SpawnBurstRingAtPoint(actor, 0, 0, 0x80, 0x3000);
						AudioManager::PlaySoundEffect(-2, &actor->pos);
						break;
					case 0x20:
						actor->hitpoints = -1;
						Particles::SpawnBurstRingAtPoint(actor, 0, 0x80, 0, 0x3000);
						AudioManager::PlaySoundEffect(-2, &actor->pos);
						break;
					case 0x21:
					case 0x2E:
						actor->hitpoints = -1;
						effectCount = -5;
						break;
					case 0x29:
						actor->hitpoints = -1;
						effectCount = -5;
						AudioManager::PlaySoundEffect(0x59, &actor->pos);
						break;
					case 0x30:
						actor->hitpoints = -1;
						Particles::SpawnBurstRingAtPoint(actor, 0x80, 0, 0, 0);
						AudioManager::PlaySoundEffect(-2, &actor->pos);
						break;
					case 0x36:
						actor->hitpoints = -1;
						Particles::SpawnBurstRingAtPoint(actor, 0, 0, 0x80, 0x5000);
						effectCount = 6;
						AudioManager::PlaySoundEffect(-2, &actor->pos);
						break;
					default:
						actor->hitpoints = -1;
						break;
				}

				if (effectCount != 0)
				{
					Vector3I16* effectOffset = &actor->collisionVolumes->offset;
					int32_t effectX = actor->pos.x + effectOffset->x;
					int32_t effectY = actor->pos.y + effectOffset->y;
					int32_t effectZ = actor->pos.z + effectOffset->z;
					if (effectCount > 0)
					{
						do
						{
							Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(effectX, effectY, effectZ, 0x23, 0xE);
							particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
							effectCount--;
						} while (effectCount != 0);
						Lighting::SpawnLight(effectX, effectY, effectZ, 0xF08000, 0x20, (int32_t)actor);
						AudioManager::PlaySoundEffect(0xA, &actor->pos);
					}
					else
					{
						int32_t particleCount = -effectCount;
						for (int32_t particleIndex = 0; particleIndex < particleCount; particleIndex++)
						{
							Nu3D::Particles::ParticleInstance* particle =
								Nu3D::Particles::SpawnFromPreset(effectX, effectY, effectZ, 0x63, (particleIndex & 1) * 10 + 4);
							particle->velY -= 0x100;
							particle->lifetime = (*g_randDatBufferPtr++ & 0x1F) + 0x78;
							int32_t colour = (*g_randDatBufferPtr++ & 0x7F) + 0x40;
							particle->colourR = colour;
							particle->colourG = colour;
							particle->colourB = colour;
							if ((*g_randDatBufferPtr++ & 1) != 0)
							{
								particle->rotSpeed = (*g_randDatBufferPtr++ & 0x3F) + 0x40;
							}
							else
							{
								particle->rotSpeed = -0x40 - (*g_randDatBufferPtr++ & 0x3F);
							}
							Nu3D::Particles::SpawnFromPreset(effectX, effectY, effectZ, 0x11, 4);
						}
						AudioManager::PlaySoundEffect(0xA, &actor->pos);
					}
				}
			}

			if ((killFlags & KILL_REMOVE_ACTOR) != 0)
			{
				g_lastKilledActor = actor;
				if (actor->respawnDelay == 0)
				{
					actor->creatureId = 0;
				}
				actor->actorFlags &= ~3;
				actor->actorPhase = 0;
				actor->scalePivotHeight = 0;

				int32_t actorIndex = 0;
				while (g_activeActors[actorIndex] != actor)
				{
					actorIndex++;
				}
				int32_t lastActorIndex = actorIndex;
				while (g_activeActors[lastActorIndex + 1] != 0)
				{
					lastActorIndex++;
				}
				g_activeActors[actorIndex] = g_activeActors[lastActorIndex];
				g_activeActors[lastActorIndex] = 0;
			}
		}

		// FUNCTION: TOY2 0x0043C1C0 [PROVISIONAL]
		void ResolveBoneAttachmentPos(Vector4I* position, Toy2Actor* actor, int32_t boneIndex)
		{
			if (actor->primaryAnimIdx != -1)
			{
				Animation::g_singleNodeIndex = boneIndex;
				CharacterLoader::CharacterAnimationData* animationData = CharacterLoader::g_characterAnimationData[actor->creatureId];
				Animation::EvaluateClip(animationData->clips[actor->primaryAnimIdx], actor->animationFramePosition, animationData->baseBoneIndex, 0);
			}

			if (actor->secondaryAnimIdx != -1)
			{
				Animation::g_singleNodeIndex = boneIndex;
				CharacterLoader::CharacterAnimationData* animationData = CharacterLoader::g_characterAnimationData[actor->creatureId];
				Animation::EvaluateClip(animationData->clips[actor->secondaryAnimIdx], actor->secondaryAnimationFramePosition, animationData->baseBoneIndex, 1);
			}

			Animation::g_singleNodeIndex = -1;
			CharacterLoader::CharacterAnimationData* animationData = CharacterLoader::g_characterAnimationData[actor->creatureId];
			CharacterLoader::BoneTransform* boneTransform = &CharacterLoader::g_boneTransforms[animationData->baseBoneIndex + boneIndex];

			Animation::g_keyframeRotation.angles.x = actor->pitchAngle;
			Animation::g_keyframeRotation.angles.y = actor->yawAngle + 0x800;
			Animation::g_keyframeRotation.angles.z = actor->rollAngle;
			Nu3D::Math::EulerToRotationMatrix(&Animation::g_keyframeRotation.angles, &Animation::g_keyframeRotation.matrix);

			if (actor->scalePivotHeight != 0)
			{
				Animation::g_keyframeRotation.matrix.m00 = (int16_t)(actor->scaleX * Animation::g_keyframeRotation.matrix.m00 / 0x1000);
				Animation::g_keyframeRotation.matrix.m10 = (int16_t)(actor->scaleX * Animation::g_keyframeRotation.matrix.m10 / 0x1000);
				Animation::g_keyframeRotation.matrix.m20 = (int16_t)(actor->scaleX * Animation::g_keyframeRotation.matrix.m20 / 0x1000);
				Animation::g_keyframeRotation.matrix.m01 = (int16_t)(actor->scaleY * Animation::g_keyframeRotation.matrix.m01 / 0x1000);
				Animation::g_keyframeRotation.matrix.m11 = (int16_t)(actor->scaleY * Animation::g_keyframeRotation.matrix.m11 / 0x1000);
				Animation::g_keyframeRotation.matrix.m21 = (int16_t)(actor->scaleY * Animation::g_keyframeRotation.matrix.m21 / 0x1000);
				Animation::g_keyframeRotation.matrix.m02 = (int16_t)(actor->scaleZ * Animation::g_keyframeRotation.matrix.m02 / 0x1000);
				Animation::g_keyframeRotation.matrix.m12 = (int16_t)(actor->scaleZ * Animation::g_keyframeRotation.matrix.m12 / 0x1000);
				Animation::g_keyframeRotation.matrix.m22 = (int16_t)(actor->scaleZ * Animation::g_keyframeRotation.matrix.m22 / 0x1000);
			}

			int32_t boneWorldX = actor->pos.x
				+ (Animation::g_keyframeRotation.matrix.m00 * boneTransform->translation.x
					  + Animation::g_keyframeRotation.matrix.m01 * boneTransform->translation.y
					  + Animation::g_keyframeRotation.matrix.m02 * boneTransform->translation.z)
					/ 0x80;
			int32_t boneWorldY = actor->pos.y
				+ (Animation::g_keyframeRotation.matrix.m10 * boneTransform->translation.x
					  + Animation::g_keyframeRotation.matrix.m11 * boneTransform->translation.y
					  + Animation::g_keyframeRotation.matrix.m12 * boneTransform->translation.z)
					/ 0x80;
			int32_t boneWorldZ = actor->pos.z
				+ (Animation::g_keyframeRotation.matrix.m20 * boneTransform->translation.x
					  + Animation::g_keyframeRotation.matrix.m21 * boneTransform->translation.y
					  + Animation::g_keyframeRotation.matrix.m22 * boneTransform->translation.z)
					/ 0x80;

			Nu3D::ViewMatrix::MultiplyFixed(&Animation::g_keyframeRotation.matrix, &boneTransform->rotation, &Animation::g_keyframeRotation.matrix);

			if (boneTransform->hasScale == 1)
			{
				Animation::g_keyframeRotation.matrix.m00 = (int16_t)(boneTransform->scaleX * Animation::g_keyframeRotation.matrix.m00 / 0x1000);
				Animation::g_keyframeRotation.matrix.m10 = (int16_t)(boneTransform->scaleX * Animation::g_keyframeRotation.matrix.m10 / 0x1000);
				Animation::g_keyframeRotation.matrix.m20 = (int16_t)(boneTransform->scaleX * Animation::g_keyframeRotation.matrix.m20 / 0x1000);
				Animation::g_keyframeRotation.matrix.m01 = (int16_t)(boneTransform->scaleY * Animation::g_keyframeRotation.matrix.m01 / 0x1000);
				Animation::g_keyframeRotation.matrix.m11 = (int16_t)(boneTransform->scaleY * Animation::g_keyframeRotation.matrix.m11 / 0x1000);
				Animation::g_keyframeRotation.matrix.m21 = (int16_t)(boneTransform->scaleY * Animation::g_keyframeRotation.matrix.m21 / 0x1000);
				Animation::g_keyframeRotation.matrix.m02 = (int16_t)(boneTransform->scaleZ * Animation::g_keyframeRotation.matrix.m02 / 0x1000);
				Animation::g_keyframeRotation.matrix.m12 = (int16_t)(boneTransform->scaleZ * Animation::g_keyframeRotation.matrix.m12 / 0x1000);
				Animation::g_keyframeRotation.matrix.m22 = (int16_t)(boneTransform->scaleZ * Animation::g_keyframeRotation.matrix.m22 / 0x1000);
			}

			int32_t localX = position->x;
			int32_t localY = position->y;
			int32_t localZ = position->z;
			position->x = boneWorldX
				+ (Animation::g_keyframeRotation.matrix.m00 * localX + Animation::g_keyframeRotation.matrix.m01 * localY
					  + Animation::g_keyframeRotation.matrix.m02 * localZ)
					/ 0x80;
			position->y = boneWorldY
				+ (Animation::g_keyframeRotation.matrix.m10 * localX + Animation::g_keyframeRotation.matrix.m11 * localY
					  + Animation::g_keyframeRotation.matrix.m12 * localZ)
					/ 0x80;
			position->z = boneWorldZ
				+ (Animation::g_keyframeRotation.matrix.m20 * localX + Animation::g_keyframeRotation.matrix.m21 * localY
					  + Animation::g_keyframeRotation.matrix.m22 * localZ)
					/ 0x80;

			if (actor->scalePivotHeight != 0)
			{
				position->y += ((0x1000 - actor->scaleY) * actor->scalePivotHeight) >> 7;
			}
		}

		// GLOBAL: TOY2 0x0052F1D0
		Toy2Actor* g_activeActors[65];

		// GLOBAL: TOY2 0x0052c840
		Toy2Actor g_creatureActors[64];

		// GLOBAL: TOY2 0x004E0588
		uint8_t* g_animationFrameSequences[26];

		// GLOBAL: TOY2 0x00830D40
		int32_t g_periodicHintSoundTimer;

		// GLOBAL: TOY2 0x00830C98
		int32_t g_coinQuestHintTimer;

		// GLOBAL: TOY2 0x00830D1C
		int32_t g_coinTokenAwarded;

		// GLOBAL: TOY2 0x00529D48
		Toy2Actor* g_renderActors[66];

		// GLOBAL: TOY2 0x0050A54C
		Toy2Actor* g_lastKilledActor;

		// GLOBAL: TOY2 0x0052ADD8
		int32_t g_unk52ADD8[0x80];

		// GLOBAL: TOY2 0x0052EF48
		int32_t g_unk52EF48;

		// GLOBAL: TOY2 0x0052EF88
		int32_t g_unk52EF88;

		// FUNCTION: TOY2 0x00407150 [PROVISIONAL]
		void InitCreatureRam()
		{
			memset(g_creatureActors, 0, sizeof(g_creatureActors));
			memset(g_unk52ADD8, 0, sizeof(g_unk52ADD8));
			g_lastKilledActor = (Toy2Actor*)-1;
			g_activeActors[0] = 0;
			g_unk52EF48 = 0;
			g_unk52EF88 = 0;

			Toy2Actor* actor = g_creatureActors;
			RawLoader::CreatureListRam* creature = RawLoader::g_creatureListRam;
			// CAP-18: MSVC anchors the creature cursor on the entCtrl store target;
			// retail anchors on the creatureId read source. The actor side matches.
			for (int32_t i = 0x40; i != 0; i--)
			{
				actor->secondaryAnimIdx = -1;
				uint8_t creatureId = creature->creatureId;
				if (creatureId != 0)
				{
					actor->creatureRam = creature;
					if (creatureId == 0x0e)
					{
						creature->entCtrl.actorPhase = 4;
					}
					Game::InitActor(actor, 1);
				}
				actor++;
				creature++;
			}
		}

		// FUNCTION: TOY2 0x004019D0 [MATCHED]
		void UpdatePrimaryAnimation(Toy2Actor* actor)
		{
			CharacterLoader::CharacterAnimationData* animationData = CharacterLoader::g_characterAnimationData[actor->creatureId];
			Animation::EvaluateClip(animationData->clips[actor->primaryAnimIdx], actor->animationFramePosition, animationData->baseBoneIndex, 0);
		}

		// FUNCTION: TOY2 0x00405C80 [PROVISIONAL]
		void StepCreatureAnimFrame(Toy2Actor* actor)
		{
			if (actor->animationFrameSequence[2] == 0xff && actor->animationFrameSequence[3] == 0)
			{
				uint8_t* frame = actor->animationFrameSequence;
				actor->animationFramePosition = ((uint32_t)frame[0] << 16) + 0xffff;
				return;
			}

			uint8_t* frame = actor->animationFrameSequence + 1;
			actor->animationFrameSequence = frame;
			if (*frame == 0xff)
			{
				if (actor->animationFrameSequence[1] != 1)
				{
					actor->animationFrameSequence -= actor->animationFrameSequence[1];
				}
				else
				{
					actor->animationFrameSequence--;
					actor->animationFramePosition |= 0xffff;
				}
			}

			actor->animationFramePosition &= 0xffff;
			actor->animationFramePosition += (uint32_t)*actor->animationFrameSequence << 16;
		}

		// FUNCTION: TOY2 0x00405CF0 [MATCHED]
		void SetAnimation(Toy2Actor* actor, int16_t animationIndex, int32_t frameSequenceIndex)
		{
			actor->primaryAnimIdx = animationIndex;
			actor->animationFrameSequence = g_animationFrameSequences[frameSequenceIndex];
			actor->animationFramePosition = (uint32_t)*actor->animationFrameSequence << 16;
		}

		// FUNCTION: TOY2 0x0049F460 [MATCHED]
		int32_t IsInsideBounds(const Vector3I* position, int32_t minX, int32_t maxX, int32_t minZ, int32_t maxZ)
		{ return position->x > minX && position->x < maxX && position->z > minZ && position->z < maxZ; }

		// FUNCTION: TOY2 0x004A28B0 [MATCHED]
		void PopulateActiveActors()
		{
			int32_t actorIndex = 0;
			while (g_activeActors[actorIndex] != 0)
			{
				g_renderActors[actorIndex] = g_activeActors[actorIndex];
				actorIndex++;
			}
			g_renderActors[actorIndex++] = (Toy2Actor*)&Toy2::g_buzzActor;
			g_renderActors[actorIndex] = 0;
		}

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

		// FUNCTION: TOY2 0x004A1CE0 [MATCHED]
		void CollectQuestReward(int32_t actorIndex, int32_t dialogueRecordIndex, int32_t actorFacingAngle, int32_t cameraFacingAngle, int32_t tokenIndex)
		{
			if (Collectables::g_tokenStates[tokenIndex].active == 0 && (g_creatureActors[actorIndex].actorFlags & ACTOR_FLAG_ACTIVE) != 0
				&& (Toy2::g_gameplayStateFlags & 1) == 0)
			{
				g_coinQuestHintTimer -= Renderer::g_frameDelta;
				if (g_coinQuestHintTimer < 0)
				{
					g_coinQuestHintTimer = *g_randDatBufferPtr++ * 2 + 0xF0;
					AudioManager::PlayOneShotSound3DActor(&g_creatureActors[actorIndex],
						AudioManager::g_oneShotPresets[0xAC].encodedSoundIndex - 1,
						AudioManager::g_oneShotPresets[0xAC].baseFrequency,
						AudioManager::g_oneShotPresets[0xAC].leftVolume,
						&g_creatureActors[actorIndex],
						0);
				}
			}

			if ((g_creatureActors[actorIndex].actorFlags & ACTOR_FLAG_INTERACTION_REQUESTED) == 0)
				return;

			g_creatureActors[actorIndex].actorFlags &= ~ACTOR_FLAG_INTERACTION_REQUESTED;
			if (Collectables::g_tokenStates[tokenIndex].active != 0)
				return;

			if (Toy2::g_buzzActor.coinsCollected >= 50)
			{
				AudioManager::PlayOneShotSound3DActor(&g_creatureActors[actorIndex],
					AudioManager::g_oneShotPresets[0xAE].encodedSoundIndex - 1,
					AudioManager::g_oneShotPresets[0xAE].baseFrequency,
					AudioManager::g_oneShotPresets[0xAE].leftVolume,
					&g_creatureActors[actorIndex],
					0);
				g_coinTokenAwarded = 1;
				Dialogue::Begin(
					actorIndex, dialogueRecordIndex, "well done buzz! here is your pizza planet ^token^.", actorFacingAngle, cameraFacingAngle, tokenIndex);
				return;
			}

			AudioManager::PlayOneShotSound3DActor(&g_creatureActors[actorIndex],
				AudioManager::g_oneShotPresets[0xAD].encodedSoundIndex - 1,
				AudioManager::g_oneShotPresets[0xAD].baseFrequency,
				AudioManager::g_oneShotPresets[0xAD].leftVolume,
				&g_creatureActors[actorIndex],
				0);
			Dialogue::Begin(actorIndex,
				dialogueRecordIndex,
				"hi buzz! if you can bring me ^fifty^ coins, i will give you a pizza planet ^token^.",
				actorFacingAngle,
				cameraFacingAngle,
				-1);
		}

		// FUNCTION: TOY2 0x004A26F0 [PROVISIONAL]
		void PlayPeriodicHintSound(int32_t actorIndex, int32_t soundPresetIndex)
		{
			if (soundPresetIndex == 0xB5)
			{
				if (Toy2::HUD::g_challengeState != 0)
					return;
			}
			else if (Toy2::g_levelObjectiveProgress < 0)
			{
				return;
			}

			if ((g_creatureActors[actorIndex].actorFlags & ACTOR_FLAG_ACTIVE) == 0 || (Toy2::g_gameplayStateFlags & 1) != 0)
				return;

			g_periodicHintSoundTimer -= Renderer::g_frameDelta;
			if (g_periodicHintSoundTimer >= 0)
				return;

			g_periodicHintSoundTimer = *g_randDatBufferPtr++ * 2 + 0xF0;
			AudioManager::OneShotSoundPreset* preset = &AudioManager::g_oneShotPresets[soundPresetIndex];
			AudioManager::PlayOneShotSound3DActor(
				&g_creatureActors[actorIndex], preset->encodedSoundIndex - 1, preset->baseFrequency, preset->leftVolume, &g_creatureActors[actorIndex], 0);
		}

		// FUNCTION: TOY2 0x00414A80 [PROVISIONAL]
		void GetCreatureList(uint8_t* creatureIdList)
		{
			InitCreatureRam();
			int32_t index = 1;
			int16_t* creatureId = &g_creatureActors[0].creatureId;
			creatureIdList[0] = 0;
			do
			{
				if (*creatureId != 0)
				{
					creatureIdList[index] = (uint8_t)*creatureId;
					index++;
				}
				creatureId += sizeof(Toy2Actor) / sizeof(int16_t);
			} while (creatureId < &g_creatureActors[64].creatureId);
			creatureIdList[index] = 0xff;
		}

		// FUNCTION: TOY2 0x004CDBF0 [MATCHED]
		int32_t FindInActorList(Toy2Actor* actor)
		{
			Toy2Actor** list = Animation::g_actorAnimList;
			if (list != 0)
			{
				int32_t index = 0;
				while (*list != 0)
				{
					if (actor == *list)
						return index;
					list++;
					index++;
				}
			}
			return -1;
		}

		// FUNCTION: TOY2 0x004CDBB0 [MATCHED]
		void SetNodeAngle(Toy2Actor* actor, int32_t nodeIndex, int32_t pitch, int32_t yaw, int32_t roll)
		{
			int32_t actorIndex = FindInActorList(actor);
			if (actorIndex >= 0)
			{
				Animation::g_nodeAngles[actorIndex][nodeIndex].x = pitch;
				Animation::g_nodeAngles[actorIndex][nodeIndex].y = yaw;
				Animation::g_nodeAngles[actorIndex][nodeIndex].z = roll;
			}
		}
	}

	namespace CreatureBehaviour
	{
		void RCCarLevel1(Actor::Toy2Actor::ActorBehaviourContext* context);
		void RCCarLevel2(Actor::Toy2Actor::ActorBehaviourContext* context);

		// STUB: TOY2 0x00406220
		void Zurg3(Actor::Toy2Actor::ActorBehaviourContext* context) {}

		// GLOBAL: TOY2 0x004E02F4
		uint16_t* g_zgCarMovementData;

		// FUNCTION: TOY2 0x004064A0 [EFFECTIVE]
		void ZgCar(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			if (actor->previousActorPhase == 0)
				actor->previousActorPhase = actor->actorPhase;

			if (actor->previousActorPhase != actor->actorPhase && actor->actorPhase < 101)
			{
				actor->previousActorPhase = actor->actorPhase;
				if (actor->primaryAnimIdx != 1)
				{
					actor->movementCommandTimer = 0;
					actor->movementData = g_zgCarMovementData + 62;
				}
			}

			if (g_framePulseOutputs.eightTick != 0 && actor->creatureRam->speedNoTarget == 0xFF)
				AudioManager::PlaySoundEffect(0x42, &actor->pos);

			if (context->localForwardSpeed > 0)
			{
				AudioManager::g_dynamicSoundFrequencies[0] = context->localForwardSpeed << 2;
				AudioManager::PlaySoundEffect(0x41, &actor->pos);
			}

			if ((actor->actorFlags & Actor::ACTOR_FLAG_TARGETABLE) == Actor::ACTOR_FLAG_TARGETABLE && actor->hitpoints >= 0
				&& context->localForwardSpeed > 0x100 && g_framePulseOutputs.fourTick != 0)
			{
				Nu3D::Particles::SpawnFromPreset(actor->pos.x + (Numerics::g_sinCosLUT[(actor->yawAngle + 0x7A0) & 0xFFF] * 3 >> 2),
					actor->pos.y - 0x1000,
					actor->pos.z + (Numerics::g_sinCosLUT[(actor->yawAngle - 0x460) & 0xFFF] * 3 >> 2),
					0x27,
					2);
				Nu3D::Particles::SpawnFromPreset(actor->pos.x + (Numerics::g_sinCosLUT[(actor->yawAngle - 0x7A0) & 0xFFF] * 3 >> 2),
					actor->pos.y - 0x1000,
					actor->pos.z + (Numerics::g_sinCosLUT[(actor->yawAngle - 0x3A0) & 0xFFF] * 3 >> 2),
					0x27,
					2);
			}
		}
		// STUB: TOY2 0x00406620
		void ZPod(Actor::Toy2Actor::ActorBehaviourContext* context) {}
		// FUNCTION: TOY2 0x00406960 [MATCHED]
		void BPlane(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			if (actor->hitpoints >= 0)
				AudioManager::PlaySoundEffect(0x5D, &actor->pos);

			if ((actor->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0 && actor->actorPhase == 1)
				Actor::HandleDamage(actor, (actor->yawAngle - 0x800) & 0xFFF, Actor::DAMAGE_SPIN);

			if (actor->previousActorPhase != 0)
			{
				actor->previousActorPhase = 0;
				Actor::Toy2Actor* anchorActor;
				if ((actor - 1)->creatureId == 25)
					anchorActor = actor - 1;
				else
					anchorActor = actor - 2;

				actor->boundary.y = anchorActor->pos.y - 0x5000;
				actor->pos.x = anchorActor->pos.x;
				actor->pos.y = anchorActor->pos.y;
				actor->pos.z = anchorActor->pos.z;
				actor->motionTargetPos.x = anchorActor->pos.x;
				actor->motionTargetPos.y = anchorActor->pos.y - 0x5000;
				actor->motionTargetPos.z = anchorActor->pos.z;
			}

			if (g_framePulseOutputs.eightTick != 0 && (actor->actorFlags & Actor::ACTOR_FLAG_TARGETABLE) != 0)
			{
				Vector4I particlePosition;
				particlePosition.x = 0;
				particlePosition.y = -200;
				particlePosition.z = 400;
				Actor::ResolveBoneAttachmentPos(&particlePosition, actor, 0);
				Nu3D::Particles::SpawnFromPreset(particlePosition.x, particlePosition.y, particlePosition.z, 0x58, 3);
			}
		}
		// FUNCTION: TOY2 0x00406A90 [PROVISIONAL]
		void FatBloke(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			if (actor->previousActorPhase == 0)
				return;

			int32_t offsetX = Numerics::g_sinCosLUT[(actor->yawAngle + 0xE0) & 0xFFF];
			int32_t offsetZ = Numerics::g_sinCosLUT[(actor->yawAngle + 0x4E0) & 0xFFF];
			Vector3I collisionPosition = actor->pos;
			collisionPosition.y -= 0x3000;

			int32_t impactAngle =
				Nu3D::Math::CartesianToFixedAngle(g_buzzActor.posAngles.pos.x - actor->pos.x - offsetX, g_buzzActor.posAngles.pos.z - actor->pos.z - offsetZ)
				& 0xFFF;
			if (((impactAngle - actor->yawAngle + 0x100) & 0xFFF) > 0x200)
				impactAngle = actor->yawAngle;

			int32_t velocityX = Numerics::g_sinCosLUT[impactAngle] >> 3;
			int32_t velocityZ = Numerics::g_sinCosLUT[(impactAngle + 0x400) & 0xFFF] >> 3;
			Vector3I movement = { velocityX * 0x50, 0, velocityZ * 0x50 };
			Collision::SweepAndSlide(&collisionPosition, &movement, 0x8000, 0, 0x100);

			int32_t movementScale;
			if (abs(velocityX) > abs(velocityZ))
				movementScale = movement.x / velocityX;
			else
				movementScale = movement.z / velocityZ;

			Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnInstance(
				collisionPosition.x + offsetX, collisionPosition.y, collisionPosition.z + offsetZ, velocityX, 0, velocityZ, 0, 0, 0, 0x61);
			int32_t lifetime = abs(movementScale) * 2 - 0xF;
			if (lifetime <= 0)
				lifetime = 1;
			particle->lifetime = (int16_t)lifetime;

			AudioManager::PlaySoundEffect(0x56, &actor->pos);
			if ((actor->actorFlags & Actor::ACTOR_FLAG_TARGETABLE) != 0)
			{
				for (int32_t particleCount = 5; particleCount != 0; particleCount--)
				{
					particle = Nu3D::Particles::SpawnFromPreset(collisionPosition.x, collisionPosition.y, collisionPosition.z, 0x64, 0xF);
					particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
				}
			}

			actor->previousActorPhase = 0;
		}

		// GLOBAL: TOY2 0x004E0318
		uint16_t* g_boxMovementData;

		// GLOBAL: TOY2 0x0050A544
		int32_t g_rcCarRearWheelRotation;

		// GLOBAL: TOY2 0x0050A548
		int32_t g_rcCarFrontWheelRotation;

		// FUNCTION: TOY2 0x004068E0 [EFFECTIVE]
		void Box(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			if (actor->previousActorPhase != 0)
			{
				actor->previousActorPhase = 0;
				if (actor[1].actorPhase == 0)
				{
					actor[1].respawnDelay = 30;
					uint16_t* movementData = g_boxMovementData + 53;
					actor->movementCommandTimer = 0;
					actor->movementData = movementData;
				}
				else if (actor[2].actorPhase == 0)
				{
					actor[2].respawnDelay = 30;
					uint16_t* movementData = g_boxMovementData + 53;
					actor->movementCommandTimer = 0;
					actor->movementData = movementData;
				}
				else
				{
					uint16_t* movementData = g_boxMovementData;
					actor->movementCommandTimer = 0;
					actor->movementData = movementData + 59;
				}
			}
		}

		// FUNCTION: TOY2 0x00406C70 [MATCHED]
		void Buzzard(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			if (actor->previousActorPhase != actor->actorPhase)
			{
				if (actor->previousActorPhase != 0)
				{
					AudioManager::PlaySoundEffect(0x58, &actor->pos);
				}
				actor->previousActorPhase = actor->actorPhase;
			}

			AudioManager::PlaySoundEffect(0x57, &actor->pos);
			if ((context->targetFlags & 1) != 0)
			{
				actor->primaryAnimIdx = 1;
			}
			else
			{
				actor->primaryAnimIdx = 0;
			}
		}

		// FUNCTION: TOY2 0x0043C070 [MATCHED]
		void SetRCCarNodeAngle(Actor::Toy2Actor* actor, int32_t nodeIndex, int32_t pitch, int32_t yaw, int32_t roll)
		{
			CharacterLoader::CharacterAnimationData* animationData = CharacterLoader::g_characterAnimationData[actor->creatureId];
			CharacterLoader::BoneTransform* transform = &CharacterLoader::g_boneTransforms[animationData->baseBoneIndex + nodeIndex];

			Animation::g_keyframeRotation.angles.x = (int16_t)pitch;
			Animation::g_keyframeRotation.angles.y = (int16_t)yaw;
			Animation::g_keyframeRotation.angles.z = (int16_t)roll;

			Nu3D::Math::EulerToRotationMatrix(&Animation::g_keyframeRotation.angles, &transform->rotation);
			Actor::SetNodeAngle(actor, nodeIndex, pitch, yaw, roll);
		}

		// FUNCTION: TOY2 0x00406A60 [MATCHED]
		void RCCar(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			if (g_levelFileIndex == 1)
			{
				RCCarLevel1(context);
			}
			if (g_levelFileIndex == 2)
			{
				RCCarLevel2(context);
			}
		}
	}
}

// GLOBAL: TOY2 0x0052B7E0
int32_t Toy2::g_hudActorAnimationFrame;
