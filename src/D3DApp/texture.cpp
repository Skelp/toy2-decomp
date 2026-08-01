#include "D3DApp/d3dappi.h"
#include "Logger.h"
#include "SoftwareRenderer.h"
#include "Toy2/Direct6.h"

#include <stdlib.h>

namespace SoftwareRenderer
{
	extern void* g_softwareTextureData[64];
}

// GLOBAL: TOY2 0x0051A840
int32_t g_masterTextureTypes[64];

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

// GLOBAL: TOY2 0x0050AA64
TextureDimensions g_textureDimensions[64];

// FUNCTION: TOY2 0x00497DB0 [MATCHED]
void AllocateTexturePixelBuffer(int16_t textureIndex)
{
	int32_t index = textureIndex;
	if (g_textureData[index] != NULL || textureIndex >= 0x40)
		return;

	switch (g_renderMode)
	{
		case RENDERMODE_SOFTWARE:
			if (SoftwareRenderer::g_bitsPerPixel != 8)
			{
				if (SoftwareRenderer::g_bitsPerPixel > 14 && SoftwareRenderer::g_bitsPerPixel <= 16)
				{
					uint16_t* pixelData = (uint16_t*)malloc(0x10000 * sizeof(uint16_t));
					g_textureData[index] = pixelData;
					SoftwareRenderer::g_softwareTextureData[index] = pixelData;

					int32_t pixelsRemaining = 0x10000;
					do
					{
						*pixelData++ = (0 << SoftwareRenderer::g_redShift) | (31 << SoftwareRenderer::g_greenShift);
					} while (--pixelsRemaining != 0);
				}
			}
			else
			{
				uint16_t* pixelData = (uint16_t*)malloc(g_textureDimensions[index].height * g_textureDimensions[index].width * sizeof(uint16_t));
				g_textureData[index] = pixelData;
				SoftwareRenderer::g_softwareTextureData[index] = pixelData;

				for (int32_t pixelIndex = 0; pixelIndex < g_textureDimensions[index].height * g_textureDimensions[index].width; ++pixelIndex)
				{
					*pixelData++ = 0;
				}
			}

			Logger::Log("MEM : SOFT Pixel buffer for tpage %d created.\n", index);
			break;

		case RENDERMODE_D3D:
			switch (g_textureFlags[index])
			{
				case 0: {
					uint8_t* pixelData = (uint8_t*)malloc(g_textureDimensions[index].height * g_textureDimensions[index].width * 3);
					g_textureData[index] = pixelData;
					SoftwareRenderer::g_softwareTextureData[index] = pixelData;

					for (int32_t pixelIndex = 0; pixelIndex < g_textureDimensions[index].height * g_textureDimensions[index].width; ++pixelIndex)
					{
						*pixelData++ = 0;
						*pixelData++ = 0xFF;
						*pixelData++ = 0;
					}
					break;
				}

				case 1: {
					uint8_t* pixelData = (uint8_t*)malloc(g_textureDimensions[index].height * g_textureDimensions[index].width);
					g_textureData[index] = pixelData;
					SoftwareRenderer::g_softwareTextureData[index] = pixelData;

					for (int32_t pixelIndex = 0; pixelIndex < g_textureDimensions[index].height * g_textureDimensions[index].width; ++pixelIndex)
					{
						*pixelData++ = 0;
					}
					break;
				}
			}

			Logger::Log("MEM : D3D Pixel buffer for tpage %d created.\n", index);
			break;
	}
}

// FUNCTION: TOY2 0x00409C80 [MATCHED]
BOOL D3DAppIReleaseAllTextures() { return TRUE; }
