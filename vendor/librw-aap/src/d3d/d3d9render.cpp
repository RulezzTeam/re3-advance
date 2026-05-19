#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define WITH_D3D
#include "../rwbase.h"
#include "../rwplg.h"
#include "../rwpipeline.h"
#include "../rwobjects.h"
#include "../rwengine.h"
#include "../rwrender.h"
#include "rwd3d.h"
#include "rwd3d9.h"

namespace rw {
namespace d3d9 {
using namespace d3d;

#ifndef RW_D3D9
void defaultRenderCB(Atomic*, InstanceDataHeader*) {}
void defaultRenderCB_Shader(Atomic *atomic, InstanceDataHeader *header) {}
#else

void
drawInst_simple(d3d9::InstanceDataHeader *header, d3d9::InstanceData *inst)
{
	d3d::flushCache();
	d3ddevice->DrawIndexedPrimitive((D3DPRIMITIVETYPE)header->primType, inst->baseIndex,
	                                0, inst->numVertices,
	                                inst->startIndex, inst->numPrimitives);
}

// Emulate PS2 GS alpha test FB_ONLY case: failed alpha writes to frame- but not to depth buffer
void
drawInst_GSemu(d3d9::InstanceDataHeader *header, InstanceData *inst)
{
	uint32 hasAlpha;
	int alphafunc, alpharef, gsalpharef;
	int zwrite;
	d3d::getRenderState(D3DRS_ALPHABLENDENABLE, &hasAlpha);
	if(hasAlpha){
		zwrite = rw::GetRenderState(rw::ZWRITEENABLE);
		alphafunc = rw::GetRenderState(rw::ALPHATESTFUNC);
		if(zwrite){
			alpharef = rw::GetRenderState(rw::ALPHATESTREF);
			gsalpharef = rw::GetRenderState(rw::GSALPHATESTREF);

			SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAGREATEREQUAL);
			SetRenderState(rw::ALPHATESTREF, gsalpharef);
			drawInst_simple(header, inst);
			SetRenderState(rw::ALPHATESTFUNC, rw::ALPHALESS);
			SetRenderState(rw::ZWRITEENABLE, 0);
			drawInst_simple(header, inst);
			SetRenderState(rw::ZWRITEENABLE, 1);
			SetRenderState(rw::ALPHATESTFUNC, alphafunc);
			SetRenderState(rw::ALPHATESTREF, alpharef);
		}else{
			SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAALWAYS);
			drawInst_simple(header, inst);
			SetRenderState(rw::ALPHATESTFUNC, alphafunc);
		}
	}else
		drawInst_simple(header, inst);
}

void
drawInst(d3d9::InstanceDataHeader *header, d3d9::InstanceData *inst)
{
	if(rw::GetRenderState(rw::GSALPHATEST))
		drawInst_GSemu(header, inst);
	else
		drawInst_simple(header, inst);
}

/*
void
defaultRenderCB_Fix(Atomic *atomic, InstanceDataHeader *header)
{
	RawMatrix world;
	Geometry *geo = atomic->geometry;

	int lighting = !!(geo->flags & rw::Geometry::LIGHT);
	if(lighting)
		d3d::lightingCB_Fix(atomic);

	d3d::setRenderState(D3DRS_LIGHTING, lighting);

	Frame *f = atomic->getFrame();
	convMatrix(&world, f->getLTM());
	d3ddevice->SetTransform(D3DTS_WORLD, (D3DMATRIX*)&world);

	setStreamSource(0, header->vertexStream[0].vertexBuffer, 0, header->vertexStream[0].stride);
	setIndices(header->indexBuffer);
	setVertexDeclaration(header->vertexDeclaration);

	InstanceData *inst = header->inst;
	for(uint32 i = 0; i < header->numMeshes; i++){
		SetRenderState(VERTEXALPHA, inst->vertexAlpha || inst->material->color.alpha != 255);
		const static rw::RGBA white = { 255, 255, 255, 255 };
		d3d::setMaterial(white, inst->material->surfaceProps);

		d3d::setRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL);
		if(geo->flags & Geometry::PRELIT)
			d3d::setRenderState(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_COLOR1);
		else
			d3d::setRenderState(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);
		d3d::setRenderState(D3DRS_DIFFUSEMATERIALSOURCE, inst->vertexAlpha ? D3DMCS_COLOR1 : D3DMCS_MATERIAL);

		if(inst->material->texture){
			// Texture
			d3d::setTexture(0, inst->material->texture);
			d3d::setTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
			d3d::setTextureStageState(0, D3DTSS_COLORARG1, D3DTA_CURRENT);
			d3d::setTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TEXTURE);
			d3d::setTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
			d3d::setTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_CURRENT);
			d3d::setTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TEXTURE);
		}else{
			d3d::setTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
			d3d::setTextureStageState(0, D3DTSS_COLORARG1, D3DTA_CURRENT);
			d3d::setTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
			d3d::setTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_CURRENT);
		}

		// Material colour
		const rw::RGBA *col = &inst->material->color;
		d3d::setTextureStageState(1, D3DTSS_CONSTANT, D3DCOLOR_ARGB(col->alpha,col->red,col->green,col->blue));
		d3d::setTextureStageState(1, D3DTSS_COLOROP, D3DTOP_MODULATE);
		d3d::setTextureStageState(1, D3DTSS_COLORARG1, D3DTA_CURRENT);
		d3d::setTextureStageState(1, D3DTSS_COLORARG2, D3DTA_CONSTANT);
		d3d::setTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
		d3d::setTextureStageState(1, D3DTSS_ALPHAARG1, D3DTA_CURRENT);
		d3d::setTextureStageState(1, D3DTSS_ALPHAARG2, D3DTA_CONSTANT);

		drawInst(header, inst);
		inst++;
	}
	d3d::setTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	d3d::setTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
}
*/

void
defaultRenderCB_Shader(Atomic *atomic, InstanceDataHeader *header)
{
	int vsBits;
	uint32 flags = atomic->geometry->flags;
	setStreamSource(0, header->vertexStream[0].vertexBuffer, 0, header->vertexStream[0].stride);
	setIndices(header->indexBuffer);
	setVertexDeclaration(header->vertexDeclaration);

	vsBits = lightingCB_Shader(atomic);
	uploadMatrices(atomic->getFrame()->getLTM());

	// Pick a shader. The pp-* variants move the lighting loops into the
	// pixel shader; we only use them when perPixelLightingEnabled is set
	// AND the matching shaders are loaded (defensive in case pp .h headers
	// weren't shipped with the build). The pp_gbuf_* variants additionally
	// write MRT slot 1 (packed world-normal + linear depth) and are picked
	// when gbufferEnabled is set by the host (CGBuffer::BeginScenePass).
	bool usePP   = perPixelLightingEnabled && default_pp_PS && default_pp_tex_PS;
	bool useGbuf = usePP && gbufferEnabled && default_pp_gbuf_PS && default_pp_gbuf_tex_PS;

	if((vsBits & VSLIGHT_MASK) == 0)
		setVertexShader(useGbuf && default_pp_gbuf_amb_VS ? default_pp_gbuf_amb_VS :
		                usePP   && default_pp_amb_VS      ? default_pp_amb_VS      :
		                                                    default_amb_VS);
	else if((vsBits & VSLIGHT_MASK) == VSLIGHT_DIRECT)
		setVertexShader(useGbuf && default_pp_gbuf_amb_dir_VS ? default_pp_gbuf_amb_dir_VS :
		                usePP   && default_pp_amb_dir_VS      ? default_pp_amb_dir_VS      :
		                                                        default_amb_dir_VS);
	else
		setVertexShader(useGbuf && default_pp_gbuf_all_VS ? default_pp_gbuf_all_VS :
		                usePP   && default_pp_all_VS      ? default_pp_all_VS      :
		                                                    default_all_VS);

	// G-buffer slot 1 colour-write is globally disabled by
	// CGBuffer::BeginScenePass to keep stray pipelines (vehicle, water,
	// particles, ...) from writing garbage into the normal/depth RT. We
	// re-enable it here only for the gbuf shader variants, then drop it
	// back to zero after the mesh loop, so every other pipeline still
	// runs with slot 1 protected.
	if(useGbuf)
		d3ddevice->SetRenderState(D3DRS_COLORWRITEENABLE1, 0x0F);

	InstanceData *inst = header->inst;
	for(uint32 i = 0; i < header->numMeshes; i++){
		Material *m = inst->material;

		SetRenderState(VERTEXALPHA, inst->vertexAlpha || m->color.alpha != 255);

		setMaterial(flags, m->color, m->surfaceProps);

		if(m->texture){
			d3d::setTexture(0, m->texture);
			setPixelShader(useGbuf ? default_pp_gbuf_tex_PS :
			               usePP   ? default_pp_tex_PS      :
			                         default_tex_PS);
		}else
			setPixelShader(useGbuf ? default_pp_gbuf_PS :
			               usePP   ? default_pp_PS      :
			                         default_PS);

		drawInst(header, inst);
		inst++;
	}

	if(useGbuf)
		d3ddevice->SetRenderState(D3DRS_COLORWRITEENABLE1, 0);
}

#endif
}
}
