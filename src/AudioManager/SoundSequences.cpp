#include "AudioManager/AudioManager.h"
#include "FileUtils.h"
#include "Logger.h"
#include "Nu3D/Camera.h"
#include "Numerics.h"
#include "Random.h"
#include "Renderer/Renderer.h"
#include "SaveManager.h"
#include "Toy2/Win95.h"
#include "Toy2/Toy2.h"
#include <math.h>
#include <cstring>
#include <stdio.h>
#include <directx6/dsound.h>
#include "AudioManager/AudioManagerInternal.h"

// The sound effects and the sequences they run: retail holds 0x0049E660
// through 0x0049EA90 as one object, in the order of this file.
namespace AudioManager
{
	// FUNCTION: TOY2 0x0049E660 [PROVISIONAL]
	void PlaySoundEffect(int32_t soundIndex, const Vector3I* position)
	{
		if (soundIndex < 0)
		{
			int32_t slotIndex = g_soundSequenceSlotIndex;
			g_soundSequenceSlots[slotIndex].position.x = position->x;
			g_soundSequenceSlots[slotIndex].position.y = position->y;
			g_soundSequenceSlots[slotIndex].position.z = position->z;
			g_soundSequenceSlots[slotIndex].cursor = (uint8_t*)g_sequenceDataPtrs[-soundIndex - 1];
			g_soundSequenceSlots[slotIndex].timer = 0;

			SequenceHeader* header = (SequenceHeader*)g_soundSequenceSlots[slotIndex].cursor;
			g_soundSequenceSlots[slotIndex].cursor = (uint8_t*)(header + 1);
			if (header->leftVolume != 0 || header->rightVolume != 0)
			{
				if (g_maxLeftVolume < header->leftVolume * 2)
				{
					g_maxLeftVolume = header->leftVolume * 2;
				}
				if (g_maxRightVolume < header->rightVolume * 2)
				{
					g_maxRightVolume = header->rightVolume * 2;
				}
				if (g_maxVolume < header->volume * 2)
				{
					g_maxVolume = header->volume * 2;
				}
			}

			slotIndex++;
			g_soundSequenceSlotIndex = slotIndex;
			if (slotIndex >= 7)
			{
				g_soundSequenceSlotIndex = 0;
			}
			return;
		}

		OneShotSoundPreset& preset = g_oneShotPresets[soundIndex];
		int32_t audibility = 150;
		uint16_t encodedSoundIndex = preset.encodedSoundIndex;
		int16_t usesLevelSoundMapping = encodedSoundIndex & 0x4000;
		if (usesLevelSoundMapping != 0)
		{
			int16_t* mapping = &g_levelSoundMappings[0].levelFileIndex + (encodedSoundIndex & 0x3fff);
			while (*mapping != Toy2::g_levelFileIndex)
			{
				mapping += 2;
			}
			encodedSoundIndex = mapping[1];
		}

		int16_t selectedSoundIndex = (int16_t)encodedSoundIndex;
		if (selectedSoundIndex != 0)
		{
			int32_t frequency = preset.baseFrequency;
			if (frequency <= 0)
			{
				frequency = g_dynamicSoundFrequencies[-frequency];
			}
			else if (preset.randomFrequencyShift >= 0)
			{
				frequency += ((int32_t)*g_randDatBufferPtr << 3) >> preset.randomFrequencyShift;
				g_randDatBufferPtr++;
			}

			if (abs((int32_t)position) < 0x100)
			{
				int32_t rightVolume = preset.rightVolume;
				int32_t leftVolume = preset.leftVolume;
				if (selectedSoundIndex > 0)
				{
					PlayOneShotSoundGlobal(selectedSoundIndex - 1, frequency, leftVolume, rightVolume);
				}
				else
				{
					PlayLoopingSound3D((void*)(int32_t)preset.baseFrequency, (encodedSoundIndex & 0x7fff) - 1, frequency, leftVolume, rightVolume);
				}
			}
			else if (selectedSoundIndex > 0)
			{
				audibility = PlayOneShotSound3D(selectedSoundIndex - 1, frequency, preset.leftVolume, position);
			}
			else
			{
				audibility = PlayLoopingSound3DPositional(
					(void*)position, (encodedSoundIndex & 0x7fff) - 1, frequency, preset.leftVolume, (void*)position, preset.rightVolume);
			}
		}

		if (g_maxLeftVolume < preset.maxLeftVolume * 2)
		{
			g_maxLeftVolume = preset.maxLeftVolume * 2;
		}
		if (g_maxRightVolume < preset.maxRightVolume * 2)
		{
			g_maxRightVolume = preset.maxRightVolume * 2;
		}

		int32_t scaledVolume = preset.maxVolumeScale * audibility / 64;
		if (scaledVolume > 0xff)
		{
			scaledVolume = 0xff;
		}
		if (g_maxVolume < scaledVolume)
		{
			g_maxVolume = (int16_t)scaledVolume;
		}
	}

	// FUNCTION: TOY2 0x0049E8D0 [MATCHED]
	void SetVolumesProcessed(int32_t musicVolume, int32_t sfxVolume) { SetVolumes(g_musicVolTable[musicVolume] * 2 / 3, g_soundVolTable[sfxVolume] * 3 / 2); }

	// FUNCTION: TOY2 0x0049E910 [MATCHED]
	void StartSoundSequenceOnActor(int32_t sequenceId, Vector3I* position)
	{
		SoundSequenceSlot& slot = g_soundSequenceSlots[7];
		slot.position.x = position->x;
		slot.position.y = position->y;
		slot.position.z = position->z;
		slot.timer = 0;

		slot.cursor = (uint8_t*)g_sequenceDataPtrs[-sequenceId - 1];
		SequenceHeader* header = (SequenceHeader*)slot.cursor;
		slot.cursor += sizeof(SequenceHeader);

		if (header->leftVolume != 0 || header->rightVolume != 0)
		{
			if (g_maxLeftVolume < header->leftVolume * 2)
			{
				g_maxLeftVolume = header->leftVolume * 2;
			}
			if (g_maxRightVolume < header->rightVolume * 2)
			{
				g_maxRightVolume = header->rightVolume * 2;
			}
			if (g_maxVolume < header->volume * 2)
			{
				g_maxVolume = header->volume * 2;
			}
		}
	}

	// FUNCTION: TOY2 0x0049E9C0 [MATCHED]
	void ClearSequence7Cursor() { g_soundSequenceSlots[7].cursor = 0; }

	// FUNCTION: TOY2 0x0049E9D0 [PROVISIONAL]
	void UpdateSoundSequences()
	{
		for (SoundSequenceSlot* slot = g_soundSequenceSlots; slot < g_soundSequenceSlots + 8; slot++)
		{
			if (slot->cursor == NULL)
			{
				continue;
			}

			slot->timer -= Renderer::g_frameDelta;
			if (slot->timer >= 0)
			{
				continue;
			}

			SoundSequenceEvent* event = (SoundSequenceEvent*)slot->cursor;
			while (event->soundIndex < 0)
			{
				if (event->soundIndex == -1)
				{
					slot->cursor = NULL;
					break;
				}
				if (event->soundIndex != -2)
				{
					break;
				}

				event = (SoundSequenceEvent*)(slot->cursor - event->frequency * 2);
				slot->cursor = (uint8_t*)event;
			}

			if (event->soundIndex >= 0)
			{
				PlayOneShotSound3D(event->soundIndex - 1, event->frequency, event->volume, &slot->position);
				slot->timer = event->delay;
				slot->cursor += sizeof(SoundSequenceEvent);
			}
		}
	}

}

namespace AudioManager
{
	namespace Preset
	{
		// FUNCTION: TOY2 0x0049EA60 [MATCHED]
		void PlayOneShotSound2(int32_t index, void* actor)
		{
			OneShotSoundPreset& preset = g_oneShotPresets[index];
			PlayOneShotSound3DActor(actor, preset.encodedSoundIndex - 1, preset.baseFrequency, preset.leftVolume, actor, 0);
		}

		// FUNCTION: TOY2 0x0049EA90 [MATCHED]
		void PlayOneShotSound(int32_t index, void* actor)
		{
			OneShotSoundPreset& preset = g_oneShotPresets[index];
			PlayOneShotSound3DActor(actor, preset.encodedSoundIndex - 1, preset.baseFrequency, preset.leftVolume, actor, 1);
		}
	}
}
