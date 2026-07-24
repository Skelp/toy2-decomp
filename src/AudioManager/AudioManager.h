#pragma once

#include "Common.h"

namespace AudioManager
{
	extern int32_t g_curTrackIndex;

	void StopAndFlush();
	int32_t IsStreamActive();
	void PlayTrackByIndex(int32_t trackIndex, int32_t fadeMode);
	int32_t PlaySoundBuffer(int32_t soundIndex, int32_t leftVolume, int32_t rightVolume, int32_t pan, int32_t volume, int32_t flags);
	void ReleaseBuffers();
	void Init();
	void SetVolumesProcessed(int32_t musicVolume, int32_t sfxVolume);
	void FlushSoundVoices();
	int32_t StopAndWait();
	void LoadSfxPackForLevel(int32_t levelId);
	int32_t PlayOneShotSoundGlobal(int32_t soundIndex, int32_t volume, int32_t leftVolume, int32_t rightVolume);
	void PlayMusicOneShot(int32_t trackIndex);
	void UpdateChannels();
}