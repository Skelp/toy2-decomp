#include "AudioManager/AudioManager.h"
#include "Logger.h"

namespace AudioManager
{
	// GLOBAL: TOY2 005282CC
	int32_t g_curTrackIndex;

	// GLOBAL: TOY2 0x00724E80
	int32_t g_audioInitialized;

	// GLOBAL: TOY2 0x00726F3C
	int32_t g_streamPending;

	// FUNCTION: TOY2 0x0047D840 [MATCHED]
	void StopAndFlush()
	{
		if (g_audioInitialized)
		{
			if (IsStreamActive())
			{
				StopAndWait();
				while (IsStreamActive()) {}
			}
			g_streamPending = 0;
		}
	}

	// STUB: TOY2 0x0047E850
	void ReleaseBuffers() {}

	// STUB: TOY2 0x0047EDE0
	void Init() {}

	// STUB: TOY2 0x0049E8D0
	void SetVolumesProcessed(int32_t musicVolume, int32_t sfxVolume) {}

	// FUNCTION: TOY2 0x004A3EF0 [MATCHED]
	void FlushSoundVoices()
	{
		Logger::Log("FlushSoundVoices : Start.\n");
		Logger::Log("FlushSoundVoices : End.\n");
	}

	// STUB: TOY2 0x00436D40
	int32_t StopAndWait() { return 1; }

	// STUB: TOY2 0x0047EC20
	void LoadSfxPackForLevel(int32_t levelId) {}

	// FUNCTION: TOY2 0x004A37E0 [MATCHED]
	int32_t PlayOneShotSoundGlobal(int32_t soundIndex, int32_t volume, int32_t leftVolume, int32_t rightVolume)
	{ return PlaySoundBuffer(soundIndex + 1, leftVolume, rightVolume, 0, volume, 0); }

	// FUNCTION: TOY2 0x0047D7F0 [MATCHED]
	void PlayMusicOneShot(int32_t trackIndex)
	{
		if (g_audioInitialized)
		{
			if (IsStreamActive())
			{
				StopAndWait();
				while (IsStreamActive()) {}
			}
			g_streamPending = 0;
			PlayTrackByIndex(trackIndex, 0);
		}
	}

	// STUB: TOY2 0x004A3BE0
	void UpdateChannels() {}

	// STUB: TOY2 0x00413300
	int32_t IsStreamActive() { return 0; }

	// STUB: TOY2 0x00413150
	void PlayTrackByIndex(int32_t trackIndex, int32_t fadeMode) {}

	// STUB: TOY2 0x0047DE50
	int32_t PlaySoundBuffer(int32_t soundIndex, int32_t leftVolume, int32_t rightVolume, int32_t pan, int32_t volume, int32_t flags) { return 0; }
}
