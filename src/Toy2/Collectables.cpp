#include "Toy2/Collectables.h"
#include "Toy2/Buzz.h"
#include "Toy2/Camera.h"
#include "Toy2/Collision.h"
#include "Toy2/Dialogue.h"
#include "Toy2/Gadget.h"
#include "Toy2/Levels.h"
#include "Toy2/Object.h"
#include "Toy2/Particles.h"
#include "Toy2/Toy2.h"
#include "AudioManager/AudioManager.h"
#include "InputManager.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Link.h"
#include "Nu3D/Particles.h"
#include "Random.h"
#include "Renderer/Renderer.h"
#include "SaveManager.h"

#include <limits.h>
#include <string.h>

namespace Toy2
{
	extern int32_t g_airborneTimer;
	extern int32_t g_gadgetRespawnTimer;
	extern int32_t g_tokenPromptSelection;

	namespace Camera
	{
		void BeginScriptedCutsceneOnBuzz(int32_t duration);
	}

	namespace Collectables
	{
		enum InteractionConstants
		{
			PICKUP_RADIUS_MASK = 0x7F,
			TOKEN_COLLECTION_STATE_IDLE = 0,
			TOKEN_COLLECTION_STATE_START = 1,
			TOKEN_COLLECTION_STATE_WAIT_FOR_LANDING = 2,
			TOKEN_COLLECTION_STATE_EXIT_LEVEL = 3,
			TOKEN_COLLECTION_STATE_LAUNCH = 4,
		};

		struct InteractionPickup : Buzz::GadgetPickup
		{
			uint8_t GetRadiusAndFlags() const { return reservedD[0]; }
		};

		STATIC_ASSERT(sizeof(InteractionPickup) == 0x10);

		void Token(int32_t tokenId);

		// GLOBAL: TOY2 0x005039C4
		int16_t g_tokenBurstVelocities[16][3] = {
			{ 0, 0x1000, 0 },
			{ 0, -0x1000, 0 },
			{ 0x1000, 0, 0 },
			{ -0x1000, 0, 0 },
			{ 0xB50, 0xB50, 0 },
			{ 0xB50, -0xB50, 0 },
			{ -0xB50, 0xB50, 0 },
			{ -0xB50, -0xB50, 0 },
			{ 0, 0xB50, 0xB50 },
			{ 0, -0xB50, 0xB50 },
			{ 0, 0xB50, -0xB50 },
			{ 0, -0xB50, -0xB50 },
			{ 0xB50, 0, 0xB50 },
			{ -0xB50, 0, 0xB50 },
			{ 0xB50, 0, -0xB50 },
			{ -0xB50, 0, -0xB50 },
		};

		// GLOBAL: TOY2 0x00830D28
		int32_t g_tokenLaunchVerticalVelocity;

		// GLOBAL: TOY2 0x00830D5C
		InteractionPickup* g_respawningGadgetPickup;

		// GLOBAL: TOY2 0x00830E34
		int32_t g_respawningGadgetPickupY;

		// FUNCTION: TOY2 0x004A0F80 [PROVISIONAL]
		void Interactions()
		{
			Vector3I position;
			Vector3I pickupPositions[2];

			if (g_framePulseOutputs.sixteenTick != 0)
			{
				for (int32_t tokenIndex = 0; tokenIndex < 5; tokenIndex++)
				{
					TokenState* token = &g_tokenStates[tokenIndex];
					if (token->active == 1 && (SaveManager::g_save0Data.tokens[g_levelFileIndex] & (1 << tokenIndex)) == 0
						&& ShowTokenSparkle(token->linkId) != 0)
					{
						Nu3D::Link::GetCurrentPosFixed(token->linkId, &position);
						int32_t distanceX = (Camera::g_renderCameraTransform.pos.x - position.x) >> 8;
						int32_t distanceY = (Camera::g_renderCameraTransform.pos.y - position.y) >> 8;
						int32_t distanceZ = (Camera::g_renderCameraTransform.pos.z - position.z) >> 8;
						int32_t distanceSquared =
							distanceZ * distanceZ + distanceY * distanceY + distanceX * distanceX;
						if (distanceSquared < 0x90000 && distanceSquared + 1 != 0)
						{
							Nu3D::Particles::ParticleInstance* particle =
								Nu3D::Particles::SpawnFromPreset(position.x, position.y, position.z, 0x29, 9);
							particle->rotSpeed = *g_randDatBufferPtr - 0x80;
							g_randDatBufferPtr++;
							particle->lifetime = (*g_randDatBufferPtr & 0xF) * 2 + 0x18;
							g_randDatBufferPtr++;
						}
					}
				}
			}

			if (g_gadgetRespawnTimer > 0)
			{
				g_gadgetRespawnTimer -= Renderer::g_frameDelta;
				if (g_gadgetRespawnTimer <= 0)
				{
					InteractionPickup* respawningPickup = g_respawningGadgetPickup;
					respawningPickup->position.y = g_respawningGadgetPickupY;
					g_gadgetRespawnTimer = 0;
					Nu3D::Link::SetScaleFromFixedOffsets(respawningPickup->linkId, 0x1000, 0x1000, 0x1000);
					position.x = respawningPickup->position.x * 0x20;
					position.y = respawningPickup->position.y * 0x20;
					position.z = respawningPickup->position.z * 0x20;
					int32_t distanceZ = (Camera::g_renderCameraTransform.pos.z - position.z) >> 8;
					int32_t distanceY = (Camera::g_renderCameraTransform.pos.y - position.y) >> 8;
					int32_t distanceX = (Camera::g_renderCameraTransform.pos.x - position.x) >> 8;
					int32_t distanceSquared = distanceZ * distanceZ;
					distanceSquared += distanceY * distanceY;
					distanceSquared += distanceX * distanceX;
					if (distanceSquared < 0x90000 && distanceSquared + 1 != 0)
						Particles::SpawnCollectSparkle(position.x, position.y, position.z, 0x32);
				}
			}

			int32_t pickupSound = 0;
			Levels::RecordData* pickupRecords = Levels::g_recordData[63];
			InteractionPickup* pickup = reinterpret_cast<InteractionPickup*>(pickupRecords + 1);
			int32_t buzzX = g_buzzActor.posAngles.pos.x >> 5;
			int32_t buzzY = g_buzzActor.posAngles.pos.y >> 5;
			int32_t buzzZ = g_buzzActor.posAngles.pos.z >> 5;

			for (uint32_t pickupCount = pickupRecords->recordCount; pickupCount != 0; pickupCount--, pickup++)
			{
				if (pickup->position.y == INT_MIN || (pickup->GetRadiusAndFlags() & PICKUP_FLAG_PERSISTENT) == 0
					|| (pickup->GetRadiusAndFlags() & PICKUP_RADIUS_MASK) == 0)
					continue;

				int32_t distanceX = (buzzX - pickup->position.x) >> 3;
				int32_t distanceY = (buzzY - 0xE6 - pickup->position.y) >> 3;
				int32_t distanceZ = (buzzZ - pickup->position.z) >> 3;
				int32_t pickupRadius = (pickup->GetRadiusAndFlags() & PICKUP_RADIUS_MASK) + 0xE;
				if (distanceX * distanceX + distanceY * distanceY + distanceZ * distanceZ >= pickupRadius * pickupRadius)
					continue;

				pickupPositions[0].x = pickup->position.x << 5;
				pickupPositions[0].y = pickup->position.y << 5;
				pickupPositions[0].z = pickup->position.z << 5;
				Particles::SpawnCollectSparkle(pickupPositions[0].x, pickupPositions[0].y, pickupPositions[0].z, pickupRadius);

				int32_t pickupType = pickup->linkId < 0x30 ? 0x10 : Object::Classify(pickup->linkId);
				int8_t removalMode = pickup->linkId >= 0x30;
				switch (pickupType)
				{
				case 0:
					HUD::g_slideTimers[1] = 0xB4;
					g_buzzActor.health += 4;
					if (g_buzzActor.health > 0xE)
						g_buzzActor.health = 0xE;
					break;
				case 1:
					CosmicShield(pickup);
					removalMode = 0;
					break;
				case 2:
				{
					for (int32_t tokenIndex = 0; tokenIndex < 5; tokenIndex++)
					{
						if (g_tokenStates[tokenIndex].linkId == pickup->linkId)
						{
							g_tokenStates[tokenIndex].active = 2;
							g_tokenStates[tokenIndex].timer = 0;
							g_tokenCollectionState = TOKEN_COLLECTION_STATE_START;
							SaveManager::g_save0Data.tokens[g_levelFileIndex] |= 1 << tokenIndex;
							break;
						}
					}
					break;
				}
				case 3:
					HUD::g_slideTimers[0] = 0xB4;
					if (g_buzzActor.lives < 9)
						g_buzzActor.lives++;
					break;
				case 4:
					Token(pickup->linkId);
					removalMode = 2;
					break;
				case 5:
					Buzz::ActivateRocketBoots(pickup);
					break;
				case 6:
				{
					g_grappleCharges += 5;
					g_discLauncherAmmo = 0;
					if (g_grappleCharges > 10)
						g_grappleCharges = 10;
					if (g_gadgetRespawnTimer > 0)
					{
						g_respawningGadgetPickup->position.y = g_respawningGadgetPickupY;
						g_gadgetRespawnTimer = 0;
						Nu3D::Link::SetScaleFromFixedOffsets(g_respawningGadgetPickup->linkId, 0x1000, 0x1000, 0x1000);
						int32_t gadgetX = g_respawningGadgetPickup->position.x;
						int32_t gadgetY = g_respawningGadgetPickup->position.y;
						int32_t gadgetZ = g_respawningGadgetPickup->position.z;
						int32_t cameraZ = (Camera::g_renderCameraTransform.pos.z - gadgetZ * 0x20) >> 8;
						int32_t cameraY = (Camera::g_renderCameraTransform.pos.y - gadgetY * 0x20) >> 8;
						int32_t cameraX = (Camera::g_renderCameraTransform.pos.x - gadgetX * 0x20) >> 8;
						int32_t cameraDistance = cameraZ * cameraZ;
						cameraDistance += cameraY * cameraY;
						cameraDistance += cameraX * cameraX;
						if (cameraDistance < 0x90000 && cameraDistance + 1 != 0)
							Particles::SpawnCollectSparkle(gadgetX * 0x20, gadgetY * 0x20, gadgetZ * 0x20, 0x32);
					}
					g_gadgetRespawnTimer = 400;
					g_respawningGadgetPickupY = pickup->position.y;
					g_respawningGadgetPickup = pickup;
					Nu3D::Link::SetScaleFromFixedOffsets(pickup->linkId, 0, 0, 0);
					break;
				}
				case 7:
				{
					g_discLauncherAmmo += 10;
					g_grappleCharges = 0;
					if (g_discLauncherAmmo > 30)
						g_discLauncherAmmo = 30;
					if (g_gadgetRespawnTimer > 0)
					{
						g_respawningGadgetPickup->position.y = g_respawningGadgetPickupY;
						g_gadgetRespawnTimer = 0;
						Nu3D::Link::SetScaleFromFixedOffsets(g_respawningGadgetPickup->linkId, 0x1000, 0x1000, 0x1000);
						int32_t gadgetX = g_respawningGadgetPickup->position.x;
						int32_t gadgetY = g_respawningGadgetPickup->position.y;
						int32_t gadgetZ = g_respawningGadgetPickup->position.z;
						int32_t cameraZ = (Camera::g_renderCameraTransform.pos.z - gadgetZ * 0x20) >> 8;
						int32_t cameraY = (Camera::g_renderCameraTransform.pos.y - gadgetY * 0x20) >> 8;
						int32_t cameraX = (Camera::g_renderCameraTransform.pos.x - gadgetX * 0x20) >> 8;
						int32_t cameraDistance = cameraZ * cameraZ;
						cameraDistance += cameraY * cameraY;
						cameraDistance += cameraX * cameraX;
						if (cameraDistance < 0x90000 && cameraDistance + 1 != 0)
							Particles::SpawnCollectSparkle(gadgetX * 0x20, gadgetY * 0x20, gadgetZ * 0x20, 0x32);
					}
					g_gadgetRespawnTimer = 400;
					g_respawningGadgetPickupY = pickup->position.y;
					g_respawningGadgetPickup = pickup;
					Nu3D::Link::SetScaleFromFixedOffsets(pickup->linkId, 0, 0, 0);
					break;
				}
				case 8:
					ActivateGravityBoots();
					if (g_gadgetRespawnTimer > 0)
					{
						g_respawningGadgetPickup->position.y = g_respawningGadgetPickupY;
						g_gadgetRespawnTimer = 0;
						Nu3D::Link::SetScaleFromFixedOffsets(g_respawningGadgetPickup->linkId, 0x1000, 0x1000, 0x1000);
						position.x = g_respawningGadgetPickup->position.x * 0x20;
						position.y = g_respawningGadgetPickup->position.y * 0x20;
						position.z = g_respawningGadgetPickup->position.z * 0x20;
						int32_t cameraZ = (Camera::g_renderCameraTransform.pos.z - position.z) >> 8;
						int32_t cameraY = (Camera::g_renderCameraTransform.pos.y - position.y) >> 8;
						int32_t cameraX = (Camera::g_renderCameraTransform.pos.x - position.x) >> 8;
						int32_t cameraDistance = cameraZ * cameraZ;
						cameraDistance += cameraY * cameraY;
						cameraDistance += cameraX * cameraX;
						if (cameraDistance < 0x90000 && cameraDistance + 1 != 0)
							Particles::SpawnCollectSparkle(position.x, position.y, position.z, 0x32);
					}
					g_gadgetRespawnTimer = 400;
					g_respawningGadgetPickupY = pickup->position.y;
					g_respawningGadgetPickup = pickup;
					Nu3D::Link::SetScaleFromFixedOffsets(pickup->linkId, 0, 0, 0);
					break;
				case 9:
					g_specialPickupCount++;
					break;
				case 10:
					g_poweredLaserCharge = 0x4B0;
					break;
				case 0x10:
					HUD::g_slideTimers[2] = 0xB4;
					if (g_buzzActor.coinsCollected < 99)
						g_buzzActor.coinsCollected++;
					if (g_buzzActor.coinsCollected == 50)
						AudioManager::PlaySoundEffect(0x4F, 0);
					break;
				case -1:
					Gadget::g_unlockNodeState = -Gadget::g_unlockNodeState;
					break;
				}

				pickupSound = -1;
				if (removalMode == 0)
					pickup->position.y = INT_MIN;
				else if (removalMode == 1)
				{
					Nu3D::Link::SetScaleFromFixedOffsets(pickup->linkId, 0, 0, 0);
					pickup->position.y = INT_MIN;
				}
			}

			if (pickupSound != 0)
				AudioManager::PlaySoundEffect(pickupSound, pickupPositions);

			for (int32_t tokenIndex = 0; tokenIndex < 5; tokenIndex++)
			{
				TokenState* token = &g_tokenStates[tokenIndex];
				int32_t oldTimer = token->timer;
				if (oldTimer == 0)
					continue;

				token->timer -= Renderer::g_frameDelta;
				if (token->timer <= 0)
					token->timer = 0;

				if (oldTimer > 100 && token->timer <= 100)
				{
					AudioManager::PlaySoundEffect(0x33, 0);
					Nu3D::Link::GetCurrentPosFixed(token->linkId, &position);
					for (int32_t particleIndex = 0; particleIndex < 16; particleIndex++)
					{
						Nu3D::Particles::SpawnInstance(position.x,
							position.y,
							position.z,
							g_tokenBurstVelocities[particleIndex][0] / 4,
							g_tokenBurstVelocities[particleIndex][1] / 4,
							g_tokenBurstVelocities[particleIndex][2] / 4,
							0x20,
							0,
							*g_randDatBufferPtr++ - 0x80,
							0x2B);
					}
				}

				int32_t scale;
				if (oldTimer < 0x58)
				{
					int32_t wave = Numerics::g_sinCosLUT[((oldTimer - 4) & 0xF) << 8] / 8;
					wave += 0x200;
					int32_t shift = 5 - oldTimer / 16;
					scale = (wave >> shift) + 0x1000;
				}
				else if (oldTimer <= 0x60)
				{
					scale = Numerics::g_sinCosLUT[(0x5F - oldTimer) << 7] / 4;
				}
				else
				{
					scale = 0;
				}
				Nu3D::Link::SetScaleFromFixedOffsets(token->linkId, scale, scale, scale);
			}

			if (g_tokenCollectionState == TOKEN_COLLECTION_STATE_IDLE)
				return;

			if (g_tokenCollectionState == TOKEN_COLLECTION_STATE_START)
			{
				ResetBuzzState();
				g_buzzActor.actorFlags |= Buzz::ACTOR_FLAG_LOCK_FACING;
				InputManager::g_directionInputState &= INPUT_SECRET_MENU | INPUT_MENU | INPUT_CAMERA_LEFT | INPUT_CAMERA_RIGHT;
				g_buzzActor.velocity.lateral = 0;
				g_buzzActor.velocity.forward = 0;
				g_tokenCollectionState = TOKEN_COLLECTION_STATE_WAIT_FOR_LANDING;
			}
			else if (g_tokenCollectionState != TOKEN_COLLECTION_STATE_WAIT_FOR_LANDING)
			{
				goto update_token_cutscene;
			}

			g_airborneTimer = 0;
			if (g_buzzActor.collisionFlags == 0 || g_buzzActor.velocity.lateral != 0 || g_buzzActor.velocity.forward != 0)
				return;

			Camera::BeginScriptedCutsceneOnBuzz(1000);
			if (g_exitLevelAfterToken != 0)
			{
				g_tokenCollectionState = TOKEN_COLLECTION_STATE_EXIT_LEVEL;
				g_levelTransition = 1;
				g_levelTransitionTimer = 1000;
				AudioManager::PlaySoundEffect(0xA9, 0);
			}
			else
			{
				g_tokenCollectionState = TOKEN_COLLECTION_STATE_CUTSCENE;
			}
			g_tokenPromptSelection = 0;

		update_token_cutscene:
			if (g_tokenCollectionState == TOKEN_COLLECTION_STATE_CUTSCENE)
			{
				Camera::g_cutsceneDuration = 1000;
				uint32_t angleDelta =
					(Camera::g_renderCameraTransform.rotation.euler.angles.yaw - g_buzzActor.posAngles.angles.yaw - 0x800) & 0xFFF;
				if (angleDelta > 0x7FF)
					angleDelta -= 0x1000;
				g_buzzActor.posAngles.angles.yaw =
					(g_buzzActor.posAngles.angles.yaw + Renderer::g_frameDelta * static_cast<int32_t>(angleDelta) / 16) & 0xFFF;
				g_buzzActor.facingAngle = g_buzzActor.posAngles.angles.yaw;
				return;
			}

			if (g_tokenCollectionState == TOKEN_COLLECTION_STATE_EXIT_LEVEL)
			{
				if (g_levelTransitionTimer < 0x37A)
				{
					Buzz::Launch(-0xC00, 5);
					g_tokenCollectionState = TOKEN_COLLECTION_STATE_LAUNCH;
					g_levelTransitionTimer = 0x44;
					AudioManager::PlaySoundEffect(0x33, &g_buzzActor.posAngles.pos);
					g_tokenLaunchVerticalVelocity = g_buzzActor.velocity.vertical;
				}
				uint32_t angleDelta =
					(Camera::g_renderCameraTransform.rotation.euler.angles.yaw - g_buzzActor.posAngles.angles.yaw - 0x800) & 0xFFF;
				if (angleDelta > 0x7FF)
					angleDelta -= 0x1000;
				g_buzzActor.posAngles.angles.yaw =
					(g_buzzActor.posAngles.angles.yaw + Renderer::g_frameDelta * static_cast<int32_t>(angleDelta) / 16) & 0xFFF;
				g_buzzActor.facingAngle = g_buzzActor.posAngles.angles.yaw;
			}

			if (g_tokenCollectionState == TOKEN_COLLECTION_STATE_LAUNCH)
			{
				Camera::g_cutsceneFocusPosition.x = g_buzzActor.posAngles.pos.x;
				Camera::g_cutsceneFocusPosition.y = g_buzzActor.posAngles.pos.y - 0x3000;
				Camera::g_cutsceneFocusPosition.z = g_buzzActor.posAngles.pos.z;
				g_tokenLaunchVerticalVelocity = g_buzzActor.velocity.vertical;
			}
		}

		// GLOBAL: TOY2 0x00830CAC
		int32_t g_exitLevelAfterToken;

		// GLOBAL: TOY2 0x00830CC4
		int32_t g_tokenCollectionState;

		// FUNCTION: TOY2 0x004A0C80 [PROVISIONAL]
		void Init(int16_t* tokenLinkIds, int32_t firstHiddenLinkId)
		{
			int32_t collectedTokens = SaveManager::g_save0Data.tokens[g_levelFileIndex];
			int32_t i = 0;
			g_tokenCollectionState = 0;

			if (tokenLinkIds == 0)
			{
				g_exitLevelAfterToken = 1;
				int32_t* active = &g_tokenStates[0].active;
				do
				{
					*active = 0;
					active += sizeof(TokenState) / sizeof(int32_t);
				} while (reinterpret_cast<int32_t>(active) < reinterpret_cast<int32_t>(&g_tokenStates[5].active));
				return;
			}

			g_exitLevelAfterToken = 0;
			TokenState* token = g_tokenStates;
			do
			{
				i = 0;
				if ((collectedTokens & 1) != 0)
					Nu3D::Link::CopyShapeId(*tokenLinkIds, firstHiddenLinkId);

				token->linkId = *tokenLinkIds++;
				token->active = 0;
				Nu3D::Link::SetScaleFromFixedOffsets(token->linkId, 0, 0, 0);

				Levels::RecordData* pickupRecords = Levels::g_recordData[63];
				PickupRecord* pickup = reinterpret_cast<PickupRecord*>(pickupRecords + 1);
				for (i = 0; i < pickupRecords->recordCount; i++, pickup++)
				{
					if (pickup->objectIndex == token->linkId)
					{
						pickup->position.y = INT_MIN;
						pickupRecords = Levels::g_recordData[63];
						token->verticalPosition = &pickup->position.y;
						break;
					}
				}

				collectedTokens >>= 1;
				for (i = 0; i < pickupRecords->recordCount; i++, pickup++)
				{
					if (pickup->objectIndex == firstHiddenLinkId)
					{
						pickup->position.y = INT_MIN;
						break;
					}
				}

				Nu3D::Link::SetScaleFromFixedOffsets(firstHiddenLinkId, 0, 0, 0);
				token++;
				firstHiddenLinkId++;
			} while (reinterpret_cast<int32_t>(token) < reinterpret_cast<int32_t>(&g_tokenStates[5]));
		}

		// GLOBAL: TOY2 0x00830CCC
		TokenState g_tokenStates[5];

		// GLOBAL: TOY2 0x0050A150
		TokenDialogueEntry g_tokenDialogueEntries[10];

		// GLOBAL: TOY2 0x004DF69C
		extern const int32_t g_tokenCutsceneScript[] = { 1, -1, 0, 4, 2, 3, -1, 10, 7, -1, 8, -1 };

		// GLOBAL: TOY2 0x005546B8
		PickupTable g_pickupTable;

		// GLOBAL: TOY2 0x00556FAC
		Levels::RecordData* g_originalPickupRecords;

		// FUNCTION: TOY2 0x00447DB0 [PROVISIONAL]
		void BuildPickupTable()
		{
			Vector3I position;
			PosAndAngles groundProbe;
			int32_t firstObjectIndex = 0x30;
			if (g_levelFileIndex == 4)
				firstObjectIndex = 0x60;
			else if (g_levelFileIndex == 5)
				firstObjectIndex = 0x50;
			else if (g_levelFileIndex == 10)
				firstObjectIndex = 0x60;
			else if (g_levelFileIndex == 11)
				firstObjectIndex = 0x60;
			else if (g_levelFileIndex == 14)
				firstObjectIndex = 0x60;
			else if (g_levelFileIndex == 16)
				firstObjectIndex = 400;

			g_originalPickupRecords = Levels::g_recordData[63];
			PickupRecord* destination = g_pickupTable.records;
			if (Levels::g_recordData[59] != 0)
			{
				for (int32_t i = 0; i < Levels::g_recordData[59]->recordCount; i++)
				{
					groundProbe.pos.x = Levels::g_recordData[59]->data[i].x << 5;
					groundProbe.pos.z = Levels::g_recordData[59]->data[i].z << 5;
					groundProbe.pos.y = (Levels::g_recordData[59]->data[i].y - 10) << 5;
					Levels::g_recordData[59]->data[i].y = Nu3D::Collision::GetGroundHeightEx(&groundProbe, 0, 0) >> 5;
				}
			}

			if (Levels::g_recordData[63] != 0)
			{
				PickupRecord* source = reinterpret_cast<PickupRecord*>(Levels::g_recordData[63] + 1);
				for (int32_t i = 0; i < Levels::g_recordData[63]->recordCount; i++)
				{
					groundProbe.pos.x = source->position.x << 5;
					groundProbe.pos.z = source->position.z << 5;
					groundProbe.pos.y = (source->position.y - 10) << 5;
					source->groundHeight = Nu3D::Collision::GetGroundHeightEx(&groundProbe, 0, 0) >> 5;
					if (source->groundHeight == 0x7FFF)
						source->groundHeight = 0x7FFE;
					if (source->groundHeight - source->position.y > 0x1800)
						source->groundHeight = 0x7FFF;
					source->facingAngle = 0x11;
					*destination++ = *source++;
				}
				g_pickupTable.recordType = g_originalPickupRecords->recordType;
				g_pickupTable.recordCount = g_originalPickupRecords->recordCount;
			}
			else
			{
				g_pickupTable.recordType = 0;
				g_pickupTable.recordCount = 0;
			}

			Levels::g_recordData[63] = reinterpret_cast<Levels::RecordData*>(&g_pickupTable);

			if (g_levelFileIndex == 5)
			{
				Levels::g_objectListBase->entries[89]->z = -18000;
				Nu3D::Link::GetCurrentPosFixed(89, &position);
				Nu3D::Link::SetPositionRawAndCommit(89, position.x >> 5, position.y >> 5, -18000);
			}

			if (Levels::g_objectListBase->count > 47 && firstObjectIndex <= Levels::g_objectListBase->count)
			{
				for (int32_t objectIndex = firstObjectIndex; objectIndex <= Levels::g_objectListBase->count; objectIndex++)
				{
					if (Levels::g_objectListBase->entries[objectIndex] != 0)
					{
						destination->position.x = Levels::g_objectListBase->entries[objectIndex]->x;
						destination->position.y = Levels::g_objectListBase->entries[objectIndex]->y;
						destination->position.z = Levels::g_objectListBase->entries[objectIndex]->z;
						destination->objectIndex = objectIndex;
						destination->facingAngle = Levels::g_objectListBase->entries[objectIndex]->facingAngle >> 3;

						groundProbe.pos.x = destination->position.x << 5;
						groundProbe.pos.z = destination->position.z << 5;
						groundProbe.pos.y = (destination->position.y - 10) << 5;
						destination->groundHeight = Nu3D::Collision::GetGroundHeightEx(&groundProbe, 0, 0) >> 5;
						if (destination->groundHeight == 0x7FFF)
							destination->groundHeight = 0x7FFE;
						if (destination->groundHeight - destination->position.y > 0x1800)
							destination->groundHeight = 0x7FFF;

						destination++;
						g_pickupTable.recordCount++;
					}
				}
			}
		}

		// FUNCTION: TOY2 0x004025C0 [PROVISIONAL]
		void LoadTokenTable(const TokenDialogueValue* values)
		{
			memset(g_tokenDialogueEntries, 0, sizeof(g_tokenDialogueEntries));

			TokenDialogueEntry* destination = g_tokenDialogueEntries;
			do
			{
				int32_t tokenId = values++->number;
				if (tokenId < 0)
					return;

				destination->tokenId = tokenId;
				destination->dialogueRecordIndex = values++->number;
				destination->subtitle = values++->text;
				destination->facingAngle = values++->number;
				destination++;
			} while (destination < &g_tokenDialogueEntries[10]);
		}

		// FUNCTION: TOY2 0x00402610 [TOOL]
		void Token(int32_t tokenId)
		{
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
				Camera::g_gameplayCamera.position.view.pos.y = cameraY;
				Camera::g_gameplayCamera.target.view.visorAimAngles.pitch = 0;
				Camera::g_gameplayCamera.position.view.lookAt.x = Camera::g_gameplayCamera.position.view.pos.x;
				Camera::g_gameplayCamera.state.fields.modeTransitionState = 0;
				Camera::g_scriptedCameraState = 0;

				Nu3D::Link::SetScaleFromFixedOffsets(0x2D, 0, 0, 0);
				Nu3D::Link::SetScaleFromFixedOffsets(0x2E, 0, 0, 0);
				Nu3D::Link::SetScaleFromFixedOffsets(0x2F, 0, 0, 0);
			}

			int32_t entryIndex = 0;
			for (; entryIndex < 10; entryIndex++)
			{
				if (g_tokenDialogueEntries[entryIndex].tokenId == tokenId)
					break;
			}

			g_gameplayStateFlags |= GAMEPLAY_STATE_CUTSCENE_ACTIVE;
			Nu3D::Camera::g_viewHistoryInitialized = 1;
			Camera::g_cutsceneElapsedTime = 0;
			Camera::g_cutsceneCommandCursor = g_tokenCutsceneScript;
			Camera::g_cutsceneWaitTimer = 0;
			Camera::g_cutsceneSegmentProgress = 0;
			Camera::g_cutsceneMoveSpeed = 0;
			Camera::g_nextCutsceneMoveSpeed = 0;
			Camera::g_cutsceneSegmentDuration = 0;
			Camera::g_cutsceneFocusPathPoint = 0;
			Camera::g_cutsceneCameraPathPoint = 0;
			Camera::g_cutsceneRecordType = g_tokenDialogueEntries[entryIndex].dialogueRecordIndex;

			char* subtitle = g_tokenDialogueEntries[entryIndex].subtitle;
			if (subtitle != 0)
			{
				Dialogue::WrapSubtitleText(subtitle);
				Dialogue::g_subtitleActive = 1;
				for (int32_t i = 0; i < 36; i++)
					Dialogue::g_subtitleCells.pairs[i] = 0x00200020;
				Dialogue::g_subtitleTextCursor = Dialogue::g_wrappedSubtitleText;
				Dialogue::g_subtitleColumn = 0;
				Dialogue::g_subtitleRowStart = 0;
				Dialogue::g_subtitleVisibleStart = -72;
				Dialogue::g_subtitleColour = 0x00808080;
				Dialogue::g_subtitleCharacterDelay = 2;
				Dialogue::g_subtitleCharacterStyle = 0;
				Dialogue::g_subtitleBoxScale = 0;
				Dialogue::g_subtitlePageState = 0;
				AudioManager::PlaySoundEffect(0x1D, 0);
			}
			else
			{
				Dialogue::g_subtitleActive = 0;
			}

			g_buzzActor.posAngles.angles.yaw = (uint16_t)g_tokenDialogueEntries[entryIndex].facingAngle;
			g_buzzActor.facingAngle = g_buzzActor.posAngles.angles.yaw;
			Camera::g_gameplayCamera.roll = g_buzzActor.posAngles.angles.yaw;
		}

		// FUNCTION: TOY2 0x004A0DB0 [MATCHED]
		void Activate(int32_t tokenIndex, int32_t skipCutscene)
		{
			if (g_tokenStates[tokenIndex].active != 0)
				return;

			g_tokenStates[tokenIndex].active = 1;
			int32_t* verticalPosition = g_tokenStates[tokenIndex].verticalPosition;
			PosAndAngles transform;
			Nu3D::Link::GetCurrentPosFixed(g_tokenStates[tokenIndex].linkId, &transform.pos);
			*verticalPosition = transform.pos.y >> 5;

			if (skipCutscene == 0)
			{
				g_tokenStates[tokenIndex].timer = 0x84;
				Camera::BeginScriptedCutsceneAtPoint(&transform.pos, 0xB4, 0x10);
				if (Camera::g_scriptedCameraState != 0)
					Camera::g_scriptedCameraState = 5;
			}
			else
			{
				Nu3D::Link::SetScaleFromFixedOffsets(g_tokenStates[tokenIndex].linkId, 0x1000, 0x1000, 0x1000);
			}
		}

		// FUNCTION: TOY2 0x004A0E60 [MATCHED]
		void Deactivate(int32_t tokenIndex)
		{
			g_tokenStates[tokenIndex].active = 0;
			*g_tokenStates[tokenIndex].verticalPosition = INT_MIN;
			Nu3D::Link::SetScaleFromFixedOffsets(g_tokenStates[tokenIndex].linkId, 0, 0, 0);
		}

		// FUNCTION: TOY2 0x004A50D0 [MATCHED]
		void CosmicShield(Buzz::GadgetPickup* pickup)
		{
			if (g_activeCosmicShieldPickup != 0)
			{
				g_activeCosmicShieldPickup->position.y = g_savedCosmicShieldPickupY;
				Nu3D::Link::SetScaleFromFixedOffsets(g_activeCosmicShieldPickup->linkId, 0x2000, 0x2000, 0x2000);
				Nu3D::Link::SetPositionRawAndCommit(g_activeCosmicShieldPickup->linkId,
					g_activeCosmicShieldPickup->position.x,
					g_activeCosmicShieldPickup->position.y,
					g_activeCosmicShieldPickup->position.z);
				g_activeCosmicShieldPickup = 0;
			}

			g_savedCosmicShieldPickupY = pickup->position.y;
			g_activeCosmicShieldPickup = pickup;
			g_buzzActor.cosmicShieldTimer = 900;
			Nu3D::Link::SetScaleFromFixedOffsets(pickup->linkId, 0x3000, 0x3000, 0x3000);
		}

		// FUNCTION: TOY2 0x004CD110 [MATCHED]
		int32_t ShowTokenSparkle(int32_t linkId) { return 1; }
	}
}
