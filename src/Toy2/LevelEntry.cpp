#include "Toy2/Toy2.h"
#include "Toy2/Toy2Internal.h"
#include "Toy2/Actor.h"
#include "Toy2/Animation.h"
#include "Toy2/Buzz.h"
#include "Toy2/Camera.h"
#include "Toy2/Collectables.h"
#include "Toy2/Collision.h"
#include "Toy2/Direct6.h"
#include "Toy2/Gadget.h"
#include "Toy2/Ini.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/LevelSelect.h"
#include "Toy2/Levels.h"
#include "Toy2/PoleRecord.h"
#include "Toy2/MainMenu.h"
#include "Toy2/Screens.h"
#include "Toy2/Weather.h"
#include "SoftwareRenderer.h"
#include "DrawingDevice.h"
#include "InputManager.h"
#include "SaveManager.h"
#include "Random.h"
#include "Nullsub.h"
#include "Logger.h"
#include "Renderer/Renderer.h"
#include "Renderer/Sprite.h"
#include "Renderer/SpriteSheets.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Font.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "AudioManager/AudioManager.h"
#include "NGNLoader/NGNLoader.h"
#include <STRING.H>

// Entering a level: EnterLevel resets the per-level state and loads the level,
// LoadLevelWithFadeIn brings it up behind a fade, ShowLevelIntroScreen holds the
// title card, and FixupRecordCoordinates rewrites the record coordinates the
// loader read.
//
// Retail run 0x00414270-0x00414720, between the Buzz.cpp and Actor.cpp runs.

namespace Toy2
{
	namespace Level
	{
		enum RecordType
		{
			RECORD_AMBIENT_EMITTER = 58,
			RECORD_SWING = 60,
			RECORD_POLE = 61,
			RECORD_ZIPLINE = 62
		};
	}

	// GLOBAL: TOY2 0x0050A0B0
	int32_t g_idleVoicePreset;

	// GLOBAL: TOY2 0x0050A0BC
	int32_t g_idleVoiceCooldown;

	// GLOBAL: TOY2 0x0052AD6A
	int16_t g_unusedLevelState[9];

	// GLOBAL: TOY2 0x00830C90
	int32_t g_gadgetRespawnTimer;

	// GLOBAL: TOY2 0x00830D20
	uint8_t g_environmentTintGreen;

	// GLOBAL: TOY2 0x00830D2C
	uint8_t g_environmentTintBlue;

	// GLOBAL: TOY2 0x00830D2D
	uint8_t g_environmentTintRed;

	// GLOBAL: TOY2 0x00830D44
	int32_t g_levelInteractionTimer;

	// GLOBAL: TOY2 0x00830E24
	uint32_t g_savedUnlocks;

	// GLOBAL: TOY2 0x00830E2C
	Nu3D::Particles::ParticleInstance* g_laserAimParticle;

	// FUNCTION: TOY2 0x00414270 [MATCHED]
	void LoadLevelWithFadeIn(int32_t levelFileIndex, int32_t displayMode)
	{
		if (displayMode != 0 && displayMode != 123)
		{
			LoadLevelGraphics(0);
		}
		else
		{
			LoadLevelGraphics(levelFileIndex);
		}

		SoftwareRenderer::g_backdropScrollOverride.x = 0;
		SoftwareRenderer::g_backdropScrollOverride.y = 0;
		Nu3D::Camera::SetTint(128, 128, 128, 12);

		int32_t fadeTimer = 28;
		do
		{
			fadeTimer -= Renderer::g_frameDelta;
			if (fadeTimer <= 0)
			{
				fadeTimer = 0;
			}
			Nu3D::Camera::FadeToTargetTint();
			if (displayMode == 0)
			{
				Renderer::Sprite::DrawScaled(96, 200, 128, 1, 255, 255, 255, 255, 2048, 2048);
			}
			Nullsub3();
			MainMenu::RenderMenu();
		} while (fadeTimer != 0);
	}

	// FUNCTION: TOY2 0x00414320 [MATCHED]
	int32_t ShowLevelIntroScreen(int32_t backgroundId, int32_t displayMode)
	{
		int32_t result = 0;
		int32_t autoAdvanceTimer;
		int32_t fadeTimer;
		int32_t isFadingOut;
		int32_t slidePhase;
		int32_t blinkTimer;

		if (displayMode != 0 && displayMode != 123)
		{
			Nu3D::Camera::SetTint(0, 0, 0, 12);
			Nu3D::Camera::g_cameraTintRed = Nu3D::Camera::g_targetTintRed;
			Nu3D::Camera::g_cameraTintGreen = Nu3D::Camera::g_targetTintGreen;
			Nu3D::Camera::g_cameraTintBlue = Nu3D::Camera::g_targetTintBlue;
			Nu3D::Camera::g_targetTintFadeSpeed = 0;
			goto cleanup;
		}

		if (g_attractModeTimer >= 0)
		{
			autoAdvanceTimer = g_attractModeTimer * 2;
		}
		else
		{
			autoAdvanceTimer = -1;
		}

		if (displayMode != 0 && displayMode != 123)
		{
			Nu3D::Camera::SetTint(0, 0, 0, 12);
			slidePhase = 0x400;
			isFadingOut = 1;
		}
		else
		{
			isFadingOut = 0;
			slidePhase = 0;
		}

		blinkTimer = 0;
		SoftwareRenderer::g_backdropScrollOverride.x = 0;
		SoftwareRenderer::g_backdropScrollOverride.y = 0;
		fadeTimer = 28;

		do
		{
			if (isFadingOut)
			{
				fadeTimer -= Renderer::g_frameDelta;
				if (fadeTimer <= 0)
				{
					fadeTimer = 0;
				}
			}

			if (autoAdvanceTimer > 0)
			{
				autoAdvanceTimer -= Renderer::g_frameDelta;
				if (autoAdvanceTimer <= 0)
				{
					InputManager::g_curButtonsPressed |= INPUT_SECRET_MENU;
					autoAdvanceTimer = 0;
				}
			}

			if ((InputManager::g_curButtonsPressed & INPUT_JUMP) != 0 && (InputManager::g_prevButtonsPressed & INPUT_JUMP) == 0 && ! isFadingOut)
			{
				Nu3D::Camera::SetTint(0, 0, 0, 12);
				isFadingOut = 1;
				AudioManager::PlaySoundEffect(7, 0);
			}

			if ((InputManager::g_curButtonsPressed & INPUT_SECRET_MENU) != 0 && ! isFadingOut && g_attractModeTimer >= 0)
			{
				Nu3D::Camera::SetTint(0, 0, 0, 12);
				isFadingOut = 1;
				result = 1;
			}

			Nu3D::Camera::FadeToTargetTint();
			blinkTimer = (blinkTimer + Renderer::g_frameDelta) & 0x3f;
			if (blinkTimer > 30)
			{
				Renderer::Sprite::DrawScaled(96, 200, 128, 0, 255, 255, 255, 255, 2048, 2048);
			}

			if (slidePhase < 0x400 && displayMode != 123)
			{
				slidePhase += Renderer::g_frameDelta * 32;
				Renderer::Sprite::DrawScaled(96, 264 - (Numerics::g_sinCosLUT[slidePhase + 0x400] >> 8), 128, 1, 255, 255, 255, 255, 2048, 2048);
			}

			Nullsub3();
			MainMenu::RenderMenu();
		} while (fadeTimer != 0);

	cleanup:
		ResetBackdropState();
		SoftwareRenderer::g_backdropScrollOverride.x = -32768;
		SoftwareRenderer::g_backdropScrollOverride.y = -32768;
		return result;
	}

	namespace Level
	{
		// FUNCTION: TOY2 0x00414550 [PROVISIONAL]
		void FixupRecordCoordinates()
		{
			Levels::g_alternateAmbientEmitterStart = 0;
			Levels::g_ambientEmitterScanIndex = 0;

			Levels::RecordData* ambientEmitters = Levels::g_recordData[RECORD_AMBIENT_EMITTER];
			if (ambientEmitters != 0)
			{
				Vector3I* output = ambientEmitters->data;
				Vector3I* source = output;
				int32_t emitterIndex = 0;
				int32_t emitterCount = ambientEmitters->recordCount;
				while (emitterIndex < emitterCount)
				{
					if (output->x == 0 && output->y == 0 && output->z == 0)
					{
						Levels::g_alternateAmbientEmitterStart = emitterIndex;
						++source;
						--Levels::g_recordData[RECORD_AMBIENT_EMITTER]->recordCount;
						++emitterIndex;
					}

					output->x = source->x << 5;
					output->y = source->y << 5;
					output->z = source->z << 5;
					++output;
					++source;
					++emitterIndex;
				}
			}

			Levels::RecordData* coordinateRecords = Levels::g_recordData[RECORD_SWING];
			if (coordinateRecords != 0)
			{
				int32_t* coordinate = &coordinateRecords->data->x;
				for (int32_t i = 0; i < Levels::g_recordData[RECORD_SWING]->recordCount * 3; ++i)
				{
					*coordinate <<= 5;
					++coordinate;
				}
			}

			Levels::RecordData* pairedRecords = Levels::g_recordData[RECORD_POLE];
			if (pairedRecords != 0)
			{
				Vector3I* source = pairedRecords->data;
				PoleRecord* output = reinterpret_cast<PoleRecord*>(source);
				int32_t poleType = 0;
				int32_t outputCount = pairedRecords->recordCount;

				for (int32_t pairIndex = 0; pairIndex < (int32_t)((uint32_t)Levels::g_recordData[RECORD_POLE]->recordCount >> 1); ++pairIndex)
				{
					while (abs(source->x) == abs(source->y) && abs(source->y) == abs(source->z))
					{
						poleType = abs(source->x) / 50;
						++source;
						--outputCount;
					}

					output->position.x = source[0].x << 5;
					output->position.y = source[0].y << 5;
					output->position.z = source[0].z << 5;
					output->type = poleType;
					output->height = output->position.y - (source[1].y << 5);
					source += 2;
					++output;
				}

				Levels::g_recordData[RECORD_POLE]->recordCount = (uint16_t)outputCount;
			}

			coordinateRecords = Levels::g_recordData[RECORD_ZIPLINE];
			if (coordinateRecords != 0)
			{
				int32_t* coordinate = &coordinateRecords->data->x;
				for (int32_t i = 0; i < Levels::g_recordData[RECORD_ZIPLINE]->recordCount * 3; ++i)
				{
					*coordinate <<= 5;
					++coordinate;
				}
			}
		}

	}
	// FUNCTION: TOY2 0x00414720 [PROVISIONAL]
	int32_t EnterLevel(int32_t levelIndex)
	{
		Nu3D::Camera::g_cameraTintRed = 0;
		Nu3D::Camera::g_cameraTintGreen = 0;
		Nu3D::Camera::g_cameraTintBlue = 0;
		Renderer::SetVirtualRatioTo54();

		int32_t demoMode = g_demoMode;
		if (demoMode == 0 || demoMode == 123)
			LoadLevelGraphics(g_levelFileIndex);
		else
			LoadLevelGraphics(0);
		SoftwareRenderer::g_backdropScrollOverride.x = 0;
		SoftwareRenderer::g_backdropScrollOverride.y = 0;

		Nu3D::Camera::SetTint(0x80, 0x80, 0x80, 12);
		int32_t fadeTimer = 28;
		do
		{
			fadeTimer -= Renderer::g_frameDelta;
			if (fadeTimer <= 0)
				fadeTimer = 0;
			Nu3D::Camera::FadeToTargetTint();
			if (demoMode == 0)
				Renderer::Sprite::DrawScaled(0x60, 200, 0x80, 1, 0xFF, 0xFF, 0xFF, 0xFF, 0x800, 0x800);
			Nullsub3();
			MainMenu::RenderMenu();
		} while (fadeTimer != 0);

		Levels::InitLevelPlay(levelIndex);
		int32_t introResult = ShowLevelIntroScreen(g_levelFileIndex, g_demoMode);
		Level::FixupRecordCoordinates();

		g_framePulseOutputs.words[0] = 0;
		g_framePulseOutputs.words[1] = 0;
		g_framePulseOutputs.words[2] = 0;
		g_framePulsePhases.words[0] = 0;

		memset(AudioManager::g_soundSequenceSlots, 0, sizeof(AudioManager::g_soundSequenceSlots));
		g_framePulsePhases.words[1] = 0;

		HUD::g_slideAngles[0] = 0x400;
		HUD::g_slideAngles[1] = 0x400;
		HUD::g_slideAngles[2] = 0x400;
		g_levelTransition = 0;
		g_levelTransitionTimer = 0;
		g_isPaused = 0;
		g_pauseMenuBlinkTimer = 0;
		g_pauseMenuSelection = 0;
		g_pauseMenuState = 0;
		AudioManager::g_maxLeftVolume = 0;
		AudioManager::g_maxRightVolume = 0;
		AudioManager::g_maxVolume = 0;
		Nu3D::Camera::g_targetTintRed = 0;
		Nu3D::Camera::g_targetTintGreen = 0;
		Nu3D::Camera::g_targetTintBlue = 0;
		Nu3D::Camera::g_targetTintFadeSpeed = 0;
		g_randDatBufferPtr = g_randDatBuffer;
		g_hudActorAnimationFrame = 54;
		g_framePulsePhases.words[2] = 0;

		HUD::g_slideAngles[3] = 0;
		HUD::g_slideAngles[4] = 0;
		HUD::g_slideAngles[5] = 0;
		HUD::g_slideAngles[6] = 0;
		HUD::g_slideAngles[7] = 0;
		HUD::g_slideAngles[8] = 0;
		HUD::g_slideAngles[9] = 0;
		HUD::g_slideAngles[10] = 0;

		HUD::g_slideTimers[0] = 180;
		HUD::g_slideTimers[1] = 180;
		HUD::g_slideTimers[2] = 180;
		HUD::g_slideTimers[3] = 0;
		HUD::g_slideTimers[4] = 0;
		HUD::g_slideTimers[5] = 0;
		HUD::g_slideTimers[6] = 0;
		HUD::g_slideTimers[7] = 0;
		HUD::g_slideTimers[8] = 0;
		HUD::g_slideTimers[9] = 0;
		HUD::g_slideTimers[10] = 0;

		Buzz::Init(&g_buzzActor, levelIndex);
		Lighting::InitBuzzLight();
		Camera::InitGameplayCamera(&Camera::g_gameplayCamera, &g_buzzActor);
		Nu3D::Particles::Init();
		ResetGadgets();

		Actor::g_renderActors[0] = 0;
		g_levelObjectiveProgress = 0;
		g_previousLevelObjectiveProgress = 0;
		g_specialPickupCount = -1;
		g_idleVoiceCooldown = 900;
		g_idleVoicePreset = 206;
		g_environmentTintRed = 0x80;
		g_environmentTintGreen = 0x80;
		g_environmentTintBlue = 0x80;
		HUD::g_challengeState = 0;
		AndysHouse::g_raceCheckpointPassCount = 0;

		InitialiseLevelVariables(levelIndex);
		Gadget::InitLevelUnlockGeometry();

		SaveManager::g_curLevelTokenData = SaveManager::g_save0Data.tokens[g_levelFileIndex];
		g_savedUnlocks = SaveManager::g_save0Data.unlocks;
		g_framePhase = 0;
		g_unusedLevelState[0] = 0;
		g_unusedLevelState[1] = 0;
		g_unusedLevelState[2] = 0;
		g_unusedLevelState[3] = 0;
		g_unusedLevelState[4] = 0;
		g_unusedLevelState[5] = 0;
		g_unusedLevelState[6] = 0;
		g_unusedLevelState[7] = 0;
		g_unusedLevelState[8] = 0;
		g_levelInteractionTimer = 0;
		g_cameraIdleTimer = 0;
		g_gadgetRespawnTimer = 0;
		g_laserAimParticle = (Nu3D::Particles::ParticleInstance*)-1;

		if (g_levelFileIndex > 0 && g_levelFileIndex < 16 && g_levelIndex < 15 && g_levelFileIndex % 3 != 0)
			AudioManager::PlayMusicLooping(g_levelIndex);

		return introResult;
	}
}
