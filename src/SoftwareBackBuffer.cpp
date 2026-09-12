#include "SoftwareRenderer.h"
#include "SoftwareRendererInternal.h"
#include "DrawingDevice.h"
#include "Renderer/Renderer.h"
#include "Toy2/Toy2.h"
#include "Toy2/Direct6.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Math.h"
#include "D3DApp/d3dapp.h"
#include "D3DApp/d3dappi.h"
#include "Logger.h"

// The back buffer of the software device: the lock and unlock pair the
// rasterizer bands draw through, the clear paths, and the colour ramp tables
// that turn a five-bit texel channel plus a light level into a pixel channel.
// RenderSoftwareFrame drives one frame through them.
//
// Retail band 0x0047C800-0x0047D650.
namespace SoftwareRenderer
{
	// GLOBAL: TOY2 0x00830C60
	int32_t g_backBufferClearComplete;

	// GLOBAL: TOY2 0x00504D34
	SoftwareRenderItem** g_softwareRenderBuckets = g_softwareRenderBucketStorage;

	// Heap buffer allocated by InitSoftwareRenderer (malloc'd, ~1.25MB) and
	// released by Destroy on shutdown.
	// GLOBAL: TOY2 0x0084CBE0
	void* g_softwareRendererBuffer;

	// GLOBAL: TOY2 0x00882910
	int32_t g_bitsPerPixel;

	// GLOBAL: TOY2 0x00703E20
	uint16_t g_blueRampFull[64];

	// GLOBAL: TOY2 0x00703EA0
	uint16_t g_greenRampFull[128];

	// GLOBAL: TOY2 0x00703FA0
	uint16_t g_redRampFull[64];

	// GLOBAL: TOY2 0x00704028
	uint16_t g_blueRampLow[64];

	// GLOBAL: TOY2 0x007040A8
	uint16_t g_blueRampHigh[64];

	// GLOBAL: TOY2 0x00704128
	uint16_t g_blueRampMedium[64];

	// GLOBAL: TOY2 0x007041A8
	uint16_t g_greenRampLow[128];

	// GLOBAL: TOY2 0x007042A8
	uint16_t g_greenRampHigh[128];

	// GLOBAL: TOY2 0x007043A8
	uint16_t g_greenRampMedium[128];

	// GLOBAL: TOY2 0x007044A8
	uint16_t g_redRampLow[64];

	// GLOBAL: TOY2 0x00704528
	uint16_t g_redRampHigh[64];

	// GLOBAL: TOY2 0x007045A8
	uint16_t g_redRampMedium[64];

	// Back-buffer surface state used by the software frame drain. The surface
	// pointer is the retail `d3dappi.lpBackBuffer` member. The locked pointer is
	// valid only between Lock and Unlock. The pitch is in pixels.
	// GLOBAL: TOY2 0x00534558
	void* g_lockedBackBuffer;

	// GLOBAL: TOY2 0x00882778
	int32_t g_backBufferPitchPixels;

	// Counts deferred frame clears. Initialisation sets it to two, and the first
	// successful clear consumes one count.
	// GLOBAL: TOY2 0x00500C04
	int32_t g_pendingBackBufferClears;

	// FUNCTION: TOY2 0x0047C8B0 [PROVISIONAL]
	void BuildColourRampTables()
	{
		if (g_bitsPerPixel == 16)
		{
			int32_t source = -12;
			int32_t tableIndex = 0;
			do
			{
				int32_t level = source;
				if (source < 0)
					level = 0;
				else if (source > 31)
					level = 31;

				g_redRampFull[tableIndex] = (uint16_t)(level << 11);
				g_redRampLow[tableIndex] = (uint16_t)((level * 10 / 32) << 11);
				g_redRampMedium[tableIndex] = (uint16_t)((level * 17 / 32) << 11);
				g_redRampHigh[tableIndex] = (uint16_t)((level * 25 / 32) << 11);
				++tableIndex;
				++source;
			} while (source + 12 < 64);

			source = -24;
			tableIndex = 0;
			do
			{
				int32_t level = source;
				if (source < 0)
					level = 0;
				else if (source > 63)
					level = 63;

				g_greenRampFull[tableIndex] = (uint16_t)(level << 5);
				g_greenRampLow[tableIndex] = (uint16_t)((level * 18 / 64) << 5);
				g_greenRampMedium[tableIndex] = (uint16_t)((level * 33 / 64) << 5);
				g_greenRampHigh[tableIndex] = (uint16_t)((level * 49 / 64) << 5);
				++tableIndex;
				++source;
			} while (source + 24 < 128);

			source = -12;
			tableIndex = 0;
			do
			{
				int32_t level = source;
				if (source < 0)
					level = 0;
				else if (source > 31)
					level = 31;

				g_blueRampFull[tableIndex] = (uint16_t)level;
				g_blueRampLow[tableIndex] = (uint16_t)(level * 10 / 32);
				g_blueRampMedium[tableIndex] = (uint16_t)(level * 17 / 32);
				g_blueRampHigh[tableIndex] = (uint16_t)(level * 25 / 32);
				++tableIndex;
				++source;
			} while (source + 12 < 64);
		}
		else
		{
			int32_t source = -12;
			int32_t tableIndex = 0;
			do
			{
				int32_t level = source;
				if (source < 0)
					level = 0;
				else if (source > 31)
					level = 31;

				g_redRampFull[tableIndex] = (uint16_t)(level << 10);
				g_redRampLow[tableIndex] = (uint16_t)((level * 10 / 32) << 10);
				g_redRampMedium[tableIndex] = (uint16_t)((level * 17 / 32) << 10);
				g_redRampHigh[tableIndex] = (uint16_t)((level * 25 / 32) << 10);
				++tableIndex;
				++source;
			} while (source + 12 < 64);

			source = -12;
			tableIndex = 0;
			do
			{
				int32_t level = source;
				if (source < 0)
					level = 0;
				else if (source > 31)
					level = 31;

				g_greenRampFull[tableIndex] = (uint16_t)(level << 5);
				g_greenRampLow[tableIndex] = (uint16_t)((level * 10 / 32) << 5);
				g_greenRampMedium[tableIndex] = (uint16_t)((level * 17 / 32) << 5);
				g_greenRampHigh[tableIndex] = (uint16_t)((level * 25 / 32) << 5);
				++tableIndex;
				++source;
			} while (source + 12 < 64);

			source = -12;
			tableIndex = 0;
			do
			{
				int32_t level = source;
				if (source < 0)
					level = 0;
				else if (source > 31)
					level = 31;

				g_blueRampFull[tableIndex] = (uint16_t)level;
				g_blueRampLow[tableIndex] = (uint16_t)(level * 10 / 32);
				g_blueRampMedium[tableIndex] = (uint16_t)(level * 17 / 32);
				g_blueRampHigh[tableIndex] = (uint16_t)(level * 25 / 32);
				++tableIndex;
				++source;
			} while (source + 12 < 64);
		}
	}

	// FUNCTION: TOY2 0x0047D0F0 [MATCHED]
	void Destroy()
	{
		Logger::Log("QUIT : Destroying software renderer.\n");
		if (g_softwareRendererBuffer)
		{
			free(g_softwareRendererBuffer);
		}
	}

	// FUNCTION: TOY2 0x0047D120 [PROVISIONAL]
	void ClearBackBufferOnce()
	{
		if (g_backBufferClearComplete != 0)
			return;

		uint32_t* pixel = static_cast<uint32_t*>(g_lockedBackBuffer);
		if (g_bitsPerPixel == 8)
		{
			int32_t rowPadding = (g_backBufferPitchPixels - Toy2::g_destRectWidth) / 4;
			int32_t rowWidth = Toy2::g_destRectWidth / 4;
			int32_t row = Toy2::g_destRectHeight;
			do
			{
				int32_t count = rowWidth;
				do
					*pixel++ = 0;
				while (--count != 0);

				uint8_t* nextRow = reinterpret_cast<uint8_t*>(pixel) + rowPadding;
				pixel = reinterpret_cast<uint32_t*>(nextRow);
			} while (--row != 0);
		}
		else
		{
			int32_t rowPadding = (g_backBufferPitchPixels - Toy2::g_destRectWidth) / 2;
			int32_t rowWidth = Toy2::g_destRectWidth / 2;
			int32_t row = Toy2::g_destRectHeight;
			do
			{
				int32_t count = rowWidth;
				do
					*pixel++ = 0;
				while (--count != 0);

				uint8_t* nextRow = reinterpret_cast<uint8_t*>(pixel) + rowPadding;
				pixel = reinterpret_cast<uint32_t*>(nextRow);
			} while (--row != 0);
		}

		g_backBufferClearComplete = 1;
		g_pendingBackBufferClears--;
	}

	// FUNCTION: TOY2 0x0047D540 [PROVISIONAL]
	void FillRect32(uint32_t* dest, int32_t width, int32_t height, int32_t rowPaddingBytes, uint32_t value)
	{
		do
		{
			int32_t count = width;
			do
				*dest++ = value;
			while (--count != 0);

			uint8_t* nextRow = reinterpret_cast<uint8_t*>(dest) + rowPaddingBytes;
			dest = reinterpret_cast<uint32_t*>(nextRow);
		} while (--height != 0);
	}

	// FUNCTION: TOY2 0x0047D650 [MATCHED]
	void ClearScanlineFlags(ScanlineScratch* scanline, int32_t count)
	{
		__asm
		{
			push edi
			mov edi, scanline
			mov ecx, count
		clearNext:
			mov byte ptr [edi], 0
			add edi, 04ch
			dec ecx
			jne clearNext
			pop edi
		}
	}

	// FUNCTION: TOY2 0x0047C800 [PROVISIONAL]
	void LockBackBuffer()
	{
		DDSURFACEDESC surfaceDesc;
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);

		HRESULT result;
		do
		{
			result = d3dappi.lpBackBuffer->Lock(NULL, &surfaceDesc, 0, NULL);
		} while (result == DDERR_WASSTILLDRAWING);

		if (result == DD_OK)
		{
			g_lockedBackBuffer = surfaceDesc.lpSurface;
			return;
		}

		g_lockedBackBuffer = NULL;
		Logger::Log("SOFT : ERROR - Failed to lock back buffer - %s.\n", D3DAppErrorToString(result));
	}

	// FUNCTION: TOY2 0x0047C870 [MATCHED]
	void UnlockBackBuffer()
	{
		HRESULT result = d3dappi.lpBackBuffer->Unlock(NULL);
		g_lockedBackBuffer = NULL;
		if (result != DD_OK)
		{
			Logger::Log("SOFT : ERROR - Failed to unlock back buffer - %s.\n", D3DAppErrorToString(result));
		}
	}

	// FUNCTION: TOY2 0x0047D210 [PROVISIONAL]
	void RenderSoftwareFrame(int32_t displayMaxX, int32_t clearValue)
	{
		DDSURFACEDESC surfaceDesc;
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);

		HRESULT result;
		do
		{
			result = d3dappi.lpBackBuffer->Lock(NULL, &surfaceDesc, 0, NULL);
		} while (result == DDERR_WASSTILLDRAWING);

		if (result == DD_OK)
		{
			g_lockedBackBuffer = surfaceDesc.lpSurface;
		}
		else
		{
			g_lockedBackBuffer = NULL;
			Logger::Log("SOFT : ERROR - Failed to lock back buffer - %s.\n", D3DAppErrorToString(result));
		}

		if (Nu3D::Camera::g_cameraTintRed != 0x80 && g_renderMode == RENDERMODE_SOFTWARE && g_bitsPerPixel != 8)
		{
			int32_t rowSkip = (g_backBufferPitchPixels - Toy2::g_destRectWidth) / 2;
			int32_t rowWidth = Toy2::g_destRectWidth / 2;
			uint32_t* pixel = static_cast<uint32_t*>(g_lockedBackBuffer);
			for (int32_t row = Toy2::g_destRectHeight; row != 0; row--)
			{
				for (int32_t count = rowWidth; count != 0; count--)
				{
					*pixel++ = 0;
				}
				uint8_t* nextRow = reinterpret_cast<uint8_t*>(pixel) + rowSkip;
				pixel = reinterpret_cast<uint32_t*>(nextRow);
			}
		}
		else
		{
			if (g_pendingBackBufferClears != 0 && g_backBufferClearComplete == 0)
			{
				uint32_t* pixel = static_cast<uint32_t*>(g_lockedBackBuffer);
				if (g_bitsPerPixel == 8)
				{
					int32_t rowSkip = (g_backBufferPitchPixels - Toy2::g_destRectWidth) / 4;
					int32_t rowWidth = Toy2::g_destRectWidth / 4;
					for (int32_t row = Toy2::g_destRectHeight; row != 0; row--)
					{
						for (int32_t count = rowWidth; count != 0; count--)
						{
							*pixel++ = 0;
						}
						uint8_t* nextRow = reinterpret_cast<uint8_t*>(pixel) + rowSkip;
						pixel = reinterpret_cast<uint32_t*>(nextRow);
					}
				}
				else
				{
					int32_t rowSkip = (g_backBufferPitchPixels - Toy2::g_destRectWidth) / 2;
					int32_t rowWidth = Toy2::g_destRectWidth / 2;
					for (int32_t row = Toy2::g_destRectHeight; row != 0; row--)
					{
						for (int32_t count = rowWidth; count != 0; count--)
						{
							*pixel++ = 0;
						}
						uint8_t* nextRow = reinterpret_cast<uint8_t*>(pixel) + rowSkip;
						pixel = reinterpret_cast<uint32_t*>(nextRow);
					}
				}
				g_backBufferClearComplete = 1;
				g_pendingBackBufferClears--;
			}

			for (int32_t bucket = 4095; bucket >= 0; bucket--)
			{
				SoftwareRenderItem* item = g_softwareRenderBuckets[bucket];
				while (item != NULL)
				{
					uint16_t flags = item->renderFlags;
					SoftwareRenderCallback callback = NULL;
					if (flags & 0x8000)
					{
						callback = g_softwareRenderDispatch->highPriority;
					}
					else if (flags & 0x80)
					{
						g_softwareRenderDispatch->flag80(item);
					}
					else
					{
						int32_t kind = (flags >> 9) & 3;
						if ((flags & SOFTWARE_RENDER_SUBTRACTIVE) && g_softwareRenderDispatch->subtractive != NULL)
						{
							callback = g_softwareRenderDispatch->subtractive;
						}
						else if ((flags & SOFTWARE_RENDER_ADDITIVE) && g_softwareRenderDispatch->additive != NULL)
						{
							callback = g_softwareRenderDispatch->additive;
						}
						else if ((flags & SOFTWARE_RENDER_COLOUR_OFFSET) && g_softwareRenderDispatch->colourOffset[kind] != NULL)
						{
							callback = g_softwareRenderDispatch->colourOffset[kind];
						}
						else
						{
							callback = g_softwareRenderDispatch->defaultCallback[kind];
						}
					}
					if (callback != NULL)
					{
						callback(item);
					}
					item = item->next;
				}
			}
		}

		result = d3dappi.lpBackBuffer->Unlock(NULL);
		g_lockedBackBuffer = NULL;
		if (result != DD_OK)
		{
			Logger::Log("SOFT : ERROR - Failed to unlock back buffer - %s.\n", D3DAppErrorToString(result));
		}
		D3DAppShowBackBuffer();
	}
}
