#pragma once

#include "Common.h"
#include <windows.h>
#include <mmsystem.h>
#ifndef DIRECTSOUND_VERSION
	#define DIRECTSOUND_VERSION 0x0600
#endif
#include <directx6/dsound.h>

namespace AudioManager
{
	extern int32_t g_curTrackIndex;
	extern int32_t g_loopingMusicTrackIndex;

	extern int32_t g_dsResult;
	extern LPDIRECTSOUNDBUFFER g_dsBuffers[768];

	void StopAndFlush();
	void OnExit();
	int32_t IsStreamActive();
	int32_t PlayTrackByIndex(int32_t trackIndex, int32_t fadeMode);
	void QueuePlay(char* path, int32_t fadeMode);
	int32_t PlaySoundBuffer(int32_t soundIndex, int32_t leftVolume, int32_t rightVolume, int32_t pan, int32_t volume, int32_t flags);
	int32_t IsEffectPlaying(int32_t index);
	int32_t IsActorSoundPlaying(void* owner);
	void ReleaseAllBuffers();
	int32_t CreateDirectSoundBuffer(LPDIRECTSOUND ds, LPDIRECTSOUNDBUFFER* outBuf, DWORD bufferBytes);
	int32_t WriteToBuffer(LPDIRECTSOUNDBUFFER buf, DWORD offset, const void* src, DWORD bytes);
	int32_t WaveLoadFile(char* path, int32_t* outBytes, int32_t* outFormatSize, HGLOBAL* outFormatHandle, void** outData);
	MMRESULT WaveReadFile(HMMIO hmmio, uint32_t size, void* buffer, MMCKINFO* chunk, uint32_t* outRead);
	void ReleaseBuffers();
	void Init();
	void SetVolumesProcessed(int32_t musicVolume, int32_t sfxVolume);
	void SetMusicVolume(int32_t musicVolume);
	void SetSfxVolume(int32_t sfxVolume);
	void SetVolumes(int32_t musicVolume, int32_t sfxVolume);
	void FlushSoundVoices();
	int32_t StopAndWait();
	void SignalThreadExit();
	int32_t IsThreadReady();
	void LoadSfxPackForLevel(int32_t levelId);
	int32_t PlayOneShotSoundGlobal(int32_t soundIndex, int32_t volume, int32_t leftVolume, int32_t rightVolume);
	int32_t PlayOneShotSound3DActor(void* actor, int32_t soundIndex, int32_t frequency, int32_t volume, void* position, int32_t flag);
	void PlayMusicOneShot(int32_t trackIndex);
	void PlayMusicLooping(int16_t trackIndex);
	int32_t PlayLoopingSound3D(void* owner, int32_t soundIndex, int32_t volume, int32_t leftVolume, int16_t rightVolume);
	int32_t PlayLoopingSound3DPositional(void* owner, int32_t soundIndex, int32_t volume, int32_t leftVolume, void* unused, int32_t rightVolume);
	void ResetChannelsTable();
	void UpdateChannels();
	int32_t RestartLoopingSound(int32_t soundId);
	void SetVolumes(int32_t musicVolume, int32_t sfxVolume);

	extern int16_t g_musicVolTable[12];
	extern int16_t g_soundVolTable[12];
	extern int32_t g_sfxVolume;
	extern int32_t g_musicVolumeLevel;
	extern LPDIRECTSOUND g_directSound;
	extern LPDIRECTSOUNDBUFFER g_dsPrimaryBuffer;
	extern LPDIRECTSOUNDBUFFER g_dsSecondaryBuffer;
	extern int16_t g_dsVolTable[151];

	extern HANDLE g_streamCommandEvent;
	extern HANDLE g_streamAckEvent;
	extern int32_t g_streamActive;
	extern int32_t g_streamFadeMode;
	extern int32_t g_streamCommand;
	extern char g_streamPath[512];
	extern int32_t g_streamThreadReady;

	extern int32_t g_pendingStreamTrack;
	extern int32_t g_pendingStreamNoFade;
	extern int16_t g_loopingSoundChannels[32][5];
	extern void* g_loopingSoundOwners[768];

	extern int32_t g_loadedSfxPackIndex;
	extern int32_t g_loadedWaveBytes;
	extern int32_t g_loadedWaveFormatSize;
	extern void* g_loadedWaveData;
	extern uint16_t g_soundFreqTable[128];
	extern char g_sfxSubPath[8];

	extern HGLOBAL g_waveFormatHandle;
	extern HGLOBAL g_sfxWaveFormatHandle;
	extern HMMIO g_waveMmioHandle;

	namespace Wave
	{
		int32_t CloseFile(HMMIO* hmmio, HGLOBAL* dataHandle);
		MMRESULT OpenFile(LPSTR path, HMMIO* outHmmio, HGLOBAL* outFormatHandle, MMCKINFO* parentChunk);
		MMRESULT SeekToChunk(HMMIO* hmmio, MMCKINFO* dataChunk, MMCKINFO* parentChunk);
	}

	namespace Stream
	{
		void Stop();
	}

	namespace Preset
	{
		void PlayOneShotSound2(int32_t index, void* actor);
		void PlayOneShotSound(int32_t index, void* actor);
	}
}
