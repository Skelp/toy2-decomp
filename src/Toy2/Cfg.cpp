#include "Toy2/Toy2.h"

#include <STDIO.H>
#include <STRING.H>

namespace Toy2
{
	// FUNCTION: TOY2 0x004CE760 [MATCHED]
	void InitCfg()
	{
		memset(&g_toyCfgData, 0, sizeof(g_toyCfgData));

		g_toyCfgData.driverIndex = -1;
		g_toyCfgData.detail = 1;
		g_toyCfgData.flags |= 7;
		g_toyCfgData.gammaCorrection = 2.0;
	}

	// FUNCTION: TOY2 0x004CE810 [MATCHED]
	int32_t ReadCfg()
	{
		InitCfg();

		FILE* fileHandle = fopen("toy2.cfg", "rb");

		if (fileHandle)
		{
			fread(&g_toyCfgData, 1, sizeof(ToyCfg), fileHandle);
			fclose(fileHandle);
			return 1;
		}

		return 0;
	}

	// FUNCTION: TOY2 0x004CE860 [MATCHED]
	int32_t WriteCfg()
	{
		FILE* fileHandle = fopen("toy2.cfg", "wb");

		if (fileHandle)
		{
			fwrite(&g_toyCfgData, 1, sizeof(ToyCfg), fileHandle);
			fclose(fileHandle);
			return 1;
		}

		return 0;
	}
} // namespace Toy2
