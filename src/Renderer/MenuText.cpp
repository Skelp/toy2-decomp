#include "Renderer/Renderer.h"
#include "Renderer/Sprite.h"
#include "Renderer/SpriteSheets.h"
#include "Nu3D/Font.h"
#include "Nu3D/Math.h"
#include "Toy2/Toy2.h"
#include <cstring>

// The text the menus draw. DrawMenuText and DrawMainMenuText lay out one centred
// line, DrawMenuTextScaled places one at a given size, DrawChar draws a single
// glyph from the font sheet, and BlitTextureByIndexOffset is the blit they share.
//
// Retail run 0x0049B260-0x0049B630, between the AudioManager.cpp and
// SaveManager.cpp runs. The run sits inside the retail toy2.cpp object, so this
// is one of the files that object holds.

namespace Renderer
{
	// FUNCTION: TOY2 0x0049B260 [MATCHED]
	void BlitTextureByIndexOffset(uint32_t textureIndex,
		int32_t destX,
		int32_t destY,
		int32_t width,
		int32_t height,
		int32_t wrapX,
		int32_t wrapY,
		int32_t sourceOffsetX,
		int32_t sourceOffsetY)
	{ BlitTextureByIndex(textureIndex, destX, destY, width, height, wrapX, wrapY, destX + sourceOffsetX, destY + sourceOffsetY); }

	// FUNCTION: TOY2 0x0049B2A0 [PROVISIONAL]
	void DrawMenuText(int16_t yPos, char* text, int32_t dimmed)
	{
		int32_t textLength = 0;

		if (*text)
			while (text[++textLength]) {};

		int32_t xPos = (40 - textLength) * 4;

		if (textLength > 0)
		{
			do
			{
				uint8_t currentChar = *text++;

				if (currentChar == '1')
					Sprite::DrawTile(xPos - 4, yPos - 3, 68, 2);
				else if (currentChar == '2')
					Sprite::DrawTile(xPos - 4, yPos - 3, 68, 3);
				else if (currentChar == '3')
					Sprite::DrawTile(xPos - 4, yPos - 3, 68, 1);
				else if (currentChar == '4')
					Sprite::DrawTile(xPos - 4, yPos - 3, 68, 0);
				else if (currentChar != ' ')
				{
					if (currentChar != '\'')
						currentChar += 0x9F;
					else
						currentChar = 47;

					if (dimmed)
						Sprite::DrawScaled(xPos, yPos, 67, currentChar, 128, 128, 128, 0, 2048, 2048);
					else
						Sprite::DrawScaled(xPos, yPos, 67, currentChar, 128, 128, 128, 255, 2048, 2048);
				}

				xPos += 8;
			} while (--textLength != 0);
		}
	}

	// FUNCTION: TOY2 0x0049B3B0 [PROVISIONAL]
	void DrawMenuTextScaled(int32_t xPos, int32_t yPos, char* text, int32_t dimmed, int32_t alignment, int32_t scale)
	{
		int32_t textLength = strlen(text);
		int32_t drawX;

		switch (alignment)
		{
			case 0:
				drawX = xPos;
				break;
			case 2:
				drawX = xPos - textLength * scale * 16 / 4096;
				break;
			default:
				drawX = xPos - textLength * scale * 16 / 8192;
				break;
		}

		if (textLength > 0)
		{
			int32_t charAdvance = scale * 16 / 4096;
			xPos = textLength;
			do
			{
				uint8_t currentChar = *text++;
				if (currentChar != ' ')
				{
					switch (currentChar)
					{
						case '\'':
							currentChar = 47;
							break;
						case ',':
							currentChar = 26;
							break;
						case '.':
							currentChar = 27;
							break;
						case '/':
							currentChar = 28;
							break;
						case '\\':
							currentChar = 29;
							break;
						case '?':
							currentChar = 30;
							break;
						case ':':
							currentChar = 31;
							break;
						case ';':
							currentChar = 32;
							break;
						case '(':
							currentChar = 33;
							break;
						case ')':
							currentChar = 34;
							break;
						case '0':
							currentChar += 0xFC;
							break;
						case '1':
						case '2':
						case '3':
						case '4':
						case '5':
						case '6':
						case '7':
						case '8':
						case '9':
							currentChar += 0xF2;
							break;
						case 'A':
						case 'B':
						case 'C':
						case 'D':
							currentChar += 0xEC;
							break;
						default:
							currentChar += 0x9F;
							break;
					}

					if (dimmed)
						Sprite::DrawScaled(drawX, yPos, 67, currentChar, 128, 128, 128, 0, scale, scale);
					else
						Sprite::DrawScaled(drawX, yPos, 67, currentChar, 128, 128, 128, 255, scale, scale);
				}

				drawX += charAdvance;
			} while (--xPos != 0);
		}
	}

	// FUNCTION: TOY2 0x0049B580 [PROVISIONAL]
	void DrawMainMenuText(int16_t yPos, char* text, int32_t fadeAlpha)
	{
		char* charPtr = text;
		int32_t strLength = 0;

		if (*text)
			while (text[++strLength]) {};

		int16_t xPos = 160 - 6 * strLength;

		if (strLength > 0)
		{
			int32_t remaining = strLength;

			do
			{
				char currentChar = *charPtr++;

				if (currentChar != ' ')
				{
					uint8_t tileIndex;

					if (currentChar == '\'')
						tileIndex = 47;
					else
						tileIndex = currentChar - 97;

					if (fadeAlpha == 128)
						Sprite::DrawScaled(xPos, yPos, 50, tileIndex, 128, 128, 128, 255, 2048, 2048);
					else
						Sprite::DrawScaled(xPos, yPos, 50, tileIndex, 255, 255, 255, (fadeAlpha << 9) + 96, 2048, 2048);
				}

				xPos += 12;
				--remaining;

			} while (remaining);
		}
	}

	// FUNCTION: TOY2 0x0049B630 [PROVISIONAL]
	void DrawChar(int32_t xPos, int32_t yPos, uint8_t character, uint32_t red, uint32_t green, uint32_t blue, int32_t fullWidthLayout)
	{
		if (character == '@')
		{
			Sprite::DrawTiledFixed((int16_t)xPos, (int16_t)(yPos - 2), 38, 1);
			return;
		}
		if (character == '~')
		{
			Sprite::DrawTiledFixed((int16_t)xPos, (int16_t)(yPos - 2), 38, 0);
			return;
		}
		if (character == ' ')
			return;

		switch (character)
		{
			case '!':
				character = 41;
				break;
			case '\'':
				character = 48;
				break;
			case '*':
				character = 50;
				break;
			case ',':
				character = 37;
				break;
			case '-':
				character = 49;
				break;
			case '.':
				character = 36;
				break;
			case '>':
				character = 45;
				break;
			case '?':
				character = 40;
				break;
			default:
				if (character <= '9')
					character += 0xEA;
				else
					character += 0x9F;
				break;
		}

		if (fullWidthLayout)
			Sprite::DrawScaled((int16_t)xPos, (int16_t)yPos, 20, character, red, green, blue, 255, 0x800, 0x800);
		else
			Sprite::DrawScaledFixed((int16_t)xPos, (int16_t)yPos, 20, character, red, green, blue, 255, 0x800, 0x800);
	}

}
