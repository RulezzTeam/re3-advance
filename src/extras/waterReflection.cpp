#define WITHD3D
#include "common.h"

#ifdef POSTFX_WATER_REFLECTION

#ifndef LIBRW
#error "POSTFX_WATER_REFLECTION needs librw"
#endif

#include "main.h"
#include "RwHelper.h"
#include "Camera.h"
#include "Renderer.h"
#include "waterReflection.h"

extern RwRGBA gColourTop;

RwRaster *CWaterReflection::pRT;
RwRaster *CWaterReflection::pZBuffer;
RwCamera *CWaterReflection::reflectionCam;
bool CWaterReflection::Enabled = false;	// opt-in until water shader hooks land
bool CWaterReflection::bRendering = false;
float CWaterReflection::WaterPlaneZ = 6.0f;	// Vice City sea level ~6m world Z
int32 CWaterReflection::Resolution = 512;

void
CWaterReflection::InitOnce(void)
{
}

void
CWaterReflection::Open(RwCamera *cam)
{
	if(pRT)
		Close();
	if(!Enabled)
		return;

	int32 size = Resolution;
	if(size < 256) size = 256;
	else if(size > 2048) size = 2048;

	// HDR-friendly RGBA16F so the reflection composes seamlessly with
	// the main HDR scene RT.
	int32 colorFmt = (int32)rw::Raster::CAMERATEXTURE | (int32)rw::Raster::F16_RGBA;
	pRT = RwRasterCreate(size, size, 0, colorFmt);
	pZBuffer = RwRasterCreate(size, size, 0, (int32)rw::Raster::ZBUFFER);

	rw::Camera *sceneCam = (rw::Camera*)cam;
	rw::Camera *rcam = rw::Camera::create();
	rw::Frame *frame = rw::Frame::create();
	rcam->setFrame(frame);
	rcam->frameBuffer = (rw::Raster*)pRT;
	rcam->zBuffer = (rw::Raster*)pZBuffer;
	rcam->setNearPlane(sceneCam->nearPlane);
	rcam->setFarPlane(sceneCam->farPlane);
	rcam->setViewWindow(&sceneCam->viewWindow);
	if(sceneCam->world)
		sceneCam->world->addCamera(rcam);
	reflectionCam = (RwCamera*)rcam;
}

void
CWaterReflection::Close(void)
{
	if(reflectionCam){
		rw::Camera *rcam = (rw::Camera*)reflectionCam;
		rcam->frameBuffer = nil;
		rcam->zBuffer = nil;
		rw::Frame *f = rcam->getFrame();
		if(f){
			rcam->setFrame(nil);
			f->destroy();
		}
		if(rcam->world)
			rcam->world->removeCamera(rcam);
		rcam->destroy();
		reflectionCam = nil;
	}
	if(pRT){ RwRasterDestroy(pRT); pRT = nil; }
	if(pZBuffer){ RwRasterDestroy(pZBuffer); pZBuffer = nil; }
}

void
CWaterReflection::RebuildResolution(void)
{
	// Force a re-Open via the existing CPostFX::Open chain; the water
	// reflection module hangs off CPostFX's lifecycle so a fresh size needs
	// a recreate.
	// (Hook from menu AfterChange callback when wired up.)
}

// Builds a 4x4 mirror matrix about a horizontal plane at planeZ:
//   mirror = I - 2 * n * n^T  with n = (0, 0, 1)
//   plus a Z-translation of 2 * planeZ so the plane passes through Z=planeZ
// Result is post-multiplied onto the scene camera's frame matrix.
static void
BuildMirrorMatrix(float planeZ, rw::Matrix *out, const rw::Matrix &sceneMtx)
{
	*out = sceneMtx;
	out->right.z   = -sceneMtx.right.z;
	out->up.z      = -sceneMtx.up.z;
	out->at.z      = -sceneMtx.at.z;
	out->pos.z     = 2.0f*planeZ - sceneMtx.pos.z;
	out->flags |= rw::Matrix::TYPEORTHONORMAL;
}

void
CWaterReflection::Render(RwCamera *cam)
{
	if(!Enabled || reflectionCam == nil || pRT == nil)
		return;
	if(bRendering)
		return;	// guard against recursion

	rw::Camera *sceneCam = (rw::Camera*)cam;
	rw::Camera *rcam = (rw::Camera*)reflectionCam;

	// Mirror the scene camera about the water plane.
	rw::Frame *sframe = sceneCam->getFrame();
	rw::Frame *rframe = rcam->getFrame();
	if(sframe == nil || rframe == nil)
		return;
	BuildMirrorMatrix(WaterPlaneZ, &rframe->matrix, sframe->matrix);
	rframe->updateObjects();

	rcam->setNearPlane(sceneCam->nearPlane);
	rcam->setFarPlane(sceneCam->farPlane);
	rcam->setViewWindow(&sceneCam->viewWindow);
	rcam->setViewOffset(&sceneCam->viewOffset);

	rw::RGBA bg;
	bg.red   = (rw::uint8)gColourTop.red;
	bg.green = (rw::uint8)gColourTop.green;
	bg.blue  = (rw::uint8)gColourTop.blue;
	bg.alpha = 0;

	// We borrow the main pipeline (RenderRoads + RenderEverythingBarRoads)
	// rather than chain a custom one — those passes already honour
	// visibility plugins, which is what we want to skip in reflection mode.
	RwCameraEndUpdate(cam);

	rcam->clear(&bg, rwCAMERACLEARIMAGE | rwCAMERACLEARZ);
	RwCameraBeginUpdate(reflectionCam);
	bRendering = true;

	// Cull anything below the water plane via D3D9 user clip plane: keeps
	// us from spending budget on submerged geometry. Plane equation in
	// world space: dot(N, P) + D = 0 with N = (0,0,1), D = -planeZ
	// (positive half-space is above water).
#ifdef RW_D3D9
	float clipPlane[4] = { 0.0f, 0.0f, 1.0f, -WaterPlaneZ };
	rw::d3d::d3ddevice->SetClipPlane(0, clipPlane);
	DWORD oldClipState;
	rw::d3d::d3ddevice->GetRenderState(D3DRS_CLIPPLANEENABLE, &oldClipState);
	rw::d3d::d3ddevice->SetRenderState(D3DRS_CLIPPLANEENABLE, D3DCLIPPLANE0);
#endif

	CRenderer::RenderRoads();
	CRenderer::RenderEverythingBarRoads();
	CRenderer::RenderFadingInEntities();

#ifdef RW_D3D9
	rw::d3d::d3ddevice->SetRenderState(D3DRS_CLIPPLANEENABLE, oldClipState);
#endif

	bRendering = false;
	RwCameraEndUpdate(reflectionCam);
	RwCameraBeginUpdate(cam);
}

#endif
