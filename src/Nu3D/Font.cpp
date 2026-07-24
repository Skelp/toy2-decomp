#include "Nu3D/Font.h"

#include "DrawingDevice.h"
#include "Nu3D/BmpDataNode.h"

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

	// GLOBAL: TOY2 0x00884570
	Font* g_fontListHead = 0;

	// GLOBAL: TOY2 0x00884568
	int32_t g_fontInitialized = 0;

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

	// GLOBAL: TOY2 0x00884588
	int32_t g_textCursorOffsetX = 0;

	// GLOBAL: TOY2 0x0088458C
	int32_t g_textClipX1 = 0;

	// GLOBAL: TOY2 0x00884590
	int32_t g_textClipY1 = 0;

	// GLOBAL: TOY2 0x00884598
	int32_t g_fontRenderFlags = 0;

	// STUB: TOY2 0x004B38E0
	void Font::SetFontScale(float scaleX, float scaleY) {}

	// FUNCTION: TOY2 0x004B3AD0
	int32_t Font::Init()
	{
		if (g_fontInitialized)
			ClearList();
		g_currentFont = 0;
		g_currentFontTexIndex = 0;
		g_textCursorOffsetX = 0;
		g_textCursorX = 0;
		g_textCursorY = 0;
		g_textVertices[0].position.z = 0.5f;
		g_textVertices[0].rhw = 2.0f;
		g_textVertices[0].diffuse.value = 0xFFFFFFFF;
		g_textVertices[0].specular.value = 0xFFFFFFFF;
		g_textVertices[1].position.z = 0.5f;
		g_textVertices[1].rhw = 2.0f;
		g_textVertices[1].diffuse.value = 0xFFFFFFFF;
		g_textVertices[1].specular.value = 0xFFFFFFFF;
		g_textVertices[2].position.z = 0.5f;
		g_textVertices[2].rhw = 2.0f;
		g_textVertices[2].diffuse.value = 0xFFFFFFFF;
		g_textVertices[2].specular.value = 0xFFFFFFFF;
		g_textVertices[3].position.z = 0.5f;
		g_textVertices[3].rhw = 2.0f;
		g_textVertices[3].diffuse.value = 0xFFFFFFFF;
		g_textVertices[3].specular.value = 0xFFFFFFFF;
		g_textVertices[4].position.z = 0.5f;
		g_textVertices[4].rhw = 2.0f;
		g_textVertices[4].diffuse.value = 0xFFFFFFFF;
		g_textVertices[4].specular.value = 0xFFFFFFFF;
		g_textVertices[5].position.z = 0.5f;
		g_textVertices[5].rhw = 2.0f;
		g_textVertices[5].diffuse.value = 0xFFFFFFFF;
		g_textVertices[5].specular.value = 0xFFFFFFFF;
		SetFontScale(1.0f, 1.0f);
		SetRenderFlags(0);
		return 1;
	}

	// STUB: TOY2 0x004B4110
	Font* Font::Build(const char* fontName, int32_t fontSize, const char* charSet) { return 0; }

	// GLOBAL: TOY2 0x0088456C
	int16_t g_nextFontId = 0;

	// GLOBAL: TOY2 0x004DDABC
	int16_t g_defaultFontType = 0;

	// FUNCTION: TOY2 0x004B3A20
	Font* Font::BuildObject(int32_t numGlyphs)
	{
		int32_t size = sizeof(Font) + numGlyphs * sizeof(GlyphInfo);
		Font* font = (Font*)malloc(size);
		if (font)
		{
			memset(font, 0, size);
			font->next = g_fontListHead;
			if (g_fontListHead)
				g_fontListHead->prev = font;
			g_fontListHead = font;
			font->unkInt1 = g_defaultFontType;
			font->fontId = g_nextFontId;
			g_nextFontId++;
		}
		return font;
	}

	// FUNCTION: TOY2 0x004B39D0
	void Font::Destroy(Font* font)
	{
		if (font->texIndex)
			ReleaseBmpDataNode_T((BmpDataNode*)font->texIndex);
		if (font->next)
			font->next->prev = font->prev;
		if (font->prev)
		{
			font->prev->next = font->next;
			free(font);
			return;
		}
		g_fontListHead = font->next;
		free(font);
	}

	// FUNCTION: TOY2 0x004B3A90
	void Font::ClearList()
	{
		if (! g_fontInitialized)
			return;
		g_fontInitialized = 0;
		while (g_fontListHead)
		{
			Destroy(g_fontListHead);
			g_currentFontTexIndex = 0;
			g_currentFont = 0;
		}
	}

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

	// FUNCTION: TOY2 0x004B3CC0
	int32_t Font::BuildTexResource(Font* font)
	{
		font->texIndex = 0;
		if (font->bmpHandle)
		{
			char name[20];
			sprintf(name, "FONT_%04d", font->fontId);
			font->texIndex = CreateTextureResource(font->bmpHandle, name, 4);
			if (font->texIndex)
				return 1;
		}
		return 0;
	}

	// FUNCTION: TOY2 0x004B3C20
	void Font::BuildFontTextures()
	{
		if (DrawingDevice::GetD3DDevice())
		{
			for (Font* font = g_fontListHead; font; font = font->next)
				BuildTexResource(font);
			SetFont(g_currentFont);
		}
	}

	// FUNCTION: TOY2 0x004B4450 [MATCHED]
	void Font::SetTextCursor(float x, float y)
	{
		g_textCursorX = x;
		g_textCursorY = y;
	}

	// FUNCTION: TOY2 0x004B38C0 [MATCHED]
	void Font::SetRenderFlags(int32_t flags) { g_fontRenderFlags = flags ? 0x200 : 0; }
}