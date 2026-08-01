#include "Common.h"
#include "D3DApp/d3dappi.h"
#include "Logger.h"
#include "Toy2/Direct6.h"

namespace Toy2
{
	void MarkTextureForDestroy(uint32_t textureSlot);

	void ConvertPSXTexturePixels(const uint8_t* sourcePixels,
		const uint8_t* palette,
		void* destinationPixels,
		int32_t sourceFormat,
		int32_t sourceWidth,
		int32_t sourceHeight,
		int32_t destinationWidth,
		int32_t destinationHeight);

	// FUNCTION: TOY2 0x00452A20 [EFFECTIVE]
	void ConvertPSXTexturePage16(int32_t sourceWidth, int32_t sourceHeight, const uint8_t* sourcePixels, uint32_t textureSlot)
	{
		MarkTextureForDestroy(textureSlot);

		uint32_t textureIndex = textureSlot & 0xFFFF;
		d3dappi.TextureType[textureIndex] = 1;
		d3dappi.TextureStatus[textureIndex] = D3DTEXTURE_STATUS_GETHANDLE;
		g_texturePaletteState[textureIndex] = 0;

		int32_t textureHeight = sourceHeight;
		int32_t textureWidth = sourceWidth;
		switch (g_renderMode)
		{
			case RENDERMODE_SOFTWARE:
				textureWidth = textureHeight = 256;
				break;

			case RENDERMODE_D3D:
				textureWidth = 1;
				while (textureWidth < sourceWidth)
					textureWidth <<= 1;

				textureHeight = 1;
				while (textureHeight < sourceHeight)
					textureHeight <<= 1;

				if (PC.D3D->isSquareTexturesOnly && textureWidth > textureHeight)
					textureHeight = textureWidth;
				else if (textureWidth < textureHeight)
					textureWidth = textureHeight;
				break;
		}

		g_textureDimensions[textureIndex].width = textureWidth;
		g_textureDimensions[textureIndex].height = textureHeight;
		g_textureFlags[textureIndex] = 0;

		Logger::Log("LOAD : CONVPSXTPAGE16 tpage %d, %dx%d\n", textureIndex, g_textureDimensions[textureIndex].width, (int16_t)textureHeight);
		AllocateTexturePixelBuffer(textureSlot);
		ConvertPSXTexturePixels(sourcePixels, NULL, g_textureData[textureIndex], 2, sourceWidth, sourceHeight, textureWidth, textureHeight);
		D3DAppIReleaseAllTextures();
	}

	// STUB: TOY2 0x0044F880
	void ConvertPSXTexturePixels(const uint8_t* sourcePixels,
		const uint8_t* palette,
		void* destinationPixels,
		int32_t sourceFormat,
		int32_t sourceWidth,
		int32_t sourceHeight,
		int32_t destinationWidth,
		int32_t destinationHeight)
	{}
}
