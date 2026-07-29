#include "Toy2/Win95.h"
#include "D3DApp/d3dapp.h"
#include "D3DApp/d3dappi.h"
#include "FileUtils.h"
#include "Logger.h"
#include "Toy2/Direct6.h"
#include "Toy2/Toy2.h"
#include "AudioManager/AudioManager.h"
#include <cstdarg>
#include <cstdio>

// GLOBAL: TOY2 0x00882C20
int32_t g_sysParamsInfo;

// GLOBAL: TOY2 0x00534488
WindowData g_windowData;

// $FUNC DEBUG
void AllocateConsole()
{
	AllocConsole();

	FILE* fp;

	// redirect STDOUT
	fp = freopen("CONOUT$", "w", stdout);
	fp = freopen("CONOUT$", "w", stderr);

	// redirect STDIN
	fp = freopen("CONIN$", "r", stdin);

	printf("[Debug Console Allocated!]\n");
}

// FUNCTION: TOY2 0x004316C0 [PROVISIONAL]
int32_t WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, char* cmdLine, int32_t cmdShow)
{
#ifdef APPLY_FIXES
	AllocateConsole();
#endif

	Toy2::g_unused0 = 0;

	memset(&d3dappi, 0, sizeof(d3dappi));

	g_no32bitColors = 1;

	FileUtils::ValidateInstall();

	Toy2::g_levelFileIndex = 1;

	memset(&g_windowData, 0, sizeof(g_windowData));

	g_windowData.hInstance = hInstance;
	g_windowData.hPrev = hPrev;
	g_windowData.lpCmdLine = cmdLine;
	g_windowData.nShowCmd = cmdShow;

	g_windowData.unkInt8 = 0;
	g_windowData.unkInt3 = 1;
	g_windowData.unkInt4 = 1;
	g_windowData.unkInt5 = 1;
	g_windowData.wndIsExiting = 0;

	Toy2::OneInit();

	Toy2::g_returnedToTitle = 0;
	Toy2::g_attractModeTimer = -1;

	Toy2::g_unused1 = 2;
	Toy2::g_unused2 = 0;

	Toy2::ReadCfg();
	ExamineMachine();
	BuildWindow();
	Toy2::ShowModeSelect();

	switch (g_renderMode)
	{
		case RENDERMODE_SOFTWARE:
			Toy2::InitSoftwareRenderer();
			break;
		case RENDERMODE_D3D:
			Toy2::InitDirect3DRenderer();
			break;
	}

	int32_t tokenCount = 0;
	char* currentToken;
	char* tokenEntries[8];

	currentToken = strtok(cmdLine, " ");

	if (currentToken)
	{
		tokenCount = 1;
		char* nextToken = strtok(0, " ");

		if (nextToken)
		{
			char** tokenArrayPtr = tokenEntries;

			do
			{
				*tokenArrayPtr = nextToken;
				++tokenCount;
				++tokenArrayPtr;

				nextToken = strtok(0, " ");
			} while (nextToken);
		}
	}

	Toy2::Run(tokenCount, &currentToken);

	g_windowData.wndIsExiting = 1;

	Toy2::CheckForQuit();

	return 0;
}

// FUNCTION: TOY2 0x004318F0 [MATCHED]
void LogErrorNotSet()
{
	char* error = D3DAppLastErrorString();
	Logger::LogLn(error);
}

// FUNCTION: TOY2 0x004A6B30 [PROVISIONAL]
int32_t BuildWindow()
{
	memset(&g_windowData.wndClass, 0, sizeof(g_windowData.wndClass));

	// GLOBAL: TOY2 0x00882E30
	static int32_t g_unused1;
	// GLOBAL: TOY2 0x00882C28
	static int32_t g_unused2;
	// GLOBAL: TOY2 0x00504E58
	static int32_t g_windowCreationError = 1;

	g_unused1 = 0;
	g_unused2 = 0;

	g_windowData.wndClass.cbSize = 48;

	CoInitialize(0);

	g_windowData.wndClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
	g_windowData.wndClass.lpfnWndProc = NormalWndProc;
	g_windowData.wndClass.cbClsExtra = 0;
	g_windowData.wndClass.cbWndExtra = 0;
	g_windowData.wndClass.hInstance = g_windowData.hInstance;
	g_windowData.wndClass.hIcon = LoadIconA(g_windowData.hInstance, "AppIcon");
	g_windowData.wndClass.lpszMenuName = "AppMenu";
	g_windowData.wndClass.lpszClassName = "Toy2";
	g_windowData.wndClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);

	if (RegisterClassExA(&g_windowData.wndClass))
	{
		HWND window = CreateWindowExA(WS_EX_TOPMOST, "Toy2", "Toy2", WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, 0, 0, g_windowData.hInstance, 0);

		g_windowData.mainHwnd = window;
		d3dappi.hwnd = window;

		if (window)
		{
			UpdateWindow(window);
			g_windowData.hAccTable = LoadAcceleratorsA(g_windowData.hInstance, "AppAccel");

			if (SystemParametersInfoA(SPI_SETSCREENSAVERRUNNING, 1u, &g_sysParamsInfo, 0))
				atexit(SysParmsOnExit);

			g_windowCreationError = 0;

			return 1;
		}
		else
		{
			Logger::LogLn("CreateWindowEx failed");
			return 0;
		}
	}
	else
	{
		int32_t lastError = GetLastError();

		char* buffer = NULL;
		FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			0,
			lastError,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			buffer,
			0,
			0);
		MessageBoxA(0, buffer, "GetLastError", MB_ICONINFORMATION);
		LocalFree(buffer);
		return 0;
	}
}

// FUNCTION: TOY2 0x004A6CC0 [MATCHED]
int32_t ProcessWndEvents()
{
	if (PeekMessageA(&g_windowData.wndEventMsg, 0, 0, 0, 1))
	{
		if (g_windowData.wndEventMsg.message == WM_QUIT)
		{
			g_windowData.wndIsExiting = 1;
			return 0;
		}

		if (g_windowData.wndEventMsg.message != WM_ACTIVATEAPP
			&& (! g_windowData.mainHwnd || ! TranslateAcceleratorA(g_windowData.mainHwnd, g_windowData.hAccTable, &g_windowData.wndEventMsg)))
		{
			TranslateMessage(&g_windowData.wndEventMsg);
			DispatchMessageA(&g_windowData.wndEventMsg);
		}
	}

	return 1;
}

// FUNCTION: TOY2 0x004A6D30 [MATCHED]
int32_t PostQuitMessage()
{
	::PostQuitMessage(0);
	return 1;
}

// FUNCTION: TOY2 0x004A6D40 [PROVISIONAL]
LRESULT WINAPI NormalWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WPARAM copyWParam = wParam;
	LPARAM copyLParam = lParam;

	if (D3DAppWindowProc(&wParam, &lParam, hWnd, msg, wParam, lParam))
	{
		if (msg == WM_DESTROY || msg == WM_NCDESTROY)
			g_windowData.wndIsExiting = 1;

		return DefWindowProcA(hWnd, msg, copyWParam, copyLParam);
	}
	else
	{
		LogErrorNotSet();
		return 0;
	}
}

// FUNCTION: TOY2 0x004A6B10 [MATCHED]
void SysParmsOnExit() { SystemParametersInfoA(SPI_SETSCREENSAVERRUNNING, g_sysParamsInfo, &g_sysParamsInfo, 0); }

namespace Logger
{
	// FUNCTION: TOY2 0x004A66A0 [MODIFIED] [PROVISIONAL]
	void Log(char* format, ...)
	{
		char buffer[1024];

		va_list argList;
		va_start(argList, format);

		memset(buffer, 0, sizeof(buffer));
		vsprintf(buffer, format, argList);

		printf("%s", buffer); // Addition

		if (g_logsEnabled)
		{
			if (g_logFileExists)
			{
				g_logFileExists = 0;
				remove("toy2.log");
			}

			FILE* file = fopen("toy2.log", "at");

			if (file)
			{
				fprintf(file, buffer);
				fclose(file);
			}
		}
	}

	// FUNCTION: TOY2 0x004A6730 [MATCHED]
	void LogLn(char* format, ...)
	{
		char buffer[1024];

		va_list argList;
		va_start(argList, format);

		memset(buffer, 0, sizeof(buffer));
		vsprintf(buffer, format, argList);
		lstrcatA(buffer, "\r\n");

		Log(buffer);
	}
}
namespace FileUtils
{
	// GLOBAL: TOY2 0x00882F40
	char g_pathRegValue[512];

	// GLOBAL: TOY2 0x00883144
	char g_cdPathRegValue[512];

	// GLOBAL: TOY2 0x00882F3C
	int32_t g_registryKeysRead;

	// GLOBAL: TOY2 0x00882E34
	char g_fileNameBuffer[260];

	// FUNCTION: TOY2 0x004A67D0 [MATCHED]
	int32_t GetFileSize(const char* fileName)
	{
		if (! g_registryKeysRead)
			ValidateInstall();

		strcpy(g_fileNameBuffer, g_pathRegValue);
		strcat(g_fileNameBuffer, fileName);

		Logger::Log("LOAD : Getting length of file %s.\n", g_fileNameBuffer);

		FILE* fd = fopen(g_fileNameBuffer, "rb");

		if (fd)
		{
			fseek(fd, 0, 2);
			int32_t fileSize = ftell(fd);
			fclose(fd);

			Logger::Log("LOAD : Length found - file %s is %d bytes long.\n", g_fileNameBuffer, fileSize);
			return fileSize;
		}
		else
		{
			Logger::Log("LOAD : File %s not found.\n", g_fileNameBuffer);
			return 0;
		}
	}

	// FUNCTION: TOY2 0x004A65E0 [MATCHED]
	void AppendCDPath(char* path)
	{
		if (! g_registryKeysRead)
			ValidateInstall();

		strcpy(path, g_cdPathRegValue);
	}

	// FUNCTION: TOY2 0x004A6790 [MATCHED]
	void AppendRegPathToBuffer()
	{
		if (! g_registryKeysRead)
			ValidateInstall();

		strcpy(g_fileNameBuffer, g_pathRegValue);
	}

	// FUNCTION: TOY2 0x004A65A0 [MATCHED]
	void GetPathValue(char* pathOut)
	{
		if (! g_registryKeysRead)
			ValidateInstall();

		strcpy(pathOut, g_pathRegValue);
	}

	// FUNCTION: TOY2 0x004A6940 [PROVISIONAL]
	size_t LoadFile(const char* fileName, void* buffer)
	{
		if (! g_registryKeysRead)
			ValidateInstall();

		strcpy(g_fileNameBuffer, g_pathRegValue);
		strcat(g_fileNameBuffer, fileName);

		Logger::Log("LOAD : Loading file %s.\n", g_fileNameBuffer);
		AudioManager::StopAndFlush();

		FILE* fin = fopen(g_fileNameBuffer, "rb");

		if (! fin)
			return 0;

		if (fseek(fin, 0, 2))
		{
			Logger::Log("FATAL_ERROR - %s failed - error was %s.\n", "fseek(fin,0,2)", strerror(errno));
			exit(1);
		}

		size_t elemSize = ftell(fin);
		if (fseek(fin, 0, 0))
		{
			Logger::Log("FATAL_ERROR - %s failed - error was %s.\n", "fseek(fin,0,0)", strerror(errno));
			exit(1);
		}

		fread(buffer, elemSize, 1, fin);
		if (fclose(fin))
		{
			Logger::Log("FATAL_ERROR - %s failed - error was %s.\n", "fclose(fin)", strerror(errno));
			exit(1);
		}

		return elemSize;
	}

	// FUNCTION: TOY2 0x004A6390 [PROVISIONAL]
	void ValidateInstall()
	{
		HKEY keyHandle;
		HKEY phkResult;
		int32_t allow32B;
		char fileNameBuffer[1024];

		g_cdPathRegValue[0] = '\0';
		g_pathRegValue[0] = '\0';

		if (! RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software", 0, KEY_ALL_ACCESS, &phkResult))
		{
			if (! RegOpenKeyExA(phkResult, "TravellersTalesToyStory2", 0, KEY_ALL_ACCESS, &keyHandle))
			{
				DWORD dataSize = 512;

				if (! RegQueryValueExA(keyHandle, "path", 0, 0, (LPBYTE)g_pathRegValue, &dataSize))
					g_pathRegValue[dataSize] = 0;

				dataSize = 512;

				if (RegQueryValueExA(keyHandle, "cdpath", 0, 0, (LPBYTE)g_cdPathRegValue, &dataSize))
					strcpy(g_cdPathRegValue, g_pathRegValue);
				else
					g_cdPathRegValue[dataSize] = 0;

				dataSize = 4;

				if (! RegQueryValueExA(keyHandle, "allow32bit", 0, 0, (LPBYTE)&allow32B, &dataSize) && allow32B)
					g_no32bitColors = 0;

				RegCloseKey(keyHandle);
			}

			RegCloseKey(phkResult);
		}

		g_registryKeysRead = 1;

		if (! g_pathRegValue[0] || ! g_cdPathRegValue[0])
			Logger::GetErrorHandler("C:\\projects\\toy2\\Win95.cpp", 217)("Toy Story 2 is not correctly installed,\r\nplease re-install.");

		strcpy(fileNameBuffer, g_cdPathRegValue);
		strcat(fileNameBuffer, "validate.tta");

		FILE* validateFile = fopen(fileNameBuffer, "rb");

		if (validateFile)
		{
			fclose(validateFile);
		}
		else
		{
			Logger::GetErrorHandler("C:\\projects\\toy2\\Win95.cpp", 227)(
				"Unable to find file \r\n\"%s\".\r\nPlease ensure your Toy Story 2 "
				"CD\r\nis in the specified drive.",
				fileNameBuffer);
		}
	}
}
