#include "D3DApp/d3dappi.h"

// GLOBAL: TOY2 0x0051A840
D3DTEXTUREHANDLE g_masterTextureHandles[64];

// GLOBAL: TOY2 0x0051AACC
int32_t g_masterTextureStatus[64];

// GLOBAL: TOY2 0x0050A590
LPVOID g_masterTextureData[64];

// GLOBAL: TOY2 0x0051A9C8
LPVOID g_textureData[64];

// GLOBAL: TOY2 0x0051AFB4
int32_t g_masterTextureFlags[64];

// GLOBAL: TOY2 0x0050A778
int32_t g_textureFlags[64];

// GLOBAL: TOY2 0x0051A944
int32_t g_masterTexturePaletteState[32];

// GLOBAL: TOY2 0x0050A690
int32_t g_texturePaletteState[32];

// FUNCTION: TOY2 0x00409C80 [MATCHED]
BOOL D3DAppIReleaseAllTextures() { return TRUE; }
