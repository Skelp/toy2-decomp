#include "Common.h"
#include "D3DApp/d3dappi.h"
#include "Logger.h"
#include "SoftwareRenderer.h"
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

	// FUNCTION: TOY2 0x0044F880 [PROVISIONAL]
	void ConvertPSXTexturePixels(const uint8_t* sourcePixels,
		const uint8_t* palette,
		void* destinationPixels,
		int32_t sourceFormat,
		int32_t sourceWidth,
		int32_t sourceHeight,
		int32_t destinationWidth,
		int32_t destinationHeight)
	{
		uint8_t convertedPalette[256 * 3];
		int32_t paletteByte;
		if (palette != NULL)
		{
			uint8_t* convertedColour = convertedPalette;
			int32_t paletteCount = 256;
			do
			{
				*convertedColour++ = *palette++;
				*convertedColour++ = *palette++;
				*convertedColour++ = *palette++;
			} while (--paletteCount != 0);

			for (paletteByte = 0; paletteByte < 256 * 3; paletteByte += 3)
			{
				uint8_t* colour = &convertedPalette[paletteByte];
				if (colour[0] == 0 && colour[1] >= 250 && colour[2] == 0)
					colour[1] = 255;
			}
		}

		int32_t packedPixelBlock = 0;
		switch (sourceFormat)
		{
			case 0: {
				for (int32_t y4 = 0; y4 < sourceHeight; ++y4)
				{
					uint8_t* indexedDestination = (uint8_t*)destinationPixels + y4 * destinationWidth;
					uint16_t* colourDestination = (uint16_t*)destinationPixels + y4 * destinationWidth;
					const uint8_t* source = sourcePixels + (y4 * sourceWidth) / 2;
					for (int32_t x4 = 0; x4 < sourceWidth; x4 += 2)
					{
						uint32_t paletteBank = ((packedPixelBlock >> 5) & 3) + ((packedPixelBlock >> 13) & 3) * 4;
						++packedPixelBlock;
						uint8_t packedPixels = *source++;
						uint8_t indices[2];
						indices[0] = (uint8_t)((packedPixels & 15) + paletteBank * 16);
						indices[1] = (uint8_t)((packedPixels >> 4) + paletteBank * 16);

						for (int32_t pixel = 0; pixel < 2; ++pixel)
						{
							switch (g_renderMode)
							{
								case RENDERMODE_SOFTWARE: {
									const uint8_t* colour = &convertedPalette[indices[pixel] * 3];
									uint8_t red = colour[0];
									uint8_t green = colour[1];
									uint8_t blue = colour[2];
									if (SoftwareRenderer::g_bitsPerPixel == 16 || SoftwareRenderer::g_bitsPerPixel == 15)
									{
										if (red == 0 && green > 250 && blue == 0)
											green = 255;
										*colourDestination++ = (uint16_t)((red >> 3) << SoftwareRenderer::g_redShift)
											| (uint16_t)((green >> 3) << SoftwareRenderer::g_greenShift) | (blue >> 3);
									}
									else if (red == 0 && green > 250 && blue == 0)
									{
										*indexedDestination++ = 0;
									}
									else
									{
										*indexedDestination++ = SoftwareRenderer::g_rgbToPaletteIndex[((red & 0xF8) * 0x20 + (green & 0xF8)) * 4 + (blue >> 3)];
									}
									break;
								}

								case RENDERMODE_D3D: {
									*indexedDestination++ = indices[pixel];
									break;
								}
							}
						}
					}
				}
				break;
			}

			case 1: {
				for (int32_t y8 = 0; y8 < sourceHeight; ++y8)
				{
					uint8_t* indexedDestination = (uint8_t*)destinationPixels + y8 * destinationWidth;
					uint16_t* colourDestination = (uint16_t*)destinationPixels + y8 * destinationWidth;
					const uint8_t* source = sourcePixels + y8 * sourceWidth;
					for (int32_t x8 = 0; x8 < sourceWidth; ++x8)
					{
						uint8_t index = *source++;
						switch (g_renderMode)
						{
							case RENDERMODE_SOFTWARE: {
								const uint8_t* colour = &convertedPalette[index * 3];
								uint8_t red = colour[0];
								uint8_t green = colour[1];
								uint8_t blue = colour[2];
								if (SoftwareRenderer::g_bitsPerPixel == 16 || SoftwareRenderer::g_bitsPerPixel == 15)
								{
									if (red == 0 && green > 250 && blue == 0)
										green = 255;
									*colourDestination++ = (uint16_t)((red >> 3) << SoftwareRenderer::g_redShift)
										| (uint16_t)((green >> 3) << SoftwareRenderer::g_greenShift) | (blue >> 3);
								}
								else if (red == 0 && green > 250 && blue == 0)
								{
									*indexedDestination++ = 0;
								}
								else
								{
									*indexedDestination++ = SoftwareRenderer::g_rgbToPaletteIndex[((red & 0xF8) * 0x20 + (green & 0xF8)) * 4 + (blue >> 3)];
								}
								break;
							}

							case RENDERMODE_D3D: {
								*indexedDestination++ = index;
								break;
							}
						}
					}
				}
				break;
			}

			case 2: {
				for (int32_t y16 = 0; y16 < sourceHeight; ++y16)
				{
					uint8_t* indexedDestination = (uint8_t*)destinationPixels + y16 * destinationWidth;
					uint16_t* colourDestination = (uint16_t*)destinationPixels + y16 * destinationWidth;
					uint8_t* rgbDestination = (uint8_t*)destinationPixels + y16 * destinationWidth * 3;
					const uint16_t* source = (const uint16_t*)sourcePixels + y16 * sourceWidth;
					for (int32_t x16 = 0; x16 < sourceWidth; ++x16)
					{
						uint16_t sourceColour = *source++;
						uint8_t red = (uint8_t)(sourceColour & 0x1F);
						uint8_t green = (uint8_t)((sourceColour >> 5) & 0x1F);
						uint8_t blue = (uint8_t)((sourceColour >> 10) & 0x1F);
						switch (g_renderMode)
						{
							case RENDERMODE_SOFTWARE: {
								if (SoftwareRenderer::g_bitsPerPixel == 16 || SoftwareRenderer::g_bitsPerPixel == 15)
								{
									if (red == 0 && green > 250 && blue == 0)
										green = 255;
									*colourDestination++ =
										(uint16_t)(red << SoftwareRenderer::g_redShift) | (uint16_t)(green << SoftwareRenderer::g_greenShift) | blue;
								}
								else if (red == 0 && green > 250 && blue == 0)
								{
									*indexedDestination++ = 0;
								}
								else
								{
									*indexedDestination++ = SoftwareRenderer::g_rgbToPaletteIndex[(red * 0x20 + green) * 0x20 + blue];
								}
								break;
							}

							case RENDERMODE_D3D: {
								if (red == 0 && green > 250 && blue == 0)
									green = 255;
								*rgbDestination++ = (uint8_t)(red << 3);
								*rgbDestination++ = (uint8_t)(green << 3);
								*rgbDestination++ = (uint8_t)(blue << 3);
								break;
							}
						}
					}
				}
				break;
			}
		}
	}
}
