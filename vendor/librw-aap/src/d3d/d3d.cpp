#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define WITH_D3D
#include "../rwbase.h"
#include "../rwerror.h"
#include "../rwlog.h"
#include "../rwplg.h"
#include "../rwpipeline.h"
#include "../rwobjects.h"
#include "../rwengine.h"
#include "rwd3d.h"
#include "rwd3dimpl.h"

#define PLUGIN_ID ID_DRIVER

namespace rw {
namespace d3d {

bool32 isP8supported = 1;	// set to 0 when actual d3d device is used

// stolen from d3d8to9
static uint32
calculateTextureSize(uint32 width, uint32 height, uint32 depth, uint32 format)
{
#define D3DFMT_W11V11U10 65
	switch(format){
	default:
	case D3DFMT_UNKNOWN:
		return 0;
	case D3DFMT_R3G3B2:
	case D3DFMT_A8:
	case D3DFMT_P8:
	case D3DFMT_L8:
	case D3DFMT_A4L4:
		return width * height * depth;
	case D3DFMT_R5G6B5:
	case D3DFMT_X1R5G5B5:
	case D3DFMT_A1R5G5B5:
	case D3DFMT_A4R4G4B4:
	case D3DFMT_A8R3G3B2:
	case D3DFMT_X4R4G4B4:
	case D3DFMT_A8P8:
	case D3DFMT_A8L8:
	case D3DFMT_V8U8:
	case D3DFMT_L6V5U5:
	case D3DFMT_D16_LOCKABLE:
	case D3DFMT_D15S1:
	case D3DFMT_D16:
	case D3DFMT_UYVY:
	case D3DFMT_YUY2:
		return width * 2 * height * depth;
	case D3DFMT_R8G8B8:
		return width * 3 * height * depth;
	case D3DFMT_A8R8G8B8:
	case D3DFMT_X8R8G8B8:
	case D3DFMT_A2B10G10R10:
	case D3DFMT_A8B8G8R8:
	case D3DFMT_X8B8G8R8:
	case D3DFMT_G16R16:
	case D3DFMT_X8L8V8U8:
	case D3DFMT_Q8W8V8U8:
	case D3DFMT_V16U16:
	case D3DFMT_W11V11U10:
	case D3DFMT_A2W10V10U10:
	case D3DFMT_D32:
	case D3DFMT_D24S8:
	case D3DFMT_D24X8:
	case D3DFMT_D24X4S4:
		return width * 4 * height * depth;
	case D3DFMT_DXT1:
		assert(depth <= 1);
		return ((width + 3) >> 2) * ((height + 3) >> 2) * 8;
	case D3DFMT_DXT2:
	case D3DFMT_DXT3:
	case D3DFMT_DXT4:
	case D3DFMT_DXT5:
		assert(depth <= 1);
		return ((width + 3) >> 2) * ((height + 3) >> 2) * 16;
	}
}

int vertFormatMap[] = {
	-1, VERT_FLOAT2, VERT_FLOAT3, VERT_FLOAT4, VERT_ARGB, VERT_RGBA /* blend indices */
};

void*
createIndexBuffer(uint32 length, bool dynamic)
{
#ifdef RW_D3D9
	// Explicit null-init — D3D9 drivers may leave the out parameter
	// untouched on failure, and an uninitialised stack pointer (MSVC
	// /RTC fills 0xCC) would otherwise propagate back as a valid-looking
	// COM handle. Check the HRESULT, not just the pointer.
	IDirect3DIndexBuffer9 *ibuf = nullptr;
	HRESULT hr;
	if(dynamic)
		hr = d3ddevice->CreateIndexBuffer(length, D3DUSAGE_WRITEONLY|D3DUSAGE_DYNAMIC, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &ibuf, 0);
	else
		hr = d3ddevice->CreateIndexBuffer(length, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ibuf, 0);
	if(FAILED(hr) || ibuf == nullptr)
		return nullptr;
	d3d9Globals.numIndexBuffers++;
	return ibuf;
#else
	return rwNewT(uint8, length, MEMDUR_EVENT | ID_DRIVER);
#endif
}

void
destroyIndexBuffer(void *indexBuffer)
{
#ifdef RW_D3D9
	if(indexBuffer){
		if(((IUnknown*)indexBuffer)->Release() != 0)
			printf("indexBuffer wasn't destroyed\n");
		d3d9Globals.numIndexBuffers--;
	}
#else
	rwFree(indexBuffer);
#endif
}

uint16*
lockIndices(void *indexBuffer, uint32 offset, uint32 size, uint32 flags)
{
	if(indexBuffer == nil)
		return nil;
#ifdef RW_D3D9
	// Null-init the out pointer + check HRESULT — Lock can fail on
	// a lost device or invalid range. Return nullptr so callers can
	// notice and skip, instead of writing into stack-uninit pattern.
	uint16 *indices = nullptr;
	IDirect3DIndexBuffer9 *ibuf = (IDirect3DIndexBuffer9*)indexBuffer;
	HRESULT hr = ibuf->Lock(offset, size, (void**)&indices, flags);
	if(FAILED(hr) || indices == nullptr)
		return nullptr;
	return indices;
#else
	(void)offset;
	(void)size;
	(void)flags;
	return (uint16*)indexBuffer;
#endif
}

void
unlockIndices(void *indexBuffer)
{
	if(indexBuffer == nil)
		return;
#ifdef RW_D3D9
	IDirect3DIndexBuffer9 *ibuf = (IDirect3DIndexBuffer9*)indexBuffer;
	ibuf->Unlock();
#endif
}

void*
createVertexBuffer(uint32 length, uint32 fvf, bool dynamic)
{
#ifdef RW_D3D9
	// Same null-init + HRESULT pattern as createIndexBuffer above.
	IDirect3DVertexBuffer9 *vbuf = nullptr;
	HRESULT hr;
	if(dynamic)
		hr = d3ddevice->CreateVertexBuffer(length, D3DUSAGE_WRITEONLY|D3DUSAGE_DYNAMIC, fvf, D3DPOOL_DEFAULT, &vbuf, 0);
	else
		hr = d3ddevice->CreateVertexBuffer(length, D3DUSAGE_WRITEONLY, fvf, D3DPOOL_MANAGED, &vbuf, 0);
	if(FAILED(hr) || vbuf == nullptr)
		return nullptr;
	d3d9Globals.numVertexBuffers++;
	return vbuf;
#else
	(void)fvf;
	return rwNewT(uint8, length, MEMDUR_EVENT | ID_DRIVER);
#endif
}

void
destroyVertexBuffer(void *vertexBuffer)
{
#ifdef RW_D3D9
	if(vertexBuffer){
		if(((IUnknown*)vertexBuffer)->Release() != 0)
			printf("vertexBuffer wasn't destroyed\n");
		d3d9Globals.numVertexBuffers--;
	}
#else
	rwFree(vertexBuffer);
#endif
}

uint8*
lockVertices(void *vertexBuffer, uint32 offset, uint32 size, uint32 flags)
{
	if(vertexBuffer == nil)
		return nil;
#ifdef RW_D3D9
	uint8 *verts = nullptr;
	IDirect3DVertexBuffer9 *vertbuf = (IDirect3DVertexBuffer9*)vertexBuffer;
	HRESULT hr = vertbuf->Lock(offset, size, (void**)&verts, flags);
	if(FAILED(hr) || verts == nullptr)
		return nullptr;
	return verts;
#else
	(void)offset;
	(void)size;
	(void)flags;
	return (uint8*)vertexBuffer;
#endif
}

void
unlockVertices(void *vertexBuffer)
{
	if(vertexBuffer == nil)
		return;
#ifdef RW_D3D9
	IDirect3DVertexBuffer9 *vertbuf = (IDirect3DVertexBuffer9*)vertexBuffer;
	vertbuf->Unlock();
#endif
}

void*
createTexture(int32 width, int32 height, int32 numlevels, uint32 usage, uint32 format)
{
#ifdef RW_D3D9
	// === Root-cause fix for "0xCCCCCCCC in natras->texture" crash ===
	//
	// MSVC debug builds with /RTC1 fill uninitialised stack locals with
	// 0xCC. The previous code had `IDirect3DTexture9 *tex;` (no init).
	// When CreateTexture fails on certain drivers (out of VRAM, format
	// not supported as MANAGED, lost device, etc) it returns FAILED(hr)
	// WITHOUT touching the out parameter — leaving `tex` as 0xCCCCCCCC.
	// `if(tex)` then evaluated 0xCC as truthy, the bogus pointer
	// propagated to natras->texture, and later `tex->GetLevelCount()`
	// inside readNativeTexture would access-violate at the bogus addr.
	//
	// Fix: zero-init `tex`, check HRESULT, return nullptr on failure.
	IDirect3DTexture9 *tex = nullptr;
	HRESULT hr = d3ddevice->CreateTexture(width, height, numlevels, usage,
	                                      (D3DFORMAT)format, D3DPOOL_MANAGED, &tex, nil);
	if(FAILED(hr) || tex == nullptr){
		rwLogf(RW_LOG_ERROR,
		    "createTexture failed: %dx%d levels=%d fmt=%u usage=%u hr=0x%08lX",
		    width, height, numlevels, format, usage, (unsigned long)hr);
		return nullptr;
	}
	d3d9Globals.numTextures++;
	return tex;
#else
	int32 w = width;
	int32 h = height;
	int32 size = 0;
	for(int32 i = 0; i < numlevels; i++){
		size += calculateTextureSize(w, h, 1, format);
		w /= 2;
		if(w == 0) w = 1;
		h /= 2;
		if(h == 0) h = 1;
	}
	uint8 *data = (uint8*)rwNew(sizeof(RasterLevels)+sizeof(RasterLevels::Level)*(numlevels-1)+size,
		MEMDUR_EVENT | ID_DRIVER);
	RasterLevels *levels = (RasterLevels*)data;
	data += sizeof(RasterLevels)+sizeof(RasterLevels::Level)*(numlevels-1);
	levels->numlevels = numlevels;
	levels->format = format;
	w = width;
	h = height;
	for(int32 i = 0; i < numlevels; i++){
		levels->levels[i].width = w;
		levels->levels[i].height = h;
		levels->levels[i].data = data;
		levels->levels[i].size = calculateTextureSize(w, h, 1, format);
		data += levels->levels[i].size;
		w /= 2;
		if(w == 0) w = 1;
		h /= 2;
		if(h == 0) h = 1;
	}
	return levels;
#endif
}

void
destroyTexture(void *texture)
{
#ifdef RW_D3D9
	if(texture){
		if(((IUnknown*)texture)->Release() != 0)
			printf("texture wasn't destroyed\n");
		d3d9Globals.numTextures--;
	}
#else
	rwFree(texture);
#endif
}

// Native Raster

int32 nativeRasterOffset;

struct RasterFormatInfo
{
	uint32 d3dformat;
	int32 depth;
	bool32 hasAlpha;
	uint32 rwFormat;
};

// indexed directly by RW format (high byte of Raster::Format)
static RasterFormatInfo formatInfoRW[16] = {
	{ 0, 0, 0, 0},
	{ D3DFMT_A1R5G5B5, 16, 1, Raster::C1555 },
	{ D3DFMT_R5G6B5,   16, 0, Raster::C565 },
	{ D3DFMT_A4R4G4B4, 16, 1, Raster::C4444 },
	{ D3DFMT_L8,        8, 0, Raster::LUM8 },
	{ D3DFMT_A8R8G8B8, 32, 1, Raster::C8888 },
	{ D3DFMT_X8R8G8B8, 32, 0, Raster::C888 },
	{ D3DFMT_D16,      16, 0, Raster::D16 },
	{ D3DFMT_D24X8,    32, 0, Raster::D24 },
	{ D3DFMT_D32,      32, 0, Raster::D32 },
	{ D3DFMT_X1R5G5B5, 16, 0, Raster::C555 },
	// 0x0B00 = Raster::F16_RGBA — RGBA16F float render target for HDR / G-buffer.
	{ D3DFMT_A16B16G16R16F, 64, 1, Raster::F16_RGBA },
	// 0x0C00 = Raster::F32_RGBA — RGBA32F for EVSM / high-precision
	// probe data. depth=128 bits/pixel, has alpha. The slot index in
	// this table is `(format >> 8) & 0xF` = 0xC, matches the enum.
	{ D3DFMT_A32B32G32R32F, 128, 1, Raster::F32_RGBA },
};

static RasterFormatInfo formatInfoFull[] = {
	{ D3DFMT_R8G8B8, 0, 24, 0 },
	{ D3DFMT_A8R8G8B8, 1, 32, Raster::C8888 },
	{ D3DFMT_X8R8G8B8, 0, 32, Raster::C888 },
	{ D3DFMT_R5G6B5, 0, 16, Raster::C565 },
	{ D3DFMT_X1R5G5B5, 0, 16, Raster::C555 },
	{ D3DFMT_A1R5G5B5, 1, 16, Raster::C1555 },
	{ D3DFMT_A4R4G4B4, 1, 16, Raster::C4444 },
	{ D3DFMT_R3G3B2, 0, 8, 0 },
	{ D3DFMT_A8, 1, 8, 0 },
	{ D3DFMT_A8R3G3B2, 1, 16, 0 },
	{ D3DFMT_X4R4G4B4, 0, 16, 0 },
	{ D3DFMT_A2B10G10R10, 1, 32, 0 },
	{ D3DFMT_A8B8G8R8, 1, 32, 0 },
	{ D3DFMT_X8B8G8R8, 0, 32, 0 },
	{ D3DFMT_G16R16, 0, 32, 0 },
	{ D3DFMT_A2R10G10B10, 1, 32, 0 },
	{ D3DFMT_A16B16G16R16, 1, 64, 0 },
	{ D3DFMT_A8P8, 1, 16, 0 },
//	{ D3DFMT_P8, 0, 8, ... },
//	{ D3DFMT_L8, 0, 8, ... },
	{ D3DFMT_A8L8, 1, 16, 0 },
	{ D3DFMT_A4L4, 1, 8, 0 },
	{ D3DFMT_V8U8, 0, 16, 0 },
	{ D3DFMT_L6V5U5, 0, 16, 0 },
	{ D3DFMT_X8L8V8U8, 0, 32, 0 },
	{ D3DFMT_Q8W8V8U8, 0, 32, 0 },
	{ D3DFMT_V16U16, 0, 32, 0 },
	{ D3DFMT_A2W10V10U10, 1, 32, 0 },
	{ D3DFMT_D16_LOCKABLE, 0, 16, Raster::D16 },
	{ D3DFMT_D32, 0, 32, Raster::D32 },
	{ D3DFMT_D15S1, 0, 16, Raster::D16 },
	{ D3DFMT_D24S8, 0, 32, Raster::D32 },
	{ D3DFMT_D24X8, 0, 32, Raster::D32 },
	{ D3DFMT_D24X4S4, 0, 32, Raster::D32 },
	{ D3DFMT_D16, 0, 16, Raster::D16 },
	{ D3DFMT_D32F_LOCKABLE, 0, 32, Raster::D32 },
	{ D3DFMT_D24FS8, 0, 32, Raster::D32 },
	{ D3DFMT_L16, 0, 16, 0 },
	{ D3DFMT_Q16W16V16U16, 0, 64, 0 },
	{ D3DFMT_R16F, 0, 16, 0 },
	{ D3DFMT_G16R16F, 0, 32, 0 },
	{ D3DFMT_A16B16G16R16F, 1, 64, 0 },
	{ D3DFMT_R32F, 0, 32, 0 },
	{ D3DFMT_G32R32F, 0, 64, 0 },
	{ D3DFMT_A32B32G32R32F, 1, 128, 0 },
	{ D3DFMT_CxV8U8, 0, 16, 0 },
};

RasterFormatInfo*
findFormatInfoD3D(uint32 d3dformat)
{
	static RasterFormatInfo fake = { 0, 0, 0, 0 };
	int i;
	for(i = 0; i < (int)nelem(formatInfoFull); i++)
		if(formatInfoFull[i].d3dformat == d3dformat)
			return &formatInfoFull[i];
	return &fake;
}


static void
rasterSetFormat(Raster *raster)
{
	if(raster->format == 0){
		// have to find a format first
		// this could perhaps be a bit more intelligent

		switch(raster->type){
		case Raster::NORMAL:
		case Raster::TEXTURE:
			raster->format = Raster::C8888;
			break;

#ifdef RW_D3D9
		case Raster::ZBUFFER:
			// TODO: allow other formats
			raster->format = findFormatInfoD3D(d3d9Globals.present.AutoDepthStencilFormat)->rwFormat;
			// can this even happen? just do something...
			if(raster->format == 0)
				raster->format = Raster::D32;
			break;

		case Raster::CAMERATEXTURE:
// let's not use this because we apparently don't want alpha
//			raster->format = findFormatInfoD3D(d3d9Globals.present.BackBufferFormat)->rwFormat;
			raster->format = findFormatInfoD3D(d3d9Globals.startMode.mode.Format)->rwFormat;
			// can this even happen? just do something...
			if(raster->format == 0)
				raster->format = Raster::C888;
			break;
		case Raster::CAMERA:
			raster->format = findFormatInfoD3D(d3d9Globals.present.BackBufferFormat)->rwFormat;
			// can this even happen? just do something...
			if(raster->format == 0)
				raster->format = Raster::C8888;
			break;
#endif
		}
	}


	D3dRaster *natras = GETD3DRASTEREXT(raster);
	if(raster->format & (Raster::PAL4 | Raster::PAL8)){
		// TODO: do we even allow PAL4?
		natras->format = D3DFMT_P8;
		raster->depth = 8;
	}else{
		natras->format = formatInfoRW[(raster->format >> 8) & 0xF].d3dformat;
		raster->depth = formatInfoRW[(raster->format >> 8) & 0xF].depth;
	}
	natras->bpp = raster->depth/8;
	natras->hasAlpha = formatInfoRW[(raster->format >> 8) & 0xF].hasAlpha;
	raster->stride = raster->width*natras->bpp;

	natras->autogenMipmap = (raster->format & (Raster::MIPMAP|Raster::AUTOMIPMAP)) == (Raster::MIPMAP|Raster::AUTOMIPMAP);
}

static Raster*
rasterCreateTexture(Raster *raster)
{
	int32 levels;
	D3dRaster *natras = GETD3DRASTEREXT(raster);

	if(natras->format == D3DFMT_P8)
		natras->palette = (uint8*)rwNew(4*256, MEMDUR_EVENT | ID_DRIVER);
	if(natras->autogenMipmap)
		levels = 0;
	else if(raster->format & Raster::MIPMAP)
		levels = Raster::calculateNumLevels(raster->width, raster->height);
	else
		levels = 1;

	assert(natras->texture == nil);
	natras->texture = createTexture(raster->width, raster->height,
	                                levels,
	                                natras->autogenMipmap ? D3DUSAGE_AUTOGENMIPMAP : 0,
	                                natras->format);
	if(natras->texture == nil){
		RWERROR((ERR_NOTEXTURE));
		return nil;
	}
	return raster;
}

#ifdef RW_D3D9

static Raster*
rasterCreateCameraTexture(Raster *raster)
{
	if(raster->format & (Raster::PAL4 | Raster::PAL8)){
		RWERROR((ERR_NOTEXTURE));
		return nil;
	}

	int32 levels;
	D3dRaster *natras = GETD3DRASTEREXT(raster);
	if(natras->autogenMipmap)
		levels = 0;
	else if(raster->format & Raster::MIPMAP)
		levels = Raster::calculateNumLevels(raster->width, raster->height);
	else
		levels = 1;

	// Null-init + HRESULT check — same root-cause discipline as
	// createTexture. CAMERATEXTURE in D3DPOOL_DEFAULT is more failure-
	// prone than MANAGED textures (VRAM pressure, MSAA mismatch, format
	// caps) so this path is the more frequent trigger of the bug.
	IDirect3DTexture9 *tex = nullptr;
	HRESULT hr = d3ddevice->CreateTexture(raster->width, raster->height,
				levels,
				(natras->autogenMipmap ? D3DUSAGE_AUTOGENMIPMAP : 0) | D3DUSAGE_RENDERTARGET,
				(D3DFORMAT)natras->format, D3DPOOL_DEFAULT, &tex, nil);
	if(FAILED(hr) || tex == nullptr){
		rwLogf(RW_LOG_ERROR,
		    "rasterCreateCameraTexture failed: %dx%d levels=%d fmt=%u hr=0x%08lX",
		    raster->width, raster->height, levels, natras->format, (unsigned long)hr);
		RWERROR((ERR_NOTEXTURE));
		natras->texture = nil;	// already nil from createNativeRaster but be explicit
		return nil;
	}
	natras->texture = tex;
	d3d9Globals.numTextures++;
	addVidmemRaster(raster);
	return raster;
}

static Raster*
rasterCreateCamera(Raster *raster)
{
	D3dRaster *natras = GETD3DRASTEREXT(raster);

	natras->autogenMipmap = 0;

	natras->format = d3d9Globals.present.BackBufferFormat;
	raster->depth = findFormatDepth(natras->format);

	natras->texture = nil;	// global default render target
	return raster;
}

static Raster*
rasterCreateZbuffer(Raster *raster)
{
	D3dRaster *natras = GETD3DRASTEREXT(raster);

	natras->autogenMipmap = 0;

	// TODO: allow other formats
	natras->format = d3d9Globals.present.AutoDepthStencilFormat;
	raster->depth = findFormatDepth(natras->format);

	RECT rect;
	GetClientRect(d3d9Globals.window, &rect);
	// This check is done by RW but it's rather strange...
	if(rect.right == raster->width && rect.bottom == raster->height)
		natras->texture = d3d9Globals.defaultDepthSurf;
	else{
		// Null-init + HRESULT. Depth-stencil creation can fail when
		// format isn't supported with current MSAA mode — common with
		// 16F depth requests on older hardware.
		IDirect3DSurface9 *surf = nullptr;
		HRESULT hr = d3ddevice->CreateDepthStencilSurface(raster->width, raster->height, (D3DFORMAT)natras->format,
			d3d9Globals.present.MultiSampleType, d3d9Globals.present.MultiSampleQuality,
			FALSE, &surf, nil);
		if(FAILED(hr) || surf == nullptr){
			rwLogf(RW_LOG_ERROR,
			    "rasterCreateZbuffer failed: %dx%d fmt=%u msaa=%u hr=0x%08lX",
			    raster->width, raster->height, natras->format,
			    d3d9Globals.present.MultiSampleType, (unsigned long)hr);
			RWERROR((ERR_NOTEXTURE));
			natras->texture = nil;
			return nil;
		}
		natras->texture = surf;
	}
	addVidmemRaster(raster);

	return raster;
}
#endif

Raster*
rasterCreate(Raster *raster)
{
	rasterSetFormat(raster);

	Raster *ret = raster;

	if(raster->width == 0 || raster->height == 0){
		raster->flags |= Raster::DONTALLOCATE;
		raster->stride = 0;
		goto ret;
	}
	if(raster->flags & Raster::DONTALLOCATE)
		goto ret;

	switch(raster->type){
	case Raster::NORMAL:
	case Raster::TEXTURE:
		ret = rasterCreateTexture(raster);
		break;

#ifdef RW_D3D9
	case Raster::CAMERATEXTURE:
		ret = rasterCreateCameraTexture(raster);
		break;
	case Raster::ZBUFFER:
		ret = rasterCreateZbuffer(raster);
		break;
	case Raster::CAMERA:
		ret = rasterCreateCamera(raster);
		break;
#endif

	default:
		RWERROR((ERR_INVRASTER));
		return nil;
	}

ret:
	raster->originalWidth = raster->width;
	raster->originalHeight = raster->height;
	raster->originalStride = raster->stride;
	raster->originalPixels = raster->pixels;
	return ret;
}

uint8*
rasterLock(Raster *raster, int32 level, int32 lockMode)
{
	D3dRaster *natras = GETD3DRASTEREXT(raster);

	// check if already locked
	if(raster->privateFlags & (Raster::PRIVATELOCK_READ|Raster::PRIVATELOCK_WRITE))
		return nil;

#ifdef RW_D3D9
	DWORD flags = D3DLOCK_NOSYSLOCK;
	if(lockMode & Raster::LOCKREAD)
		flags |= D3DLOCK_READONLY | D3DLOCK_NO_DIRTY_UPDATE;
	IDirect3DTexture9 *tex = (IDirect3DTexture9*)natras->texture;
	// Defensive: if rasterCreateTexture / allocateDXT / camera-tex
	// creation paths produced a raster with no underlying d3d resource
	// (CreateTexture failed, format unsupported, …), `tex` is nullptr.
	// Return nil — that's the lock() contract for "couldn't lock", and
	// callers (rasterFromImage, readNativeTexture, …) handle it.
	if(tex == nullptr)
		return nil;
	// All out-pointers explicitly null-init so a failed GetSurfaceLevel
	// or CreateOffscreenPlainSurface doesn't propagate stack garbage.
	IDirect3DSurface9 *surf = nullptr;
	IDirect3DSurface9 *rt   = nullptr;
	D3DLOCKED_RECT lr = {};

	switch(raster->type){
	case Raster::NORMAL:
	case Raster::TEXTURE: {
		HRESULT hr = tex->GetSurfaceLevel(level, &surf);
		if(FAILED(hr) || surf == nullptr)
			return nil;
		natras->lockedSurf = surf;
		HRESULT res = surf->LockRect(&lr, 0, flags);
		if(FAILED(res) || lr.pBits == nullptr){
			surf->Release();
			natras->lockedSurf = nullptr;
			return nil;
		}
		break;
		}

	case Raster::CAMERATEXTURE:
	case Raster::CAMERA: {
		if(lockMode & Raster::PRIVATELOCK_WRITE)
			assert(0 && "can't lock framebuffer for writing");
		HRESULT hr;
		if(raster->type == Raster::CAMERA){
			rt = d3d9Globals.defaultRenderTarget;
			if(rt == nullptr) return nil;
		}else{
			hr = tex->GetSurfaceLevel(level, &rt);
			if(FAILED(hr) || rt == nullptr) return nil;
		}
		D3DSURFACE_DESC desc = {};
		if(FAILED(rt->GetDesc(&desc))){
			if(raster->type == Raster::CAMERATEXTURE) rt->Release();
			return nil;
		}
		HRESULT res = d3ddevice->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &surf, nil);
		if(FAILED(res) || surf == nullptr){
			if(raster->type == Raster::CAMERATEXTURE) rt->Release();
			return nil;
		}
		hr = d3ddevice->GetRenderTargetData(rt, surf);
		// rt was AddRef'd by GetSurfaceLevel — release the CAMERATEXTURE one
		if(raster->type == Raster::CAMERATEXTURE) rt->Release();
		if(FAILED(hr)){
			surf->Release();
			return nil;
		}
		natras->lockedSurf = surf;
		res = surf->LockRect(&lr, 0, flags);
		if(FAILED(res) || lr.pBits == nullptr){
			surf->Release();
			natras->lockedSurf = nullptr;
			return nil;
		}
		break;
		}

	default:
		assert(0 && "can't lock this raster type (yet)");
		return nil;
	}

	raster->pixels = (uint8*)lr.pBits;
	raster->width >>= level;
	raster->height >>= level;
	raster->stride = lr.Pitch;
	if(raster->width == 0) raster->width = 1;
	if(raster->height == 0) raster->height = 1;
#else
	RasterLevels *levels = (RasterLevels*)natras->texture;
	raster->pixels = levels->levels[level].data;
	raster->width = levels->levels[level].width;
	raster->height = levels->levels[level].height;
	raster->stride = raster->width*natras->bpp;
#endif
	if(lockMode & Raster::LOCKREAD) raster->privateFlags |= Raster::PRIVATELOCK_READ;
	if(lockMode & Raster::LOCKWRITE) raster->privateFlags |= Raster::PRIVATELOCK_WRITE;

	return raster->pixels;
}

void
rasterUnlock(Raster *raster, int32 level)
{
#if RW_D3D9
	D3dRaster *natras = GETD3DRASTEREXT(raster);
	IDirect3DSurface9 *surf = (IDirect3DSurface9*)natras->lockedSurf;
	surf->UnlockRect();
	surf->Release();
	natras->lockedSurf = nil;
#endif
	raster->width = raster->originalWidth;
	raster->height = raster->originalHeight;
	raster->stride = raster->originalStride;
	raster->pixels = raster->originalPixels;

	raster->privateFlags &= ~(Raster::PRIVATELOCK_READ|Raster::PRIVATELOCK_WRITE);
}

int32
rasterNumLevels(Raster *raster)
{
	D3dRaster *natras = GETD3DRASTEREXT(raster);
#ifdef RW_D3D9
	IDirect3DTexture9 *tex = (IDirect3DTexture9*)natras->texture;
	return tex->GetLevelCount();
#else
	RasterLevels *levels = (RasterLevels*)natras->texture;
	return levels->numlevels;
#endif
}

// Almost the same as ps2 and gl3 function
bool32
imageFindRasterFormat(Image *img, int32 type,
	int32 *pWidth, int32 *pHeight, int32 *pDepth, int32 *pFormat)
{
	int32 width, height, depth, format;

	assert((type&0xF) == Raster::TEXTURE);

//	for(width = 1; width < img->width; width <<= 1);
//	for(height = 1; height < img->height; height <<= 1);
	// Perhaps non-power-of-2 textures are acceptable?
	width = img->width;
	height = img->height;

	depth = img->depth;

	if(depth <= 8 && !isP8supported)
		depth = 32;

	switch(depth){
	case 32:
		if(img->hasAlpha())
			format = Raster::C8888;
		else{
			format = Raster::C888;
			depth = 24;
		}
		break;
	case 24:
		format = Raster::C888;
		break;
	case 16:
		format = Raster::C1555;
		break;
	case 8:
		format = Raster::PAL8 | Raster::C8888;
		break;
	case 4:
		format = Raster::PAL4 | Raster::C8888;
		break;
	default:
		RWERROR((ERR_INVRASTER));
		return 0;
	}

	format |= type;

	*pWidth = width;
	*pHeight = height;
	*pDepth = depth;
	*pFormat = format;

	return 1;
}

bool32
rasterFromImage(Raster *raster, Image *image)
{
	if((raster->type&0xF) != Raster::TEXTURE)
		return 0;

	void (*conv)(uint8 *out, uint8 *in) = nil;

	// Unpalettize image if necessary but don't change original
	Image *truecolimg = nil;
	if(image->depth <= 8 && !isP8supported){
		truecolimg = Image::create(image->width, image->height, image->depth);
		truecolimg->pixels = image->pixels;
		truecolimg->stride = image->stride;
		truecolimg->palette = image->palette;
		truecolimg->unpalettize();
		image = truecolimg;
	}

	D3dRaster *natras = GETD3DRASTEREXT(raster);
	int32 format = raster->format&(Raster::PAL8 | Raster::PAL4 | 0xF00);
	switch(image->depth){
	case 32:
		if(format == Raster::C8888)
			conv = conv_BGRA8888_from_RGBA8888;
		else if(format == Raster::C888)
			conv = conv_BGR888_from_RGB888;
		else
			goto err;
		break;
	case 24:
		if(format == Raster::C8888)
			conv = conv_BGRA8888_from_RGB888;
		else if(format == Raster::C888)
			conv = conv_BGR888_from_RGB888;
		else
			goto err;
		break;
	case 16:
		if(format == Raster::C1555)
			conv = conv_ARGB1555_from_ARGB1555;
		else
			goto err;
		break;
	case 8:
		if(format == (Raster::PAL8 | Raster::C8888))
			conv = conv_8_from_8;
		else
			goto err;
		break;
	case 4:
		if(format == (Raster::PAL4 | Raster::C8888) ||
		   format == (Raster::PAL8 | Raster::C8888))
			conv = conv_8_from_8;
		else
			goto err;
		break;
	default:
	err:
fprintf(stderr, "%d %x\n", image->depth, format); fflush(stdout);
		RWERROR((ERR_INVRASTER));
		return 0;
	}

	uint8 *in, *out;
	int pallength = 0;
	if(raster->format & Raster::PAL4)
		pallength = 16;
	else if(raster->format & Raster::PAL8)
		pallength = 256;
	if(pallength){
		in = image->palette;
		out = (uint8*)natras->palette;
		for(int32 i = 0; i < pallength; i++){
			conv_RGBA8888_from_RGBA8888(out, in);
			in += 4;
			out += 4;
		}
	}

	bool unlock = false;
	if(raster->pixels == nil){
		raster->lock(0, Raster::LOCKWRITE|Raster::LOCKNOFETCH);
		unlock = true;
	}

	uint8 *pixels = raster->pixels;
	assert(pixels);
	uint8 *imgpixels = image->pixels;

	int x, y;
	assert(image->width == raster->width);
	assert(image->height == raster->height);
	for(y = 0; y < image->height; y++){
		uint8 *imgrow = imgpixels;
		uint8 *rasrow = pixels;
		for(x = 0; x < image->width; x++){
			conv(rasrow, imgrow);
			imgrow += image->bpp;
			rasrow += natras->bpp;
		}
		imgpixels += image->stride;
		pixels += raster->stride;
	}
	if(unlock)
		raster->unlock(0);

	if(truecolimg)
		truecolimg->destroy();

	return 1;
}

Image*
rasterToImage(Raster *raster)
{
	int32 depth;
	Image *image;

	bool unlock = false;
	if(raster->pixels == nil){
		raster->lock(0, Raster::LOCKREAD);
		unlock = true;
	}

	D3dRaster *natras = GETD3DRASTEREXT(raster);
	if(natras->customFormat){
		int w = raster->width;
		int h = raster->height;
		// pixels are in the upper right corner
		if(w < 4) w = 4;
		if(h < 4) h = 4;
		image = Image::create(w, h, 32);
		image->allocate();
		uint8 *pix = raster->pixels;
		switch(natras->format){
		case D3DFMT_DXT1:
			image->setPixelsDXT(1, pix);
			if((raster->format & 0xF00) == Raster::C565)
				image->removeMask();
			break;
		case D3DFMT_DXT3:
			image->setPixelsDXT(3, pix);
			break;
		case D3DFMT_DXT5:
			image->setPixelsDXT(5, pix);
			break;
		default:
			image->destroy();
			if(unlock)
				raster->unlock(0);
			return nil;
		}
		// fix it up again
		image->width = raster->width;
		image->height = raster->height;

		if(unlock)
			raster->unlock(0);
		return image;
	}

	void (*conv)(uint8 *out, uint8 *in) = nil;
	switch(raster->format & 0xF00){
	case Raster::C1555:
		depth = 16;
		conv = conv_ARGB1555_from_ARGB1555;
		break;
	case Raster::C8888:
		depth = 32;
		conv = conv_RGBA8888_from_BGRA8888;
		break;
	case Raster::C888:
		depth = 24;
		conv = conv_RGB888_from_BGR888;
		break;
	case Raster::C555:
		depth = 16;
		conv = conv_ARGB1555_from_RGB555;
		break;

	default:
	case Raster::C565:
	case Raster::C4444:
	case Raster::LUM8:
		RWERROR((ERR_INVRASTER));
		return nil;
	}
	int32 pallength = 0;
	if((raster->format & Raster::PAL4) == Raster::PAL4){
		depth = 4;
		pallength = 16;
	}else if((raster->format & Raster::PAL8) == Raster::PAL8){
		depth = 8;
		pallength = 256;
	}

	uint8 *in, *out;
	image = Image::create(raster->width, raster->height, depth);
	image->allocate();

	if(pallength){
		out = image->palette;
		in = (uint8*)natras->palette;
		for(int32 i = 0; i < pallength; i++){
			conv_RGBA8888_from_RGBA8888(out, in);
			in += 4;
			out += 4;
		}
	}

	uint8 *imgpixels = image->pixels;
	uint8 *pixels = raster->pixels;

	int x, y;
	assert(image->width == raster->width);
	assert(image->height == raster->height);
	for(y = 0; y < image->height; y++){
		uint8 *imgrow = imgpixels;
		uint8 *rasrow = pixels;
		for(x = 0; x < image->width; x++){
			conv(imgrow, rasrow);
			imgrow += image->bpp;
			rasrow += natras->bpp;
		}
		imgpixels += image->stride;
		pixels += raster->stride;
	}
	image->compressPalette();

	if(unlock)
		raster->unlock(0);

	return image;
}

int32
getLevelSize(Raster *raster, int32 level)
{
	D3dRaster *ras = GETD3DRASTEREXT(raster);
#ifdef RW_D3D9
	IDirect3DTexture9 *tex = (IDirect3DTexture9*)ras->texture;
	D3DSURFACE_DESC desc;
	tex->GetLevelDesc(level, &desc);
	return calculateTextureSize(desc.Width, desc.Height, 1, desc.Format);
#else
	RasterLevels *levels = (RasterLevels*)ras->texture;
	return levels->levels[level].size;
#endif
}

void
allocateDXT(Raster *raster, int32 dxt, int32 numLevels, bool32 hasAlpha)
{
	static uint32 dxtMap[] = {
		0x31545844,	// DXT1
		0x32545844,	// DXT2
		0x33545844,	// DXT3
		0x34545844,	// DXT4
		0x35545844,	// DXT5
	};
	D3dRaster *ras = GETD3DRASTEREXT(raster);
	ras->format = dxtMap[dxt-1];
	ras->hasAlpha = hasAlpha;
	ras->customFormat = 1;
	if(ras->autogenMipmap)
		numLevels = 0;
	else if(raster->format & Raster::MIPMAP)
		{}
	else
		numLevels = 1;
	ras->texture = createTexture(raster->width, raster->height,
	                             numLevels,
	                             ras->autogenMipmap ? D3DUSAGE_AUTOGENMIPMAP : 0,
	                             ras->format);
	// If CreateTexture failed (out of VRAM, unsupported DXT+AUTOGENMIPMAP
	// combo, …), keep DONTALLOCATE set so subsequent setTexels skips this
	// raster gracefully rather than dereferencing nullptr texture.
	if(ras->texture == nullptr){
		rwLogf(RW_LOG_ERROR,
		    "allocateDXT failed: %dx%d dxt=%d numLevels=%d", raster->width, raster->height, dxt, numLevels);
		raster->flags |= Raster::DONTALLOCATE;
		return;
	}
	raster->flags &= ~Raster::DONTALLOCATE;
}

void
setPalette(Raster *raster, void *palette, int32 size)
{
	D3dRaster *ras = GETD3DRASTEREXT(raster);
	memcpy(ras->palette, palette, 4*size);
}

void
setTexels(Raster *raster, void *texels, int32 level)
{
	uint8 *dst = raster->lock(level, Raster::LOCKWRITE|Raster::LOCKNOFETCH);
	memcpy(dst, texels, getLevelSize(raster, level));
	raster->unlock(level);
}

static void*
createNativeRaster(void *object, int32 offset, int32)
{
	// Zero EVERY field. The previous version left bpp and
	// autogenMipmap uninitialised — those get overwritten later by
	// rasterSetFormat, but any code path that reads them between
	// construct and setFormat would see MSVC /RTC stack-fill (0xCC)
	// or heap-fill (0xCD). Belt-and-suspenders: explicit zero.
	D3dRaster *r = PLUGINOFFSET(D3dRaster, object, offset);
	r->texture       = nil;
	r->palette       = nil;
	r->lockedSurf    = nil;
	r->format        = 0;
	r->bpp           = 0;
	r->hasAlpha      = 0;
	r->customFormat  = 0;
	r->autogenMipmap = 0;
	return object;
}

static void*
destroyNativeRaster(void *object, int32 offset, int32)
{
	Raster *raster = (Raster*)object;
	D3dRaster *natras = PLUGINOFFSET(D3dRaster, raster, offset);
#ifdef RW_D3D9
	removeVidmemRaster(raster);
	evictD3D9Raster(raster);
#endif
	switch(raster->type){
	case Raster::NORMAL:
	case Raster::TEXTURE:
	case Raster::CAMERATEXTURE:
		// destroyTexture is null-safe (early-out on nullptr) but we
		// explicit-nil the handle afterwards so a stray double-destroy
		// can't try to re-Release the same COM object.
		if(natras->texture){
			destroyTexture(natras->texture);
			natras->texture = nil;
		}
		break;

	case Raster::ZBUFFER:
#ifdef RW_D3D9
		if(raster->flags & Raster::DONTALLOCATE)
			break;
		if(natras->texture && natras->texture != d3d9Globals.defaultDepthSurf)
			((IDirect3DSurface9*)natras->texture)->Release();
		natras->texture = nil;
#endif
		break;
	case Raster::CAMERA:
		// Camera rasters don't own their backing surface (it's the
		// device's default render target). Just clear the handle.
		natras->texture = nil;
		break;
	}
	if(natras->palette){
		rwFree(natras->palette);
		natras->palette = nil;
	}
	natras->lockedSurf = nil;
	return object;
}

static void*
copyNativeRaster(void *dst, void *, int32 offset, int32)
{
	// Don't share the source texture/palette — the new Raster will
	// allocate its own through the usual rasterCreate path. Mirror
	// createNativeRaster: explicitly zero every D3dRaster field so the
	// new raster starts in a known-clean state regardless of MSVC
	// debug heap-fill patterns.
	D3dRaster *r = PLUGINOFFSET(D3dRaster, dst, offset);
	r->texture       = nil;
	r->palette       = nil;
	r->lockedSurf    = nil;
	r->format        = 0;
	r->bpp           = 0;
	r->hasAlpha      = 0;
	r->customFormat  = 0;
	r->autogenMipmap = 0;
	return dst;
}

void
registerNativeRaster(void)
{
	nativeRasterOffset = Raster::registerPlugin(sizeof(D3dRaster),
	                                            ID_RASTERD3D9,
	                                            createNativeRaster,
	                                            destroyNativeRaster,
	                                            copyNativeRaster);
}

}
}
