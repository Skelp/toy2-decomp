#include "SoftwareRenderer.h"
#include "SoftwareRendererInternal.h"
#include "D3DApp/d3dapp.h"
#include "D3DApp/d3dappi.h"
#include "Nu3D/Camera.h"
#include "Toy2/Camera.h"
#include "Toy2/Toy2.h"
#include "Logger.h"
#include <stdlib.h>

// The palette tables of the software renderer: retail holds 0x00470BF0
// through 0x00471300 as one object, in the order of this file.
// BuildPaletteLightingTable (0x004319E0) lies in another object. It builds
// g_paletteLightingTable, so it stays with the table it fills, and it stays
// ahead of BuildRGBToPaletteTable: the map gap of 0x004319E0 reaches the
// prologue of the next function, which reccmp scores.
namespace SoftwareRenderer
{
	// The software renderer's DirectDraw palette and its backing entry buffers.
	// SetPaletteOnAPI creates the palette from g_paletteEntries (entry 0 onward)
	// and attaches it to the front/back buffers. UpdatePaletteTint rebuilds the live
	// entries (1..255) by tinting the same range in the source palette by the
	// camera tint colour, preserving entry 0, then SetEntries on the palette.
	// The palette is stored B,G,R,X per entry (byte 0 = blue, 1 = green,
	// 2 = red); entry 0 is skipped by the tint loop and the SetEntries call.
	// GLOBAL: TOY2 0x00704E44
	LPDIRECTDRAWPALETTE g_lpPalette;

	// GLOBAL: TOY2 0x00704630
	uint8_t g_paletteEntries[0x400];

	// Maps each 5-bit BGR colour to the nearest entry in g_paletteSource.
	// GLOBAL: TOY2 0x0070462C
	uint8_t* g_rgbToPaletteIndex;

	// GLOBAL: TOY2 0x00704A38
	uint8_t g_paletteSource[0x400];

	// Maps a palette entry and a 0..127 light level to the nearest lit palette
	// entry. Palette entry zero is reserved and is not selected.
	// GLOBAL: TOY2 0x00534564
	uint8_t g_paletteLightingTable[128][256];

	// GLOBAL: TOY2 0x00704A34
	uint8_t* g_additivePaletteTable;

	// For each palette entry, stores the 8x8x8 combinations of channel offsets
	// from -96 through +128 in steps of 32.
	// GLOBAL: TOY2 0x00704A30
	uint8_t* g_paletteColourOffsetTable;

	// GLOBAL: TOY2 0x00704E48
	uint8_t* g_subtractivePaletteTable;

	// GLOBAL: TOY2 0x00704E38
	uint8_t* g_paletteBlend25Table;

	// GLOBAL: TOY2 0x00704E3C
	uint8_t* g_paletteBlend50Table;

	// GLOBAL: TOY2 0x00704E40
	uint8_t* g_paletteBlend75Table;

	enum PaletteTableFlags
	{
		PALETTE_TABLE_RGB = 0x1,
		PALETTE_TABLE_BLEND = 0x10,
		PALETTE_TABLE_ADDITIVE = 0x100,
		PALETTE_TABLE_SUBTRACTIVE = 0x1000,
		PALETTE_TABLE_COLOUR_OFFSET = 0x10000
	};

	// Builds one 256x256 colour-scale LUT into `tableGlobal`. The LUT maps
	// table[scale*256 + colour] = (min(colour*scale, 0xffff) >> shift) & mask,
	// placing a single colour component into its 16-bit channel position. The
	// table global is referenced directly (not cached) so MSVC6 reloads it each
	// inner iteration, matching retail's conservative-aliasing codegen.
	// FUNCTION: TOY2 0x00470BF0 [MATCHED]
	void SetPaletteOnAPI()
	{
		HRESULT result = d3dappi.lpDD->CreatePalette(0x44, (LPPALETTEENTRY)g_paletteEntries, &g_lpPalette, NULL);
		if (result < 0)
		{
			Logger::LogDDError("d3dappi.lpDD->CreatePalette(0x00000004l|0x00000040l,&pal[0],&SonicRPalette,0)", result);
		}
		result = d3dappi.lpFrontBuffer->SetPalette(g_lpPalette);
		if (result < 0)
		{
			Logger::LogDDError("d3dappi.lpFrontBuffer->SetPalette(SonicRPalette)", result);
		}
		result = d3dappi.lpBackBuffer->SetPalette(g_lpPalette);
		if (result < 0)
		{
			Logger::LogDDError("d3dappi.lpBackBuffer->SetPalette(SonicRPalette)", result);
		}
	}

	// FUNCTION: TOY2 0x00470C70 [MATCHED]
	void UpdatePaletteTint()
	{
		for (int32_t i = 0; i < 0x3fc; i += 4)
		{
			g_paletteEntries[i + 4] = (uint8_t)(g_paletteSource[i + 4] * Nu3D::Camera::g_cameraTintRed / 128);
			g_paletteEntries[i + 5] = (uint8_t)(g_paletteSource[i + 5] * Nu3D::Camera::g_cameraTintGreen / 128);
			g_paletteEntries[i + 6] = (uint8_t)(g_paletteSource[i + 6] * Nu3D::Camera::g_cameraTintBlue / 128);
		}
		g_lpPalette->SetEntries(0, 1, 255, (LPPALETTEENTRY)&g_paletteEntries[4]);
	}

	// FUNCTION: TOY2 0x00470D00 [PROVISIONAL]
	void LoadPaletteEntries(const uint8_t* source)
	{
		for (int32_t entry = 0; entry < 256; entry++)
		{
			g_paletteEntries[entry * 4] = 0;
			g_paletteEntries[entry * 4 + 1] = 0;
			g_paletteEntries[entry * 4 + 2] = 0;
		}

		for (int32_t sourceEntry = 0; sourceEntry < 256; sourceEntry++)
		{
			g_paletteSource[sourceEntry * 4] = source[sourceEntry * 3];
			g_paletteSource[sourceEntry * 4 + 1] = source[sourceEntry * 3 + 1];
			g_paletteSource[sourceEntry * 4 + 2] = source[sourceEntry * 3 + 2];
		}

		g_paletteSource[0] = 0;
		g_paletteSource[1] = 0;
		g_paletteSource[2] = 0;
		BuildPaletteLightingTable();
	}

	// FUNCTION: TOY2 0x004319E0 [PROVISIONAL]
	void BuildPaletteLightingTable()
	{
		for (int32_t lightLevel = 0; lightLevel < 128; lightLevel++)
		{
			for (int32_t sourceEntry = 0; sourceEntry < 256; sourceEntry++)
			{
				int32_t blue = g_paletteSource[sourceEntry * 4] * lightLevel / 64;
				int32_t green = g_paletteSource[sourceEntry * 4 + 1] * lightLevel / 64;
				int32_t red = g_paletteSource[sourceEntry * 4 + 2] * lightLevel / 64;
				if (blue > 255)
					blue = 255;
				if (green > 255)
					green = 255;
				if (red > 255)
					red = 255;

				int32_t bestDistance = 0x7fffffff;
				uint8_t bestEntry;
				for (int32_t candidate = 1; candidate < 256; candidate++)
				{
					int32_t blueDifference = abs(blue - g_paletteSource[candidate * 4]) * 4;
					int32_t greenDifference = abs(green - g_paletteSource[candidate * 4 + 1]) * 5;
					int32_t redDifference = abs(red - g_paletteSource[candidate * 4 + 2]) * 3;
					int32_t distance = blueDifference * blueDifference + greenDifference * greenDifference + redDifference * redDifference;
					if (distance < bestDistance)
					{
						bestDistance = distance;
						bestEntry = (uint8_t)candidate;
					}
				}
				g_paletteLightingTable[lightLevel][sourceEntry] = bestEntry;
			}
		}
		OutputDebugStringA("Generated lighting\n");
	}

	// FUNCTION: TOY2 0x00470D60 [PROVISIONAL]
	void BuildRGBToPaletteTable()
	{
		uint8_t* output = g_rgbToPaletteIndex;
		for (int32_t blue = 0; blue < 256; blue += 8)
		{
			for (int32_t green = 0; green < 256; green += 8)
			{
				for (int32_t red = 0; red < 256; red += 8)
				{
					uint8_t nearestEntry = 0;
					int32_t nearestDistance = 9999999;
					for (int32_t entry = 1; entry < 256; entry++)
					{
						int32_t blueDifference = (g_paletteSource[entry * 4] - blue) * 4;
						int32_t greenDifference = (g_paletteSource[entry * 4 + 1] - green) * 5;
						int32_t redDifference = (g_paletteSource[entry * 4 + 2] - red) * 3;
						int32_t totalDifference = blueDifference + greenDifference + redDifference;
						int32_t distance = totalDifference * totalDifference + blueDifference * blueDifference + greenDifference * greenDifference
							+ redDifference * redDifference;
						if (distance < nearestDistance)
						{
							nearestEntry = (uint8_t)entry;
							nearestDistance = distance;
						}
					}
					*output++ = nearestEntry;
				}
			}
		}
	}

	// FUNCTION: TOY2 0x00470FB0 [PROVISIONAL]
	void BuildPaletteColourOffsetTable()
	{
		uint8_t* output = g_paletteColourOffsetTable;
		uint8_t* palette = g_paletteSource + 1;
		do
		{
			int32_t sourceGreen = palette[0];
			int32_t sourceBlue = palette[-1];
			int32_t sourceRed = palette[1];
			int32_t blue = sourceBlue - 96;
			int32_t blueCount = 8;
			do
			{
				int32_t green = sourceGreen - 96;
				int32_t greenCount = 8;
				do
				{
					int32_t red = sourceRed - 96;
					int32_t redCount = 8;
					do
					{
						int32_t clampedBlue = blue;
						int32_t clampedGreen = green;
						int32_t clampedRed = red;
						if (clampedBlue < 0)
							clampedBlue = 0;
						else if (clampedBlue > 255)
							clampedBlue = 255;
						if (clampedGreen < 0)
							clampedGreen = 0;
						else if (clampedGreen > 255)
							clampedGreen = 255;
						if (clampedRed < 0)
							clampedRed = 0;
						else if (clampedRed > 255)
							clampedRed = 255;
						int32_t lookup = ((clampedBlue & ~7) * 32 + (clampedGreen & ~7)) * 4 + (clampedRed >> 3);
						*output++ = g_rgbToPaletteIndex[lookup];
						red += 32;
						redCount--;
					} while (redCount != 0);
					green += 32;
					greenCount--;
				} while (greenCount != 0);
				blue += 32;
				blueCount--;
			} while (blueCount != 0);
			palette += 4;
		} while (palette < g_paletteSource + sizeof(g_paletteSource) + 1);
	}

	// FUNCTION: TOY2 0x004710C0 [PROVISIONAL]
	void BuildPaletteBlendTable(uint8_t* output, int32_t blendWeight)
	{
		int32_t baseWeight = 1024 - blendWeight;
		for (int32_t baseEntry = 0; baseEntry < 256; baseEntry++)
		{
			int32_t baseBlue = g_paletteSource[baseEntry * 4] * baseWeight;
			int32_t baseGreen = g_paletteSource[baseEntry * 4 + 1] * baseWeight;
			int32_t baseRed = g_paletteSource[baseEntry * 4 + 2] * baseWeight;
			for (int32_t blendEntry = 0; blendEntry < 256; blendEntry++)
			{
				int32_t blue = (g_paletteSource[blendEntry * 4] * blendWeight + baseBlue) >> 10;
				int32_t green = (g_paletteSource[blendEntry * 4 + 1] * blendWeight + baseGreen) >> 10;
				int32_t red = (g_paletteSource[blendEntry * 4 + 2] * blendWeight + baseRed) >> 10;
				int32_t lookup = ((blue & ~7) * 32 + (green & ~7)) * 4 + (red >> 3);
				*output++ = g_rgbToPaletteIndex[lookup];
			}
		}
	}

	// FUNCTION: TOY2 0x00471190 [PROVISIONAL]
	void BuildAdditivePaletteTable()
	{
		uint8_t* output = g_additivePaletteTable;
		for (int32_t first = 0; first < 256; first++)
		{
			int32_t firstBlue = g_paletteSource[first * 4];
			int32_t firstGreen = g_paletteSource[first * 4 + 1];
			int32_t firstRed = g_paletteSource[first * 4 + 2];
			for (int32_t second = 0; second < 256; second++)
			{
				int32_t blue = g_paletteSource[second * 4] + firstBlue;
				int32_t green = g_paletteSource[second * 4 + 1] + firstGreen;
				int32_t red = g_paletteSource[second * 4 + 2] + firstRed;
				if (blue > 255)
					blue = 255;
				if (green > 255)
					green = 255;
				if (red > 255)
					red = 255;
				int32_t lookup = ((blue & ~7) * 32 + (green & ~7)) * 4 + (red >> 3);
				*output++ = g_rgbToPaletteIndex[lookup];
			}
		}
	}

	// FUNCTION: TOY2 0x00471250 [PROVISIONAL]
	void BuildSubtractivePaletteTable()
	{
		uint8_t* output = g_subtractivePaletteTable;
		for (int32_t first = 0; first < 256; first++)
		{
			int32_t firstBlue = g_paletteSource[first * 4];
			int32_t firstGreen = g_paletteSource[first * 4 + 1];
			int32_t firstRed = g_paletteSource[first * 4 + 2];
			for (int32_t second = 0; second < 256; second++)
			{
				int32_t blue = firstBlue - g_paletteSource[second * 4];
				int32_t green = firstGreen - g_paletteSource[second * 4 + 1];
				int32_t red = firstRed - g_paletteSource[second * 4 + 2];
				if (blue < 0)
					blue = 0;
				if (green < 0)
					green = 0;
				if (red < 0)
					red = 0;
				int32_t lookup = ((blue & ~7) * 32 + (green & ~7)) * 4 + (red >> 3);
				*output++ = g_rgbToPaletteIndex[lookup];
			}
		}
	}

	// FUNCTION: TOY2 0x00471300 [MATCHED]
	void SetNewPalette(const uint8_t* source, uint32_t tableFlags)
	{
		Logger::Log("SOFT : SetNewPalette.\n");
		LoadPaletteEntries(source);

		if (g_rgbToPaletteIndex == NULL)
			g_rgbToPaletteIndex = (uint8_t*)malloc(0x8000);
		if (g_paletteBlend25Table == NULL)
			g_paletteBlend25Table = (uint8_t*)malloc(0x10000);
		if (g_paletteBlend50Table == NULL)
			g_paletteBlend50Table = (uint8_t*)malloc(0x10000);
		if (g_paletteBlend75Table == NULL)
			g_paletteBlend75Table = (uint8_t*)malloc(0x10000);
		if (g_additivePaletteTable == NULL)
			g_additivePaletteTable = (uint8_t*)malloc(0x10000);
		if (g_subtractivePaletteTable == NULL)
			g_subtractivePaletteTable = (uint8_t*)malloc(0x10000);
		if (g_paletteColourOffsetTable == NULL)
			g_paletteColourOffsetTable = (uint8_t*)malloc(0x20000);

		if (tableFlags & PALETTE_TABLE_RGB)
			BuildRGBToPaletteTable();
		if (tableFlags & PALETTE_TABLE_BLEND)
		{
			BuildPaletteBlendTable(g_paletteBlend25Table, 0x100);
			BuildPaletteBlendTable(g_paletteBlend50Table, 0x200);
			BuildPaletteBlendTable(g_paletteBlend75Table, 0x300);
		}
		if (tableFlags & PALETTE_TABLE_ADDITIVE)
			BuildAdditivePaletteTable();
		if (tableFlags & PALETTE_TABLE_SUBTRACTIVE)
			BuildSubtractivePaletteTable();
		if (tableFlags & PALETTE_TABLE_COLOUR_OFFSET)
			BuildPaletteColourOffsetTable();
	}
}
