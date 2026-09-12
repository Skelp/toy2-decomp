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

// The streamed audio: retail holds 0x00412EE0 through 0x00413310 as one
// object, in the order of this file.
namespace AudioManager
{
	// FUNCTION: TOY2 0x00412EE0 [MATCHED]
	void ShutdownHandles()
	{
		if (g_streamAckEvent != NULL)
		{
			CloseHandle(g_streamAckEvent);
		}
		if (g_streamCommandEvent != NULL)
		{
			CloseHandle(g_streamCommandEvent);
		}
		if (g_streamStopEvent != NULL)
		{
			CloseHandle(g_streamStopEvent);
		}
		if (g_streamFillEvent != NULL)
		{
			CloseHandle(g_streamFillEvent);
		}

		g_streamAckEvent = NULL;
		g_streamCommandEvent = NULL;
		g_streamStopEvent = NULL;
		g_streamFillEvent = NULL;
	}

}

namespace AudioManager
{
	namespace Stream
	{
		// FUNCTION: TOY2 0x00412FF0 [PROVISIONAL]
		int32_t Init()
		{
			if (g_streamInitialized == 0)
			{
				InitializeCriticalSection(&g_streamCriticalSection);
				g_streamFillEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
				if (g_streamFillEvent != NULL)
				{
					g_streamStopEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
					if (g_streamStopEvent != NULL)
					{
						g_streamCommandEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
						if (g_streamCommandEvent != NULL)
						{
							g_streamAckEvent = CreateSemaphore(NULL, 0, 1, NULL);
							if (g_streamAckEvent == NULL)
							{
								CloseHandle(g_streamCommandEvent);
								g_streamCommandEvent = NULL;
							}
							else
							{
								DWORD threadId;
								HANDLE thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ThreadProc, NULL, 0, &threadId);
								if (thread != NULL)
								{
									while (! IsThreadReady()) {}
									g_streamInitialized = 1;
									atexit(OnExit);
									return 1;
								}

								if (g_streamAckEvent != NULL)
								{
									CloseHandle(g_streamAckEvent);
								}
								if (g_streamCommandEvent != NULL)
								{
									CloseHandle(g_streamCommandEvent);
								}
								if (g_streamStopEvent != NULL)
								{
									CloseHandle(g_streamStopEvent);
								}
								if (g_streamFillEvent != NULL)
								{
									CloseHandle(g_streamFillEvent);
								}
								g_streamAckEvent = NULL;
								g_streamCommandEvent = NULL;
								g_streamStopEvent = NULL;
								g_streamFillEvent = NULL;
								return 0;
							}
						}
						CloseHandle(g_streamStopEvent);
						g_streamStopEvent = NULL;
					}
					CloseHandle(g_streamFillEvent);
					g_streamFillEvent = NULL;
					return 0;
				}
			}
			return 0;
		}

	}
}

namespace AudioManager
{
	// FUNCTION: TOY2 0x00413140 [MATCHED]
	void OnExit() { SignalThreadExit(); }

	// FUNCTION: TOY2 0x00413150 [MATCHED]
	int32_t PlayTrackByIndex(int32_t trackIndex, int32_t looping)
	{
		char buffer[512];

		StopAndWait();
		if (trackIndex >= 0x16 || trackIndex < 0)
			return 0;

		FileUtils::AppendCDPath(buffer);
		strcat(buffer, "audio\\");
		strcat(buffer, g_trackNames[trackIndex]);
		strcat(buffer, ".wav");
		QueuePlay(buffer, looping);
		g_curTrackIndex = trackIndex;
		return 1;
	}

	// FUNCTION: TOY2 0x00413240 [MATCHED]
	void ThreadPlay(char* path, int32_t looping)
	{
		if (g_directSound != NULL && LoadFile(path))
		{
			g_streamLooping = looping & 1;
			g_dsPrimaryBuffer->SetCurrentPosition(0);
			SetMusicVolume(g_musicVolumeLevel);
			g_dsPrimaryBuffer->Play(0, 0, DSBPLAY_LOOPING);
		}
	}

}

namespace AudioManager
{
	namespace Stream
	{
		// FUNCTION: TOY2 0x004132A0 [MATCHED]
		void Stop()
		{
			if (g_directSound != NULL && g_dsPrimaryBuffer != NULL)
			{
				g_dsPrimaryBuffer->Stop();
				Wave::CloseFile(&g_waveMmioHandle, &g_waveFormatHandle);
				g_dsNotify->Release();
				g_dsNotify = NULL;
				g_dsPrimaryBuffer->Release();
				g_dsPrimaryBuffer = NULL;
			}
		}

	}
}

namespace AudioManager
{
	// FUNCTION: TOY2 0x00413300 [MATCHED]
	int32_t IsStreamActive() { return g_streamActive; }

	// FUNCTION: TOY2 0x00413310 [PROVISIONAL]
	int32_t LoadFile(char* path)
	{
		if (g_quietMode == 0)
		{
			if (Wave::OpenFile(path, &g_waveMmioHandle, &g_waveFormatHandle, &g_streamParentChunk) == MMSYSERR_NOERROR)
			{
				if (Wave::SeekToChunk(&g_waveMmioHandle, &g_streamDataChunk, &g_streamParentChunk) == MMSYSERR_NOERROR)
				{
					WAVEFORMATEX* waveFormat = static_cast<WAVEFORMATEX*>(g_waveFormatHandle);
					g_streamFillBytes = waveFormat->nSamplesPerSec * waveFormat->nBlockAlign * 3 >> 4;
					uint32_t remainder = g_streamFillBytes % waveFormat->nBlockAlign;
					if (remainder != 0)
					{
						g_streamFillBytes += waveFormat->nBlockAlign - remainder;
					}

					DSBUFFERDESC desc;
					memset(&desc, 0, sizeof(desc));
					desc.dwSize = sizeof(desc);
					desc.dwFlags = DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_GLOBALFOCUS
						| DSBCAPS_GETCURRENTPOSITION2;
					g_streamBufferBytes = g_streamFillBytes << 4;
					desc.dwBufferBytes = g_streamBufferBytes;
					desc.lpwfxFormat = waveFormat;
					if (g_directSound->CreateSoundBuffer(&desc, &g_dsPrimaryBuffer, NULL) == DS_OK)
					{
						g_streamReachedEnd = 0;
						g_streamBufferAlias = g_dsPrimaryBuffer;
						g_streamWriteOffset = 0;
						g_streamLooping = 0;
						if (g_dsPrimaryBuffer->QueryInterface(IID_IDirectSoundNotify, (void**)&g_dsNotify) == S_OK)
						{
							g_streamNotifications[0].dwOffset = g_streamFillBytes;
							g_streamNotifications[0].hEventNotify = g_streamFillEvent;
							for (int32_t index = 1; index < 16; index++)
							{
								g_streamNotifications[index].dwOffset = g_streamNotifications[index - 1].dwOffset + g_streamFillBytes;
								g_streamNotifications[index].hEventNotify = g_streamFillEvent;
							}
							g_streamNotifications[15].dwOffset--;
							g_streamNotifications[16].dwOffset = DSBPN_OFFSETSTOP;
							g_streamNotifications[16].hEventNotify = g_streamStopEvent;
							g_dsNotify->SetNotificationPositions(17, g_streamNotifications);

							void* bufferData1;
							DWORD bufferBytes1;
							void* bufferData2;
							DWORD bufferBytes2;
							if (g_dsPrimaryBuffer->Lock(0, g_streamBufferBytes, &bufferData1, &bufferBytes1, &bufferData2, &bufferBytes2, 0) == DS_OK)
							{
								uint32_t bytesRead;
								WaveReadFile(g_waveMmioHandle, bufferBytes1, bufferData1, &g_streamDataChunk, &bytesRead);
								if (bytesRead < bufferBytes1)
								{
									if (g_streamLooping == 0)
									{
										if (bytesRead < g_streamFillBytes)
										{
											g_streamReachedEnd = 1;
										}
										uint8_t silence = static_cast<WAVEFORMATEX*>(g_waveFormatHandle)->wBitsPerSample == 8 ? 0x80 : 0;
										memset(static_cast<uint8_t*>(bufferData1) + bytesRead, silence, bufferBytes1 - bytesRead);
									}
									else
									{
										uint32_t filledBytes = bytesRead;
										do
										{
											Wave::SeekToChunk(&g_waveMmioHandle, &g_streamDataChunk, &g_streamParentChunk);
											WaveReadFile(g_waveMmioHandle,
												bufferBytes1 - filledBytes,
												static_cast<uint8_t*>(bufferData1) + filledBytes,
												&g_streamDataChunk,
												&bytesRead);
											filledBytes += bytesRead;
										} while (filledBytes < bufferBytes1);
									}
								}

								g_dsPrimaryBuffer->Unlock(bufferData1, bufferBytes1, NULL, 0);
								g_streamWriteOffset += bufferBytes1;
								if (g_streamWriteOffset >= g_streamBufferBytes)
								{
									g_streamWriteOffset -= g_streamBufferBytes;
								}
								g_streamBytesPlayed = 0;
								g_streamLastPlayCursor = 0;
							}

							g_streamPlaybackFinished = 0;
							return 1;
						}
						g_dsPrimaryBuffer->Release();
					}
				}
				Wave::CloseFile(&g_waveMmioHandle, &g_waveFormatHandle);
			}
		}
		return 0;
	}
}
