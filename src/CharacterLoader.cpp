#include "CharacterLoader.h"

namespace CharacterLoader
{
	// GLOBAL: TOY2 0x0053EEE0
	BoneTransform g_boneTransforms[300];

	// GLOBAL: TOY2 0x0054717C
	int16_t g_boneRemapCount;

	// GLOBAL: TOY2 0x00546D78
	int16_t g_processedBoneRemapCount;

	// GLOBAL: TOY2 0x00547CD0
	int16_t g_animationSlotCount;

	// GLOBAL: TOY2 0x00547CD4
	CharacterAnimationData* g_characterAnimationData[128];

	// GLOBAL: TOY2 0x0053E4C8
	int32_t g_alternateAllParse[128];

	// GLOBAL: TOY2 0x0053E8C8
	uint8_t* g_charFileDataCache[128];

	// GLOBAL: TOY2 0x0043B9B0
	void Start(int32_t* value, uint8_t** buffer, uint8_t* creatureList) {}

	// FUNCTION: TOY2 0x0043C700 [MATCHED]
	void InitGlobals()
	{
		g_boneRemapCount = 0;
		g_processedBoneRemapCount = 0;
		g_animationSlotCount = 0;
	}
}
