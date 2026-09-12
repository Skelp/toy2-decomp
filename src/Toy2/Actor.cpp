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

namespace Toy2
{
	extern int32_t g_hudActorAnimationFrame;

	namespace Dialogue
	{
		// GLOBAL: TOY2 0x004DF684
		char g_continuePrompt[] = "press jump to continue";

		// GLOBAL: TOY2 0x004DF6CC
		CutsceneScript g_dialogueCutsceneScript = { 0, 0, 1, -1, 0, 1, 1, 1, 2, -1, 0, 2, 1, 0 };

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
				Camera::g_gameplayCamera.position.view.pos.y = cameraY;
				Camera::g_gameplayCamera.target.view.visorAimAngles.pitch = 0;
				Camera::g_gameplayCamera.position.view.lookAt.x = Camera::g_gameplayCamera.position.view.pos.x;
				Camera::g_gameplayCamera.state.fields.modeTransitionState = 0;
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
				g_subtitlePageState = SUBTITLE_PAGE_REVEALING;
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

	namespace Particles
	{
		// ActorParticles.cpp holds these.
		void SpawnCollectSparkle(int32_t x, int32_t y, int32_t z, int32_t particleSpread);
		void SpawnBurstRingAtPoint(Actor::Toy2Actor* actor, uint8_t red, uint8_t green, uint8_t blue, int32_t verticalOffset);
		void SpawnBurstRingAtActor(Actor::Toy2Actor* actor, uint8_t red, uint8_t green, uint8_t blue, int32_t verticalOffset);
	}
	namespace Lighting
	{
		// GLOBAL: TOY2 0x00503890
		BuzzLightPreset g_buzzLightPresets[15] = {
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
			int32_t effectCount = 0;
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

				switch (actor->creatureId)
				{
					case 3:
					case 0xE:
						actor->primaryAnimIdx = 2;
						actor->animationFrameSequence = g_animationFrameSequences[1 - 1];
						effectCount = 3;
						actor->hitpoints = -0x5E;
						actor->animationFramePosition = *actor->animationFrameSequence << 16;
						break;
					case 4:
						actor->primaryAnimIdx = 2;
						actor->animationFrameSequence = g_animationFrameSequences[7 - 1];
						effectCount = 3;
						actor->hitpoints = -0x3E;
						actor->animationFramePosition = *actor->animationFrameSequence << 16;
						break;
					case 0xF:
						actor->hitpoints = -1;
						Particles::SpawnBurstRingAtPoint(actor, 0x20, 0x20, 0x20, 0x2000);
						AudioManager::PlaySoundEffect(-2, &actor->pos);
						break;
					case 0x14:
						actor->primaryAnimIdx = 1;
						actor->animationFrameSequence = g_animationFrameSequences[0x14 - 1];
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
						actor->animationFrameSequence = g_animationFrameSequences[1 - 1];
						effectCount = 3;
						actor->hitpoints = -0x5E;
						actor->animationFramePosition = *actor->animationFrameSequence << 16;
						actor->respawnDelay = 10000;
						break;
					case 0x10:
					case 0x19:
						actor->hitpoints = -1;
						effectCount = 6;
						AudioManager::PlaySoundEffect(-2, &actor->pos);
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
						int32_t particleIndex = 0;
						if (particleCount > 0)
						{
							do
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
								particleIndex++;
							} while (particleIndex < particleCount);
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

		// GLOBAL: TOY2 0x0052F1D0
		Toy2Actor* g_activeActors[65];

		// GLOBAL: TOY2 0x0052c840
		Toy2Actor g_creatureActors[64];

		// GLOBAL: TOY2 0x004E0374
		uint8_t g_animationFrameData[536] = {
#include "Toy2/ActorAnimationFrameData.inc"
		};

		// GLOBAL: TOY2 0x004E058C
		uint8_t* g_animationFrameSequences[25] = {
			g_animationFrameData + 0x000,
			g_animationFrameData + 0x01C,
			g_animationFrameData + 0x02C,
			g_animationFrameData + 0x030,
			g_animationFrameData + 0x03C,
			g_animationFrameData + 0x048,
			g_animationFrameData + 0x050,
			g_animationFrameData + 0x064,
			g_animationFrameData + 0x088,
			g_animationFrameData + 0x0A4,
			g_animationFrameData + 0x0AC,
			g_animationFrameData + 0x0C0,
			g_animationFrameData + 0x0D0,
			g_animationFrameData + 0x0D8,
			g_animationFrameData + 0x0E0,
			g_animationFrameData + 0x0EC,
			g_animationFrameData + 0x100,
			g_animationFrameData + 0x10C,
			g_animationFrameData + 0x120,
			g_animationFrameData + 0x12C,
			g_animationFrameData + 0x140,
			g_animationFrameData + 0x154,
			g_animationFrameData + 0x17C,
			g_animationFrameData + 0x1C8,
			g_animationFrameData + 0x1FC,
		};

		// GLOBAL: TOY2 0x00830D40
		int32_t g_periodicHintSoundTimer;

		// GLOBAL: TOY2 0x00830C98
		int32_t g_coinQuestHintTimer;

		// GLOBAL: TOY2 0x00830CA4
		int32_t g_rotatingHintSoundTimer;

		// GLOBAL: TOY2 0x00830D1C
		int32_t g_coinTokenAwarded;

		// GLOBAL: TOY2 0x00830D24
		int32_t g_rotatingHintIndex;

		// GLOBAL: TOY2 0x00830D34
		int32_t g_itemReturnHintSoundTimer;

		// GLOBAL: TOY2 0x00529D48
		Toy2Actor* g_renderActors[64];

		// GLOBAL: TOY2 0x0050A54C
		Toy2Actor* g_lastKilledActor;

		// GLOBAL: TOY2 0x0052ADD8
		int32_t g_unusedActorStateBuffer[0x80];

		// GLOBAL: TOY2 0x0052EF48
		int32_t g_unusedActorState0;

		// GLOBAL: TOY2 0x0052EF88
		int32_t g_unusedActorState1;

		// FUNCTION: TOY2 0x00407150 [PROVISIONAL]
		void InitCreatureRam()
		{
			memset(g_creatureActors, 0, sizeof(g_creatureActors));
			memset(g_unusedActorStateBuffer, 0, sizeof(g_unusedActorStateBuffer));
			g_lastKilledActor = (Toy2Actor*)-1;
			g_activeActors[0] = 0;
			g_unusedActorState0 = 0;
			g_unusedActorState1 = 0;

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
			Animation::EvaluateClip(animationData->clips[actor->primaryAnimIdx].pointer, actor->animationFramePosition, animationData->baseBoneIndex, 0);
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
			actor->animationFrameSequence = g_animationFrameSequences[frameSequenceIndex - 1];
			actor->animationFramePosition = (uint32_t)*actor->animationFrameSequence << 16;
		}

		// FUNCTION: TOY2 0x0049F460 [MATCHED]
		int32_t IsInsideBounds(const Vector3I* position, int32_t minX, int32_t maxX, int32_t minZ, int32_t maxZ)
		{ return position->x > minX && position->x < maxX && position->z > minZ && position->z < maxZ; }

		// FUNCTION: TOY2 0x00414A80 [TOOL]
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
			} while (reinterpret_cast<int32_t>(creatureId) < reinterpret_cast<int32_t>(&g_creatureActors[64].creatureId));
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

	namespace Levels
	{
		// Record type of the pickup render records, as g_type63CullDistance also states.
		const int32_t RECORD_TYPE_PICKUPS = 63;
	}

	namespace Gadget
	{
	}

	namespace CreatureBehaviour
	{
		union BeamVector
		{
			Vector3I vector;
			Vector4I beam;
		};

		STATIC_ASSERT(sizeof(BeamVector) == 0x10);

		void RCCarLevel1(Actor::Toy2Actor::ActorBehaviourContext* context);
		void RCCarLevel2(Actor::Toy2Actor::ActorBehaviourContext* context);

		// FUNCTION: TOY2 0x00406220 [MATCHED]
		void Zurg3(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			int32_t localStrafeSpeed = context->localStrafeSpeed;
			if (localStrafeSpeed > 0x100)
				localStrafeSpeed = 0x100;
			else if (localStrafeSpeed < -0x100)
				localStrafeSpeed = -0x100;

			actor->rollAngle -= (int16_t)((actor->rollAngle + localStrafeSpeed) * Renderer::g_frameDelta / 16);

			if (actor->hitpoints >= 0)
				AudioManager::PlaySoundEffect(0x2C, &actor->pos);

			if ((context->targetFlags & 1) != 0)
			{
				int32_t previousAttackTimer = actor->previousActorPhase;
				actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
				if (actor->previousActorPhase < 0)
				{
					actor->previousActorPhase = 0x168;
					int32_t movementAngle = actor->yawAngle + 0x200;
					if (*g_randDatBufferPtr++ < 0x80)
						movementAngle -= 0x400;
					actor->velX = Numerics::g_sinCosLUT[movementAngle] >> 4;
					actor->velForward = Numerics::g_sinCosLUT[(movementAngle + 0x400) & 0xFFF] >> 4;
					return;
				}

				if (previousAttackTimer > 200 && actor->previousActorPhase <= 200)
				{
					actor->primaryAnimIdx = 1;
					actor->animationFrameSequence = Actor::g_animationFrameSequences[8 - 1];
					actor->animationFramePosition = *actor->animationFrameSequence << 16;
				}

				if (previousAttackTimer > 182 && actor->previousActorPhase <= 182)
				{
					Nu3D::Particles::SpawnInstance(actor->pos.x + (Numerics::g_sinCosLUT[(actor->yawAngle + 0x400) & 0xFFF] >> 2),
						actor->pos.y,
						actor->pos.z + (Numerics::g_sinCosLUT[(actor->yawAngle - 0x800) & 0xFFF] >> 2),
						Numerics::g_sinCosLUT[actor->yawAngle] >> 4,
						-0x200,
						Numerics::g_sinCosLUT[(actor->yawAngle + 0x400) & 0xFFF] >> 4,
						0x40,
						0,
						0x80,
						0x26);
					AudioManager::PlaySoundEffect(0xD, &actor->pos);
				}

				if (previousAttackTimer > 166 && actor->previousActorPhase <= 166)
				{
					Nu3D::Particles::SpawnInstance(actor->pos.x + (Numerics::g_sinCosLUT[(actor->yawAngle - 0x400) & 0xFFF] >> 2),
						actor->pos.y,
						actor->pos.z + (Numerics::g_sinCosLUT[actor->yawAngle & 0xFFF] >> 2),
						Numerics::g_sinCosLUT[actor->yawAngle] >> 4,
						-0x200,
						Numerics::g_sinCosLUT[(actor->yawAngle + 0x400) & 0xFFF] >> 4,
						0x40,
						0,
						0x80,
						0x26);
					AudioManager::PlaySoundEffect(0xD, &actor->pos);
				}

				if (previousAttackTimer > 136 && actor->previousActorPhase <= 136)
				{
					actor->primaryAnimIdx = 0;
					actor->animationFrameSequence = Actor::g_animationFrameSequences[7 - 1];
					actor->animationFramePosition = *actor->animationFrameSequence << 16;
				}
			}
			else
			{
				if (actor->primaryAnimIdx == 1)
				{
					actor->primaryAnimIdx = 0;
					actor->animationFrameSequence = Actor::g_animationFrameSequences[7 - 1];
					actor->animationFramePosition = *actor->animationFrameSequence << 16;
				}
				actor->previousActorPhase = 0x104;
			}
		}

		extern uint16_t* g_zgCarMovementData;

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
		// FUNCTION: TOY2 0x00406620 [PROVISIONAL]
		void ZPod(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			int32_t localStrafeSpeed = context->localStrafeSpeed;
			if (localStrafeSpeed > 0x200)
				localStrafeSpeed = 0x200;
			else if (localStrafeSpeed < -0x200)
				localStrafeSpeed = -0x200;

			actor->rollAngle -= (int16_t)((actor->rollAngle + localStrafeSpeed) * Renderer::g_frameDelta / 16);

			if (actor->hitpoints >= 0)
				AudioManager::PlaySoundEffect(0x5B, &actor->pos);

			if ((context->targetFlags & 1) != 0)
			{
				actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
				if (actor->previousActorPhase < 0)
				{
					actor->previousActorPhase = 0x168;
					int32_t movementAngle = actor->yawAngle + 0x200;
					if (*g_randDatBufferPtr++ < 0x80)
						movementAngle -= 0x400;
					actor->velX = Numerics::g_sinCosLUT[movementAngle] >> 4;
					actor->velForward = Numerics::g_sinCosLUT[(movementAngle + 0x400) & 0xFFF] >> 4;
				}

				if (actor->previousActorPhase <= 0x118 && actor->previousActorPhase >= 0x64)
				{
					BeamVector beamPosition;
					beamPosition.beam.x = 0;
					beamPosition.beam.y = 0;
					beamPosition.beam.z = -100;
					Actor::ResolveBoneAttachmentPos(&beamPosition.beam, actor, 0);

					BeamVector beamDirection;
					beamDirection.vector.y = 96000;
					beamDirection.vector.x = Numerics::g_sinCosLUT[(uint16_t)actor->yawAngle & 0xFFF] << 2;
					beamDirection.vector.z = Numerics::g_sinCosLUT[((uint16_t)actor->yawAngle + 0x400) & 0xFFF] << 2;
					Collision::SweepAndSlide(&beamPosition.vector, &beamDirection.vector, 0x8000, 0, 0x100);
					Renderer::Beam::QueueBeam(9, 0x20, 400, &beamPosition.beam, &beamDirection.beam, 0, 0x80, 0);

					beamPosition.vector.x += beamDirection.vector.x;
					beamPosition.vector.y += beamDirection.vector.y;
					beamPosition.vector.z += beamDirection.vector.z;
					AudioManager::PlaySoundEffect(0x5C, &beamPosition.vector);

					for (int32_t particleIndex = 0; particleIndex < g_framePulseOutputs.twoTickCount; particleIndex++)
						Nu3D::Particles::SpawnFromPreset(beamPosition.vector.x, beamPosition.vector.y, beamPosition.vector.z, 4, 4);

					if (g_framePulseOutputs.fourTick != 0)
						Nu3D::Particles::SpawnFromPreset(beamPosition.vector.x, beamPosition.vector.y, beamPosition.vector.z, 0x46, 2);

					if (g_framePulseOutputs.thirtyTwoTick != 0)
						Lighting::SpawnLight(beamPosition.vector.x, beamPosition.vector.y, beamPosition.vector.z, 0xC000, 0x10, -2);

					Renderer::LensFlare::RegisterLight(beamPosition.vector.x, beamPosition.vector.y, beamPosition.vector.z, 0, 0x80, 0, 0x20);
					if (Nu3D::Math::IsWithinDistance(&g_buzzActor.posAngles.pos, &beamPosition.vector, 0x19) != 0)
					{
						uint32_t direction = Nu3D::Math::CartesianToFixedAngle(
							g_buzzActor.posAngles.pos.x - beamPosition.vector.x, g_buzzActor.posAngles.pos.z - beamPosition.vector.z);
						Buzz::HandleDamage(direction, 3);
					}
				}
			}
			else
			{
				actor->previousActorPhase = 0x168;
			}
		}
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

		// GLOBAL: TOY2 0x004DF810
		uint16_t g_tinManMovementScript[] = {
			0x000D, 0x0000, 0x0001, 0x0016, 0xFFE0, 0xFFE0, 0x0011, 0x0004, 0xFFFF, 0x0002,
			0x000D, 0x0000, 0x0001, 0x0016, 0xFFE0, 0xFFE0, 0x0011, 0x0008, 0x0004, 0x0004,
			0xFFFF, 0x0004, 0x0016, 0xFFC0, 0xFFC0, 0x000D, 0x0001, 0x0009, 0x0001, 0x002C,
			0x0004, 0x000D, 0x0002, 0x0001, 0x0001, 0x017D, 0x0004, 0x000C, 0xFEF7, 0x0000,
			0x000D, 0x0003, 0x0009, 0x0001, 0x0040, 0x0004, 0x0016, 0xFFE0, 0xFFE0, 0x000D,
			0x0005, 0x000A, 0x0017, 0x0022, 0x0001, 0x0020, 0x0004, 0x000D, 0x0005, 0x000A,
			0x0017, 0x0022, 0x0001, 0x0020, 0x0004, 0x000D, 0x0005, 0x000A, 0x0017, 0x0022,
			0x0001, 0x0020, 0x0004, 0x000C, 0xFEFF, 0x0100, 0x0016, 0xFFC0, 0xFFC0, 0x000D,
			0x0004, 0x0009, 0x0001, 0x002C, 0x0004, 0x000C, 0xFFF7, 0x0008, 0xFFFF, 0x004E,
			0x0016, 0xFFE0, 0xFFE0, 0x000D, 0x0006, 0x0004, 0x0001, 0x0020, 0x0004, 0xFFFF,
			0x001A, 0x0016, 0xFFE0, 0xFFE0, 0x000D, 0x0007, 0x000B, 0x0004, 0xFFFF, 0x0001,
		};

		// GLOBAL: TOY2 0x004DF9C4
		uint16_t g_zgCarMovementScript[] = {
			0x000D, 0x0000, 0x000E, 0x001E, 0x0007, 0x0016, 0xFFE0, 0xFFE0, 0x0011, 0x0008,
			0x0005, 0x0005, 0x0400, 0xFFFF, 0xFFFD, 0x001B, 0x000C, 0xFFFB, 0x0004, 0x0014,
			0x0060, 0x001D, 0x000A, 0x0004, 0x0012, 0x0100, 0x0008, 0x0003, 0xFFFF, 0x0005,
			0x000C, 0xFFFB, 0x0000, 0x0001, 0x003C, 0x0004, 0x000C, 0xFFFB, 0x0004, 0x0014,
			0x00FF, 0x001D, 0x0002, 0x0011, 0x0008, 0x0005, 0x0005, 0x0400, 0xFFFF, 0xFFFD,
			0x001B, 0x0001, 0x003C, 0x0004, 0x000C, 0xFFFB, 0x0000, 0x0001, 0x001E, 0x0004,
			0xFFFF, 0x0034, 0x001E, 0x0004, 0x0016, 0xFFC0, 0xFFC0, 0x000D, 0x0001, 0x0007,
			0x000C, 0xFEFB, 0x0000, 0x0001, 0x001C, 0x0004, 0x000C, 0xFEFF, 0x0100, 0xFFFF,
			0x004F,
		};

		// GLOBAL: TOY2 0x004DFCBC
		uint16_t g_boxMovementScript[] = {
			0x0016, 0xFFE0, 0xFFE0, 0x000D, 0x0000, 0x0002, 0x0001, 0x001E, 0x0010, 0x0BB8,
			0x0008, 0x0025, 0x0010, 0x07D0, 0x0008, 0x0004, 0x0004, 0xFFFF, 0x0009, 0x0023,
			0x0002, 0x0019, 0x0001, 0x0001, 0x000A, 0x0004, 0x000D, 0x0002, 0x0004, 0x0001,
			0x0016, 0x0004, 0x000D, 0x0002, 0x0005, 0x0018, 0xFB00, 0x0017, 0x000E, 0x0007,
			0x000D, 0x0002, 0x0006, 0x0001, 0x000C, 0x0004, 0xFFFF, 0x002B, 0x0021, 0x0001,
			0x0004, 0xFFFF, 0x0001, 0x000D, 0x0001, 0x0001, 0x0001, 0x005C, 0x0004, 0x000D,
			0x0000, 0x0002, 0x0001, 0x005C, 0x0004, 0xFFFF, 0x0035, 0x0000,
		};

		// GLOBAL: TOY2 0x004DFDE4
		uint16_t g_dinoMovementScript[] = {
			0x0016, 0xFFE0, 0xFFE0, 0x000C, 0xFFFB, 0x0000, 0x000D, 0x0000, 0x0002, 0x001E,
			0x0004, 0x0004, 0xFFFF, 0x0001, 0x001E, 0x0007, 0x000D, 0x0000, 0x0002, 0x0001,
			0x003C, 0x0020, 0x0010, 0x03E8, 0x0008, 0x0011, 0x000C, 0xFFFB, 0x0004, 0x000D,
			0x0001, 0x0001, 0x001B, 0x0001, 0x000A, 0x0004, 0x0010, 0x03E8, 0x0008, 0x0003,
			0xFFFF, 0x0008, 0x000C, 0xFFFB, 0x0000, 0x000D, 0x0003, 0x0009, 0x0001, 0x0020,
			0x0020, 0x0021, 0x001E, 0x0001, 0x0038, 0x0020, 0xFFFF, 0x0028, 0x000C, 0xFFFB,
			0x0000, 0x0016, 0xFFE0, 0xFFE0, 0x000D, 0x0002, 0x000C, 0x0004, 0xFFFF, 0x0001,
		};

		// GLOBAL: TOY2 0x004DFEF4
		uint16_t g_clownMovementScript[] = {
			0x0016, 0xFFE0, 0xFFE0, 0x000C, 0xFFF3, 0x0000, 0x000D, 0x0000,
			0x0002, 0x001E, 0x0004, 0x0004, 0xFFFF, 0x0001, 0x001E, 0x0007,
			0x000C, 0xFFF3, 0x000C, 0x0004, 0xFFFF, 0x0001, 0x0016, 0xFFE0,
			0xFFE0, 0x000D, 0x0001, 0x0001, 0x0011, 0x0008, 0x0004, 0x0004,
			0xFFFF, 0x0004, 0x001F, 0x0000, 0x0019, 0x0001, 0x000D, 0x0000,
			0x0004, 0x0001, 0x0016, 0x0004, 0x000D, 0x0000, 0x0005, 0x0018,
			0xFA00, 0x0017, 0x005A, 0x0007, 0x000D, 0x0000, 0x0006, 0x0001,
			0x0016, 0x0004, 0x001F, 0xFF00, 0x0019, 0x0001, 0x000D, 0x0000,
			0x0004, 0x0001, 0x0016, 0x0004, 0x000D, 0x0000, 0x0005, 0x0018,
			0xFA00, 0x0017, 0x005A, 0x0007, 0x000D, 0x0000, 0x0006, 0x0001,
			0x0016, 0x0004, 0xFFFF, 0x0039, 0x000D, 0x0000, 0x0002, 0x0016,
			0xFFC0, 0xFFC0, 0x0004, 0xFFFF, 0x0001, 0x0000, 0x000D, 0x0002,
			0x0002, 0x0016, 0xFFD0, 0xFFD0, 0x0004, 0xFFFF, 0x0001, 0x0000,
			0x0016, 0xFFC0, 0xFFC0, 0x000D, 0x0000, 0x0002, 0x0005, 0x0400,
			0x0002, 0x003F, 0x0040, 0x0004, 0xFFFF, 0x0006,
		};

		// GLOBAL: TOY2 0x004DFFE0
		uint16_t g_gunslingerMovementScript[] = {
			0x0016, 0xFFD0, 0xFFD0, 0x000C, 0xFFF3, 0x0000, 0x000D, 0x0003, 0x0002, 0x001E,
			0x0004, 0x0004, 0xFFFF, 0x0001, 0x001E, 0x0007, 0x000C, 0xFFFB, 0x0004, 0x000D,
			0x0000, 0x0001, 0x001B, 0x0001, 0x0021, 0x0004, 0x001B, 0x0001, 0x0021, 0x0004,
			0x0010, 0x0FA0, 0x0008, 0x0003, 0xFFFF, 0x000F, 0x000C, 0xFFFB, 0x0000, 0x000D,
			0x0001, 0x0008, 0x0001, 0x0015, 0x0020, 0x0021, 0x001E, 0x0001, 0x0033, 0x0020,
			0xFFFF, 0x0022, 0x000C, 0xFFFB, 0x0000, 0x0016, 0xFFE0, 0xFFE0, 0x000D, 0x0002,
			0x0015, 0x0004, 0xFFFF, 0x0001, 0x0016, 0xFFD0, 0xFFD0, 0x000D, 0x0000, 0x0001,
			0x000C, 0xFFFB, 0x0004, 0x0005, 0x0200, 0x0002, 0x003F, 0x0080, 0x0004, 0x000C,
			0xFFFB, 0x0000, 0x001B, 0x000D, 0x0001, 0x0016, 0x0001, 0x0027, 0x0020, 0x0021,
			0x0001, 0x0001, 0x003F, 0x0020, 0x0011, 0x0008, 0x0003, 0xFFFF, 0x001E, 0xFFFF,
			0x0011, 0x0000,
		};

		// GLOBAL: TOY2 0x004E00AC
		uint16_t g_buggyMovementScript[] = {
			0x0016, 0xFFC0, 0xFFC0, 0x000C, 0xFFF3, 0x0000, 0x000D, 0x0001, 0x0001, 0x001E,
			0x0004, 0x0004, 0xFFFF, 0x0001, 0x0001, 0x003C, 0x0004, 0x001E, 0x0007, 0x000C,
			0xFFFB, 0x0004, 0x0016, 0x0040, 0xFFC0, 0x000D, 0x0000, 0x0002, 0x001B, 0x0001,
			0x0042, 0x0004, 0xFFFF, 0x0004, 0x000C, 0xFFFB, 0x0000, 0x0016, 0xFFE0, 0xFFE0,
			0x000D, 0x0002, 0x0002, 0x0004, 0xFFFF, 0x0001,
		};

		// GLOBAL: TOY2 0x004E0178
		uint16_t g_smithMovementScript[] = {
			0x0016, 0xFFE0, 0xFFE0, 0x000C, 0xFFF3, 0x0000, 0x000D, 0x0000,
			0x0001, 0x001E, 0x0004, 0x0004, 0xFFFF, 0x0001, 0x001E, 0x0006,
			0x0016, 0xFFE0, 0xFFE0, 0x000C, 0xFFF3, 0x0000, 0x000D, 0x0000,
			0x0001, 0x0001, 0x0090, 0x0004, 0x0016, 0xFFC0, 0xFFC0, 0x000C,
			0xFFF3, 0x000C, 0x000D, 0x0001, 0x0001, 0x0005, 0x0080, 0x0002,
			0x003F, 0x0040, 0x0004, 0xFFFF, 0x0006, 0x000C, 0xFFF3, 0x0000,
			0x0016, 0xFFE0, 0xFFE0, 0x000D, 0x0002, 0x0009, 0x0004, 0xFFFF,
			0x0001, 0x0000, 0x0016, 0xFFE0, 0xFFE0, 0x0005, 0x0000, 0x0019,
			0x0001, 0x000D, 0x0000, 0x0004, 0x0001, 0x0016, 0x0004, 0x000D,
			0x0000, 0x0005, 0x0018, 0xFB00, 0x0017, 0x000E, 0x0007, 0x000D,
			0x0000, 0x0006, 0x0001, 0x0014, 0x0004, 0xFFFF, 0x0018, 0x0000,
		};

		// GLOBAL: TOY2 0x004E0228
		uint16_t g_prospectorMovementScript[] = {
			0x0016, 0xFFE0, 0xFFE0, 0x000C, 0xFFF3, 0x0000, 0x000D, 0x0000,
			0x0002, 0x001E, 0x0004, 0x0004, 0xFFFF, 0x0001, 0x001E, 0x0006,
			0x0016, 0xFFE0, 0xFFE0, 0x000C, 0xFFF3, 0x0000, 0x000D, 0x0000,
			0x0002, 0x0001, 0x0018, 0x0004, 0x0016, 0xFFC0, 0xFFC0, 0x000C,
			0xFFF3, 0x000C, 0x000D, 0x0004, 0x0001, 0x0005, 0x0080, 0x0002,
			0x003F, 0x0040, 0x0004, 0xFFFF, 0x0006, 0x000C, 0xFFF3, 0x0000,
			0x0016, 0xFFE0, 0xFFE0, 0x000D, 0x0001, 0x0015, 0x0004, 0xFFFF,
			0x0001, 0x0000, 0x000D, 0x0000, 0x0010, 0x0016, 0xFFE0, 0xFFE0,
			0x0004, 0xFFFF, 0x0001, 0x0000, 0x000D, 0x0000, 0x0001, 0x0016,
			0xFFC0, 0xFFC0, 0x0004, 0xFFFF, 0x0001, 0x0000,
		};

		// GLOBAL: TOY2 0x004E02D8
		uint16_t* g_tinManMovementData = g_tinManMovementScript;
		// GLOBAL: TOY2 0x004E02F4
		uint16_t* g_zgCarMovementData = g_zgCarMovementScript;
		// GLOBAL: TOY2 0x004E0318
		uint16_t* g_boxMovementData = g_boxMovementScript;
		// GLOBAL: TOY2 0x004E0324
		uint16_t* g_dinoMovementData = g_dinoMovementScript;
		// GLOBAL: TOY2 0x004E0334
		uint16_t* g_clownMovementData = g_clownMovementScript;
		// GLOBAL: TOY2 0x004E0348
		uint16_t* g_gunslingerMovementData = g_gunslingerMovementScript;
		// GLOBAL: TOY2 0x004E0350
		uint16_t* g_buggyMovementData = g_buggyMovementScript;
		// GLOBAL: TOY2 0x004E0360
		uint16_t* g_smithMovementData = g_smithMovementScript;
		// GLOBAL: TOY2 0x004E0368
		uint16_t* g_prospectorMovementData = g_prospectorMovementScript;

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

			Nu3D::Math::SetRotationXYZ(&Animation::g_keyframeRotation.angles, &transform->rotation);
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
