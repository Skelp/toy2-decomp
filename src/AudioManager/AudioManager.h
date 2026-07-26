#pragma once

#include "Common.h"
#include <windows.h>

namespace AudioManager
{
	extern int32_t g_curTrackIndex;
	extern int32_t g_loopingMusicTrackIndex;

	void StopAndFlush();
	int32_t IsStreamActive();
	int32_t PlayTrackByIndex(int32_t trackIndex, int32_t fadeMode);
	void QueuePlay(char* path, int32_t fadeMode);
	int32_t PlaySoundBuffer(int32_t soundIndex, int32_t leftVolume, int32_t rightVolume, int32_t pan, int32_t volume, int32_t flags);
	void ReleaseBuffers();
	void Init();
	void SetVolumesProcessed(int32_t musicVolume, int32_t sfxVolume);
	void SetMusicVolume(int32_t musicVolume);
	void SetSfxVolume(int32_t sfxVolume);
	void SetVolumes(int32_t musicVolume, int32_t sfxVolume);
	void FlushSoundVoices();
	int32_t StopAndWait();
	void LoadSfxPackForLevel(int32_t levelId);
	int32_t PlayOneShotSoundGlobal(int32_t soundIndex, int32_t volume, int32_t leftVolume, int32_t rightVolume);
	void PlayMusicOneShot(int32_t trackIndex);
	void PlayMusicLooping(int16_t trackIndex);
	int32_t PlayLoopingSound3D(void* owner, int32_t soundIndex, int32_t volume, int32_t leftVolume, int32_t rightVolume);
	int32_t PlayLoopingSound3DPositional(void* owner, int32_t soundIndex, int32_t volume, int32_t leftVolume, void* unused, int32_t rightVolume);
	void UpdateChannels();
	int32_t RestartLoopingSound(int32_t soundId);
	void SetVolumes(int32_t musicVolume, int32_t sfxVolume);

	extern int16_t g_musicVolTable[12];
	extern int16_t g_soundVolTable[12];
	extern int32_t g_sfxVolume;
	extern int32_t g_musicVolumeLevel;
	extern void* g_dsPrimaryBuffer;
	extern int16_t g_dsVolTable[151];

	extern HANDLE g_streamCommandEvent;
	extern HANDLE g_streamAckEvent;
	extern int32_t g_streamActive;
	extern int32_t g_streamCommand;

	extern int32_t g_pendingStreamTrack;
	extern int32_t g_pendingStreamNoFade;
	extern int16_t g_loopingSoundChannels[32][5];
}
