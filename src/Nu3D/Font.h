#pragma once

#include "Common.h"
#include "Nu3D/Nu3D.h"
#include <windows.h>

// Looks like these methods are purely for debug methods that are never called
namespace Nu3D
{
	typedef void (*DrawTextStringFunc)(const char* text);
	typedef int32_t (*CalculateTextSizeFunc)(const char* text);

	struct GlyphInfo
	{
		float uvMinX;
		float uvMinY;
		float uvMaxX;
		float uvMaxY;
		int16_t width;
		int16_t height;
	};

	struct Font
	{
		Nu3D::Font* next;
		Nu3D::Font* prev;
		int16_t unkInt1;
		int16_t unkInt2;
		char fontName[12];
		int16_t fontId;
		int16_t fontHeight;
		int16_t fontAscent;
		int16_t unkInt3;
		int16_t atlasWidth;
		int16_t atlasHeight;
		int16_t numGlyphs;
		int16_t unkInt4;
		HBITMAP bmpHandle;
		int32_t texIndex;
		uint8_t charToGlyphIndex[256];
		GlyphInfo glyphs[];

		static int32_t Init();
		static Font* Build(const char* fontName, int32_t fontSize, const char* charSet);
		static void SetFont(Font* font);
		static void SetTextColor(int32_t color);
		static void SetTextClipRect(int32_t x1, int32_t y1, int32_t x2, int32_t y2);
		static void BuildFontTextures();
		static int32_t BuildTexResource(Font* font);
		static void SetFontScale(float scaleX, float scaleY);
		static void SetTextCursor(float x, float y);
		static void SetRenderFlags(int32_t flags);
		static void ResetContext();
		static void DrawTextString(const char* text);
		static void DrawScaledTextString(const char* text);
		static int32_t CalculateUnscaledTextSize(const char* text);
		static int32_t CalculateScaledTextSize(const char* text);
		static HBITMAP CreateAtlasBmp(int32_t width, int32_t height);
		static void Destroy(Font* font);
		static void ClearList();
		static Font* BuildObject(int32_t numGlyphs);
	};

	extern float g_scaledFontAscent;

	extern HDC g_fontDC;
	extern int32_t g_fontDCReady;
	extern HGDIOBJ g_oldBitmap;

	extern VertexTL g_textVertices[6];
	extern Font* g_currentFont;
	extern int32_t g_currentFontTexIndex;
	extern Font* g_fontListHead;
	extern int32_t g_fontInitialized;
	extern float g_textCursorX;
	extern float g_textCursorY;
	extern int32_t g_textCursorOffsetX;
	extern int32_t g_textClipX1;
	extern int32_t g_textClipY1;
	extern int32_t g_textClipX2;
	extern int32_t g_textClipY2;
	extern float g_fontScaleY;
	extern float g_fontScaleX;
	extern DrawTextStringFunc g_drawTextStringFunc;
	extern CalculateTextSizeFunc g_calculateTextSizeFunc;
	extern float g_scaledFontHeight;
	extern int32_t g_textHeight;
	extern int32_t g_fontRenderFlags;

	STATIC_ASSERT(sizeof(Font) == 0x130);
	STATIC_ASSERT(sizeof(GlyphInfo) == 0x14);
}