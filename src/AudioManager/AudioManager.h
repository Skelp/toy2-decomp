#pragma once

#include "Common.h"
#include "Numerics.h"
#include <windows.h>
#include <mmsystem.h>
#ifndef DIRECTSOUND_VERSION
	#define DIRECTSOUND_VERSION 0x0600
#endif
#include <directx6/dsound.h>

namespace AudioManager
{
	enum MusicTrack
	{
		MUSIC_TRACK_BOSS = 15,
		MUSIC_TRACK_CHALLENGE = 16,
		MUSIC_TRACK_CREDITS = 18,
	};

	enum StreamCommand
	{
		STREAM_COMMAND_NONE = 0,
		STREAM_COMMAND_STOP = 1,
		STREAM_COMMAND_PLAY = 2,
		STREAM_COMMAND_EXIT = 3,
	};

	struct SoundSequenceSlot
	{
		Vector3I position;
		uint8_t* cursor;
		int16_t timer;
		int16_t reserved;
	};

	STATIC_ASSERT(sizeof(SoundSequenceSlot) == 0x14);

	struct SoundSequenceEvent
	{
		int16_t soundIndex;
		int16_t frequency;
		int16_t volume;
		int16_t delay;
	};

	STATIC_ASSERT(sizeof(SoundSequenceEvent) == 0x8);

	struct OneShotSoundPreset
	{
		int16_t encodedSoundIndex;
		int16_t baseFrequency;
		int16_t leftVolume;
		int16_t rightVolume;
		int16_t randomFrequencyShift;
		int16_t maxLeftVolume;
		int16_t maxRightVolume;
		int16_t maxVolumeScale;
	};

	STATIC_ASSERT(sizeof(OneShotSoundPreset) == 0x10);

	extern int32_t g_curTrackIndex;
	extern int32_t g_loopingMusicTrackIndex;
	extern int32_t g_quietMode;

	extern int32_t g_dsResult;
	extern LPDIRECTSOUNDBUFFER g_dsBuffers[768];

	void StopAndFlush();
	void OnExit();
	void ShutdownHandles();
	int32_t IsStreamActive();
	int32_t PlayTrackByIndex(int32_t trackIndex, int32_t looping);
	void QueuePlay(char* path, int32_t looping);
	void ThreadPlay(char* path, int32_t looping);
	int32_t LoadFile(char* path);
	void FillBuffer();
	int32_t PlaySoundBuffer(int32_t soundIndex, int32_t leftVolume, int32_t rightVolume, void* owner, int32_t unused, int32_t looping);
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
	int32_t PlayOneShotSound3D(int32_t soundIndex, int32_t frequency, int32_t volume, const Vector3I* position);
	int32_t PlayOneShotSound3DActor(void* actor, int32_t soundIndex, int32_t frequency, int32_t volume, void* position, int32_t flag);
	void ClearSequence7Cursor();
	void StartSoundSequenceOnActor(int32_t sequenceId, Vector3I* position);
	void UpdateSoundSequences();
	BOOL CALLBACK Enumerate(LPGUID lpGuid, LPCSTR lpcstrDescription, LPCSTR lpcstrModule, LPVOID lpContext);
	void PlayMusicOneShot(int32_t trackIndex);
	void PlaySoundEffect(int32_t soundIndex, const Vector3I* position);
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
	extern HANDLE g_streamFillEvent;
	extern HANDLE g_streamStopEvent;
	extern int32_t g_streamPlaybackFinished;
	extern CRITICAL_SECTION g_streamCriticalSection;
	extern int32_t g_streamInitialized;
	extern int32_t g_streamActive;
	extern int32_t g_queuedStreamLooping;
	extern int32_t g_streamLooping;
	extern int32_t g_streamCommand;
	extern char g_streamPath[512];
	extern int32_t g_streamThreadReady;
	extern SoundSequenceSlot g_soundSequenceSlots[8];
	extern int32_t g_soundSequenceSlotIndex;
	extern int16_t g_maxLeftVolume;
	extern int16_t g_maxRightVolume;
	extern int16_t g_maxVolume;
	extern int16_t g_dynamicSoundFrequencies[14];
	extern OneShotSoundPreset g_oneShotPresets[218];

	extern int32_t g_pendingStreamTrack;
	extern int16_t g_loopingSoundChannels[32][5];
	extern void* g_loopingSoundOwners[768];

	extern int32_t g_loadedSfxPackIndex;
	extern int32_t g_loadedWaveBytes;
	extern int32_t g_loadedWaveFormatSize;
	extern void* g_loadedWaveData;
	extern uint16_t g_soundFreqTable[128];
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
		int32_t Init();
		int32_t __cdecl ThreadProc(LPVOID unused);
		void Stop();
	}

	namespace Preset
	{
		void PlayOneShotSound2(int32_t index, void* actor);
		void PlayOneShotSound(int32_t index, void* actor);
	}
}
