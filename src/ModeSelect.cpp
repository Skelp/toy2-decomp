#include "ModeSelect.h"
#include "DrawingDevice.h"
#include "Logger.h"
#include "FileUtils.h"
#include "Toy2/Direct6.h"
#include "Toy2/Win95.h"
#include "Toy2/Toy2.h"

#include <directx6/ddraw.h>
#include <directx6/d3d.h>
#include <cstdio>
#include <windows.h>
#include "ModeSelectInternal.h"

namespace ModeSelect
{
	enum SelectionState
	{
		SELECTION_STATE_DRIVER,
		SELECTION_STATE_RENDER_METHOD,
		SELECTION_STATE_DISPLAY_MODE,
		SELECTION_STATE_WINDOW_MODE,
		SELECTION_STATE_EXIT,
	};

	// GLOBAL: TOY2 0x00505590
	int32_t g_forceFullscreen = 1;

	// GLOBAL: TOY2 0x004ECC10
	int32_t g_unusedFlag1 = 1;

	// GLOBAL: TOY2 0x004ECC0C
	int32_t g_unusedFlag2 = 1;

	// GLOBAL: TOY2 0x00884034;
	DeviceFilterCallback_t g_ddDeviceFilterCallback;

	// GLOBAL: TOY2 0x00884020
	int32_t g_foundRefDevice;

	// GLOBAL: TOY2 0x00884024
	int32_t g_foundAnyD3DDevice;

	// GLOBAL: TOY2 0x0088401C
	LPDIRECTDRAW4 g_ddraw4;

	// GLOBAL: TOY2 0x0053C598
	HWND g_hWnd;

	// GLOBAL: TOY2 0x0053C59C
	HBITMAP g_backgroundBitmap;

	// GLOBAL: TOY2 0x0053C5BC
	int32_t g_lineHeight;

	// GLOBAL: TOY2 0x0053C5B8
	int32_t g_textStartY;

	// GLOBAL: TOY2 0x0053C5B4
	int32_t g_textStartX;

	// GLOBAL: TOY2 0x0053C5C0
	int32_t g_instructionLineHeight;

	// GLOBAL: TOY2 0x0053C5A4
	HFONT g_mainFont;

	// GLOBAL: TOY2 0x0053C5A8
	HFONT g_headerFont;

	// GLOBAL: TOY2 0x0053C5A0
	HFONT g_instructionFont;

	// GLOBAL: TOY2 0x0053C57C
	DrawingDevice::DDAppDevice::App* g_ddAppIterator;

	// GLOBAL: TOY2 0x0053C58C
	int32_t g_savedDriverIndex;

	// GLOBAL: TOY2 0x0053C590
	int32_t g_savedDeviceIndex;

	// GLOBAL: TOY2 0x0053C594
	int32_t g_savedModeIndex;

	// GLOBAL: TOY2 0x0053C5B0
	int32_t g_selectionState;

	// GLOBAL: TOY2 0x0053C5AC
	int32_t g_keyDown;

	// GLOBAL: TOY2 0x0053C580
	DrawingDevice::DDAppDevice::App* g_selectedDDApp;

	// GLOBAL: TOY2 0x0053C588
	DrawingDevice::DDAppDevice* g_ddAppSelectedDevice;

	// GLOBAL: TOY2 0x0053C574
	DrawingDevice::DDAppDevice::DisplayMode* g_ddAppSelectedDisplayMode;

	// GLOBAL: TOY2 0x0053C568
	HDC g_offscreenDC;

	// GLOBAL: TOY2 0x0053C564
	HGDIOBJ g_offscreenBitmap;

	// GLOBAL: TOY2 0x0053C570
	HGDIOBJ g_originalBitmap;

	// GLOBAL: TOY2 0x0053C5CC
	int32_t g_paintError;

	// GLOBAL: TOY2 0x0053C56C
	HGDIOBJ g_savedHeaderFont;

	// GLOBAL: TOY2 0x0053C584
	COLORREF g_savedTextColor;

	// GLOBAL: TOY2 0x0053C578
	int32_t g_savedBkMode;

	// GLOBAL: TOY2 0x004F574C
	int32_t g_highlightColor = 0xFFFF;

	// GLOBAL: TOY2 0x004F56FC
	const char* g_deviceSelectionInstructions[5] = {
		"Please select your preferred display",
		"device using the arrow keys.",
		"Press the space bar to confirm",
		"your choice.",
		0,
	};

	// GLOBAL: TOY2 0x004F5710
	const char* g_renderMethodInstructions[7] = {
		"First, choose a Render Method",
		"using the up and down arrow keys.",
		"Select \"Hardware\" for 3D cards or",
		"\"Software\" for older or non-3D",
		"cards. Then press the space bar",
		"to continue.",
		0,
	};

	// GLOBAL: TOY2 0x004F572C
	const char* g_resolutionSelectionInstructions[8] = {
		"Choose a Screen Resolution using",
		"the up and down arrow keys.",
		"A higher resolution will look",
		"better but may play more slowly.",
		"Press the space bar to play the",
		"game.",
		0,
		reinterpret_cast<const char*>(5),
	};
}

namespace ModeSelect
{
	// FUNCTION: TOY2 0x004AC390 [MATCHED]
	void SetForceFullscreen_T(int32_t forceFullscreen) { SetForceFullscreen(forceFullscreen); }

	// FUNCTION: TOY2 0x004AC3A0 [MATCHED]
	int32_t EnumerateDrivers_T(DeviceFilterCallback_t callback) { return EnumerateDrivers(callback); }

	// FUNCTION: TOY2 0x00412B00 [MATCHED]
	int32_t DeviceFilterCallback(LPDDCAPS caps, void* context) { return FALSE; }

	// FUNCTION: TOY2 0x00431C40 [MATCHED]
	BOOL DrawTextOutA(HDC hdc, int32_t x, int32_t y, const char* format, ...)
	{
		char buffer[512];
		va_list argList;

		va_start(argList, format);
		vsprintf(buffer, format, argList);

		return TextOutA(hdc, x, y, buffer, strlen(buffer));
	}

	// FUNCTION: TOY2 0x00433410 [PROVISIONAL]
	COLORREF ApplyTextStyle(HDC hdc, HFONT hfont, COLORREF textColor, int32_t styleOp)
	{
		COLORREF result;

		if (styleOp == 1)
		{
			if (hfont)
				g_savedHeaderFont = SelectObject(hdc, hfont);

			g_savedTextColor = SetTextColor(hdc, textColor);
			result = SetBkMode(hdc, 1);
			g_savedBkMode = result;
		}
		else if (styleOp == 2)
		{
			if (hfont)
				SelectObject(hdc, g_savedHeaderFont);

			SetTextColor(hdc, g_savedTextColor);
			return SetBkMode(hdc, g_savedBkMode);
		}
		else
		{
			if (hfont)
				SelectObject(hdc, hfont);

			return SetTextColor(hdc, textColor);
		}

		return result;
	}

	// FUNCTION: TOY2 0x00432AD0 [PROVISIONAL]
	LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		if (msg > WM_CLOSE)
		{
			if (msg != WM_QUIT)
			{
				if (msg == WM_KEYDOWN)
					g_keyDown = wParam;

				return DefWindowProcA(hWnd, msg, wParam, lParam);
			}

			Logger::GetErrorHandler("C:\\projects\\toy2\\modesel.cpp", 162)("");
			return DefWindowProcA(hWnd, msg, wParam, lParam);
		}

		switch (msg)
		{
			case WM_CLOSE:
				Logger::GetErrorHandler("C:\\projects\\toy2\\modesel.cpp", 162)("");
				return DefWindowProcA(hWnd, msg, wParam, lParam);

			case WM_CREATE:
				g_offscreenDC = 0;
				g_offscreenBitmap = 0;
				g_originalBitmap = 0;
				return DefWindowProcA(hWnd, msg, wParam, lParam);

			case WM_DESTROY:
				if (g_offscreenDC)
				{
					if (g_offscreenBitmap)
					{
						SelectObject(g_offscreenDC, g_originalBitmap);
						DeleteObject(g_offscreenBitmap);
					}

					DeleteDC(g_offscreenDC);
				}

				g_selectionState = SELECTION_STATE_EXIT;
				break;

			case WM_PAINT:
				PAINTSTRUCT paint;
				HDC paintDC = BeginPaint(hWnd, &paint);

				if (! paintDC)
				{
					g_paintError = 1;
					return DefWindowProcA(hWnd, msg, wParam, lParam);
				}

				RECT clientRect;
				GetClientRect(hWnd, &clientRect);

				if (! g_offscreenDC)
				{
					g_offscreenDC = CreateCompatibleDC(paintDC);

					if (! g_offscreenDC)
					{
						EndPaint(hWnd, &paint);
						g_paintError = 1;
						return DefWindowProcA(hWnd, msg, wParam, lParam);
					}

					g_offscreenBitmap = CreateCompatibleBitmap(paintDC, clientRect.right, clientRect.bottom);

					if (g_offscreenBitmap)
						g_originalBitmap = SelectObject(g_offscreenDC, g_offscreenBitmap);
				}

				if (g_backgroundBitmap)
				{
					HDC imageDC = CreateCompatibleDC(paintDC);

					if (imageDC)
					{
						BITMAP bitmapInfo;
						HGDIOBJ oldBitmap = SelectObject(imageDC, g_backgroundBitmap);

						GetObjectA(g_backgroundBitmap, sizeof(BITMAP), &bitmapInfo);
						StretchBlt(g_offscreenDC, 0, 0, clientRect.right, clientRect.bottom, imageDC, 0, 0, bitmapInfo.bmWidth, bitmapInfo.bmHeight, SRCCOPY);

						SelectObject(imageDC, oldBitmap);
						DeleteDC(imageDC);
					}
				}

				int32_t textX = g_textStartX;
				int32_t textY = g_textStartY;

				if (g_headerFont)
					g_savedHeaderFont = SelectObject(g_offscreenDC, g_headerFont);

				g_savedTextColor = SetTextColor(g_offscreenDC, 0xFFFF);
				g_savedBkMode = SetBkMode(g_offscreenDC, 1);

				if (g_ddAppIterator->chainDDApp)
				{
					if (g_headerFont)
						SelectObject(g_offscreenDC, g_headerFont);

					SetTextColor(g_offscreenDC, 0);
					DrawTextOutA(g_offscreenDC, textX - 2, textY - 2, "Device");
					DrawTextOutA(g_offscreenDC, textX + 2, textY + 2, "Device");

					if (g_headerFont)
						SelectObject(g_offscreenDC, g_headerFont);

					SetTextColor(g_offscreenDC, 0xFF);
					DrawTextOutA(g_offscreenDC, textX, textY, "Device");

					textY += 4 * g_lineHeight / 2;
				}

				if (g_headerFont)
					SelectObject(g_offscreenDC, g_headerFont);

				SetTextColor(g_offscreenDC, 0);
				DrawTextOutA(g_offscreenDC, textX - 2, textY - 2, "Render Method");
				DrawTextOutA(g_offscreenDC, textX + 2, textY + 2, "Render Method");

				if (g_headerFont)
					SelectObject(g_offscreenDC, g_headerFont);

				SetTextColor(g_offscreenDC, 0xFF);
				DrawTextOutA(g_offscreenDC, textX, textY, "Render Method");

				int32_t nextTextY = 4 * g_lineHeight / 2 + textY;

				if (g_headerFont)
					SelectObject(g_offscreenDC, g_headerFont);

				SetTextColor(g_offscreenDC, 0);
				DrawTextOutA(g_offscreenDC, textX - 2, nextTextY - 2, "Screen Resolution");
				DrawTextOutA(g_offscreenDC, textX + 2, nextTextY + 2, "Screen Resolution");

				if (g_headerFont)
					SelectObject(g_offscreenDC, g_headerFont);

				SetTextColor(g_offscreenDC, 0xFF);
				DrawTextOutA(g_offscreenDC, textX, nextTextY, "Screen Resolution");

				if (g_headerFont)
					SelectObject(g_offscreenDC, g_savedHeaderFont);

				SetTextColor(g_offscreenDC, g_savedTextColor);
				SetBkMode(g_offscreenDC, g_savedBkMode);

				if (g_mainFont)
					g_savedHeaderFont = SelectObject(g_offscreenDC, g_mainFont);

				g_savedTextColor = SetTextColor(g_offscreenDC, 0);
				g_savedBkMode = SetBkMode(g_offscreenDC, 1);

				int32_t deviceTextX = g_textStartX;
				int32_t deviceTextY = g_textStartY + g_lineHeight / 2;

				if (g_ddAppIterator->chainDDApp)
				{
					DrawTextOutA(g_offscreenDC, g_textStartX - 2, deviceTextY - 2, g_selectedDDApp->driverName);
					DrawTextOutA(g_offscreenDC, deviceTextX + 2, deviceTextY + 2, g_selectedDDApp->driverName);

					int32_t savedTextColor;

					if (g_selectionState)
						savedTextColor = 0x8888;
					else
						savedTextColor = g_highlightColor;

					if (g_mainFont)
						SelectObject(g_offscreenDC, g_mainFont);

					SetTextColor(g_offscreenDC, savedTextColor);
					DrawTextOutA(g_offscreenDC, deviceTextX, deviceTextY, g_selectedDDApp->driverName);

					deviceTextY += 4 * g_lineHeight / 2;
				}

				if (g_mainFont)
					SelectObject(g_offscreenDC, g_mainFont);

				SetTextColor(g_offscreenDC, 0);
				char* deviceName = g_ddAppSelectedDevice->deviceName;

				if (! strcmpi(g_ddAppSelectedDevice->deviceName, "rgb emulation"))
					deviceName = "Software";

				if (! strcmpi(deviceName, "direct3d hal"))
					deviceName = "Hardware";

				DrawTextOutA(g_offscreenDC, deviceTextX - 2, deviceTextY - 2, deviceName);
				DrawTextOutA(g_offscreenDC, deviceTextX + 2, deviceTextY + 2, deviceName);

				int32_t deviceHightlightColor;

				if (g_selectionState == SELECTION_STATE_RENDER_METHOD)
					deviceHightlightColor = g_highlightColor;
				else
					deviceHightlightColor = 0x8888;

				if (g_mainFont)
					SelectObject(g_offscreenDC, g_mainFont);

				SetTextColor(g_offscreenDC, deviceHightlightColor);
				DrawTextOutA(g_offscreenDC, g_textStartX, deviceTextY, deviceName);

				int32_t modeTextY = 4 * g_lineHeight / 2 + deviceTextY;

				if (g_selectionState == SELECTION_STATE_WINDOW_MODE)
				{
					if (g_mainFont)
						SelectObject(g_offscreenDC, g_mainFont);

					SetTextColor(g_offscreenDC, 0);

					char* selectedModeText;

					if (g_ddAppSelectedDevice->canRenderWindowedOnPrimary)
						selectedModeText = "Windowed";
					else
						selectedModeText = g_ddAppSelectedDisplayMode->modeText;

					DrawTextOutA(g_offscreenDC, g_textStartX - 2, modeTextY - 2, selectedModeText);

					if (g_ddAppSelectedDevice->canRenderWindowedOnPrimary)
						DrawTextOutA(g_offscreenDC, g_textStartX + 2, modeTextY + 2, "Windowed");
					else
						DrawTextOutA(g_offscreenDC, g_textStartX + 2, modeTextY + 2, g_ddAppSelectedDisplayMode->modeText);

					if (g_mainFont)
						SelectObject(g_offscreenDC, g_mainFont);

					SetTextColor(g_offscreenDC, 0xFF);

					char* windowedModeText;

					if (g_ddAppSelectedDevice->canRenderWindowedOnPrimary)
						windowedModeText = "Windowed";
					else
						windowedModeText = g_ddAppSelectedDisplayMode->modeText;

					DrawTextOutA(g_offscreenDC, g_textStartX, modeTextY, windowedModeText);
				}
				else
				{
					if (g_mainFont)
						SelectObject(g_offscreenDC, g_mainFont);

					SetTextColor(g_offscreenDC, 0);
					DrawTextOutA(g_offscreenDC, g_textStartX - 2, modeTextY - 2, g_ddAppSelectedDisplayMode->modeText);
					DrawTextOutA(g_offscreenDC, g_textStartX + 2, modeTextY + 2, g_ddAppSelectedDisplayMode->modeText);

					int32_t modeHighlightColor;

					if (g_selectionState == SELECTION_STATE_DISPLAY_MODE)
						modeHighlightColor = g_highlightColor;
					else
						modeHighlightColor = 0x8888;

					if (g_mainFont)
						SelectObject(g_offscreenDC, g_mainFont);

					SetTextColor(g_offscreenDC, modeHighlightColor);
					DrawTextOutA(g_offscreenDC, g_textStartX, modeTextY, g_ddAppSelectedDisplayMode->modeText);
				}

				int32_t instructionFirstLineY = 3 * g_lineHeight / 2 + modeTextY;
				const char** instructionLines;

				if (g_selectionState)
				{
					if (g_selectionState == SELECTION_STATE_RENDER_METHOD)
					{
						instructionLines = g_renderMethodInstructions;
					}
					else
					{
						if (g_selectionState != SELECTION_STATE_DISPLAY_MODE)
						{
						LBL_FINISH_PAINT:

							if (g_mainFont)
								SelectObject(g_offscreenDC, g_savedHeaderFont);

							SetTextColor(g_offscreenDC, g_savedTextColor);
							SetBkMode(g_offscreenDC, g_savedBkMode);

							BitBlt(paintDC, 0, 0, clientRect.right, clientRect.bottom, g_offscreenDC, 0, 0, SRCCOPY);
							EndPaint(hWnd, &paint);

							g_paintError = 1;

							return DefWindowProcA(hWnd, msg, wParam, lParam);
						}

						instructionLines = g_resolutionSelectionInstructions;
					}
				}
				else
				{
					instructionLines = g_deviceSelectionInstructions;
				}

				if (instructionLines)
				{
					const char** shadowIter = instructionLines;

					int32_t instructionLineYIter = instructionFirstLineY;

					ApplyTextStyle(g_offscreenDC, g_instructionFont, 0, 0);

					const char* currentInstructionShadow = *instructionLines;

					if (*instructionLines)
					{
						int32_t shadowLineY = instructionFirstLineY + 2;

						do
						{
							DrawTextOutA(g_offscreenDC, g_textStartX - 2, shadowLineY - 4, currentInstructionShadow);
							DrawTextOutA(g_offscreenDC, g_textStartX + 2, shadowLineY, *shadowIter);

							currentInstructionShadow = shadowIter[1];
							++shadowIter;

							shadowLineY += g_instructionLineHeight;

						} while (currentInstructionShadow);
					}

					const char** lineIter = instructionLines;

					ApplyTextStyle(g_offscreenDC, g_instructionFont, 0xFFFFFF, 0);

					for (const char* instructionLine = *instructionLines; instructionLine;
						instructionLineYIter = (instructionLineYIter + g_instructionLineHeight))
					{
						DrawTextOutA(g_offscreenDC, g_textStartX, instructionLineYIter, instructionLine);

						instructionLine = lineIter[1];
						++lineIter;
					}
				}

				goto LBL_FINISH_PAINT;
		}

		return DefWindowProcA(hWnd, msg, wParam, lParam);
	}

	// FUNCTION: TOY2 0x00431CA0 [EFFECTIVE]
	int32_t SelectDDAppByIndex(int32_t index)
	{
		int32_t result = 0;
		DrawingDevice::DDAppDevice::App* iterator = g_ddAppIterator;

		for (g_selectedDDApp = g_ddAppIterator; result < index || index < 0; g_selectedDDApp = iterator)
		{
			iterator = iterator->chainDDApp;

			if (! iterator)
				break;

			++result;
		}

		if (index > result)
		{
			g_selectedDDApp = g_ddAppIterator;
			return 0;
		}

		return result;
	}

	// FUNCTION: TOY2 0x00431CE0 [PROVISIONAL]
	int32_t SelectDeviceByIndex(int32_t index)
	{
		int32_t result = 0;

		DrawingDevice::DDAppDevice* nextDevice;
		DrawingDevice::DDAppDevice* deviceListHead = g_selectedDDApp->deviceListHead;

		for (g_ddAppSelectedDevice = deviceListHead; result < index || index < 0; g_ddAppSelectedDevice = nextDevice)
		{
			nextDevice = deviceListHead->nextDevice;

			if (! nextDevice)
				break;

			deviceListHead = deviceListHead->nextDevice;

			++result;
		}

		if (index <= result)
		{
			g_selectedDDApp->primaryDevice = deviceListHead;
		}
		else
		{
			result = 0;

			g_ddAppSelectedDevice = g_selectedDDApp->deviceListHead;
			g_selectedDDApp->primaryDevice = g_ddAppSelectedDevice;
		}

		return result;
	}

	// FUNCTION: TOY2 0x00431DA0 [PROVISIONAL]
	int32_t SelectDisplayModeByIndex(int32_t index)
	{
		int32_t result = 0;

		DrawingDevice::DDAppDevice::DisplayMode* nextDisplayMode;
		DrawingDevice::DDAppDevice::DisplayMode* displayModeListHead = g_ddAppSelectedDevice->displayModeListHead;

		for (g_ddAppSelectedDisplayMode = displayModeListHead; result < index || index < 0; g_ddAppSelectedDisplayMode = nextDisplayMode)
		{
			nextDisplayMode = displayModeListHead->nextDisplayMode;

			if (! nextDisplayMode)
				break;

			displayModeListHead = displayModeListHead->nextDisplayMode;

			++result;
		}

		if (index <= result)
		{
			g_ddAppSelectedDevice->primaryDisplayMode = displayModeListHead;
		}
		else
		{
			result = 0;
			g_ddAppSelectedDisplayMode = g_ddAppSelectedDevice->displayModeListHead;
			g_ddAppSelectedDevice->primaryDisplayMode = g_ddAppSelectedDisplayMode;
		}

		return result;
	}

	// FUNCTION: TOY2 0x00431D40 [PROVISIONAL]
	int32_t FindFirstHardwareDevice()
	{
		int32_t result = 0;

		g_ddAppSelectedDevice = g_selectedDDApp->deviceListHead;
		DrawingDevice::DDAppDevice* selectedDevice = g_ddAppSelectedDevice;

		if (g_ddAppSelectedDevice)
		{
			while (! selectedDevice->isHardwareAccelerated)
			{
				selectedDevice = selectedDevice->nextDevice;

				++result;

				g_ddAppSelectedDevice = selectedDevice;

				if (! selectedDevice)
				{
					g_ddAppSelectedDevice = g_selectedDDApp->deviceListHead;
					g_selectedDDApp->primaryDevice = g_ddAppSelectedDevice;
					return 0;
				}
			}

			g_selectedDDApp->primaryDevice = selectedDevice;
		}
		else
		{
			g_ddAppSelectedDevice = g_selectedDDApp->deviceListHead;
			g_selectedDDApp->primaryDevice = g_ddAppSelectedDevice;
			return 0;
		}

		return result;
	}

	// FUNCTION: TOY2 0x00431E00 [PROVISIONAL]
	int32_t SelectSuitableDisplayMode()
	{
		int32_t result = 0;

		DrawingDevice::DDAppDevice::DisplayMode* displayModeListHead = g_ddAppSelectedDevice->displayModeListHead;
		g_ddAppSelectedDisplayMode = displayModeListHead;

		for (DrawingDevice::DDAppDevice::DisplayMode* index = displayModeListHead->nextDisplayMode; index; index = index->nextDisplayMode)
		{
			if (displayModeListHead->surfaceDesc.dwHeight >= 480)
				break;

			displayModeListHead = index;

			++result;
			g_ddAppSelectedDisplayMode = index;
		}

		g_ddAppSelectedDevice->primaryDisplayMode = displayModeListHead;

		return result;
	}

	// FUNCTION: TOY2 0x00432020 [PROVISIONAL]
	void Run()
	{
		using namespace DrawingDevice;

		WNDCLASSA wndClass;
		wndClass.style = CS_VREDRAW | CS_HREDRAW;
		wndClass.lpfnWndProc = WndProc;
		wndClass.cbClsExtra = 0;
		wndClass.cbWndExtra = 0;
		wndClass.hInstance = g_windowData.hInstance;
		wndClass.hIcon = LoadIconA(0, IDI_WINLOGO);
		wndClass.hCursor = LoadCursorA(0, IDC_ARROW);
		wndClass.hbrBackground = 0;
		wndClass.lpszMenuName = "";
		wndClass.lpszClassName = "Screen Mode Select";

		if (! RegisterClassA(&wndClass))
			return;

		int32_t wndExStyle = WS_EX_TOPMOST;

#ifdef APPLY_FIXES
		wndExStyle = 0;
#endif

		g_hWnd = CreateWindowExA(wndExStyle,
			"Screen Mode Select",
			"Screen Mode Select",
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			0,
			0,
			g_windowData.hInstance,
			0);

		if (! g_hWnd)
			return;

		HBITMAP createdDIBitmap = 0;

		char imagePath[512];
		FileUtils::GetPathValue(imagePath);

		strcat(imagePath, "pcbits\\rezsel.bmp");

		HANDLE loadedImage = LoadImageA(g_windowData.hInstance, imagePath, 0, 0, 0, LR_CREATEDIBSECTION | LR_LOADFROMFILE);

		if (loadedImage)
		{
			DIBSECTION dibSection;
			GetObjectA(loadedImage, sizeof(DIBSECTION), &dibSection);
			HDC windowDC = GetDC(g_hWnd);

			if (windowDC)
			{
				HDC processedDC = CreateCompatibleDC(windowDC);

				if (processedDC)
				{
					HDC imageDC = CreateCompatibleDC(windowDC);

					if (imageDC)
					{
						createdDIBitmap = CreateDIBitmap(windowDC, &dibSection.dsBmih, 0, 0, 0, 0);

						if (createdDIBitmap)
						{
							HGDIOBJ oldProcessedBitmap = SelectObject(processedDC, createdDIBitmap);
							HGDIOBJ oldDIBitmap = SelectObject(imageDC, loadedImage);

							BitBlt(processedDC, 0, 0, dibSection.dsBmih.biWidth, dibSection.dsBmih.biHeight, imageDC, 0, 0, SRCCOPY);
							SelectObject(imageDC, oldDIBitmap);
							SelectObject(processedDC, oldProcessedBitmap);
						}

						DeleteDC(imageDC);
					}

					DeleteDC(processedDC);
				}
			}

			ReleaseDC(g_hWnd, windowDC);
			DeleteObject(loadedImage);
		}

		g_backgroundBitmap = createdDIBitmap;

		g_lineHeight = 40 * GetSystemMetrics(SM_CYFULLSCREEN) / 480;
		g_textStartY = 20 * GetSystemMetrics(SM_CYFULLSCREEN) / 480;
		g_textStartX = 20 * GetSystemMetrics(SM_CXFULLSCREEN) / 640;

		g_instructionLineHeight = 5 * g_lineHeight / 6;

		g_mainFont = CreateFontA(g_lineHeight, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "comic sans ms");
		g_headerFont = CreateFontA(g_lineHeight / 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "comic sans ms");
		g_instructionFont = CreateFontA(5 * g_lineHeight / 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "comic sans ms");

		g_ddAppIterator = DrawingDevice::GetListHead();

		int32_t suitableDisplayMode;

		if (Toy2::g_toyCfgData.driverIndex < 0)
		{
			g_savedDriverIndex = SelectDDAppByIndex(0);
			g_savedDeviceIndex = FindFirstHardwareDevice();

			suitableDisplayMode = SelectSuitableDisplayMode();
		}
		else
		{
			g_savedDriverIndex = SelectDDAppByIndex(Toy2::g_toyCfgData.driverIndex);
			g_savedDeviceIndex = SelectDeviceByIndex(Toy2::g_toyCfgData.deviceIndex);

			suitableDisplayMode = SelectDisplayModeByIndex(Toy2::g_toyCfgData.displayModeIndex);
		}

		g_savedModeIndex = suitableDisplayMode;

		ShowWindow(g_hWnd, SW_SHOWMAXIMIZED);
		UpdateWindow(g_hWnd);
		SetFocus(g_hWnd);

		g_selectionState = SELECTION_STATE_DRIVER;

		while (true)
		{
			int32_t shouldExit = 0;

			MSG msg;
			if (PeekMessageA(&msg, 0, 0, 0, 0))
			{
				do
				{
					if (! GetMessageA(&msg, 0, 0, 0))
						shouldExit = 1;

					DispatchMessageA(&msg);

				} while (PeekMessageA(&msg, 0, 0, 0, 0));

				if (shouldExit)
					break;
			}

			if (g_selectionState == SELECTION_STATE_EXIT)
				break;

			int32_t shouldRedraw = 0;
			switch (g_selectionState)
			{
				case SELECTION_STATE_DRIVER: {
					if (! g_ddAppIterator->chainDDApp)
					{
						g_selectionState = SELECTION_STATE_RENDER_METHOD;
						shouldRedraw = 1;
						break;
					}

					switch (g_keyDown)
					{
						case VK_RETURN:
						case VK_SPACE: {
							DDAppDevice* deviceListHead = g_selectedDDApp->deviceListHead;

							if (deviceListHead->nextDevice)
							{
								g_selectionState = SELECTION_STATE_RENDER_METHOD;
								shouldRedraw = 1;
								break;
							}

							int32_t restoredDeviceIndex = 0;
							DDAppDevice* restoredDevice;

							for (g_ddAppSelectedDevice = g_selectedDDApp->deviceListHead; restoredDeviceIndex < g_savedDeviceIndex || g_savedDeviceIndex < 0;
								g_ddAppSelectedDevice = restoredDevice)
							{
								restoredDevice = deviceListHead->nextDevice;

								if (! restoredDevice)
									break;

								deviceListHead = deviceListHead->nextDevice;
								++restoredDeviceIndex;
							}

							if (g_savedDeviceIndex > restoredDeviceIndex)
							{
								deviceListHead = g_selectedDDApp->deviceListHead;
								restoredDeviceIndex = 0;
								g_ddAppSelectedDevice = deviceListHead;
							}

							g_selectedDDApp->primaryDevice = deviceListHead;
							g_savedDeviceIndex = restoredDeviceIndex;

							DDAppDevice::DisplayMode* restoredModeListHead = g_ddAppSelectedDevice->displayModeListHead;
							int32_t restoredModeIndex = 0;
							DDAppDevice::DisplayMode* restoredMode;

							for (g_ddAppSelectedDisplayMode = restoredModeListHead; restoredModeIndex < g_savedModeIndex || g_savedModeIndex < 0;
								g_ddAppSelectedDisplayMode = restoredMode)
							{
								restoredMode = restoredModeListHead->nextDisplayMode;

								if (! restoredMode)
									break;

								restoredModeListHead = restoredModeListHead->nextDisplayMode;
								++restoredModeIndex;
							}

							if (g_savedModeIndex > restoredModeIndex)
							{
								restoredModeListHead = g_ddAppSelectedDevice->displayModeListHead;
								restoredModeIndex = 0;
								g_ddAppSelectedDisplayMode = restoredModeListHead;
							}

							g_ddAppSelectedDevice->primaryDisplayMode = restoredModeListHead;
							g_savedModeIndex = restoredModeIndex;
							g_selectionState = SELECTION_STATE_DISPLAY_MODE;
							shouldRedraw = 1;
							break;
						}

						case VK_UP: {
							DDAppDevice::App* currentApp = g_ddAppIterator;
							int32_t deviceIndex = g_savedDriverIndex - 1;
							int32_t appIndex;

							for (appIndex = 0;; ++appIndex)
							{
								g_selectedDDApp = currentApp;

								if (appIndex >= deviceIndex && deviceIndex >= 0)
									break;

								if (! currentApp->chainDDApp)
									break;

								currentApp = currentApp->chainDDApp;
							}

							if (deviceIndex > appIndex)
							{
								currentApp = g_ddAppIterator;
								appIndex = 0;
								g_selectedDDApp = g_ddAppIterator;
							}

							g_savedDriverIndex = appIndex;
							int32_t deviceCount = 0;
							g_ddAppSelectedDevice = currentApp->deviceListHead;
							DDAppDevice* currentDevice = g_ddAppSelectedDevice;

							while (currentDevice && ! currentDevice->isHardwareAccelerated)
							{
								currentDevice = currentDevice->nextDevice;
								++deviceCount;
								g_ddAppSelectedDevice = currentDevice;
							}

							if (! currentDevice)
							{
								g_ddAppSelectedDevice = currentApp->deviceListHead;
								currentApp->primaryDevice = g_ddAppSelectedDevice;
								g_savedDeviceIndex = 0;
							}
							else
							{
								currentApp->primaryDevice = currentDevice;
								g_savedDeviceIndex = deviceCount;
							}

							int32_t modeCount = 0;
							DDAppDevice::DisplayMode* currentMode = g_ddAppSelectedDevice->displayModeListHead;
							g_ddAppSelectedDisplayMode = currentMode;

							for (DDAppDevice::DisplayMode* nextMode = currentMode->nextDisplayMode; nextMode; nextMode = nextMode->nextDisplayMode)
							{
								if (currentMode->surfaceDesc.dwHeight >= 480)
									break;

								currentMode = nextMode;
								++modeCount;
								g_ddAppSelectedDisplayMode = nextMode;
							}

							g_ddAppSelectedDevice->primaryDisplayMode = currentMode;
							g_savedModeIndex = modeCount;
							shouldRedraw = 1;
							break;
						}

						case VK_DOWN: {
							int32_t nextAppIndex = 0;
							int32_t nextDeviceIndex = g_savedDriverIndex + 1;
							DDAppDevice::App* nextApp;

							for (nextApp = g_ddAppIterator;; nextApp = nextApp->chainDDApp)
							{
								g_selectedDDApp = nextApp;

								if (nextAppIndex >= nextDeviceIndex && nextDeviceIndex >= 0)
									break;

								if (! nextApp->chainDDApp)
									break;

								++nextAppIndex;
							}

							if (nextDeviceIndex > nextAppIndex)
							{
								nextApp = g_ddAppIterator;
								nextAppIndex = 0;
								g_selectedDDApp = g_ddAppIterator;
							}

							g_savedDriverIndex = nextAppIndex;
							int32_t nextDeviceCount = 0;
							g_ddAppSelectedDevice = nextApp->deviceListHead;
							DDAppDevice* nextDevice = g_ddAppSelectedDevice;

							while (nextDevice && ! nextDevice->isHardwareAccelerated)
							{
								nextDevice = nextDevice->nextDevice;
								++nextDeviceCount;
								g_ddAppSelectedDevice = nextDevice;
							}

							if (! nextDevice)
							{
								g_ddAppSelectedDevice = nextApp->deviceListHead;
								nextApp->primaryDevice = g_ddAppSelectedDevice;
								g_savedDeviceIndex = 0;
							}
							else
							{
								nextApp->primaryDevice = nextDevice;
								g_savedDeviceIndex = nextDeviceCount;
							}

							int32_t nextModeCount = 0;
							DDAppDevice::DisplayMode* nextDisplayMode = g_ddAppSelectedDevice->displayModeListHead;
							g_ddAppSelectedDisplayMode = nextDisplayMode;

							for (DDAppDevice::DisplayMode* modeIterator = nextDisplayMode->nextDisplayMode; modeIterator;
								modeIterator = modeIterator->nextDisplayMode)
							{
								if (nextDisplayMode->surfaceDesc.dwHeight >= 480)
									break;

								nextDisplayMode = modeIterator;
								++nextModeCount;
								g_ddAppSelectedDisplayMode = modeIterator;
							}

							g_ddAppSelectedDevice->primaryDisplayMode = nextDisplayMode;
							g_savedModeIndex = nextModeCount;
							shouldRedraw = 1;

							break;
						}

						default:
							break;
					}

					break;
				}
				case SELECTION_STATE_RENDER_METHOD: {
					DDAppDevice* fallbackDevice = g_selectedDDApp->deviceListHead;

					if (fallbackDevice->nextDevice)
					{
						switch (g_keyDown)
						{
							case VK_BACK:
								if (g_ddAppIterator->chainDDApp)
								{
									g_selectionState = SELECTION_STATE_DRIVER;
									shouldRedraw = 1;
								}
								break;

							case VK_RETURN:
							case VK_SPACE:
								g_selectionState = SELECTION_STATE_DISPLAY_MODE;
								shouldRedraw = 1;
								break;

							case VK_UP: {
								int32_t fallbackModeIndex = 0;
								int32_t upArrowDeviceIndex = g_savedDeviceIndex - 1;

								DDAppDevice* upArrowDevice;

								while (true)
								{
									upArrowDevice = fallbackDevice;
									g_ddAppSelectedDevice = fallbackDevice;

									if (fallbackModeIndex >= upArrowDeviceIndex && upArrowDeviceIndex >= 0)
										break;

									fallbackDevice = fallbackDevice->nextDevice;

									if (! fallbackDevice)
										break;

									++fallbackModeIndex;
								}

								if (upArrowDeviceIndex > fallbackModeIndex)
								{
									upArrowDevice = g_selectedDDApp->deviceListHead;
									fallbackModeIndex = 0;
									g_ddAppSelectedDevice = upArrowDevice;
								}

								g_selectedDDApp->primaryDevice = upArrowDevice;
								g_savedDeviceIndex = fallbackModeIndex;

								int32_t upArrowModeCount = 0;

								DDAppDevice::DisplayMode* upArrowMode = g_ddAppSelectedDevice->displayModeListHead;
								g_ddAppSelectedDisplayMode = upArrowMode;

								for (DDAppDevice::DisplayMode* upArrowModeIter = upArrowMode->nextDisplayMode; upArrowModeIter;
									upArrowModeIter = upArrowModeIter->nextDisplayMode)
								{
									if (upArrowMode->surfaceDesc.dwHeight >= 480)
										break;

									upArrowMode = upArrowModeIter;
									++upArrowModeCount;
									g_ddAppSelectedDisplayMode = upArrowModeIter;
								}

								g_ddAppSelectedDevice->primaryDisplayMode = upArrowMode;
								g_savedModeIndex = upArrowModeCount;
								shouldRedraw = 1;
								break;
							}

							case VK_DOWN: {
								DDAppDevice* downArrowDevice;

								int32_t downArrowIndex;
								int32_t downArrowDeviceIndex = g_savedDeviceIndex + 1;

								for (downArrowIndex = 0;; ++downArrowIndex)
								{
									downArrowDevice = fallbackDevice;
									g_ddAppSelectedDevice = fallbackDevice;

									if (downArrowIndex >= downArrowDeviceIndex && downArrowDeviceIndex >= 0)
										break;

									fallbackDevice = fallbackDevice->nextDevice;

									if (! fallbackDevice)
										break;
								}

								if (downArrowDeviceIndex > downArrowIndex)
								{
									downArrowDevice = g_selectedDDApp->deviceListHead;
									downArrowIndex = 0;
									g_ddAppSelectedDevice = downArrowDevice;
								}

								g_selectedDDApp->primaryDevice = downArrowDevice;
								g_savedDeviceIndex = downArrowIndex;
								g_savedModeIndex = SelectSuitableDisplayMode();
								shouldRedraw = 1;
								break;
							}

							default:
								break;
						}

						break;
					}

					DDAppDevice* selectedDevice = g_selectedDDApp->deviceListHead;
					int32_t fallbackDeviceIndex = 0;

					DDAppDevice* fallbackDeviceIter;

					for (g_ddAppSelectedDevice = selectedDevice; fallbackDeviceIndex < g_savedDeviceIndex || g_savedDeviceIndex < 0;
						g_ddAppSelectedDevice = fallbackDeviceIter)
					{
						fallbackDeviceIter = selectedDevice->nextDevice;

						if (! fallbackDeviceIter)
							break;

						selectedDevice = selectedDevice->nextDevice;
						++fallbackDeviceIndex;
					}

					if (g_savedDeviceIndex > fallbackDeviceIndex)
					{
						selectedDevice = g_selectedDDApp->deviceListHead;
						fallbackDeviceIndex = 0;
						g_ddAppSelectedDevice = selectedDevice;
					}

					g_selectedDDApp->primaryDevice = selectedDevice;
					g_savedDeviceIndex = fallbackDeviceIndex;

					DDAppDevice::DisplayMode* selectedModeListHead = g_ddAppSelectedDevice->displayModeListHead;
					int32_t restoredModeIndex = 0;

					DDAppDevice::DisplayMode* fallbackMode;

					for (g_ddAppSelectedDisplayMode = selectedModeListHead; restoredModeIndex < g_savedModeIndex || g_savedModeIndex < 0;
						g_ddAppSelectedDisplayMode = fallbackMode)
					{
						fallbackMode = selectedModeListHead->nextDisplayMode;

						if (! fallbackMode)
							break;

						selectedModeListHead = selectedModeListHead->nextDisplayMode;
						++restoredModeIndex;
					}

					if (g_savedModeIndex > restoredModeIndex)
					{
						selectedModeListHead = g_ddAppSelectedDevice->displayModeListHead;
						restoredModeIndex = 0;

						g_ddAppSelectedDisplayMode = selectedModeListHead;
					}

					g_ddAppSelectedDevice->primaryDisplayMode = selectedModeListHead;
					g_savedModeIndex = restoredModeIndex;
					g_selectionState = SELECTION_STATE_DISPLAY_MODE;
					shouldRedraw = 1;
					break;
				}

				case SELECTION_STATE_DISPLAY_MODE: {
					switch (g_keyDown)
					{
						case VK_BACK:
							if (g_selectedDDApp->deviceListHead->nextDevice)
								g_selectionState = SELECTION_STATE_RENDER_METHOD;
							else if (g_ddAppIterator->chainDDApp)
								g_selectionState = SELECTION_STATE_DRIVER;
							else
								break;

							shouldRedraw = 1;
							break;

						case VK_RETURN:
						case VK_SPACE:
							g_selectionState = SELECTION_STATE_EXIT;
							shouldRedraw = 1;
							break;

						case VK_UP:
							g_savedModeIndex = SelectDisplayModeByIndex(g_savedModeIndex - 1);
							shouldRedraw = 1;
							break;

						case VK_DOWN:
							g_savedModeIndex = SelectDisplayModeByIndex(g_savedModeIndex + 1);
							shouldRedraw = 1;
							break;

						default:
							break;
					}
					break;
				}

				case SELECTION_STATE_WINDOW_MODE: {
					switch (g_keyDown)
					{
						case VK_BACK:
							g_selectionState = SELECTION_STATE_DISPLAY_MODE;
							shouldRedraw = 1;
							break;

						case VK_RETURN:
						case VK_SPACE:
							g_selectionState = SELECTION_STATE_EXIT;
							shouldRedraw = 1;
							break;

						case VK_UP:
						case VK_DOWN:
							g_ddAppSelectedDevice->canRenderWindowedOnPrimary = 1 - g_ddAppSelectedDevice->canRenderWindowedOnPrimary;
							shouldRedraw = 1;
							break;

						default:
							break;
					}
					break;
				}

				default:
					break;
			}

			g_keyDown = 0;
			if (shouldRedraw)
				InvalidateRect(g_hWnd, 0, 0);
		}

		if (g_instructionFont)
			DeleteObject(g_instructionFont);

		if (g_headerFont)
			DeleteObject(g_headerFont);

		if (g_mainFont)
			DeleteObject(g_mainFont);

		if (g_backgroundBitmap)
			DeleteObject(g_backgroundBitmap);

		DestroyWindow(g_hWnd);

		g_primaryDDApp = g_selectedDDApp;

		Toy2::g_toyCfgData.driverIndex = g_savedDriverIndex;
		Toy2::g_toyCfgData.deviceIndex = g_savedDeviceIndex;
		Toy2::g_toyCfgData.displayModeIndex = g_savedModeIndex;

		g_ddAppSelectedDevice->canRenderWindowedOnPrimary = 0;
	}

	// FUNCTION: TOY2 0x004334B0 [MATCHED]
	void Show()
	{
		ShowCursor(0);
		Run();
		ShowCursor(1);
	}
}
