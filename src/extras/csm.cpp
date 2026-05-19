#define WITHD3D
#include "common.h"

#ifdef POSTFX_CSM

#ifndef LIBRW
#error "POSTFX_CSM needs librw"
#endif

#include "main.h"
#include "Camera.h"
#include "Timecycle.h"
#include "RwHelper.h"
#include "Renderer.h"
#include "csm.h"

CCSM::Cascade CCSM::Cascades[CSM_NUM_CASCADES];
bool CCSM::Enabled = false;		// opt-in until receiver lands in default_pp_PS
bool CCSM::bRendering = false;
int32 CCSM::MapSize = CSM_DEFAULT_SIZE;
float CCSM::Strength = 0.85f;
float CCSM::Bias = 0.003f;
int32 CCSM::NumCascades = 3;

void *csmDepthVS;
void *csmDepthPS;

// Practical Split Scheme split distances (mix uniform + log) for the view
// frustum's z range. lambda = 0.5 favours mid-range detail.
static void
ComputeSplitDistances(float zNear, float zFar, float outSplits[CSM_NUM_CASCADES + 1])
{
	const float lambda = 0.5f;
	outSplits[0] = zNear;
	for(int i = 1; i <= CCSM::NumCascades; i++){
		float si = (float)i / (float)CCSM::NumCascades;
		float logSplit = zNear * powf(zFar / zNear, si);
		float linSplit = zNear + (zFar - zNear) * si;
		outSplits[i] = lambda * logSplit + (1.0f - lambda) * linSplit;
	}
}

void
CCSM::InitOnce(void)
{
	for(int i = 0; i < CSM_NUM_CASCADES; i++){
		Cascades[i].depthRT = nil;
		Cascades[i].zBuffer = nil;
		Cascades[i].lightCam = nil;
		Cascades[i].splitDist = 0;
	}
}

void
CCSM::Open(RwCamera *cam)
{
	if(Cascades[0].depthRT)
		Close();
	if(!Enabled)
		return;

	int32 size = MapSize;
	if(size < 512) size = 512;
	else if(size > 4096) size = 4096;
	MapSize = size;

	rw::Camera *sceneCam = (rw::Camera*)cam;

	// Use the rw "f16" path is overkill for depth; R32F would be ideal but
	// we don't have it in the librw format table yet. F16_RGBA is the
	// closest float colour format and gives us 16-bit precision per channel
	// which is enough for ~mm precision on a 250m far clip cascade.
	int32 colorFmt = (int32)rw::Raster::CAMERATEXTURE | (int32)rw::Raster::F16_RGBA;

	for(int i = 0; i < CSM_NUM_CASCADES; i++){
		Cascades[i].depthRT = RwRasterCreate(size, size, 0, colorFmt);
		Cascades[i].zBuffer = RwRasterCreate(size, size, 0, (int32)rw::Raster::ZBUFFER);

		rw::Camera *lc = rw::Camera::create();
		rw::Frame *frame = rw::Frame::create();
		lc->setFrame(frame);
		lc->frameBuffer = (rw::Raster*)Cascades[i].depthRT;
		lc->zBuffer = (rw::Raster*)Cascades[i].zBuffer;
		lc->setNearPlane(0.5f);
		lc->setFarPlane(500.0f);
		rw::V2d vw = { 1.0f, 1.0f };	// orthographic; we set proj manually each frame
		lc->setViewWindow(&vw);
		if(sceneCam->world)
			sceneCam->world->addCamera(lc);
		Cascades[i].lightCam = (RwCamera*)lc;
	}
}

void
CCSM::Close(void)
{
	for(int i = 0; i < CSM_NUM_CASCADES; i++){
		if(Cascades[i].lightCam){
			rw::Camera *lc = (rw::Camera*)Cascades[i].lightCam;
			lc->frameBuffer = nil;
			lc->zBuffer = nil;
			rw::Frame *f = lc->getFrame();
			if(f){
				lc->setFrame(nil);
				f->destroy();
			}
			if(lc->world)
				lc->world->removeCamera(lc);
			lc->destroy();
			Cascades[i].lightCam = nil;
		}
		if(Cascades[i].depthRT){ RwRasterDestroy(Cascades[i].depthRT); Cascades[i].depthRT = nil; }
		if(Cascades[i].zBuffer){ RwRasterDestroy(Cascades[i].zBuffer); Cascades[i].zBuffer = nil; }
	}
}

void
CCSM::ComputeCascades(RwCamera *cam)
{
	if(!Enabled || Cascades[0].lightCam == nil)
		return;

	rw::Camera *sceneCam = (rw::Camera*)cam;
	float zNear = sceneCam->nearPlane;
	float zFar  = sceneCam->farPlane;

	float splits[CSM_NUM_CASCADES + 1];
	ComputeSplitDistances(zNear, zFar, splits);

	// Sun direction in world space. CTimeCycle returns the direction *to*
	// the sun; we want the light propagation direction (away from sun).
	CVector sunTo = CTimeCycle::GetSunDirection();
	rw::V3d lightDir = { -sunTo.x, -sunTo.y, -sunTo.z };
	float len = sqrtf(lightDir.x*lightDir.x + lightDir.y*lightDir.y + lightDir.z*lightDir.z);
	if(len > 0.0001f){
		lightDir.x /= len; lightDir.y /= len; lightDir.z /= len;
	}else{
		lightDir.x = 0; lightDir.y = 0; lightDir.z = -1;
	}

	// World-up; if the sun is straight down (rare), use X axis as up to
	// avoid a degenerate cross product.
	rw::V3d worldUp = { 0, 0, 1 };
	if(fabsf(lightDir.z) > 0.95f){
		worldUp.x = 1; worldUp.y = 0; worldUp.z = 0;
	}

	rw::V3d camPos = sceneCam->getFrame()->getLTM()->pos;
	rw::V3d camFwd = sceneCam->getFrame()->getLTM()->at;

	for(int i = 0; i < NumCascades; i++){
		float sNear = splits[i];
		float sFar  = splits[i + 1];
		float midDist = (sNear + sFar) * 0.5f;
		float radius = (sFar - sNear) * 0.5f;
		// Bounding sphere of cascade slice — radius equals half the
		// frustum slice's diagonal but for simplicity we approximate as
		// half the slice depth. Good enough for first-pass CSM.

		// Sphere centre = camera + camFwd * midDist
		rw::V3d centre;
		centre.x = camPos.x + camFwd.x * midDist;
		centre.y = camPos.y + camFwd.y * midDist;
		centre.z = camPos.z + camFwd.z * midDist;

		// Texel-snap centre so the shadow map stays stable as the camera
		// rotates / moves. Snap unit = 2 * radius / size in world units
		// per texel.
		float texelSize = (2.0f * radius) / (float)MapSize;
		centre.x = floorf(centre.x / texelSize) * texelSize;
		centre.y = floorf(centre.y / texelSize) * texelSize;
		centre.z = floorf(centre.z / texelSize) * texelSize;

		// Build light view matrix: position = centre - lightDir * pullBack,
		// orientation = lookAt(centre - lightDir, centre, worldUp).
		float pullBack = 250.0f;	// extrude back so casters behind frustum still occlude
		rw::Camera *lc = (rw::Camera*)Cascades[i].lightCam;
		rw::Frame *lframe = lc->getFrame();
		rw::Matrix &m = lframe->matrix;
		m.pos.x = centre.x - lightDir.x * pullBack;
		m.pos.y = centre.y - lightDir.y * pullBack;
		m.pos.z = centre.z - lightDir.z * pullBack;
		m.at = lightDir;
		// Right = normalize(cross(worldUp, at))
		rw::V3d right;
		right.x = worldUp.y * m.at.z - worldUp.z * m.at.y;
		right.y = worldUp.z * m.at.x - worldUp.x * m.at.z;
		right.z = worldUp.x * m.at.y - worldUp.y * m.at.x;
		float rl = sqrtf(right.x*right.x + right.y*right.y + right.z*right.z);
		if(rl > 0.0001f){ right.x/=rl; right.y/=rl; right.z/=rl; }
		m.right = right;
		// Up = cross(at, right)
		m.up.x = m.at.y * m.right.z - m.at.z * m.right.y;
		m.up.y = m.at.z * m.right.x - m.at.x * m.right.z;
		m.up.z = m.at.x * m.right.y - m.at.y * m.right.x;
		m.flags |= rw::Matrix::TYPEORTHONORMAL;
		lframe->updateObjects();

		lc->setNearPlane(0.5f);
		lc->setFarPlane(pullBack + radius + 50.0f);
		// Orthographic-ish via tiny near plane; rw::Camera supports a
		// PROJECTION setting we'd ideally pull on — left as TODO since
		// librw exposes its setProjection API for us already.
		rw::V2d vw = { radius, radius };
		lc->setViewWindow(&vw);

		Cascades[i].splitDist = sFar;
		// lightViewProj is computed lazily by rw::Camera::beginUpdate; we
		// can read it from cam->devView * cam->devProj after that fires.
		// Receiver-side upload happens once the SHADOWS_CSM PS variant is
		// in place.
		(void)Cascades[i].lightViewProj;
	}
}

void
CCSM::RenderShadowMaps(RwCamera *cam)
{
	if(!Enabled || Cascades[0].lightCam == nil)
		return;
	if(bRendering)
		return;

	ComputeCascades(cam);

	RwCameraEndUpdate(cam);
	bRendering = true;

	rw::RGBA white;
	white.red = 255; white.green = 255; white.blue = 255; white.alpha = 255;

	for(int i = 0; i < NumCascades; i++){
		rw::Camera *lc = (rw::Camera*)Cascades[i].lightCam;
		lc->clear(&white, rwCAMERACLEARIMAGE | rwCAMERACLEARZ);
		RwCameraBeginUpdate(Cascades[i].lightCam);
		// TODO: route opaque geometry through a depth-only pipeline that
		// uses csmDepthVS/csmDepthPS. For now we exit before the render
		// call so we can at least verify that the cascade cameras + RTs
		// are constructed correctly. Full receiver + render integration
		// is a follow-up commit.
		RwCameraEndUpdate(Cascades[i].lightCam);
	}

	bRendering = false;
	RwCameraBeginUpdate(cam);
}

#endif
