#ifndef LEVELSINTERNAL_H
#define LEVELSINTERNAL_H

#include "Toy2/Levels.h"

// Declarations the level objects share but that no public header states: the
// load-configuration bits, the sizes the level files use, and the buffers one
// object fills and another reads. Levels.cpp holds the definitions.
namespace Toy2
{
	namespace Levels
	{
		// Bits of the level load configuration.
		const int32_t kLoadSkipCreatures = 16;
		const int32_t kLoadTextureSet1 = 64;
		const int32_t kLoadTextureSetMask = 192;
		const int32_t kLoadBonusVariant = 256;

		const int32_t kBackdropScrollUnset = -32768;
		const int32_t kMaxParticleInstances = 64;
		const int32_t kLevelFileNameSize = 128;
		const int32_t kMaxCreatureIds = 128;
		const int32_t kTextureSuffixLevelId = 16;

		extern int32_t g_initialLevelLoadConfig;
		extern void* g_cachedAllBuffer;
		extern void* g_levelDataBase;

		int32_t LoadDAT(int32_t levelId, int32_t fileSize);
		void BuildLevelPath(int32_t level, char* output, const char* suffix);
	}
}

#endif
