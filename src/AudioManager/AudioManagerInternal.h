#ifndef AUDIOMANAGERINTERNAL_H
#define AUDIOMANAGERINTERNAL_H

#include "AudioManager/AudioManager.h"

#include <mmsystem.h>
#include <dsound.h>

// State and types the objects of the audio manager share but that no public
// header states. AudioManager.cpp holds the definitions; the objects split out
// of it need the same view.
namespace AudioManager
{
	enum
	{
		WAVE_NAME_LENGTH = 256,
		SOUND_BUFFER_COUNT = 768,
		LOOPING_SOUND_CHANNEL_COUNT = 32,
		MAX_SFX_LEVEL_ID = 16,
		SOUND_VOLUME_MAX = 128,
		SFX_VOLUME_SCALE = 256,
	};

	// Header at the start of each sequence-data blob: three peak-volume
	// fields that the engine doubles into the 8-bit DirectSound range.
	struct SequenceHeader
	{
		int16_t leftVolume; // +0x00
		int16_t rightVolume; // +0x02
		int16_t volume; // +0x04
	};

	// The sound each level file selects.
	struct LevelSoundMapping
	{
		int16_t levelFileIndex;
		int16_t soundIndex;
	};

	STATIC_ASSERT(sizeof(LevelSoundMapping) == 0x4);

	extern int32_t g_audioInitialized;
	extern LPGUID g_deviceGuids[16];
	extern LevelSoundMapping g_levelSoundMappings[26];
	extern int16_t* g_sequenceDataPtrs[6];
	extern const char* g_trackNames[22];

	// The streamed audio buffer and the cursor the play thread walks.
	extern int32_t g_streamPending;
	extern LPDIRECTSOUNDBUFFER g_streamBufferAlias;
	extern DSBPOSITIONNOTIFY g_streamNotifications[17];
	extern MMCKINFO g_streamDataChunk;
	extern MMCKINFO g_streamParentChunk;
	extern uint32_t g_streamBufferBytes;
	extern uint32_t g_streamFillBytes;
	extern uint32_t g_streamWriteOffset;
	extern uint32_t g_streamBytesPlayed;
	extern uint32_t g_streamLastPlayCursor;
	extern int32_t g_streamReachedEnd;
}

#endif
