#pragma once

#include "Common.h"

namespace Toy2
{
	namespace Gadget
	{
		struct LevelUnlockInfo
		{
			uint8_t modelNodeIndex;
			uint8_t unlockFlag;
		};

		struct UnlockGeometryEntry
		{
			uint8_t levelIndex;
			uint8_t lockedLinkId;
			uint8_t unlockedLinkId;
			uint8_t platformIndex;
		};

		extern LevelUnlockInfo g_levelUnlockInfo[16];
		extern UnlockGeometryEntry g_unlockBit1Geometry[];
		extern UnlockGeometryEntry g_unlockBit2Geometry[];
		extern UnlockGeometryEntry g_unlockBit4Geometry[];
		extern UnlockGeometryEntry g_unlockBit8Geometry[];
		extern UnlockGeometryEntry g_unlockBit16Geometry[];
		extern int32_t g_unlockNodeState;

		void InitLevelUnlockGeometry();
		void ApplyUnlockToGeometry(const UnlockGeometryEntry* entries, int32_t scale);

		STATIC_ASSERT(sizeof(LevelUnlockInfo) == 0x2);
		STATIC_ASSERT(sizeof(UnlockGeometryEntry) == 0x4);
	}
}
