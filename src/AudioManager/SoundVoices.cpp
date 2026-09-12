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

// The sound voices and the wave files they read: retail holds 0x004A37E0
// through 0x004A4680 as one object, in the order of this file.
namespace AudioManager
{
	// FUNCTION: TOY2 0x004A37E0 [MATCHED]
	int32_t PlayOneShotSoundGlobal(int32_t soundIndex, int32_t volume, int32_t leftVolume, int32_t rightVolume)
	{ return PlaySoundBuffer(soundIndex + 1, leftVolume, rightVolume, 0, volume, 0); }

	// FUNCTION: TOY2 0x004A3810 [PROVISIONAL]
	int32_t PlayOneShotSound3D(int32_t soundIndex, int32_t frequency, int32_t volume, const Vector3I* position)
	{
		int32_t z = position->z - Nu3D::Camera::g_fixedViewPosition.z;
		int32_t y = position->y - Nu3D::Camera::g_fixedViewPosition.y;
		int32_t x = position->x - Nu3D::Camera::g_fixedViewPosition.x;

		const Nu3D::Camera::FixedViewTransform& view = Nu3D::Camera::g_fixedViewTransform;
		int32_t viewX = (view.rotation.m00 * x + view.rotation.m01 * y + view.rotation.m02 * z) / 0x20000;
		int32_t viewY = (view.rotation.m20 * x + view.rotation.m21 * y + view.rotation.m22 * z) / 0x20000 / 2;
		int32_t viewYSquared = viewY * viewY;

		int32_t distance = (int32_t)sqrt((double)((viewX + 0x400) * (viewX + 0x400) + viewYSquared));
		int32_t leftVolume = ((0xc00 - distance) / 24) * volume / 128;
		distance = (int32_t)sqrt((double)((viewX - 0x400) * (viewX - 0x400) + viewYSquared));
		int32_t rightVolume = ((0xc00 - distance) / 24) * volume / 128;

		if (leftVolume < 0)
		{
			leftVolume = 0;
		}
		else if (leftVolume > 128)
		{
			leftVolume = 128;
		}
		if (rightVolume < 0)
		{
			rightVolume = 0;
		}
		else if (rightVolume > 128)
		{
			rightVolume = 128;
		}

		PlaySoundBuffer(soundIndex + 1, leftVolume, rightVolume, 0, frequency, 0);
		return (leftVolume + rightVolume) / 2;
	}

	// FUNCTION: TOY2 0x004A3980 [PROVISIONAL]
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
		} while (chan < &g_loopingSoundChannels[LOOPING_SOUND_CHANNEL_COUNT][0]);

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
			int32_t soundId = PlaySoundBuffer(soundIndex + 1, leftVol, rightVol, owner, g_loopingSoundChannels[foundIndex][2], 1);
			g_loopingSoundChannels[foundIndex][0] = (int16_t)soundId;
			g_loopingSoundChannels[foundIndex][1] = 0x10;
			return (leftVol + rightVol) / 2;
		}

		int32_t soundId = PlaySoundBuffer(soundIndex + 1, leftVol, rightVol, owner, volume, 1);
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
			} while (p < &g_loopingSoundChannels[LOOPING_SOUND_CHANNEL_COUNT][0]);

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

	// FUNCTION: TOY2 0x004A3B90 [MATCHED]
	int32_t PlayLoopingSound3DPositional(void* owner, int32_t soundIndex, int32_t volume, int32_t leftVolume, void* unused, int32_t rightVolume)
	{ return PlayLoopingSound3D(owner, soundIndex, volume, leftVolume, rightVolume); }

	// FUNCTION: TOY2 0x004A3BC0 [TOOL]
	void ResetChannelsTable()
	{
		for (int32_t i = 0; i < LOOPING_SOUND_CHANNEL_COUNT; i++)
			g_loopingSoundChannels[i][0] = -1;
	}

	// FUNCTION: TOY2 0x004A3BE0 [PROVISIONAL]
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
		} while (p < &g_loopingSoundChannels[LOOPING_SOUND_CHANNEL_COUNT][1]);
	}

	// FUNCTION: TOY2 0x004A3C80 [PROVISIONAL]
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
			int32_t viewX = (view.rotation.m00 * x + view.rotation.m01 * y + view.rotation.m02 * z) / 4096;
			int32_t viewY = (view.rotation.m20 * x + view.rotation.m21 * y + view.rotation.m22 * z) / 4096 / 2;
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

		int32_t soundId = PlaySoundBuffer(soundIndex + 1, leftVolume, rightVolume, actor, 0, 0);
		if (soundId != -1)
		{
			int32_t freeIndex = -1;
			for (int32_t i = 0; i < LOOPING_SOUND_CHANNEL_COUNT; i++)
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

	// FUNCTION: TOY2 0x004A3E60 [PROVISIONAL]
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
			} while (chan < &g_loopingSoundChannels[LOOPING_SOUND_CHANNEL_COUNT][0]);
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

	// FUNCTION: TOY2 0x004A3EF0 [MATCHED]
	void FlushSoundVoices()
	{
		Logger::Log("FlushSoundVoices : Start.\n");
		Logger::Log("FlushSoundVoices : End.\n");
	}

}

namespace AudioManager
{
	namespace Wave
	{
		// FUNCTION: TOY2 0x004A3F10 [PROVISIONAL]
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
						result = 0xe101;
						goto cleanup;
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
						result = 0xe101;
						goto cleanup;
					}
					PCMWAVEFORMAT format;
					if (mmioRead(hmmio, (HPSTR)&format, 0x10) != 0x10)
					{
						result = 0xe102;
						goto cleanup;
					}
					uint16_t cbSize;
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
					*outFormatHandle = GlobalAlloc(GMEM_FIXED, cbSize + sizeof(WAVEFORMATEX));
					if (*outFormatHandle == NULL)
					{
						result = 0xe000;
						goto cleanup;
					}
					*(PCMWAVEFORMAT*)*outFormatHandle = format;
					((WAVEFORMATEX*)*outFormatHandle)->cbSize = cbSize;
					if (cbSize != 0)
					{
						if (mmioRead(hmmio, (HPSTR)((char*)*outFormatHandle + sizeof(WAVEFORMATEX)), cbSize) != cbSize)
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
}

namespace AudioManager
{
	// FUNCTION: TOY2 0x004A4130 [PROVISIONAL]
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
							result = 0xe103;
							goto fail;
						}
					}
					((char*)buffer)[count++] = *info.pchNext++;
				} while (count < size);
			}
			result = mmioSetInfo(hmmio, &info, 0);
			if (result != 0)
				goto fail;

			*outRead = size;
			return result;
		}
	fail:
		*outRead = 0;
		return result;
	}

}

namespace AudioManager
{
	namespace Wave
	{
		// FUNCTION: TOY2 0x004A4200 [MATCHED]
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
}

namespace AudioManager
{
	// FUNCTION: TOY2 0x004A4680 [PROVISIONAL]
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
}
