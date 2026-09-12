#include "AudioManager/AudioManagerInternal.h"
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

namespace AudioManager
{
	// GLOBAL: TOY2 0x005282CC
	int32_t g_curTrackIndex;

	// GLOBAL: TOY2 0x00559C68
	int32_t g_loopingMusicTrackIndex;

	// GLOBAL: TOY2 0x00724E7C
	int32_t g_dsResult;

	// GLOBAL: TOY2 0x00724E78
	DWORD g_soundPlayCursor;

	// GLOBAL: TOY2 0x00724E80
	int32_t g_audioInitialized;

	// GLOBAL: TOY2 0x00724E84
	int32_t g_deviceCount;

	// GLOBAL: TOY2 0x00534484
	int32_t g_quietMode;

	// GLOBAL: TOY2 0x00726F3C
	int32_t g_streamPending;

	// GLOBAL: TOY2 0x005028A8
	int16_t g_musicVolTable[12] = { 0, 9, 18, 27, 36, 45, 54, 63, 72, 81, 90, 99 };

	// GLOBAL: TOY2 0x005028C8
	int16_t g_soundVolTable[12] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88 };

	// GLOBAL: TOY2 0x004FCDB0
	int32_t g_sfxVolume = 256;

	// GLOBAL: TOY2 0x005282C8
	int32_t g_musicVolumeLevel;

	// GLOBAL: TOY2 0x005282C0
	HANDLE g_streamCommandEvent;

	// GLOBAL: TOY2 0x005282C4
	HANDLE g_streamAckEvent;

	// GLOBAL: TOY2 0x005282B8
	HANDLE g_streamFillEvent;

	// GLOBAL: TOY2 0x005282BC
	HANDLE g_streamStopEvent;

	// GLOBAL: TOY2 0x00528220
	int32_t g_streamPlaybackFinished;

	// GLOBAL: TOY2 0x005282D8
	CRITICAL_SECTION g_streamCriticalSection;

	// GLOBAL: TOY2 0x005282F8
	int32_t g_streamInitialized;

	// GLOBAL: TOY2 0x005282FC
	int32_t g_streamActive;

	// GLOBAL: TOY2 0x0053C844
	int32_t g_queuedStreamLooping;

	// GLOBAL: TOY2 0x00528224
	int32_t g_streamLooping;

	// GLOBAL: TOY2 0x0053C848
	int32_t g_streamCommand;

	// GLOBAL: TOY2 0x0053C84C
	char g_streamPath[512];

	// GLOBAL: TOY2 0x0053CA50
	int32_t g_streamThreadReady;

	// GLOBAL: TOY2 0x00725E94
	int32_t g_pendingStreamTrack;

	// GLOBAL: TOY2 0x00725E98
	LPGUID g_deviceGuids[16];

	// GLOBAL: TOY2 0x00830E58
	int16_t g_loopingSoundChannels[LOOPING_SOUND_CHANNEL_COUNT][5];

	// GLOBAL: TOY2 0x00725294
	void* g_loopingSoundOwners[SOUND_BUFFER_COUNT];

	// GLOBAL: TOY2 0x00726230
	int32_t g_loadedBufferCount;

	// GLOBAL: TOY2 0x00726338
	LPDIRECTSOUNDBUFFER g_dsBuffers[SOUND_BUFFER_COUNT];

	// GLOBAL: TOY2 0x00726334
	DWORD g_soundWriteCursor;

	// GLOBAL: TOY2 0x005282F0
	LPDIRECTSOUNDBUFFER g_dsPrimaryBuffer;

	// GLOBAL: TOY2 0x005282F4
	LPDIRECTSOUNDNOTIFY g_dsNotify;

	// GLOBAL: TOY2 0x00528208
	LPDIRECTSOUNDBUFFER g_streamBufferAlias;

	// GLOBAL: TOY2 0x00528230
	DSBPOSITIONNOTIFY g_streamNotifications[17];

	// GLOBAL: TOY2 0x005281D8
	HGLOBAL g_waveFormatHandle;

	// GLOBAL: TOY2 0x00725290
	HGLOBAL g_sfxWaveFormatHandle;

	// GLOBAL: TOY2 0x005281DC
	HMMIO g_waveMmioHandle;

	// GLOBAL: TOY2 0x005281E0
	MMCKINFO g_streamDataChunk;

	// GLOBAL: TOY2 0x005281F4
	MMCKINFO g_streamParentChunk;

	// GLOBAL: TOY2 0x0052820C
	uint32_t g_streamBufferBytes;

	// GLOBAL: TOY2 0x00528210
	uint32_t g_streamFillBytes;

	// GLOBAL: TOY2 0x00528214
	uint32_t g_streamWriteOffset;

	// GLOBAL: TOY2 0x00528218
	uint32_t g_streamBytesPlayed;

	// GLOBAL: TOY2 0x0052821C
	uint32_t g_streamLastPlayCursor;

	// GLOBAL: TOY2 0x00528228
	int32_t g_streamReachedEnd;

	// GLOBAL: TOY2 0x00725F24
	LPDIRECTSOUND g_directSound;

	// GLOBAL: TOY2 0x004FCDBC
	int32_t g_loadedSfxPackIndex = -1;

	// GLOBAL: TOY2 0x00724E88
	void* g_loadedWaveData;

	// GLOBAL: TOY2 0x00724E8C
	char g_deviceNames[16][32];

	// GLOBAL: TOY2 0x00725F28
	int32_t g_loadedWaveFormatSize;

	// GLOBAL: TOY2 0x00725F2C
	int32_t g_loadedWaveBytes;

	// GLOBAL: TOY2 0x00726234
	uint16_t g_soundFreqTable[128];

	// GLOBAL: TOY2 0x00534074
	char g_sfxSubPath[4];

	// GLOBAL: TOY2 0x004FD668
	// clang-format off
	int16_t g_dsVolTable[151] = {
#include "DsVolTable.inc"
	};
	// clang-format on

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

	// FUNCTION: TOY2 0x0047E420 [MATCHED]
	int32_t CreateDirectSoundBuffer(LPDIRECTSOUND ds, LPDIRECTSOUNDBUFFER* outBuf, DWORD bufferBytes)
	{
		PCMWAVEFORMAT wfx;
		memset(&wfx, 0, sizeof(wfx));
		PCMWAVEFORMAT* src = (PCMWAVEFORMAT*)g_sfxWaveFormatHandle;
		wfx.wf.wFormatTag = src->wf.wFormatTag;
		wfx.wf.nChannels = src->wf.nChannels;
		wfx.wf.nSamplesPerSec = src->wf.nSamplesPerSec;
		wfx.wf.nBlockAlign = src->wf.nBlockAlign;
		wfx.wf.nAvgBytesPerSec = src->wf.nAvgBytesPerSec;
		wfx.wBitsPerSample = src->wBitsPerSample;

		DSBUFFERDESC desc;
		memset(&desc, 0, sizeof(desc));
		desc.dwSize = sizeof(desc);
		desc.dwFlags = 0x80e0;
		desc.dwBufferBytes = bufferBytes;
		desc.lpwfxFormat = (WAVEFORMATEX*)&wfx;

		if (ds->CreateSoundBuffer(&desc, outBuf, NULL) == 0)
		{
			return 1;
		}
		*outBuf = NULL;
		return 0;
	}

	// FUNCTION: TOY2 0x0047E4E0 [PROVISIONAL]
	int32_t WriteToBuffer(LPDIRECTSOUNDBUFFER buf, DWORD offset, const void* src, DWORD bytes)
	{
		void* audioPtr1;
		DWORD audioBytes1;
		void* audioPtr2;
		DWORD audioBytes2;

		HRESULT result = buf->Lock(offset, bytes, &audioPtr1, &audioBytes1, &audioPtr2, &audioBytes2, 0);
		if (result == DSERR_BUFFERLOST)
		{
			buf->Restore();
			result = buf->Lock(offset, bytes, &audioPtr1, &audioBytes1, &audioPtr2, &audioBytes2, 0);
		}
		if (result == DS_OK)
		{
			memcpy(audioPtr1, src, audioBytes1);
			if (audioPtr2 != NULL)
			{
				memcpy(audioPtr2, (const BYTE*)src + audioBytes1, audioBytes2);
			}
			if (buf->Unlock(audioPtr1, audioBytes1, audioPtr2, audioBytes2) == DS_OK)
			{
				return 1;
			}
		}
		return 0;
	}

	struct SoundPackDescriptor
	{
		char** soundNames;
		int32_t firstSoundIndex;
	};

	STATIC_ASSERT(sizeof(SoundPackDescriptor) == 0x8);

	// GLOBAL: TOY2 0x004FCDC4
	char* g_primarySoundNames[] = {
#include "PrimarySoundNames.inc"
	};

	// GLOBAL: TOY2 0x004FD1C8
	char* g_secondarySoundNames[] = {
#include "SecondarySoundNames.inc"
	};

	// GLOBAL: TOY2 0x00726F40
	char* g_emptySoundNames[1];

	void LoadSoundEffect(char* name, int32_t index, int32_t flag);

	// FUNCTION: TOY2 0x0047D670 [MATCHED]
	void LoadSoundPack(SoundPackDescriptor* table, int32_t index)
	{
		if (index >= 0 && index <= 0x10)
		{
			char** filenames = table[index].soundNames;
			int32_t baseIndex = table[index].firstSoundIndex;
			char* filename = *filenames;
			while (filename != NULL)
			{
				if (*filename != '\0')
				{
					char path[256];
					sprintf(path, "%s.wav", filename);
					LoadSoundEffect(path, baseIndex, 0);
				}
				filenames++;
				baseIndex++;
				filename = *filenames;
			}
		}
	}

	// FUNCTION: TOY2 0x0047E5B0 [PROVISIONAL]
	void LoadSoundEffect(char* name, int32_t index, int32_t flag)
	{
		char path[1024];
		if (g_loadedSfxPackIndex == -1 || index == g_loadedSfxPackIndex)
		{
			FileUtils::GetPathValue(path);
			strcat(path, "sfx\\");
			if (flag != 0)
			{
				strcat(path, g_sfxSubPath);
			}
			strcat(path, name);
			Logger::Log("LoadSoundEffect : Loading %s, effect %d.\n", path, index, 1);
			if (WaveLoadFile(path, &g_loadedWaveBytes, &g_loadedWaveFormatSize, &g_sfxWaveFormatHandle, &g_loadedWaveData) == 0)
			{
				LPDIRECTSOUNDBUFFER* outBuf = &g_dsBuffers[index * 6];
				if (CreateDirectSoundBuffer(g_directSound, outBuf, g_loadedWaveBytes) == 1)
				{
					WriteToBuffer(*outBuf, 0, g_loadedWaveData, g_loadedWaveBytes);
					uint16_t freq = ((PCMWAVEFORMAT*)g_sfxWaveFormatHandle)->wf.nSamplesPerSec;
					g_soundFreqTable[index] = freq;
					(*outBuf)->SetFrequency(freq);
					(*outBuf)->SetPan(0);
					(*outBuf)->SetVolume(0);
					g_loopingSoundOwners[index * 6] = (void*)1;
					for (int32_t i = 1; i <= 5; i++)
					{
						g_dsResult = g_directSound->DuplicateSoundBuffer(*outBuf, &g_dsBuffers[index * 6 + i]);
						if (g_dsResult == DS_OK)
						{
							g_dsBuffers[index * 6 + i]->SetFrequency(g_soundFreqTable[index]);
							g_dsBuffers[index * 6 + i]->SetPan(0);
							g_dsBuffers[index * 6 + i]->SetVolume(0);
							g_loopingSoundOwners[index * 6 + i] = (void*)1;
						}
					}
					return;
				}
			}
			else
			{
				Logger::Log("LoadSoundEffect : Calling WaveLoadFile failed - no sound effect %s, effect %d.\n", path, index);
			}
		}
	}

	// Stops one sound buffer when it is playing, releases it and clears its slot and
	// its looping owner. Both release paths state this body.
#define STOP_AND_RELEASE_SOUND_BUFFER()       \
	DWORD status;                             \
	DWORD playing;                            \
	if (g_audioInitialized == 0)              \
	{                                         \
		playing = 0;                          \
	}                                         \
	else                                      \
	{                                         \
		g_dsResult = buf->GetStatus(&status); \
		playing = status;                     \
	}                                         \
	if ((playing & 1) == 1)                   \
	{                                         \
		g_dsBuffers[i]->Stop();               \
	}                                         \
	g_dsBuffers[i]->Release();                \
	g_dsBuffers[i] = NULL;                    \
	g_loopingSoundOwners[i] = NULL;
	// FUNCTION: TOY2 0x0047E7D0 [MATCHED]
	void ReleaseAllBuffers()
	{
		if (g_audioInitialized != 0)
		{
			for (int32_t i = 767; i >= 0; i--)
			{
				LPDIRECTSOUNDBUFFER buf = g_dsBuffers[i];
				if (buf != NULL)
				{
					STOP_AND_RELEASE_SOUND_BUFFER();
				}
			}
		}
	}

	// FUNCTION: TOY2 0x0047E850 [MATCHED]
	void ReleaseBuffers()
	{
		if (g_audioInitialized != 0)
		{
			int32_t i;
			StopAndWait();
			if (g_audioInitialized != 0)
			{
				for (i = 767; i >= 0; i--)
				{
					LPDIRECTSOUNDBUFFER buf = g_dsBuffers[i];
					if (buf != NULL)
					{
						STOP_AND_RELEASE_SOUND_BUFFER();
					}
				}
			}
			if (g_directSound != NULL)
			{
				g_directSound->Release();
			}
			g_directSound = NULL;
			ResetChannelsTable();
			g_loadedBufferCount = 0;
			for (i = 0; i < SOUND_BUFFER_COUNT; i++)
			{
				g_dsBuffers[i] = NULL;
			}
			for (i = 0; i < SOUND_BUFFER_COUNT; i++)
			{
				g_loopingSoundOwners[i] = NULL;
			}
			g_deviceCount = 0;
			g_audioInitialized = 0;
		}
	}

#undef STOP_AND_RELEASE_SOUND_BUFFER

	// FUNCTION: TOY2 0x0047E3C0 [PROVISIONAL]
	BOOL CALLBACK Enumerate(LPGUID lpGuid, LPCSTR lpcstrDescription, LPCSTR lpcstrModule, LPVOID lpContext)
	{
		int32_t index = g_deviceCount;
		strcpy(g_deviceNames[index], lpcstrDescription);
		g_deviceGuids[index] = lpGuid;
		g_deviceCount = index + 1;
		return TRUE;
	}

	// GLOBAL: TOY2 0x004FD140
	SoundPackDescriptor g_primarySoundPacks[17] = {
		{ g_primarySoundNames + 0, 1 },
		{ g_primarySoundNames + 71, 87 },
		{ g_primarySoundNames + 82, 87 },
		{ g_primarySoundNames + 121, 87 },
		{ g_primarySoundNames + 100, 87 },
		{ g_primarySoundNames + 111, 87 },
		{ g_primarySoundNames + 93, 87 },
		{ g_primarySoundNames + 127, 87 },
		{ g_primarySoundNames + 138, 87 },
		{ g_primarySoundNames + 150, 87 },
		{ g_primarySoundNames + 157, 87 },
		{ g_primarySoundNames + 168, 87 },
		{ g_primarySoundNames + 178, 87 },
		{ g_primarySoundNames + 183, 87 },
		{ g_primarySoundNames + 188, 87 },
		{ g_primarySoundNames + 194, 87 },
		{ g_primarySoundNames + 199, 1 },
	};

	// GLOBAL: TOY2 0x004FD5E0
	SoundPackDescriptor g_secondarySoundPacks[17] = {
		{ g_secondarySoundNames + 0, 70 },
		{ g_secondarySoundNames + 8, 67 },
		{ g_secondarySoundNames + 29, 67 },
		{ g_secondarySoundNames + 84, 67 },
		{ g_secondarySoundNames + 48, 67 },
		{ g_secondarySoundNames + 66, 67 },
		{ g_secondarySoundNames + 44, 67 },
		{ g_secondarySoundNames + 88, 67 },
		{ g_secondarySoundNames + 99, 67 },
		{ g_secondarySoundNames + 108, 67 },
		{ g_secondarySoundNames + 112, 67 },
		{ g_secondarySoundNames + 123, 67 },
		{ g_secondarySoundNames + 138, 67 },
		{ g_secondarySoundNames + 171, 67 },
		{ g_secondarySoundNames + 202, 67 },
		{ g_secondarySoundNames + 223, 67 },
		{ g_emptySoundNames, 67 },
	};

	// GLOBAL: TOY2 0x004FCDC0
	int32_t g_currentSfxLevelId = 1;

	// Clears every sound buffer slot, its owner and every looping channel pair.
#define CLEAR_SOUND_BUFFER_TABLES()                   \
	for (i = 0; i < SOUND_BUFFER_COUNT; i++)          \
	{                                                 \
		g_dsBuffers[i] = NULL;                        \
	}                                                 \
	for (i = 0; i < SOUND_BUFFER_COUNT; i++)          \
	{                                                 \
		g_loopingSoundOwners[i] = NULL;               \
	}                                                 \
	for (i = 0; i < LOOPING_SOUND_CHANNEL_COUNT; i++) \
	{                                                 \
		g_loopingSoundChannels[i][0] = -1;            \
		g_loopingSoundChannels[i][1] = -1;            \
	}

	// FUNCTION: TOY2 0x0047EDE0 [PROVISIONAL]
	void Init()
	{
		char waveName[257];
		ResetChannelsTable();

		int32_t i;
		for (i = 0; i < SOUND_BUFFER_COUNT; i++)
		{
			g_dsBuffers[i] = NULL;
		}
		for (i = 0; i < SOUND_BUFFER_COUNT; i++)
		{
			g_loopingSoundOwners[i] = NULL;
		}

		g_loadedBufferCount = 0;
		g_deviceCount = 0;
		g_audioInitialized = 0;
		if (g_quietMode == 0)
		{
			DirectSoundEnumerateA(Enumerate, waveName);
			DirectSoundCreate(g_deviceGuids[1], &g_directSound, NULL);
			if (g_directSound != NULL)
			{
				g_directSound->SetCooperativeLevel(g_windowData.mainHwnd, DSSCL_EXCLUSIVE);
				g_audioInitialized = 1;
				if (IsStreamActive())
				{
					StopAndWait();
					while (IsStreamActive()) {}
				}

				g_streamPending = 0;
				if (g_audioInitialized != 0)
				{
					for (i = 767; i >= 0; i--)
					{
						if (g_dsBuffers[i] != NULL)
						{
							if ((IsEffectPlaying(i) & 1) == 1)
							{
								g_dsBuffers[i]->Stop();
							}
							g_dsBuffers[i]->Release();
							g_dsBuffers[i] = NULL;
							g_loopingSoundOwners[i] = NULL;
						}
					}

					CLEAR_SOUND_BUFFER_TABLES()
				}

				SoundPackDescriptor* pack = &g_primarySoundPacks[0];
				char** soundName = pack->soundNames;
				int32_t soundIndex = pack->firstSoundIndex;
				while (*soundName != NULL)
				{
					if (**soundName != '\0')
					{
						sprintf(waveName, "%s.wav", *soundName);
						LoadSoundEffect(waveName, soundIndex, 0);
					}
					soundName++;
					soundIndex++;
				}

				g_currentSfxLevelId = 0;
				Stream::Init();
			}
		}
	}

	// FUNCTION: TOY2 0x0049AE20 [MATCHED]
	void SetSfxVolume(int32_t sfxVolume) { g_sfxVolume = (sfxVolume & 0xff) << 1; }

	// FUNCTION: TOY2 0x0049AE40 [PROVISIONAL]
	void SetMusicVolume(int32_t musicVolume)
	{
		g_musicVolumeLevel = musicVolume;
		int32_t scaledVol = g_dsVolTable[musicVolume * 150 / 64];
		Logger::Log("SETMUSICVOL : Vol %d \n", scaledVol);
		if (g_dsPrimaryBuffer != NULL)
		{
			int32_t result = g_dsPrimaryBuffer->SetVolume(scaledVol);
			if (result)
			{
				char* msg;
				switch (result)
				{
					case 0x80004002:
						msg = "The requested COM interface is not available. ";
						break;
					case 0x80004001:
						msg = "The function called is not supported at this time. ";
						break;
					case 0x80004005:
						msg = "An undetermined error occurred inside the DirectSound subsystem. ";
						break;
					case 0x80040110:
						msg = "The object does not support aggregation. ";
						break;
					case 0x8007000E:
						msg = "The DirectSound subsystem could not allocate sufficient memory to complete the caller's request. ";
						break;
					case 0x8878001E:
						msg = "The control (volume, pan, and so forth) requested by the caller is not available. ";
						break;
					case 0x8878000A:
						msg = "The request failed because resources, such as a priority level, were already in use by another caller. ";
						break;
					case 0x80070057:
						msg = "An invalid parameter was passed to the returning function. ";
						break;
					case 0x88780032:
						msg = "This function is not valid for the current state of this object. ";
						break;
					case 0x88780082:
						msg = "The object is already initialized. ";
						break;
					case 0x88780064:
						msg = "The specified wave format is not supported. ";
						break;
					case 0x88780096:
						msg = "The buffer memory has been lost and must be restored. ";
						break;
					case 0x88780078:
						msg = "No sound driver is available for use. ";
						break;
					case 0x887800A0:
						msg = "Another application has a higher priority level, preventing this call from succeeding ";
						break;
					case 0x88780046:
						msg = "The caller does not have the priority level required for the function to succeed. ";
						break;
					case 0x887800AA:
						msg = "The IDirectSound::Initialize method has not been called or has not been called successfully before other methods were called. ";
						break;
					default:
						msg = "Unknown error!!";
						break;
				}
				Logger::Log("SETMUSICVOL : Failed to set volume - error is %s.\n", msg);
			}
		}
	}

	// FUNCTION: TOY2 0x00436C90 [MATCHED]
	void SignalThreadExit()
	{
		if (g_streamCommandEvent != NULL)
		{
			g_streamCommand = 3;
			g_streamThreadReady = 0;
			SetEvent(g_streamCommandEvent);
		}
	}

	// FUNCTION: TOY2 0x00436CC0 [MATCHED]
	int32_t IsThreadReady() { return g_streamThreadReady; }

	// FUNCTION: TOY2 0x00436D40 [MATCHED]
	int32_t StopAndWait()
	{
		if (g_dsPrimaryBuffer != NULL && g_streamCommandEvent != NULL)
		{
			g_streamCommand = 1;
			SetEvent(g_streamCommandEvent);
			WaitForSingleObject(g_streamAckEvent, INFINITE);
		}
		return 1;
	}

	// FUNCTION: TOY2 0x0047EC20 [PROVISIONAL]
	void LoadSfxPackForLevel(int32_t levelId)
	{
		char waveName[WAVE_NAME_LENGTH];
		if (g_audioInitialized != 0)
		{
			if (IsStreamActive())
			{
				StopAndWait();
				while (IsStreamActive()) {}
			}

			g_streamPending = 0;
			if (g_audioInitialized != 0)
			{
				int32_t i;
				for (i = 767; i >= 0; i--)
				{
					if (g_dsBuffers[i] != NULL)
					{
						if ((IsEffectPlaying(i) & 1) == 1)
						{
							g_dsBuffers[i]->Stop();
						}
						g_dsBuffers[i]->Release();
						g_dsBuffers[i] = NULL;
						g_loopingSoundOwners[i] = NULL;
					}
				}

				CLEAR_SOUND_BUFFER_TABLES()
			}
		}

		SoundPackDescriptor* pack = &g_primarySoundPacks[0];
		char** soundName = pack->soundNames;
		int32_t soundIndex = pack->firstSoundIndex;
		while (*soundName != NULL)
		{
			if (**soundName != '\0')
			{
				sprintf(waveName, "%s.wav", *soundName);
				LoadSoundEffect(waveName, soundIndex, 0);
			}
			soundName++;
			soundIndex++;
		}

		if (levelId > 0)
		{
			if (levelId <= MAX_SFX_LEVEL_ID)
			{
				pack = &g_primarySoundPacks[levelId];
				soundName = pack->soundNames;
				soundIndex = pack->firstSoundIndex;
				while (*soundName != NULL)
				{
					if (**soundName != '\0')
					{
						sprintf(waveName, "%s.wav", *soundName);
						LoadSoundEffect(waveName, soundIndex, 0);
					}
					soundName++;
					soundIndex++;
				}
			}

			if (levelId <= MAX_SFX_LEVEL_ID)
			{
				pack = &g_secondarySoundPacks[levelId];
				soundName = pack->soundNames;
				soundIndex = pack->firstSoundIndex;
				while (*soundName != NULL)
				{
					if (**soundName != '\0')
					{
						sprintf(waveName, "%s.wav", *soundName);
						LoadSoundEffect(waveName, soundIndex, 0);
					}
					soundName++;
					soundIndex++;
				}
			}
		}

		g_currentSfxLevelId = levelId;
	}

	enum EncodedSoundIndexFlag
	{
		LEVEL_SOUND_MAPPING_OFFSET_FLAG = 0x4000,
		LOOPING_SOUND_INDEX_FLAG = -0x8000,
	};

	// GLOBAL: TOY2 0x00502950
	OneShotSoundPreset g_oneShotPresets[218] = {
#include "OneShotSoundPresets.inc"
	};

	// GLOBAL: TOY2 0x005028E8
	LevelSoundMapping g_levelSoundMappings[26] = {
		{ 1, 88 },
		{ 2, 95 },
		{ 8, 92 },
		{ 1, LOOPING_SOUND_INDEX_FLAG | 92 },
		{ 2, LOOPING_SOUND_INDEX_FLAG | 88 },
		{ 2, LOOPING_SOUND_INDEX_FLAG | 87 },
		{ 5, LOOPING_SOUND_INDEX_FLAG | 89 },
		{ 11, LOOPING_SOUND_INDEX_FLAG | 95 },
		{ 2, 92 },
		{ 5, 87 },
		{ 5, LOOPING_SOUND_INDEX_FLAG | 88 },
		{ 3, LOOPING_SOUND_INDEX_FLAG | 89 },
		{ 14, LOOPING_SOUND_INDEX_FLAG | 89 },
		{ 6, LOOPING_SOUND_INDEX_FLAG | 92 },
		{ 3, LOOPING_SOUND_INDEX_FLAG | 87 },
		{ 9, LOOPING_SOUND_INDEX_FLAG | 92 },
		{ 12, LOOPING_SOUND_INDEX_FLAG | 87 },
		{ 15, LOOPING_SOUND_INDEX_FLAG | 87 },
		{ 4, 91 },
		{ 13, 88 },
		{ 4, 90 },
		{ 13, 89 },
		{ 3, 88 },
		{ 14, 90 },
		{ 13, 90 },
		{ 15, 90 },
	};

	namespace Preset
	{
	}

	// GLOBAL: TOY2 0x0052f120
	SoundSequenceSlot g_soundSequenceSlots[8];

	// GLOBAL: TOY2 0x0052AD80
	int32_t g_soundSequenceSlotIndex;

	struct SoundSequenceData
	{
		int16_t sequence1[28];
		int16_t sequence2[24];
		int16_t sequence3[22];
		int16_t sequence4[24];
		int16_t sequence5[44];
		int16_t sequence6[14];
	};

	STATIC_ASSERT(sizeof(SoundSequenceData) == 0x138);

	// GLOBAL: TOY2 0x005036F0
	SoundSequenceData g_soundSequenceData = {
#include "SoundSequences.inc"
	};

	// GLOBAL: TOY2 0x00503828
	int16_t* g_sequenceDataPtrs[6] = {
		g_soundSequenceData.sequence1,
		g_soundSequenceData.sequence2,
		g_soundSequenceData.sequence3,
		g_soundSequenceData.sequence4,
		g_soundSequenceData.sequence5,
		g_soundSequenceData.sequence6,
	};

	// Peak sound volumes for the current level. Respawn and EnterLevel reset
	// these to zero. PlaySoundEffect and StartSoundSequenceOnActor raise them
	// when a sequence or one-shot exceeds the current peak. The left/right pair
	// is stored doubled (the source field is a 7-bit value; the engine doubles
	// it to the 8-bit DirectSound range). PlaySoundEffect clamps g_maxVolume to
	// 0xff, which confirms it is a volume.
	// GLOBAL: TOY2 0x0052ef44
	int16_t g_maxLeftVolume;
	// GLOBAL: TOY2 0x0052ef46
	int16_t g_maxRightVolume;
	// GLOBAL: TOY2 0x0052f2da
	int16_t g_maxVolume;

	// GLOBAL: TOY2 0x00830E3C
	int16_t g_dynamicSoundFrequencies[14];

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

	// FUNCTION: TOY2 0x0047D880 [MATCHED]
	void PlayMusicLooping(int16_t trackIndex)
	{
		if (g_audioInitialized)
		{
			if (IsStreamActive())
			{
				StopAndWait();
				while (IsStreamActive()) {}
			}
			g_streamPending = 0;
			PlayTrackByIndex(trackIndex, 1);
			g_loopingMusicTrackIndex = trackIndex;
		}
	}

	enum
	{
		SOUND_BUFFER_GROUP_MASK = 0xFFFF8,
	};

	// FUNCTION: TOY2 0x0047D930 [PROVISIONAL]
	int32_t RestartLoopingSound(int32_t soundId)
	{
		char waveName[WAVE_NAME_LENGTH];
		DWORD status;
		if (g_audioInitialized == 0)
		{
			return 0;
		}

		g_dsResult = g_dsBuffers[soundId]->GetStatus(&status);
		if ((status & DSBSTATUS_BUFFERLOST) != 0)
		{
			int32_t bufferIndex = soundId & SOUND_BUFFER_GROUP_MASK;
			int32_t bufferEnd = bufferIndex + 8;
			for (; bufferIndex < bufferEnd; bufferIndex++)
			{
				g_loopingSoundOwners[bufferIndex] = NULL;
				if (g_audioInitialized != 0)
				{
					g_dsBuffers[bufferIndex]->Release();
				}
			}

			int32_t levelId = g_currentSfxLevelId;
			g_loadedSfxPackIndex = soundId / 8;
			if (g_audioInitialized != 0)
			{
				if (IsStreamActive())
				{
					StopAndWait();
					while (IsStreamActive()) {}
				}

				g_streamPending = 0;
				if (g_audioInitialized != 0)
				{
					ReleaseAllBuffers();
					int32_t i;
					CLEAR_SOUND_BUFFER_TABLES()
				}
			}

			LoadSoundPack(g_primarySoundPacks, 0);
			if (levelId > 0)
			{
				LoadSoundPack(g_primarySoundPacks, levelId);
				if (levelId <= MAX_SFX_LEVEL_ID)
				{
					SoundPackDescriptor* pack = &g_secondarySoundPacks[levelId];
					char** soundName = pack->soundNames;
					int32_t soundIndex = pack->firstSoundIndex;
					while (*soundName != NULL)
					{
						if (**soundName != '\0')
						{
							sprintf(waveName, "%s.wav", *soundName);
							LoadSoundEffect(waveName, soundIndex, 0);
						}
						soundName++;
						soundIndex++;
					}
				}
			}

			g_currentSfxLevelId = levelId;
			g_loadedSfxPackIndex = -1;
		}

		DWORD loopingStatus;
		if (g_audioInitialized == 0)
		{
			loopingStatus = 0;
		}
		else
		{
			g_dsResult = g_dsBuffers[soundId]->GetStatus(&status);
			loopingStatus = status;
		}

		if ((loopingStatus & DSBSTATUS_LOOPING) != 0)
		{
			g_dsResult = g_dsBuffers[soundId]->Play(0, 0, 0);
			if (g_dsResult != DS_OK)
			{
				g_dsResult = g_dsBuffers[soundId]->Stop();
				if (g_dsResult != DS_OK)
				{
					return 1;
				}
			}
			return 0;
		}
		return 1;
	}

	// GLOBAL: TOY2 0x004ECCD8
	const char* g_trackNames[22] = { "house",
		"neighbou",
		"buzvred",
		"constr",
		"alley",
		"slime",
		"als_toyb",
		"spacelnd",
		"buzvbuz",
		"elev",
		"als_pent",
		"buzvzurg",
		"convey",
		"tarmac",
		"buzvpros",
		"miniboss",
		"minirace",
		"over",
		"complete",
		"ygafim",
		"titlescr",
		"levcomp" };

	// FUNCTION: TOY2 0x00436CE0 [MATCHED]
	void QueuePlay(char* path, int32_t looping)
	{
		if (g_streamCommandEvent != NULL)
		{
			strcpy(g_streamPath, path);
			g_streamCommand = 2;
			g_queuedStreamLooping = looping;
			SetEvent(g_streamCommandEvent);
			WaitForSingleObject(g_streamAckEvent, INFINITE);
		}
	}

	// FUNCTION: TOY2 0x00436D80 [PROVISIONAL]
	void FillBuffer()
	{
		uint32_t bytesRead = 0;
		if (g_directSound == NULL || g_dsPrimaryBuffer == NULL)
		{
			return;
		}

		DWORD playCursor;
		DWORD writeCursor;
		if (g_dsPrimaryBuffer->GetCurrentPosition(&playCursor, &writeCursor) == DS_OK)
		{
			uint32_t bytesAdvanced;
			if (playCursor < g_streamLastPlayCursor)
			{
				bytesAdvanced = g_streamBufferBytes - g_streamLastPlayCursor + playCursor;
			}
			else
			{
				bytesAdvanced = playCursor - g_streamLastPlayCursor;
			}
			g_streamLastPlayCursor = playCursor;
			g_streamBytesPlayed += bytesAdvanced;
		}

		void* bufferData;
		DWORD lockedBytes;
		if (g_streamReachedEnd == 0)
		{
			if (g_dsPrimaryBuffer->Lock(g_streamWriteOffset, g_streamFillBytes, &bufferData, &lockedBytes, NULL, NULL, 0) != DS_OK)
			{
				return;
			}

			WaveReadFile(g_waveMmioHandle, lockedBytes, bufferData, &g_streamDataChunk, &bytesRead);
			if (bytesRead < lockedBytes)
			{
				if (g_streamLooping == 0)
				{
					g_streamReachedEnd = 1;
					uint8_t silence = static_cast<WAVEFORMATEX*>(g_waveFormatHandle)->wBitsPerSample == 8 ? 0x80 : 0;
					uint8_t* bufferBytes = static_cast<uint8_t*>(bufferData);
					memset(bufferBytes + bytesRead, silence, lockedBytes - bytesRead);
				}
				else
				{
					uint8_t* bufferBytes = static_cast<uint8_t*>(bufferData);
					uint32_t filledBytes = bytesRead;
					while (filledBytes < lockedBytes)
					{
						if (Wave::SeekToChunk(&g_waveMmioHandle, &g_streamDataChunk, &g_streamParentChunk) != MMSYSERR_NOERROR
							|| WaveReadFile(g_waveMmioHandle, lockedBytes - filledBytes, bufferBytes + filledBytes, &g_streamDataChunk, &bytesRead)
								!= MMSYSERR_NOERROR)
						{
							break;
						}
						filledBytes += bytesRead;
					}
				}
			}

			g_dsPrimaryBuffer->Unlock(bufferData, lockedBytes, NULL, 0);
			g_streamWriteOffset += lockedBytes;
			if (g_streamWriteOffset >= g_streamBufferBytes)
			{
				g_streamWriteOffset -= g_streamBufferBytes;
			}
		}
		else
		{
			if (g_dsPrimaryBuffer->Lock(g_streamWriteOffset, g_streamFillBytes, &bufferData, &lockedBytes, NULL, NULL, 0) == DS_OK)
			{
				uint8_t silence = static_cast<WAVEFORMATEX*>(g_waveFormatHandle)->wBitsPerSample == 8 ? 0x80 : 0;
				memset(bufferData, silence, lockedBytes);
				g_dsPrimaryBuffer->Unlock(bufferData, lockedBytes, NULL, 0);
			}

			uint32_t streamBytes = g_streamParentChunk.cksize;
			if ((streamBytes > g_streamFillBytes && g_streamBytesPlayed >= streamBytes - g_streamFillBytes) || g_streamBytesPlayed >= streamBytes)
			{
				g_streamPlaybackFinished = 1;
			}
		}
	}

	namespace Stream
	{
		// FUNCTION: TOY2 0x00437010 [MATCHED]
		int32_t __cdecl ThreadProc(LPVOID unused)
		{
			g_streamThreadReady = 1;
			do
			{
				while (g_streamFillEvent != NULL)
				{
					DWORD waitResult = WaitForMultipleObjects(3, &g_streamFillEvent, FALSE, INFINITE);
					if (waitResult == WAIT_FAILED)
					{
						break;
					}

					switch (waitResult - WAIT_OBJECT_0)
					{
						case 0:
							if (g_streamPlaybackFinished == 1)
							{
								Stop();
								ResetEvent(g_streamStopEvent);
							}
							else
							{
								FillBuffer();
							}
							break;

						case 1:
							Stop();
							break;

						case 2:
							switch (g_streamCommand)
							{
								case STREAM_COMMAND_STOP:
									Stop();
									ResetEvent(g_streamStopEvent);
									break;

								case STREAM_COMMAND_PLAY:
									ThreadPlay(g_streamPath, g_queuedStreamLooping);
									break;

								case STREAM_COMMAND_EXIT:
									ShutdownHandles();
									break;
							}
							g_streamCommand = STREAM_COMMAND_NONE;
							ReleaseSemaphore(g_streamAckEvent, 1, NULL);
							break;
					}
				}
			} while (g_streamThreadReady != 0);

			return 1;
		}
	}

	// FUNCTION: TOY2 0x0047D8E0 [MATCHED]
	int32_t IsEffectPlaying(int32_t index)
	{
		if (g_audioInitialized == 0)
		{
			return 0;
		}
		DWORD status;
		g_dsResult = g_dsBuffers[index]->GetStatus(&status);
		return status;
	}

	// FUNCTION: TOY2 0x0047DE50 [PROVISIONAL]
	int32_t PlaySoundBuffer(int32_t soundIndex, int32_t leftVolume, int32_t rightVolume, void* owner, int32_t unused, int32_t looping)
	{
		char waveName[WAVE_NAME_LENGTH];
		DWORD status;
		if (g_audioInitialized != 0)
		{
			int32_t firstBuffer = soundIndex * 6;
			if (g_loopingSoundOwners[firstBuffer] != NULL)
			{
				g_dsResult = g_dsBuffers[firstBuffer]->GetStatus(&status);
				// A lost buffer means the device was reset. Release every buffer and
				// load the sound packs of the current level again.
				if ((status & DSBSTATUS_BUFFERLOST) != 0)
				{
					int32_t bufferIndex = firstBuffer & SOUND_BUFFER_GROUP_MASK;
					int32_t bufferEnd = bufferIndex + 8;
					for (; bufferIndex < bufferEnd; bufferIndex++)
					{
						g_loopingSoundOwners[bufferIndex] = NULL;
						if (g_audioInitialized != 0)
						{
							g_dsBuffers[bufferIndex]->Release();
						}
					}

					g_loadedSfxPackIndex = firstBuffer / 8;
					int32_t levelId = g_currentSfxLevelId;
					if (g_audioInitialized != 0)
					{
						if (IsStreamActive())
						{
							StopAndWait();
							while (IsStreamActive()) {}
						}

						g_streamPending = 0;
						if (g_audioInitialized != 0)
						{
							int32_t i;
							for (i = SOUND_BUFFER_COUNT - 1; i >= 0; i--)
							{
								if (g_dsBuffers[i] != NULL)
								{
									DWORD playing;
									if (g_audioInitialized == 0)
									{
										playing = 0;
									}
									else
									{
										g_dsResult = g_dsBuffers[i]->GetStatus(&status);
										playing = status;
									}
									if ((playing & DSBSTATUS_PLAYING) == DSBSTATUS_PLAYING)
									{
										g_dsBuffers[i]->Stop();
									}
									g_dsBuffers[i]->Release();
									g_dsBuffers[i] = NULL;
									g_loopingSoundOwners[i] = NULL;
								}
							}
							CLEAR_SOUND_BUFFER_TABLES()
						}
					}

					SoundPackDescriptor* pack = &g_primarySoundPacks[0];
					char** soundName = pack->soundNames;
					int32_t packSoundIndex = pack->firstSoundIndex;
					while (*soundName != NULL)
					{
						if (**soundName != '\0')
						{
							sprintf(waveName, "%s.wav", *soundName);
							LoadSoundEffect(waveName, packSoundIndex, 0);
						}
						soundName++;
						packSoundIndex++;
					}

					if (levelId > 0)
					{
						if (levelId <= MAX_SFX_LEVEL_ID)
						{
							pack = &g_primarySoundPacks[levelId];
							soundName = pack->soundNames;
							packSoundIndex = pack->firstSoundIndex;
							while (*soundName != NULL)
							{
								if (**soundName != '\0')
								{
									sprintf(waveName, "%s.wav", *soundName);
									LoadSoundEffect(waveName, packSoundIndex, 0);
								}
								soundName++;
								packSoundIndex++;
							}
						}

						if (levelId <= MAX_SFX_LEVEL_ID)
						{
							pack = &g_secondarySoundPacks[levelId];
							soundName = pack->soundNames;
							packSoundIndex = pack->firstSoundIndex;
							while (*soundName != NULL)
							{
								if (**soundName != '\0')
								{
									sprintf(waveName, "%s.wav", *soundName);
									LoadSoundEffect(waveName, packSoundIndex, 0);
								}
								soundName++;
								packSoundIndex++;
							}
						}
					}

					g_currentSfxLevelId = levelId;
					g_loadedSfxPackIndex = -1;
				}

				DWORD oldestPlayCursor = 0;
				int32_t oldestBuffer = -1;
				DWORD frequency = g_soundFreqTable[soundIndex];
				int32_t pan = (rightVolume - leftVolume) * DSBPAN_RIGHT / SOUND_VOLUME_MAX;
				if (pan > DSBPAN_RIGHT)
				{
					pan = DSBPAN_RIGHT;
				}
				else if (pan < DSBPAN_LEFT)
				{
					pan = DSBPAN_LEFT;
				}

				int32_t volume = rightVolume;
				if (volume < leftVolume)
				{
					volume = leftVolume;
				}
				volume = g_dsVolTable[g_sfxVolume * volume / SFX_VOLUME_SCALE];

				if (owner != NULL)
				{
					for (int32_t i = 0; i < 6; i++)
					{
						if (g_loopingSoundOwners[firstBuffer + i] == owner)
						{
							int32_t bufferIndex = firstBuffer + i;
							g_dsResult = g_dsBuffers[bufferIndex]->GetCurrentPosition(&g_soundPlayCursor, &g_soundWriteCursor);
							if (g_dsResult == DS_OK)
							{
								g_dsBuffers[bufferIndex]->SetFrequency(frequency);
								g_dsBuffers[bufferIndex]->SetPan(pan);
								g_dsBuffers[bufferIndex]->SetVolume(volume);
								if (g_soundPlayCursor == 0)
								{
									if (looping != 0)
									{
										g_dsResult = g_dsBuffers[bufferIndex]->Play(0, 0, DSBPLAY_LOOPING);
									}
									else
									{
										g_dsResult = g_dsBuffers[bufferIndex]->Play(0, 0, 0);
									}
								}
							}
							return bufferIndex;
						}
					}
				}
				else
				{
					owner = (void*)1;
				}

				int32_t bufferIndex = firstBuffer;
				for (int32_t i = 0; i < 6; i++)
				{
					if (g_loopingSoundOwners[bufferIndex] != NULL)
					{
						g_dsResult = g_dsBuffers[bufferIndex]->GetCurrentPosition(&g_soundPlayCursor, &g_soundWriteCursor);
						if (g_dsResult == DS_OK)
						{
							if (g_soundPlayCursor == 0)
							{
								g_dsBuffers[bufferIndex]->SetFrequency(frequency);
								g_dsBuffers[bufferIndex]->SetPan(pan);
								g_dsBuffers[bufferIndex]->SetVolume(volume);
								g_dsBuffers[bufferIndex]->SetCurrentPosition(0);
								if (looping != 0)
								{
									g_dsResult = g_dsBuffers[bufferIndex]->Play(0, 0, DSBPLAY_LOOPING);
								}
								else
								{
									g_dsResult = g_dsBuffers[bufferIndex]->Play(0, 0, 0);
								}
								g_loopingSoundOwners[bufferIndex] = owner;
								return bufferIndex;
							}
							if (oldestPlayCursor < g_soundPlayCursor)
							{
								oldestPlayCursor = g_soundPlayCursor;
								oldestBuffer = bufferIndex;
							}
						}
					}
					bufferIndex++;
				}

				// No free buffer: reuse the one that has played for the longest time.
				if (oldestBuffer != -1)
				{
					g_dsBuffers[oldestBuffer]->SetFrequency(frequency);
					g_dsBuffers[oldestBuffer]->SetPan(pan);
					g_dsBuffers[oldestBuffer]->SetVolume(volume);
					g_dsBuffers[oldestBuffer]->SetCurrentPosition(0);
					if (looping != 0)
					{
						g_dsResult = g_dsBuffers[oldestBuffer]->Play(0, 0, DSBPLAY_LOOPING);
					}
					else
					{
						g_dsResult = g_dsBuffers[oldestBuffer]->Play(0, 0, 0);
					}
					g_loopingSoundOwners[oldestBuffer] = owner;
					return oldestBuffer;
				}
			}
		}
		return -1;
	}

	namespace Wave
	{
	}

	namespace Stream
	{
	}
}
