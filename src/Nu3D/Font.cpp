#include "Nu3D/Font.h"

#include "DrawingDevice.h"

namespace Nu3D
{
	// --------- Nu3D::Font ---------

	// GLOBAL: TOY2 0x0050865C
	int32_t g_textClipX2 = 0;

	// GLOBAL: TOY2 0x00508660
	int32_t g_textClipY2 = 0;

	// GLOBAL: TOY2 0x0050866C
	float g_fontScaleY = 0.0;

	// GLOBAL: TOY2 0x00508670
	float g_scaledFontHeight = 0.0;

	// GLOBAL: TOY2 0x00508674
	float g_scaledFontAscent = 1.0;

	// GLOBAL: TOY2 0x00884490
	VertexTL g_textVertices[6];

	// GLOBAL: TOY2 0x00884560
	int32_t g_currentFontTexIndex = 0;

	// GLOBAL: TOY2 0x00884574
	Font* g_currentFont = 0;

	// GLOBAL: TOY2 0x00884580
	float g_textCursorX = 0.0;

	// GLOBAL: TOY2 0x00884584
	float g_textCursorY = 0.0;

	// GLOBAL: TOY2 0x0088458C
	int32_t g_textClipX1 = 0;

	// GLOBAL: TOY2 0x00884590
	int32_t g_textClipY1 = 0;

	// STUB: TOY2 0x004B3AD0
	int32_t Font::Init() { return 0; }

	// STUB: TOY2 0x004B4110
	Font* Font::Build(const char* fontName, int32_t fontSize, const char* charSet) { return 0; }

	// FUNCTION: TOY2 0x004B3C60 [MATCHED]
	void Font::SetFont(Font* font)
	{
		if (DrawingDevice::GetD3DDevice())
		{
			g_currentFont = font;
			if (font)
			{
				g_currentFontTexIndex = font->texIndex;
				g_scaledFontHeight = (float)font->fontHeight * g_fontScaleY;
				g_scaledFontAscent = (float)font->fontAscent * g_fontScaleY;
			}
			else
			{
				g_currentFontTexIndex = 0;
			}
		}
	}

	// FUNCTION: TOY2 0x004B44D0 [MATCHED]
	void Font::SetTextColor(int32_t color)
	{
		uint32_t c = (uint32_t)color | 0xFF000000;
		g_textVertices[0].diffuse.value = c;
		g_textVertices[0].specular.value = c;
		g_textVertices[1].diffuse.value = c;
		g_textVertices[1].specular.value = c;
		g_textVertices[2].diffuse.value = c;
		g_textVertices[2].specular.value = c;
		g_textVertices[3].diffuse.value = c;
		g_textVertices[3].specular.value = c;
		g_textVertices[4].diffuse.value = c;
		g_textVertices[4].specular.value = c;
		g_textVertices[5].diffuse.value = c;
		g_textVertices[5].specular.value = c;
	}

	// FUNCTION: TOY2 0x004B3980 [MATCHED]
	void Font::SetTextClipRect(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
	{
		g_textClipX1 = x1;
		g_textClipY1 = y1;
		g_textClipX2 = x2;
		g_textClipY2 = y2;
	}

	// STUB: TOY2 0x004B3C20
	void Font::BuildFontTextures() {}

	// FUNCTION: TOY2 0x004B4450 [MATCHED]
	void Font::SetTextCursor(float x, float y)
	{
		g_textCursorX = x;
		g_textCursorY = y;
	}
}