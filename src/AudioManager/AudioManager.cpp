#include "AudioManager/AudioManager.h"
#include "FileUtils.h"
#include "Logger.h"
#include <cstring>
#include <directx6/dsound.h>

namespace AudioManager
{
	// GLOBAL: TOY2 0x005282CC
	int32_t g_curTrackIndex;

	// GLOBAL: TOY2 0x00559C68
	int32_t g_loopingMusicTrackIndex;

	// GLOBAL: TOY2 0x00724E7C
	int32_t g_dsResult;

	// GLOBAL: TOY2 0x00724E80
	int32_t g_audioInitialized;

	// GLOBAL: TOY2 0x00724E84
	int32_t g_streamState;

	// GLOBAL: TOY2 0x00726F3C
	int32_t g_streamPending;

	// GLOBAL: TOY2 0x005028A8
	int16_t g_musicVolTable[12] = { 0, 9, 18, 27, 36, 45, 54, 63, 72, 81, 90, 99 };

	// GLOBAL: TOY2 0x005028C8
	int16_t g_soundVolTable[12] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88 };

	// GLOBAL: TOY2 0x004FCDB0
	int32_t g_sfxVolume;

	// GLOBAL: TOY2 0x005282C8
	int32_t g_musicVolumeLevel;

	// GLOBAL: TOY2 0x005282C0
	HANDLE g_streamCommandEvent;

	// GLOBAL: TOY2 0x005282C4
	HANDLE g_streamAckEvent;

	// GLOBAL: TOY2 0x005282FC
	int32_t g_streamActive;

	// GLOBAL: TOY2 0x0053C844
	int32_t g_streamFadeMode;

	// GLOBAL: TOY2 0x0053C848
	int32_t g_streamCommand;

	// GLOBAL: TOY2 0x0053C84C
	char g_streamPath[512];

	// GLOBAL: TOY2 0x0053CA50
	int32_t g_streamThreadReady;

	// GLOBAL: TOY2 0x00725E94
	int32_t g_pendingStreamTrack;

	// GLOBAL: TOY2 0x00725E98
	int32_t g_pendingStreamNoFade;

	// GLOBAL: TOY2 0x00830E58
	int16_t g_loopingSoundChannels[32][5];

	// GLOBAL: TOY2 0x00725294
	void* g_loopingSoundOwners[768];

	// GLOBAL: TOY2 0x00726230
	int32_t g_loadedBufferCount;

	// GLOBAL: TOY2 0x00726338
	LPDIRECTSOUNDBUFFER g_dsBuffers[768];

	// GLOBAL: TOY2 0x005282F0
	LPDIRECTSOUNDBUFFER g_dsPrimaryBuffer;

	// GLOBAL: TOY2 0x005282F4
	LPDIRECTSOUNDBUFFER g_dsSecondaryBuffer;

	// GLOBAL: TOY2 0x005281D8
	HGLOBAL g_waveFormatHandle;

	// GLOBAL: TOY2 0x00725290
	HGLOBAL g_sfxWaveFormatHandle;

	// GLOBAL: TOY2 0x005281DC
	HMMIO g_waveMmioHandle;

	// GLOBAL: TOY2 0x00725F24
	LPDIRECTSOUND g_directSound;

	// GLOBAL: TOY2 0x004FCDBC
	int32_t g_loadedSfxPackIndex;

	// GLOBAL: TOY2 0x00724E88
	void* g_loadedWaveData;

	// GLOBAL: TOY2 0x00725F28
	int32_t g_loadedWaveFormatSize;

	// GLOBAL: TOY2 0x00725F2C
	int32_t g_loadedWaveBytes;

	// GLOBAL: TOY2 0x00726234
	uint16_t g_soundFreqTable[128];

	// GLOBAL: TOY2 0x00534074
	char g_sfxSubPath[8];

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

	// FUNCTION: TOY2 0x0047E4E0
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

	// STUB: TOY2 0x004A4680
	int32_t WaveLoadFile(char* path, int32_t* outBytes, int32_t* outFormatSize, HGLOBAL* outFormatHandle, void** outData) { return 0; }

	// FUNCTION: TOY2 0x0047E5B0 [MATCHED]
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
					DWORD status;
					DWORD playing;
					if (g_audioInitialized == 0)
					{
						playing = 0;
					}
					else
					{
						g_dsResult = buf->GetStatus(&status);
						playing = status;
					}
					if ((playing & 1) == 1)
					{
						g_dsBuffers[i]->Stop();
					}
					g_dsBuffers[i]->Release();
					g_dsBuffers[i] = NULL;
					g_loopingSoundOwners[i] = NULL;
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
						DWORD status;
						DWORD playing;
						if (g_audioInitialized == 0)
						{
							playing = 0;
						}
						else
						{
							g_dsResult = buf->GetStatus(&status);
							playing = status;
						}
						if ((playing & 1) == 1)
						{
							g_dsBuffers[i]->Stop();
						}
						g_dsBuffers[i]->Release();
						g_dsBuffers[i] = NULL;
						g_loopingSoundOwners[i] = NULL;
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
			for (i = 0; i < 768; i++)
			{
				g_dsBuffers[i] = NULL;
			}
			for (i = 0; i < 768; i++)
			{
				g_loopingSoundOwners[i] = NULL;
			}
			g_streamState = 0;
			g_audioInitialized = 0;
		}
	}

	// STUB: TOY2 0x0047EDE0
	void Init() {}

	// FUNCTION: TOY2 0x0049AE20 [MATCHED]
	void SetSfxVolume(int32_t sfxVolume) { g_sfxVolume = (sfxVolume & 0xff) << 1; }

	// FUNCTION: TOY2 0x0049AE40 [MATCHED]
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

	// FUNCTION: TOY2 0x0049E8D0 [MATCHED]
	void SetVolumesProcessed(int32_t musicVolume, int32_t sfxVolume) { SetVolumes(g_musicVolTable[musicVolume] * 2 / 3, g_soundVolTable[sfxVolume] * 3 / 2); }

	// FUNCTION: TOY2 0x004A3EF0 [MATCHED]
	void FlushSoundVoices()
	{
		Logger::Log("FlushSoundVoices : Start.\n");
		Logger::Log("FlushSoundVoices : End.\n");
	}

	// FUNCTION: TOY2 0x00436C90
	void SignalThreadExit()
	{
		if (g_streamCommandEvent != NULL)
		{
			g_streamCommand = 3;
			g_streamThreadReady = 0;
			SetEvent(g_streamCommandEvent);
		}
	}

	// FUNCTION: TOY2 0x00436CC0
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

	// FUNCTION: TOY2 0x0047D880
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

	// FUNCTION: TOY2 0x004A3980
	int32_t PlayLoopingSound3D(void* owner, int32_t soundIndex, int32_t volume, int32_t leftVolume, int16_t rightVolume)
	{
		int32_t leftVol = leftVolume;
		if (leftVol < 0)
		{
			leftVol = 0;
		}
		else if (leftVol > 0x96)
		{
			leftVol = 0x96;
		}
		int32_t rightVol = leftVolume;
		if (rightVol < 0)
		{
			rightVol = 0;
		}
		else if (rightVol > 0x96)
		{
			rightVol = 0x96;
		}

		int32_t foundIndex = -1;
		int16_t* chan = &g_loopingSoundChannels[0][0];
		void** ownerp = g_loopingSoundOwners;
		int32_t i = 0;
		do
		{
			if (chan[0] != -1 && *ownerp == owner)
			{
				foundIndex = i;
			}
			chan += 5;
			ownerp++;
			i++;
		} while (chan < &g_loopingSoundChannels[32][0]);

		if (foundIndex != -1)
		{
			g_loopingSoundChannels[foundIndex][3] = (int16_t)volume;
			if (g_loopingSoundChannels[foundIndex][4] <= 0)
			{
				g_loopingSoundChannels[foundIndex][4] = rightVolume;
				g_loopingSoundChannels[foundIndex][3] = (int16_t)volume;
			}
			else
			{
				g_loopingSoundChannels[foundIndex][4] = g_loopingSoundChannels[foundIndex][4] - 1;
			}
			if (g_loopingSoundChannels[foundIndex][3] > g_loopingSoundChannels[foundIndex][2])
			{
				g_loopingSoundChannels[foundIndex][2] = g_loopingSoundChannels[foundIndex][2] + 0x40;
			}
			if (g_loopingSoundChannels[foundIndex][3] < g_loopingSoundChannels[foundIndex][2])
			{
				g_loopingSoundChannels[foundIndex][2] = g_loopingSoundChannels[foundIndex][2] - 0x40;
			}
			int32_t soundId = PlaySoundBuffer(soundIndex + 1, leftVol, rightVol, (int32_t)owner, g_loopingSoundChannels[foundIndex][2], 1);
			g_loopingSoundChannels[foundIndex][0] = (int16_t)soundId;
			g_loopingSoundChannels[foundIndex][1] = 0x10;
			return (leftVol + rightVol) / 2;
		}

		int32_t soundId = PlaySoundBuffer(soundIndex + 1, leftVol, rightVol, (int32_t)owner, volume, 1);
		if (soundId != -1)
		{
			int32_t freeIndex = -1;
			int16_t* p = &g_loopingSoundChannels[0][0];
			int32_t j = 0;
			do
			{
				if (p[0] == (int16_t)soundId)
				{
					g_loopingSoundChannels[j][1] = 0x10;
					g_loopingSoundChannels[j][2] = (int16_t)volume;
					g_loopingSoundChannels[j][3] = (int16_t)volume;
					g_loopingSoundChannels[j][4] = rightVolume;
					return (leftVol + rightVol) / 2;
				}
				if (p[0] == -1)
				{
					freeIndex = j;
				}
				p += 5;
				j++;
			} while (p < &g_loopingSoundChannels[32][0]);

			if (freeIndex != -1)
			{
				g_loopingSoundChannels[freeIndex][0] = (int16_t)soundId;
				g_loopingSoundChannels[freeIndex][1] = 0x10;
				g_loopingSoundChannels[freeIndex][2] = (int16_t)volume;
				g_loopingSoundChannels[freeIndex][3] = (int16_t)volume;
				g_loopingSoundChannels[freeIndex][4] = rightVolume;
			}
		}
		return (leftVol + rightVol) / 2;
	}

	// FUNCTION: TOY2 0x004A3B90
	int32_t PlayLoopingSound3DPositional(void* owner, int32_t soundIndex, int32_t volume, int32_t leftVolume, void* unused, int32_t rightVolume)
	{ return PlayLoopingSound3D(owner, soundIndex, volume, leftVolume, rightVolume); }

	// STUB: TOY2 0x0047D930
	int32_t RestartLoopingSound(int32_t soundId) { return 0; }

	// FUNCTION: TOY2 0x004A3BC0
	void ResetChannelsTable()
	{
		int16_t* p = &g_loopingSoundChannels[0][0];
		do
		{
			*p = -1;
			p += 5;
		} while (p < &g_loopingSoundChannels[32][0]);
	}

	// FUNCTION: TOY2 0x004A3BE0
	void UpdateChannels()
	{
		if (g_streamPending != 0 && --g_streamPending == 0)
		{
			if (g_pendingStreamNoFade != 0)
			{
				PlayTrackByIndex(g_pendingStreamTrack, 0);
			}
			else
			{
				PlayTrackByIndex(g_pendingStreamTrack, 1);
			}
		}
		int16_t* p = &g_loopingSoundChannels[0][1];
		do
		{
			if (p[-1] != -1 && *p != -1)
			{
				if (--*p <= 0)
				{
					if (RestartLoopingSound(p[-1]) == 1)
					{
						*p = 1;
					}
					else
					{
						p[-1] = -1;
					}
				}
			}
			p += 5;
		} while (p < &g_loopingSoundChannels[32][1]);
	}

	// FUNCTION: TOY2 0x004A3ED0 [MATCHED]
	void SetVolumes(int32_t musicVolume, int32_t sfxVolume)
	{
		SetMusicVolume(musicVolume);
		SetSfxVolume(sfxVolume);
	}

	// FUNCTION: TOY2 0x00413300 [MATCHED]
	int32_t IsStreamActive() { return g_streamActive; }

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

	// FUNCTION: TOY2 0x00436CE0
	void QueuePlay(char* path, int32_t fadeMode)
	{
		if (g_streamCommandEvent != NULL)
		{
			strcpy(g_streamPath, path);
			g_streamCommand = 2;
			g_streamFadeMode = fadeMode;
			SetEvent(g_streamCommandEvent);
			WaitForSingleObject(g_streamAckEvent, INFINITE);
		}
	}

	// FUNCTION: TOY2 0x00413140
	void OnExit() { SignalThreadExit(); }

	// FUNCTION: TOY2 0x00413150
	int32_t PlayTrackByIndex(int32_t trackIndex, int32_t fadeMode)
	{
		char buffer[512];

		StopAndWait();
		if (trackIndex >= 0x16 || trackIndex < 0)
			return 0;

		FileUtils::AppendCDPath(buffer);
		strcat(buffer, "audio\\");
		strcat(buffer, g_trackNames[trackIndex]);
		strcat(buffer, ".wav");
		QueuePlay(buffer, fadeMode);
		g_curTrackIndex = trackIndex;
		return 1;
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

	// STUB: TOY2 0x0047DE50
	int32_t PlaySoundBuffer(int32_t soundIndex, int32_t leftVolume, int32_t rightVolume, int32_t pan, int32_t volume, int32_t flags) { return 0; }

	namespace Wave
	{
		// FUNCTION: TOY2 0x004A4200
		int32_t CloseFile(HMMIO* hmmio, HGLOBAL* dataHandle)
		{
			if (*dataHandle != NULL)
			{
				GlobalFree(*dataHandle);
				*dataHandle = NULL;
			}
			if (*hmmio != NULL)
			{
				mmioClose(*hmmio, 0);
				*hmmio = NULL;
			}
			return 0;
		}
	}

	namespace Stream
	{
		// FUNCTION: TOY2 0x004132A0
		void Stop()
		{
			if (g_directSound != NULL && g_dsPrimaryBuffer != NULL)
			{
				g_dsPrimaryBuffer->Stop();
				Wave::CloseFile(&g_waveMmioHandle, &g_waveFormatHandle);
				g_dsSecondaryBuffer->Release();
				g_dsSecondaryBuffer = NULL;
				g_dsPrimaryBuffer->Release();
				g_dsPrimaryBuffer = NULL;
			}
		}
	}
}
