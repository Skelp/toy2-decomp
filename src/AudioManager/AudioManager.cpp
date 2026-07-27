#include "AudioManager/AudioManager.h"
#include "FileUtils.h"
#include "Logger.h"
#include "Nu3D/Camera.h"
#include "Numerics.h"
#include <math.h>
#include <cstring>
#include <stdio.h>
#include <directx6/dsound.h>

namespace AudioManager
{
	// STUB: TOY2 0x0049E660
	void PlaySoundEffect(int32_t soundIndex, int32_t flags) {}

	// GLOBAL: TOY2 0x005282CC
	int32_t g_curTrackIndex;

	// GLOBAL: TOY2 0x00559C68
	int32_t g_loopingMusicTrackIndex;

	// GLOBAL: TOY2 0x00724E7C
	int32_t g_dsResult;

	// GLOBAL: TOY2 0x00724E80
	int32_t g_audioInitialized;

	// GLOBAL: TOY2 0x00724E84
	int32_t g_deviceCount;

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
	LPGUID g_deviceGuids[16];

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

	// GLOBAL: TOY2 0x00724E8C
	char g_deviceNames[16][32];

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

	// FUNCTION: TOY2 0x004A4130
	MMRESULT WaveReadFile(HMMIO hmmio, uint32_t size, void* buffer, MMCKINFO* chunk, uint32_t* outRead)
	{
		MMIOINFO info;
		MMRESULT result = mmioGetInfo(hmmio, &info, 0);
		if (result == 0)
		{
			uint32_t cksize = chunk->cksize;
			if (size > cksize)
			{
				size = cksize;
			}
			chunk->cksize = cksize - size;
			uint32_t count = 0;
			if (size > 0)
			{
				HPSTR pchEndRead = info.pchEndRead;
				do
				{
					if (info.pchNext == pchEndRead)
					{
						result = mmioAdvance(hmmio, &info, 0);
						if (result != 0)
						{
							goto fail;
						}
						pchEndRead = info.pchEndRead;
						if (info.pchNext == pchEndRead)
						{
							goto emptyBuffer;
						}
					}
					((char*)buffer)[count++] = *info.pchNext++;
				} while (count < size);
			}
			result = mmioSetInfo(hmmio, &info, 0);
			if (result == 0)
			{
				goto success;
			}
		}
	fail:
		*outRead = 0;
		return result;
	emptyBuffer:
		*outRead = 0;
		return 0xe103;
	success:
		*outRead = size;
		return result;
	}

	// FUNCTION: TOY2 0x004A4680
	int32_t WaveLoadFile(char* path, int32_t* outBytes, int32_t* outFormatSize, HGLOBAL* outFormatHandle, void** outData)
	{
		HMMIO hmmio;
		MMCKINFO parentChunk;
		MMCKINFO dataChunk;
		HGLOBAL data;
		uint32_t bytesRead;
		*outData = NULL;
		*outFormatHandle = NULL;
		*outBytes = 0;
		MMRESULT result = Wave::OpenFile(path, &hmmio, outFormatHandle, &parentChunk);
		if (result != 0)
		{
			Logger::DebugLog("Unable to open %s\r\n", path);
			goto cleanup;
		}
		mmioSeek(hmmio, parentChunk.dwDataOffset + 4, SEEK_SET);
		dataChunk.ckid = mmioFOURCC('d', 'a', 't', 'a');
		result = mmioDescend(hmmio, &dataChunk, &parentChunk, MMIO_FINDCHUNK);
		if (result != 0)
		{
			goto cleanup;
		}
		data = GlobalAlloc(GMEM_FIXED, dataChunk.cksize);
		*outData = data;
		if (data == NULL)
		{
			result = 0xe000;
			goto cleanup;
		}
		result = WaveReadFile(hmmio, dataChunk.cksize, data, &dataChunk, &bytesRead);
		if (result != 0)
		{
		cleanup:
			if (*outData != NULL)
			{
				GlobalFree(*outData);
				*outData = NULL;
			}
			if (*outFormatHandle != NULL)
			{
				GlobalFree(*outFormatHandle);
				*outFormatHandle = NULL;
			}
		}
		else
		{
			*outBytes = bytesRead;
		}
		if (hmmio != NULL)
		{
			mmioClose(hmmio, 0);
		}
		return result;
	}

	struct SfxPackEntry
	{
		char** filenames;
		int32_t baseIndex;
	};

	void LoadSoundEffect(char* name, int32_t index, int32_t flag);

	// FUNCTION: TOY2 0x0047D670 [MATCHED]
	void LoadSoundPack(SfxPackEntry* table, int32_t index)
	{
		if (index >= 0 && index <= 0x10)
		{
			char** filenames = table[index].filenames;
			int32_t baseIndex = table[index].baseIndex;
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
			g_deviceCount = 0;
			g_audioInitialized = 0;
		}
	}

	// FUNCTION: TOY2 0x0047E3C0
	BOOL CALLBACK Enumerate(LPGUID lpGuid, LPCSTR lpcstrDescription, LPCSTR lpcstrModule, LPVOID lpContext)
	{
		int32_t index = g_deviceCount;
		strcpy(g_deviceNames[index], lpcstrDescription);
		g_deviceGuids[index] = lpGuid;
		g_deviceCount = index + 1;
		return TRUE;
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

	// FUNCTION: TOY2 0x004A3C80
	int32_t PlayOneShotSound3DActor(void* actor, int32_t soundIndex, int32_t frequency, int32_t volume, void* position, int32_t flag)
	{
		int32_t leftVolume;
		int32_t rightVolume;
		if (position != NULL)
		{
			const Vector3I* soundPosition = (const Vector3I*)position;
			int32_t z = (soundPosition->z >> 9) - (Nu3D::Camera::g_fixedViewPosition.z >> 9);
			int32_t y = (soundPosition->y >> 9) - (Nu3D::Camera::g_fixedViewPosition.y >> 9);
			int32_t x = (soundPosition->x >> 9) - (Nu3D::Camera::g_fixedViewPosition.x >> 9);

			const Nu3D::Camera::FixedViewTransform& view = Nu3D::Camera::g_fixedViewTransform;
			int32_t viewX = (view.m00 * x + view.m01 * y + view.m02 * z) / 4096;
			int32_t viewY = (view.m10 * x + view.m11 * y + view.m12 * z) / 4096 / 2;
			int32_t viewYSquared = viewY * viewY;

			int32_t distance = (int32_t)sqrt((double)((viewX + 64) * (viewX + 64) + viewYSquared));
			leftVolume = (192 - distance) * 16 / 24;
			distance = (int32_t)sqrt((double)((viewX - 64) * (viewX - 64) + viewYSquared));
			rightVolume = (192 - distance) * 16 / 24;
		}
		else
		{
			leftVolume = volume;
			rightVolume = volume;
		}

		if (leftVolume < 0)
		{
			leftVolume = 0;
		}
		else if (leftVolume > 150)
		{
			leftVolume = 150;
		}
		if (rightVolume < 0)
		{
			rightVolume = 0;
		}
		else if (rightVolume > 150)
		{
			rightVolume = 150;
		}

		int32_t soundId = PlaySoundBuffer(soundIndex + 1, leftVolume, rightVolume, (int32_t)actor, 0, 0);
		if (soundId != -1)
		{
			int32_t freeIndex = -1;
			for (int32_t i = 0; i < 32; i++)
			{
				if (g_loopingSoundChannels[i][0] == soundId)
				{
					g_loopingSoundChannels[i][1] = -1;
					return (leftVolume + rightVolume) / 2;
				}
				if (g_loopingSoundChannels[i][0] == -1)
				{
					freeIndex = i;
				}
			}
			if (freeIndex != -1)
			{
				g_loopingSoundChannels[freeIndex][0] = (int16_t)soundId;
				g_loopingSoundChannels[freeIndex][1] = -1;
			}
		}

		return (leftVolume + rightVolume) / 2;
	}

	// A 16-byte preset table entry. The preset functions read the first three
	// int16 fields. The roles of the remaining bytes are not known.
	struct OneShotSoundPreset
	{
		int16_t soundIndex;
		int16_t frequency;
		int16_t volume;
		int16_t reserved0;
		int32_t reserved1;
		int32_t reserved2;
	};

	// GLOBAL: TOY2 0x00502950
	OneShotSoundPreset g_oneShotPresets[218];

	namespace Preset
	{
		// FUNCTION: TOY2 0x0049EA60 [MATCHED]
		void PlayOneShotSound2(int32_t index, void* actor)
		{
			OneShotSoundPreset& preset = g_oneShotPresets[index];
			PlayOneShotSound3DActor(actor, preset.soundIndex - 1, preset.frequency, preset.volume, actor, 0);
		}

		// FUNCTION: TOY2 0x0049EA90 [MATCHED]
		void PlayOneShotSound(int32_t index, void* actor)
		{
			OneShotSoundPreset& preset = g_oneShotPresets[index];
			PlayOneShotSound3DActor(actor, preset.soundIndex - 1, preset.frequency, preset.volume, actor, 1);
		}
	}

	// A sound-sequence slot. The engine runs up to eight concurrent
	// sequences. Each slot holds the listener position, a cursor into the
	// sequence event data, and a countdown timer. ClearSequence7Cursor resets
	// slot 7, the slot that StartSoundSequenceOnActor initializes.
	struct SoundSequenceSlot
	{
		Vector3I position; // +0x00
		uint8_t* cursor; // +0x0c
		int16_t timer; // +0x10
		int16_t reserved; // +0x12
	};

	// GLOBAL: TOY2 0x0052f120
	SoundSequenceSlot g_soundSequenceSlots[8];

	// FUNCTION: TOY2 0x0049E9C0 [MATCHED]
	void ClearSequence7Cursor() { g_soundSequenceSlots[7].cursor = 0; }

	// Pointer table for the six sequence-data blobs. Callers pass a negative
	// identifier (-1..-6); the access is g_sequenceDataPtrs[-sequenceId - 1].
	// A NULL sentinel follows the six valid entries. The build leaves the
	// table zero-initialized; reccmp compares only .text, so the referencing
	// code resolves by symbol name.
	// GLOBAL: TOY2 0x00503828
	int16_t* g_sequenceDataPtrs[7];

	// Header at the start of each sequence-data blob: three peak-volume
	// fields that the engine doubles into the 8-bit DirectSound range.
	struct SequenceHeader
	{
		int16_t leftVolume; // +0x00
		int16_t rightVolume; // +0x02
		int16_t volume; // +0x04
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

	// FUNCTION: TOY2 0x0049E910
	void StartSoundSequenceOnActor(int32_t sequenceId, Vector3I* position)
	{
		SoundSequenceSlot& slot = g_soundSequenceSlots[7];
		slot.position.x = position->x;
		slot.position.y = position->y;
		slot.position.z = position->z;
		slot.timer = 0;

		SequenceHeader* header = (SequenceHeader*)g_sequenceDataPtrs[-sequenceId - 1];
		slot.cursor = (uint8_t*)(header + 1);

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
			if (g_deviceGuids[0] != NULL)
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

	// FUNCTION: TOY2 0x004A3E60
	int32_t IsActorSoundPlaying(void* owner)
	{
		if (g_audioInitialized != 0)
		{
			int16_t* chan = &g_loopingSoundChannels[0][0];
			int32_t i = 0;
			do
			{
				if (chan[0] != -1 && chan[1] == -1 && g_loopingSoundOwners[chan[0]] == owner)
				{
					goto found;
				}
				chan += 5;
				i++;
			} while (chan < &g_loopingSoundChannels[32][0]);
			return 0;
		found:
			if ((IsEffectPlaying(g_loopingSoundChannels[i][0]) & 1) != 0)
			{
				return 1;
			}
			g_loopingSoundChannels[i][0] = -1;
		}
		return 0;
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

		// FUNCTION: TOY2 0x004A3F10
		MMRESULT OpenFile(LPSTR path, HMMIO* outHmmio, HGLOBAL* outFormatHandle, MMCKINFO* parentChunk)
		{
			*outFormatHandle = NULL;
			HMMIO hmmio = NULL;
			MMRESULT result;
			FILE* file = fopen(path, "rb");
			if (file != NULL)
			{
				fclose(file);
				hmmio = mmioOpen(path, NULL, MMIO_ALLOCBUF);
				if (hmmio != NULL)
				{
					result = mmioDescend(hmmio, parentChunk, NULL, 0);
					if (result != 0)
					{
						goto cleanup;
					}
					if (parentChunk->ckid != mmioFOURCC('R', 'I', 'F', 'F') || parentChunk->fccType != mmioFOURCC('W', 'A', 'V', 'E'))
					{
						goto badFormat;
					}
					MMCKINFO fmtChunk;
					fmtChunk.ckid = mmioFOURCC('f', 'm', 't', ' ');
					result = mmioDescend(hmmio, &fmtChunk, parentChunk, MMIO_FINDCHUNK);
					if (result != 0)
					{
						goto cleanup;
					}
					if (fmtChunk.cksize < 0x10)
					{
					badFormat:
						result = 0xe101;
						goto cleanup;
					}
					PCMWAVEFORMAT format;
					if (mmioRead(hmmio, (HPSTR)&format, 0x10) != 0x10)
					{
						result = 0xe102;
						goto cleanup;
					}
					uint32_t cbSize;
					if (format.wf.wFormatTag == WAVE_FORMAT_PCM)
					{
						cbSize = 0;
					}
					else
					{
						if (mmioRead(hmmio, (HPSTR)&cbSize, 2) != 2)
						{
							result = 0xe102;
							goto cleanup;
						}
					}
					HGLOBAL h;
					h = GlobalAlloc(GMEM_FIXED, (cbSize & 0xffff) + sizeof(WAVEFORMATEX));
					*outFormatHandle = h;
					if (h == NULL)
					{
						result = 0xe000;
						goto cleanup;
					}
					*(PCMWAVEFORMAT*)h = format;
					((WAVEFORMATEX*)*outFormatHandle)->cbSize = (uint16_t)cbSize;
					if (cbSize != 0)
					{
						if (mmioRead(hmmio, (HPSTR)((char*)h + sizeof(WAVEFORMATEX)), cbSize & 0xffff) != (cbSize & 0xffff))
						{
							result = 0xe101;
							goto cleanup;
						}
					}
					result = mmioAscend(hmmio, &fmtChunk, 0);
					if (result != 0)
					{
						goto cleanup;
					}
					*outHmmio = hmmio;
					return result;
				}
			}
			result = 0xe100;
		cleanup:
			if (*outFormatHandle != NULL)
			{
				GlobalFree(*outFormatHandle);
				*outFormatHandle = NULL;
			}
			if (hmmio == NULL)
			{
				*outHmmio = NULL;
				return result;
			}
			mmioClose(hmmio, 0);
			*outHmmio = NULL;
			return result;
		}

		// FUNCTION: TOY2 0x004A40F0 [MATCHED]
		MMRESULT SeekToChunk(HMMIO* hmmio, MMCKINFO* dataChunk, MMCKINFO* parentChunk)
		{
			mmioSeek(*hmmio, parentChunk->dwDataOffset + 4, 0);
			dataChunk->ckid = mmioFOURCC('d', 'a', 't', 'a');
			return mmioDescend(*hmmio, dataChunk, parentChunk, MMIO_FINDCHUNK);
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
