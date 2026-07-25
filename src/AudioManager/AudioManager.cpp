#include "AudioManager/AudioManager.h"
#include "Logger.h"

struct IDirectSoundBuffer;

typedef int32_t(__stdcall* DSBSetVolumeFunc)(IDirectSoundBuffer*, int32_t);

struct IDirectSoundBufferVtbl
{
	void* QueryInterface;
	void* AddRef;
	void* Release;
	void* GetCaps;
	void* GetCurrentPosition;
	void* GetFormat;
	void* GetVolume;
	void* GetPan;
	void* GetFrequency;
	void* GetStatus;
	void* Initialize;
	void* Lock;
	void* Play;
	void* SetCurrentPosition;
	void* SetFormat;
	DSBSetVolumeFunc SetVolume;
};

struct IDirectSoundBuffer
{
	IDirectSoundBufferVtbl* lpVtbl;
};

namespace AudioManager
{
	// GLOBAL: TOY2 005282CC
	int32_t g_curTrackIndex;

	// GLOBAL: TOY2 0x00724E80
	int32_t g_audioInitialized;

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

	// GLOBAL: TOY2 0x005282F0
	void* g_dsPrimaryBuffer;

	// GLOBAL: TOY2 0x004FD668
	int16_t g_dsVolTable[151] = { -10000,
		-9800,
		-9600,
		-9300,
		-9000,
		-8700,
		-8300,
		-8000,
		-7250,
		-6500,
		-5750,
		-5500,
		-5250,
		-5000,
		-4500,
		-4000,
		-3900,
		-3800,
		-3700,
		-3600,
		-3500,
		-3400,
		-3300,
		-3200,
		-3100,
		-3000,
		-2920,
		-2840,
		-2770,
		-2700,
		-2630,
		-2560,
		-2490,
		-2420,
		-2350,
		-2280,
		-2210,
		-2140,
		-2070,
		-2000,
		-1960,
		-1912,
		-1864,
		-1830,
		-1790,
		-1750,
		-1710,
		-1670,
		-1630,
		-1590,
		-1550,
		-1510,
		-1470,
		-1430,
		-1390,
		-1340,
		-1290,
		-1250,
		-1210,
		-1168,
		-1126,
		-1084,
		-1042,
		-1000,
		-985,
		-970,
		-955,
		-940,
		-925,
		-910,
		-895,
		-880,
		-865,
		-850,
		-835,
		-820,
		-805,
		-790,
		-775,
		-759,
		-743,
		-727,
		-711,
		-695,
		-679,
		-663,
		-647,
		-631,
		-615,
		-599,
		-583,
		-567,
		-551,
		-535,
		-519,
		-503,
		-487,
		-471,
		-455,
		-439,
		-423,
		-407,
		-391,
		-375,
		-359,
		-343,
		-327,
		-311,
		-295,
		-279,
		-263,
		-247,
		-231,
		-215,
		-199,
		-183,
		-167,
		-151,
		-135,
		-119,
		-103,
		-87,
		-71,
		-55,
		-39,
		-23,
		-7,
		-6,
		-5,
		-4,
		-3,
		-2,
		-1,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0 };

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
			int32_t result = ((IDirectSoundBuffer*)g_dsPrimaryBuffer)->lpVtbl->SetVolume((IDirectSoundBuffer*)g_dsPrimaryBuffer, scaledVol);
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

	// STUB: TOY2 0x0047D880
	void PlayMusicLooping(int16_t trackIndex) {}

	// STUB: TOY2 0x004A3B90
	int32_t PlayLoopingSound3DPositional(void* owner, int32_t soundIndex, int32_t volume, int32_t leftVolume, void* unused, int16_t rightVolume) { return 0; }

	// STUB: TOY2 0x004A3BE0
	void UpdateChannels() {}

	// FUNCTION: TOY2 0x004A3ED0 [MATCHED]
	void SetVolumes(int32_t musicVolume, int32_t sfxVolume)
	{
		SetMusicVolume(musicVolume);
		SetSfxVolume(sfxVolume);
	}

	// STUB: TOY2 0x00413300
	int32_t IsStreamActive() { return 0; }

	// STUB: TOY2 0x00413150
	void PlayTrackByIndex(int32_t trackIndex, int32_t fadeMode) {}

	// STUB: TOY2 0x0047DE50
	int32_t PlaySoundBuffer(int32_t soundIndex, int32_t leftVolume, int32_t rightVolume, int32_t pan, int32_t volume, int32_t flags) { return 0; }
}
