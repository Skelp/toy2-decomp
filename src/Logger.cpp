#include "Logger.h"
#include "D3DApp/d3dapp.h"

#include <cstdio>
#include <cstdarg>

namespace Logger
{
	// GLOBAL: TOY2 0x00B6269C
	const char* g_errorHandlerPath;

	// GLOBAL: TOY2 0x00B626A0
	int32_t g_errorHandlerLine;

	// GLOBAL: TOY2 0x00883348
	int32_t g_showMsgBoxOnThrow;

	// GLOBAL: TOY2 0x00882F38
	int32_t g_logsEnabled;

	// GLOBAL: TOY2 0x00504E54
	int16_t g_logFileExists = 1;

	// GLOBAL: TOY2 0x005066A0
	extern const char g_surfaceLostError[] = {
#include "LoggerSurfaceLostError.inc"
	};

	// GLOBAL: TOY2 0x00506B48
	extern const char g_missingOverlayDestinationError[] = {
#include "LoggerMissingOverlayDestinationError.inc"
	};

	// GLOBAL: TOY2 0x005071F4
	extern const char g_cannotCreateDeviceContextError[] = {
#include "LoggerCannotCreateDeviceContextError.inc"
	};

	// GLOBAL: TOY2 0x00507540
	extern const char g_noZBufferHardwareError[] = {
#include "LoggerNoZBufferHardwareError.inc"
	};
}

namespace Logger
{
	// FUNCTION: TOY2 0x004A87C0 [MATCHED]
	ThrowErrorFunc GetErrorHandler(char* filePath, int32_t lineNumber)
	{
		g_errorHandlerPath = filePath;
		g_errorHandlerLine = lineNumber;

		return ThrowError;
	}

	// FUNCTION: TOY2 0x004A8710 [MATCHED]
	void ThrowError(char* format, ...)
	{
		char caption[256];
		char text[1024];

		va_list argList;
		va_start(argList, format);

		if (*format)
		{
			sprintf(caption, "Soft Abort - %s Line %d", g_errorHandlerPath, g_errorHandlerLine);
			vsprintf(text, format, argList);

			FILE* file = fopen("toy2.err", "wb");

			if (file)
			{
				fprintf(file, "%s\r\n%s\r\n", caption, text);
				fclose(file);
			}

			if (! g_showMsgBoxOnThrow)
				MessageBoxA(0, text, caption, 0);
		}

		exit(-1);
	}

	// FUNCTION: TOY2 0x004ADFD0 [MATCHED]
	void LogD3DError(int32_t errorCode)
	{
		const char* message = D3DErrorToString(errorCode);
		OutputDebugStringA(message);
		OutputDebugStringA("\n");
	}

	// FUNCTION: TOY2 0x00431900 [MATCHED]
	void LogDDError(const char* message, HRESULT error)
	{
		char buffer[2048];

		memset(buffer, 0, sizeof(buffer));
		char* errorToMsg = D3DAppErrorToString(error);
		sprintf(buffer, "ERROR - %s\nERROR - %s\n", message, errorToMsg);

		LogLn(buffer);
	}

	// FUNCTION: TOY2 0x004A8870 [MATCHED]
	void DebugLog(char* format, ...)
	{
		char buffer[1024];
		va_list vaList;

		va_start(vaList, format);
		vsprintf(buffer, format, vaList);
		OutputDebugStringA(buffer);
	}

	// FUNCTION: TOY2 0x004ADFF0 [MATCHED]
	const char* D3DErrorToString(HRESULT errorCode)
	{
		switch (errorCode)
		{
			case DDERR_GENERIC:
				return "There is an undefined error condition.";
			case DDERR_UNSUPPORTED:
				return "The operation is not supported.";
			case DDERR_NOTINITIALIZED:
				return "An attempt was made to call an interface method of a DirectDraw object created by CoCreateInstance before the object was initialized.";
			case DDERR_INVALIDPARAMS:
				return "One or more of the parameters passed to the method are incorrect.";
			case DDERR_OUTOFMEMORY:
				return "DirectDraw does not have enough memory to perform the operation.";
			case DDERR_ALREADYINITIALIZED:
				return "The object has already been initialized.";
			case DDERR_CANNOTATTACHSURFACE:
				return "A surface cannot be attached to another requested surface.";
			case DDERR_CANNOTDETACHSURFACE:
				return "A surface cannot be detached from another requested surface.";
			case DDERR_CURRENTLYNOTAVAIL:
				return "No support is currently available.";
			case DDERR_EXCEPTION:
				return "An exception was encountered while performing the requested operation.";
			case DDERR_HEIGHTALIGN:
				return "The height of the provided rectangle is not a multiple of the required alignment.";
			case DDERR_INCOMPATIBLEPRIMARY:
				return "The primary surface creation request does not match with the existing primary surface.";
			case DDERR_INVALIDCAPS:
				return "One or more of the capability bits passed to the callback function are incorrect.";
			case DDERR_INVALIDCLIPLIST:
				return "DirectDraw does not support the provided clip list.";
			case DDERR_INVALIDMODE:
				return "DirectDraw does not support the requested mode.";
			case DDERR_INVALIDOBJECT:
				return "DirectDraw received a pointer that was an invalid DirectDraw object.";
			case DDERR_INVALIDPIXELFORMAT:
				return "The pixel format was invalid as specified.";
			case DDERR_INVALIDRECT:
				return "The provided rectangle was invalid.";
			case DDERR_LOCKEDSURFACES:
				return "One or more surfaces are locked, causing the failure of the requested operation.";
			case DDERR_NO3D:
				return "No 3-D hardware or emulation is present.";
			case DDERR_NOALPHAHW:
				return "No alpha acceleration hardware is present or available, causing the failure of the requested operation.";
			case DDERR_NOCLIPLIST:
				return "No clip list is available.";
			case DDERR_NOCOLORCONVHW:
				return "The operation cannot be carried out because no color-conversion hardware is present or available.";
			case DDERR_NOCOOPERATIVELEVELSET:
				return "A create function is called without the IDirectDraw4::SetCooperativeLevel method being called.";
			case DDERR_NOCOLORKEY:
				return "The surface does not currently have a color key.";
			case DDERR_NOCOLORKEYHW:
				return "The operation cannot be carried out because there is no hardware support for the destination color key.";
			case DDERR_NODIRECTDRAWSUPPORT:
				return "DirectDraw support is not possible with the current display driver.";
			case DDERR_NOEXCLUSIVEMODE:
				return "The operation requires the application to have exclusive mode, but the application does not have exclusive mode.";
			case DDERR_NOFLIPHW:
				return "Flipping visible surfaces is not supported.";
			case DDERR_NOGDI:
				return "No GDI is present.";
			case DDERR_NOMIRRORHW:
				return "The operation cannot be carried out because no mirroring hardware is present or available.";
			case DDERR_NOOVERLAYHW:
				return "The operation cannot be carried out because no overlay hardware is present or available.";
			case DDERR_NORASTEROPHW:
				return "The operation cannot be carried out because no appropriate raster operation hardware is present or available.";
			case DDERR_NOROTATIONHW:
				return "The operation cannot be carried out because no rotation hardware is present or available.";
			case DDERR_NOSTRETCHHW:
				return "The operation cannot be carried out because there is no hardware support for stretching.";
			case DDERR_NOT4BITCOLOR:
				return "The DirectDrawSurface object is not using a 4-bit color palette and the requested operation requires a 4-bit color palette.";
			case DDERR_NOT4BITCOLORINDEX:
				return "The DirectDrawSurface object is not using a 4-bit color index palette and the requested operation requires a 4-bit color index "
					   "palette.";
			case DDERR_NOT8BITCOLOR:
				return "The DirectDrawSurface object is not using an 8-bit color palette and the requested operation requires an 8-bit color palette.";
			case DDERR_NOTEXTUREHW:
				return "The operation cannot be carried out because no texture-mapping hardware is present or available.";
			case DDERR_NOTFOUND:
				return "The requested item was not found.";
			case DDERR_NOVSYNCHW:
				return "The operation cannot be carried out because there is no hardware support for vertical blank synchronized operations.";
			case DDERR_NOZBUFFERHW:
				return g_noZBufferHardwareError;
			case DDERR_NOZOVERLAYHW:
				return "The overlay surfaces cannot be z-layered based on the z-order because the hardware does not support z-ordering of overlays.";
			case DDERR_OUTOFCAPS:
				return "The hardware needed for the requested operation has already been allocated.";
			case DDERR_OUTOFVIDEOMEMORY:
				return "DirectDraw does not have enough display memory to perform the operation.";
			case DDERR_OVERLAPPINGRECTS:
				return "Operation could not be carried out because the source and destination rectangles are on the same surface and overlap each other.";
			case DDERR_OVERLAYCANTCLIP:
				return "The hardware does not support clipped overlays.";
			case DDERR_BLTFASTCANTCLIP:
				return "A DirectDrawClipper object is attached to a source surface that has passed into a call to the IDirectDrawSurface4::BltFast method.";
			case DDERR_CANTCREATEDC:
				return g_cannotCreateDeviceContextError;
			case DDERR_CANTDUPLICATE:
				return "Primary and 3-D surfaces, or surfaces that are implicitly created, cannot be duplicated.";
			case DDERR_CANTLOCKSURFACE:
				return "Access to this surface is refused because an attempt was made to lock the primary surface without DCI support.";
			case DDERR_CLIPPERISUSINGHWND:
				return "An attempt was made to set a clip list for a DirectDrawClipper object that is already monitoring a window handle.";
			case DDERR_COLORKEYNOTSET:
				return "No source color key is specified for this operation.";
			case DDERR_DIRECTDRAWALREADYCREATED:
				return "A DirectDraw object representing this driver has already been created for this process.";
			case DDERR_EXCLUSIVEMODEALREADYSET:
				return "An attempt was made to set the cooperative level when it was already set to exclusive.";
			case DDERR_HWNDALREADYSET:
				return "The DirectDraw cooperative level window handle has already been set. It cannot be reset while the process has surfaces or palettes "
					   "created.";
			case DDERR_HWNDSUBCLASSED:
				return "DirectDraw is prevented from restoring state because the DirectDraw cooperative level window handle has been subclassed.";
			case DDERR_INVALIDDIRECTDRAWGUID:
				return "The globally unique identifier (GUID) passed to the DirectDrawCreate function is not a valid DirectDraw driver identifier.";
			case DDERR_INVALIDPOSITION:
				return "The position of the overlay on the destination is no longer legal.";
			case DDERR_INVALIDSTREAM:
				return "The specified stream contains invalid data.";
			case DDERR_NOBLTHW:
				return "No blitter hardware is present.";
			case DDERR_NOCLIPPERATTACHED:
				return "No DirectDrawClipper object is attached to the surface object.";
			case DDERR_NODC:
				return "No DC has ever been created for this surface.";
			case DDERR_NODDROPSHW:
				return "No DirectDraw raster operation (ROP) hardware is available.";
			case DDERR_NODIRECTDRAWHW:
				return "Hardware-only DirectDraw object creation is not possible; the driver does not support any hardware.";
			case DDERR_NOEMULATION:
				return "Software emulation is not available.";
			case DDERR_NOHWND:
				return "Clipper notification requires a window handle, or no window handle has been previously set as the cooperative level window handle.";
			case DDERR_NOOVERLAYDEST:
				return g_missingOverlayDestinationError;
			case DDERR_NOPALETTEATTACHED:
				return "No palette object is attached to this surface.";
			case DDERR_NOPALETTEHW:
				return "There is no hardware support for 16- or 256-color palettes.";
			case DDERR_NOTAOVERLAYSURFACE:
				return "An overlay component is called for a non-overlay surface.";
			case DDERR_NOTFLIPPABLE:
				return "An attempt has been made to flip a surface that cannot be flipped.";
			case DDERR_NOTLOCKED:
				return "An attempt is made to unlock a surface that was not locked.";
			case DDERR_OVERLAYCOLORKEYONLYONEACTIVE:
				return "An attempt was made to have more than one color key active on an overlay.";
			case DDERR_OVERLAYNOTVISIBLE:
				return "The IDirectDrawSurface4::GetOverlayPosition method is called on a hidden overlay.";
			case DDERR_PALETTEBUSY:
				return "Access to this palette is refused because the palette is locked by another thread.";
			case DDERR_PRIMARYSURFACEALREADYEXISTS:
				return "This process has already created a primary surface.";
			case DDERR_REGIONTOOSMALL:
				return "The region passed to the IDirectDrawClipper::GetClipList method is too small.";
			case DDERR_SURFACEALREADYATTACHED:
				return "An attempt was made to attach a surface to another surface to which it is already attached.";
			case DDERR_SURFACEALREADYDEPENDENT:
				return "An attempt was made to make a surface a dependency of another surface to which it is already dependent.";
			case DDERR_SURFACEBUSY:
				return "Access to the surface is refused because the surface is locked by another thread.";
			case DDERR_SURFACEISOBSCURED:
				return "Access to the surface is refused because the surface is obscured.";
			case DDERR_SURFACELOST:
				return g_surfaceLostError;
			case DDERR_SURFACENOTATTACHED:
				return "The requested surface is not attached.";
			case DDERR_TOOBIGHEIGHT:
				return "The height requested by DirectDraw is too large.";
			case DDERR_TOOBIGSIZE:
				return "The size requested by DirectDraw is too large. However, the individual height and width are valid sizes.";
			case DDERR_TOOBIGWIDTH:
				return "The width requested by DirectDraw is too large.";
			case DDERR_UNSUPPORTEDFORMAT:
				return "The FourCC format requested is not supported by DirectDraw.";
			case DDERR_UNSUPPORTEDMASK:
				return "The bitmask in the pixel format requested is not supported by DirectDraw.";
			case DDERR_VERTICALBLANKINPROGRESS:
				return "A vertical blank is in progress.";
			case DDERR_WASSTILLDRAWING:
				return "The previous blit operation that is transferring information to or from this surface is incomplete.";
			case DDERR_XALIGN:
				return "The provided rectangle was not horizontally aligned on a required boundary.";
			case DDERR_WRONGMODE:
				return "This surface cannot be restored because it was created in a different mode.";
			case DDERR_IMPLICITLYCREATED:
				return "The surface cannot be restored because it is an implicitly created surface.";
			case DDERR_INVALIDSURFACETYPE:
				return "The requested operation could not be performed because the surface was of the wrong type.";
			case DDERR_NOFOCUSWINDOW:
				return "An attempt was made to create or set a device window without first setting the focus window.";
			case DDERR_NOMIPMAPHW:
				return "The operation cannot be carried out because no mipmap capable texture mapping hardware is present or available.";
			case DDERR_NOOPTIMIZEHW:
				return "The device does not support optimized surfaces.";
			case DDERR_NOTLOADED:
				return "The surface is an optimized surface, but it has not yet been allocated any memory.";
			case DDERR_NOTPALETTIZED:
				return "The surface being used is not a palette-based surface.";
			case DDERR_UNSUPPORTEDMODE:
				return "The display is currently in an unsupported mode.";
			case DDERR_DCALREADYCREATED:
				return "A device context (DC) has already been returned for this surface. Only one DC can be retrieved for each surface.";
			case DDERR_CANTPAGELOCK:
				return "An attempt to page lock a surface failed. Page lock will not work on a display-memory surface or an emulated primary surface.";
			case DDERR_CANTPAGEUNLOCK:
				return "An attempt to page unlock a surface failed. Page unlock will not work on a display-memory surface or an emulated primary surface.";
			case DDERR_DEVICEDOESNTOWNSURFACE:
				return "Surfaces created by one DirectDraw device cannot be used directly by another DirectDraw device.";
			case DDERR_EXPIRED:
				return "The data has expired and is therefore no longer valid.";
			case DDERR_MOREDATA:
				return "There is more data available than the specified buffer size can hold.";
			case DDERR_NONONLOCALVIDMEM:
				return "An attempt was made to allocate non-local video memory from a device that does not support non-local video memory.";
			case DDERR_NOTPAGELOCKED:
				return "An attempt is made to page unlock a surface with no outstanding page locks.";
			case DDERR_VIDEONOTACTIVE:
				return "The video port is not active.";
			case D3DERR_BADMAJORVERSION:
				return "D3DERR_BADMAJORVERSION";
			case D3DERR_BADMINORVERSION:
				return "D3DERR_BADMINORVERSION";
			case D3DERR_DEVICEAGGREGATED:
				return "D3DERR_DEVICEAGGREGATED";
			case D3DERR_EXECUTE_CLIPPED_FAILED:
				return "D3DERR_EXECUTE_CLIPPED_FAILED";
			case D3DERR_EXECUTE_CREATE_FAILED:
				return "D3DERR_EXECUTE_CREATE_FAILED";
			case D3DERR_EXECUTE_DESTROY_FAILED:
				return "D3DERR_EXECUTE_DESTROY_FAILED";
			case D3DERR_EXECUTE_FAILED:
				return "D3DERR_EXECUTE_FAILED";
			case D3DERR_EXECUTE_LOCK_FAILED:
				return "D3DERR_EXECUTE_LOCK_FAILED";
			case D3DERR_EXECUTE_LOCKED:
				return "D3DERR_EXECUTE_LOCKED";
			case D3DERR_EXECUTE_NOT_LOCKED:
				return "D3DERR_EXECUTE_NOT_LOCKED";
			case D3DERR_EXECUTE_UNLOCK_FAILED:
				return "D3DERR_EXECUTE_UNLOCK_FAILED";
			case D3DERR_INITFAILED:
				return "D3DERR_INITFAILED";
			case D3DERR_INVALID_DEVICE:
				return "D3DERR_INVALID_DEVICE";
			case D3DERR_INVALIDCURRENTVIEWPORT:
				return "D3DERR_INVALIDCURRENTVIEWPORT";
			case D3DERR_INVALIDPRIMITIVETYPE:
				return "D3DERR_INVALIDPRIMITIVETYPE";
			case D3DERR_INVALIDRAMPTEXTURE:
				return "D3DERR_INVALIDRAMPTEXTURE";
			case D3DERR_INVALIDVERTEXTYPE:
				return "D3DERR_INVALIDVERTEXTYPE";
			case D3DERR_MATRIX_CREATE_FAILED:
				return "D3DERR_MATRIX_CREATE_FAILED";
			case D3DERR_MATRIX_DESTROY_FAILED:
				return "D3DERR_MATRIX_DESTROY_FAILED";
			case D3DERR_MATRIX_GETDATA_FAILED:
				return "D3DERR_MATRIX_GETDATA_FAILED";
			case D3DERR_MATRIX_SETDATA_FAILED:
				return "D3DERR_MATRIX_SETDATA_FAILED";
			case D3DERR_SETVIEWPORTDATA_FAILED:
				return "D3DERR_SETVIEWPORTDATA_FAILED";
			case D3DERR_TEXTURE_BADSIZE:
				return "D3DERR_TEXTURE_BADSIZE";
			case D3DERR_TEXTURE_CREATE_FAILED:
				return "D3DERR_TEXTURE_CREATE_FAILED";
			case D3DERR_TEXTURE_DESTROY_FAILED:
				return "D3DERR_TEXTURE_DESTROY_FAILED";
			case D3DERR_TEXTURE_GETSURF_FAILED:
				return "D3DERR_TEXTURE_GETSURF_FAILED";
			case D3DERR_TEXTURE_LOAD_FAILED:
				return "D3DERR_TEXTURE_LOAD_FAILED";
			case D3DERR_TEXTURE_LOCK_FAILED:
				return "D3DERR_TEXTURE_LOCK_FAILED";
			case D3DERR_TEXTURE_LOCKED:
				return "D3DERR_TEXTURE_LOCKED";
			case D3DERR_TEXTURE_NO_SUPPORT:
				return "D3DERR_TEXTURE_NO_SUPPORT";
			case D3DERR_TEXTURE_NOT_LOCKED:
				return "D3DERR_TEXTURE_NOT_LOCKED";
			case D3DERR_TEXTURE_SWAP_FAILED:
				return "D3DERR_TEXTURE_SWAP_FAILED";
			case D3DERR_TEXTURE_UNLOCK_FAILED:
				return "D3DERR_TEXTURE_UNLOCK_FAILED";
			case D3DERR_MATERIAL_CREATE_FAILED:
				return "D3DERR_MATERIAL_CREATE_FAILED";
			case D3DERR_INBEGIN:
				return "D3DERR_INBEGIN";
			case D3DERR_INVALIDPALETTE:
				return "D3DERR_INVALIDPALETTE";
			case D3DERR_LIGHT_SET_FAILED:
				return "D3DERR_LIGHT_SET_FAILED";
			case D3DERR_LIGHTHASVIEWPORT:
				return "D3DERR_LIGHTHASVIEWPORT";
			case D3DERR_LIGHTNOTINTHISVIEWPORT:
				return "D3DERR_LIGHTNOTINTHISVIEWPORT";
			case D3DERR_MATERIAL_DESTROY_FAILED:
				return "D3DERR_MATERIAL_DESTROY_FAILED";
			case D3DERR_MATERIAL_GETDATA_FAILED:
				return "D3DERR_MATERIAL_GETDATA_FAILED";
			case D3DERR_MATERIAL_SETDATA_FAILED:
				return "D3DERR_MATERIAL_SETDATA_FAILED";
			case D3DERR_NOCURRENTVIEWPORT:
				return "D3DERR_NOCURRENTVIEWPORT";
			case D3DERR_NOTINBEGIN:
				return "D3DERR_NOTINBEGIN";
			case D3DERR_NOVIEWPORTS:
				return "D3DERR_NOVIEWPORTS";
			case D3DERR_SCENE_BEGIN_FAILED:
				return "D3DERR_SCENE_BEGIN_FAILED";
			case D3DERR_SCENE_END_FAILED:
				return "D3DERR_SCENE_END_FAILED";
			case D3DERR_SCENE_IN_SCENE:
				return "D3DERR_SCENE_IN_SCENE";
			case D3DERR_SCENE_NOT_IN_SCENE:
				return "D3DERR_SCENE_NOT_IN_SCENE";
			case D3DERR_SURFACENOTINVIDMEM:
				return "D3DERR_SURFACENOTINVIDMEM";
			case D3DERR_VIEWPORTDATANOTSET:
				return "D3DERR_VIEWPORTDATANOTSET";
			case D3DERR_VIEWPORTHASNODEVICE:
				return "D3DERR_VIEWPORTHASNODEVICE";
			case D3DERR_ZBUFF_NEEDS_SYSTEMMEMORY:
				return "D3DERR_ZBUFF_NEEDS_SYSTEMMEMORY";
			case D3DERR_ZBUFF_NEEDS_VIDEOMEMORY:
				return "D3DERR_ZBUFF_NEEDS_VIDEOMEMORY";
			case D3DERR_INVALIDVERTEXFORMAT:
				return "D3DERR_INVALIDVERTEXFORMAT";
			case D3DERR_COLORKEYATTACHED:
				return "D3DERR_COLORKEYATTACHED";
			case D3DERR_STENCILBUFFER_NOTPRESENT:
				return "D3DERR_STENCILBUFFER_NOTPRESENT";
			case D3DERR_UNSUPPORTEDALPHAOPERATION:
				return "D3DERR_UNSUPPORTEDALPHAOPERATION";
			case D3DERR_UNSUPPORTEDCOLORARG:
				return "D3DERR_UNSUPPORTEDCOLORARG";
			case D3DERR_UNSUPPORTEDCOLOROPERATION:
				return "D3DERR_UNSUPPORTEDCOLOROPERATION";
			case D3DERR_VBUF_CREATE_FAILED:
				return "D3DERR_VBUF_CREATE_FAILED";
			case D3DERR_VERTEXBUFFERLOCKED:
				return "D3DERR_VERTEXBUFFERLOCKED";
			case D3DERR_VERTEXBUFFEROPTIMIZED:
				return "D3DERR_VERTEXBUFFEROPTIMIZED";
			case D3DERR_WRONGTEXTUREFORMAT:
				return "D3DERR_WRONGTEXTUREFORMAT";
			case D3DERR_ZBUFFER_NOTPRESENT:
				return "D3DERR_ZBUFFER_NOTPRESENT";
			case D3DERR_UNSUPPORTEDALPHAARG:
				return "D3DERR_UNSUPPORTEDALPHAARG";
			case D3DERR_CONFLICTINGTEXTUREFILTER:
				return "D3DERR_CONFLICTINGTEXTUREFILTER";
			case D3DERR_CONFLICTINGTEXTUREPALETTE:
				return "D3DERR_CONFLICTINGTEXTUREPALETTE";
			case D3DERR_CONFLICTINGRENDERSTATE:
				return "D3DERR_CONFLICTINGRENDERSTATE";
			case D3DERR_INVALIDMATRIX:
				return "D3DERR_INVALIDMATRIX";
			case D3DERR_TOOMANYOPERATIONS:
				return "D3DERR_TOOMANYOPERATIONS";
			case D3DERR_TOOMANYPRIMITIVES:
				return "D3DERR_TOOMANYPRIMITIVES";
			case D3DERR_UNSUPPORTEDFACTORVALUE:
				return "D3DERR_UNSUPPORTEDFACTORVALUE";
			case D3DERR_UNSUPPORTEDTEXTUREFILTER:
				return "D3DERR_UNSUPPORTEDTEXTUREFILTER";
			case DD_OK:
				return "The request completed successfully.";
			default:
				return "Unknow value";
		}
	}
}
