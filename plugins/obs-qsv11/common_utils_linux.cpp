#include "common_utils.h"

#include <time.h>
#include <cpuid.h>

#include <obs.h>
#include <obs-encoder.h>
#include <graphics/graphics.h>
#include <util/c99defs.h>
#include <util/dstr.h>
#include <util/bmem.h>
#include <va/va_drm.h>
#include <va/va_drmcommon.h>
#include <va/va_str.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <dirent.h>

#define DEVICE_MGR_TYPE MFX_HANDLE_VA_DISPLAY

struct surface_info {
	VASurfaceID id;
	int32_t width, height;
	gs_texture_t *tex_y;
	gs_texture_t *tex_uv;
};

mfxStatus simple_alloc(mfxHDL pthis, mfxFrameAllocRequest *request,
		       mfxFrameAllocResponse *response)
{
	if (request->Type &
	    (MFX_MEMTYPE_SYSTEM_MEMORY |
	     // MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET | // but why is this allocated?
	     MFX_MEMTYPE_VIDEO_MEMORY_PROCESSOR_TARGET))
		return MFX_ERR_UNSUPPORTED;

	response->mids = (mfxMemId *)nullptr;
	response->NumFrameActual = 0;

	MFXVideoSession *session = (MFXVideoSession *)pthis;
	VADisplay display;
	mfxStatus sts = session->GetHandle(DEVICE_MGR_TYPE, &display);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	// Thanks intel https://ffmpeg.org/doxygen/5.1/hwcontext__vaapi_8c_source.html#l00109
	// though earlier comments suggest the driver ignores rt_format so we could choose whatever.
	unsigned int rt_format;
	int32_t pix_format;
	switch (request->Info.FourCC) {
	case MFX_FOURCC_P010:
		rt_format = VA_RT_FORMAT_YUV420_10;
		pix_format = VA_FOURCC_P010;
		break;
	case MFX_FOURCC_NV12:
	default:
		rt_format = VA_RT_FORMAT_YUV420;
		pix_format = VA_FOURCC_NV12;
		break;
	}

	int num_attrs = 2;
	VASurfaceAttrib attrs[2] = {
		{
			.type = VASurfaceAttribMemoryType,
			.flags = VA_SURFACE_ATTRIB_SETTABLE,
			.value =
				{
					.type = VAGenericValueTypeInteger,
					.value =
						{.i = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2},
				},
		},
		{
			.type = VASurfaceAttribPixelFormat,
			.flags = VA_SURFACE_ATTRIB_SETTABLE,
			.value =
				{
					.type = VAGenericValueTypeInteger,
					.value = {.i = (int)pix_format},
				},
		}};

	unsigned int num_surfaces = request->NumFrameSuggested;
	VASurfaceID temp_surfaces[64] = {0};
	// assert(num_surfaces < 64);
	VAStatus vasts;
	if ((vasts = vaCreateSurfaces(display, rt_format, request->Info.Width,
				      request->Info.Height, temp_surfaces,
				      num_surfaces, attrs, num_attrs)) !=
	    VA_STATUS_SUCCESS) {
		blog(LOG_ERROR, "failed to create surfaces: %d", vasts);
		return MFX_ERR_MEMORY_ALLOC;
	}

	// Follow the ffmpeg trick and stuff our pointer at the end.
	mfxMemId *mids =
		(mfxMemId *)bmalloc(sizeof(mfxMemId) * num_surfaces + 1);
	struct surface_info *surfaces = (struct surface_info *)bmalloc(
		sizeof(struct surface_info) * num_surfaces);

	mids[num_surfaces] = surfaces; // stuff it
	for (uint64_t i = 0; i < num_surfaces; i++) {
		surfaces[i].id = temp_surfaces[i];
		surfaces[i].width = request->Info.Width;
		surfaces[i].height = request->Info.Height;
		mids[i] = &surfaces[i];

		VADRMPRIMESurfaceDescriptor surfDesc = {0};
		if (vaExportSurfaceHandle(display, surfaces[i].id,
					  VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
					  VA_EXPORT_SURFACE_READ_WRITE,
					  &surfDesc) != VA_STATUS_SUCCESS)
			return MFX_ERR_MEMORY_ALLOC;

		obs_enter_graphics();
		// TODO: P010 format handling.
		// TODO: Verify its one FD
		assert(surfDesc.num_objects == 1);
		int fds[4] = {0};
		uint32_t strides[4] = {0};
		uint32_t offsets[4] = {0};
		uint64_t modifiers[4] = {0};
		fds[0] =
			surfDesc.objects[surfDesc.layers[0].object_index[0]].fd;
		fds[1] =
			surfDesc.objects[surfDesc.layers[1].object_index[0]].fd;
		strides[0] = surfDesc.layers[0].pitch[0];
		strides[1] = surfDesc.layers[1].pitch[0];
		offsets[0] = surfDesc.layers[0].offset[0];
		offsets[1] = surfDesc.layers[1].offset[0];
		modifiers[0] =
			surfDesc.objects[surfDesc.layers[0].object_index[0]]
				.drm_format_modifier;
		modifiers[1] =
			surfDesc.objects[surfDesc.layers[1].object_index[0]]
				.drm_format_modifier;

		surfaces[i].tex_y = gs_texture_create_from_dmabuf(
			surfDesc.width, surfDesc.height,
			surfDesc.layers[0].drm_format, GS_R8, 1, fds, strides,
			offsets, modifiers);
		surfaces[i].tex_uv = gs_texture_create_from_dmabuf(
			surfDesc.width / 2, surfDesc.height,
			surfDesc.layers[1].drm_format, GS_R8G8, 1, fds + 1,
			strides + 1, offsets + 1, modifiers + 1);
		obs_leave_graphics();

		close(surfDesc.objects[surfDesc.layers[0].object_index[0]].fd);
		if (!surfaces[i].tex_y || !surfaces[i].tex_uv) {
			return MFX_ERR_MEMORY_ALLOC;
		}
	}

	response->mids = (mfxMemId *)mids;
	response->NumFrameActual = num_surfaces;
	return MFX_ERR_NONE;
}
mfxStatus simple_lock(mfxHDL pthis, mfxMemId mid, mfxFrameData *ptr)
{
	UNUSED_PARAMETER(pthis);
	UNUSED_PARAMETER(mid);
	UNUSED_PARAMETER(ptr);
	return MFX_ERR_UNSUPPORTED;
}
mfxStatus simple_unlock(mfxHDL pthis, mfxMemId mid, mfxFrameData *ptr)
{
	UNUSED_PARAMETER(pthis);
	UNUSED_PARAMETER(mid);
	UNUSED_PARAMETER(ptr);
	return MFX_ERR_UNSUPPORTED;
}
mfxStatus simple_gethdl(mfxHDL pthis, mfxMemId mid, mfxHDL *handle)
{
	UNUSED_PARAMETER(pthis);
	if (NULL == handle)
		return MFX_ERR_INVALID_HANDLE;

	// Seemingly undocumented, but Pair format defined by
	// oneVPL-intel-gpu-intel-onevpl-23.1.0/_studio/mfx_lib/encode_hw/av1/linux/base/av1ehw_base_va_packer_lin.cpp
	mfxHDLPair *pPair = (mfxHDLPair *)handle;

	// Pointer to a VASurfaceID, will be dereferenced by the driver.
	pPair->first = &((struct surface_info *)mid)->id;
	pPair->second = 0;

	return MFX_ERR_NONE;
}
mfxStatus simple_free(mfxHDL pthis, mfxFrameAllocResponse *response)
{
	if (response->mids == nullptr || response->NumFrameActual == 0)
		return MFX_ERR_NONE;

	MFXVideoSession *session = (MFXVideoSession *)pthis;
	VADisplay display;
	mfxStatus sts = session->GetHandle(DEVICE_MGR_TYPE, &display);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	struct surface_info *surfs =
		(struct surface_info *)response->mids[response->NumFrameActual];
	VASurfaceID temp_surfaces[64] = {0};
	obs_enter_graphics();
	for (int i = 0; i < response->NumFrameActual; i++) {
		temp_surfaces[i] = *(VASurfaceID *)response->mids[i];
		gs_texture_destroy(surfs[i].tex_y);
		gs_texture_destroy(surfs[i].tex_uv);
	}
	obs_leave_graphics();

	bfree(surfs);
	bfree(response->mids);
	if (vaDestroySurfaces(display, temp_surfaces,
			      response->NumFrameActual) != VA_STATUS_SUCCESS)
		return MFX_ERR_MEMORY_ALLOC;

	return MFX_ERR_NONE;
}

mfxStatus simple_copytex(mfxHDL pthis, mfxMemId mid, void *tex, mfxU64 lock_key,
			 mfxU64 *next_key)
{
	UNUSED_PARAMETER(lock_key);
	UNUSED_PARAMETER(next_key);

	profile_start("copy_tex");

	MFXVideoSession *session = (MFXVideoSession *)pthis;
	VADisplay display;
	mfxStatus sts = session->GetHandle(DEVICE_MGR_TYPE, &display);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	struct encoder_texture *ptex = (struct encoder_texture *)tex;
	struct surface_info *surf = (struct surface_info *)mid;

	obs_enter_graphics();
	if (1) {
		// Extreme danger, executing this where the any plane
		// has silently failed to import will crash/stall the gpu and
		// result in extreme sadness.
		gs_copy_texture(surf->tex_y, ptex->tex[0]);
		gs_copy_texture(surf->tex_uv, ptex->tex[1]);
	} else {
		gs_texture_t *prev_target = gs_get_render_target();
		gs_effect_t *const effect =
			obs_get_base_effect(OBS_EFFECT_DEFAULT);
		gs_eparam_t *image =
			gs_effect_get_param_by_name(effect, "image");
		float width = surf->width;
		float height = surf->height;

		gs_viewport_push();
		gs_projection_push();
		gs_ortho(0.0f, width, 0.0f, height, -100.0f, 100.0f);
		gs_set_viewport(0, 0, width, height);

		gs_effect_set_texture_srgb(image, ptex->tex[0]);
		gs_set_render_target(surf->tex_y, NULL);
		while (gs_effect_loop(effect, "Draw")) {
			gs_draw_sprite(ptex->tex[0], 0, 0, 0);
		}

		gs_effect_set_texture_srgb(image, ptex->tex[1]);
		gs_set_render_target(surf->tex_uv, NULL);
		while (gs_effect_loop(effect, "Draw")) {
			gs_draw_sprite(ptex->tex[1], 0, 0, 0);
		}

		gs_set_render_target(prev_target, NULL);
		// gs_flush();
	}
	obs_leave_graphics();

	profile_end("copy_tex");
	return MFX_ERR_NONE;
}

// Initialize Intel Media SDK Session, device/display and memory manager
mfxStatus Initialize(mfxIMPL impl, mfxVersion ver, MFXVideoSession *pSession,
		     mfxFrameAllocator *pmfxAllocator, mfxHDL *deviceHandle,
		     bool bCreateSharedHandles, bool dx9hack)
{
	UNUSED_PARAMETER(bCreateSharedHandles);
	UNUSED_PARAMETER(dx9hack);
	mfxStatus sts = MFX_ERR_NONE;

	// If mfxFrameAllocator is provided it means we need to setup vaapi device and memory allocator
	if (pmfxAllocator) {
		// Initialize Intel Media SDK Session
		sts = pSession->Init(impl, &ver);
		MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

		// Create DirectX device context
		if (deviceHandle == NULL || *deviceHandle == NULL) {
			mfxIMPL impl;
			const char *implDRM;

			// Extract Media SDK base implementation type
			MFXQueryIMPL(*pSession, &impl);
			impl = MFX_IMPL_BASETYPE(impl);
			switch (impl) {
			case MFX_IMPL_HARDWARE:
				implDRM = "/dev/dri/renderD128";
				break;
			case MFX_IMPL_HARDWARE2:
				implDRM = "/dev/dri/renderD129";
				break;
			case MFX_IMPL_HARDWARE3:
				implDRM = "/dev/dri/renderD130";
				break;
			case MFX_IMPL_HARDWARE4:
				implDRM = "/dev/dri/renderD131";
				break;
			default:
				sts = MFX_ERR_DEVICE_FAILED;
			}
			MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

			int card = open(implDRM, O_RDWR);
			VADisplay vaDisplay = vaGetDisplayDRM(card);
			int mj, mn;
			if (vaInitialize(vaDisplay, &mj, &mn) !=
			    VA_STATUS_SUCCESS) {
				sts = MFX_ERR_DEVICE_FAILED;
				MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
			}
			*deviceHandle = (mfxHDL)vaDisplay;
		}

		// Provide device manager to Media SDK
		sts = pSession->SetHandle(DEVICE_MGR_TYPE, *deviceHandle);
		MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

		pmfxAllocator->pthis =
			pSession; // We use Media SDK session ID as the allocation identifier
		pmfxAllocator->Alloc = simple_alloc;
		pmfxAllocator->Free = simple_free;
		pmfxAllocator->Lock = simple_lock;
		pmfxAllocator->Unlock = simple_unlock;
		pmfxAllocator->GetHDL = simple_gethdl;

		// Since we are using video memory we must provide Media SDK with an external allocator
		sts = pSession->SetFrameAllocator(pmfxAllocator);
		MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
	} else {
		// Initialize Intel Media SDK Session, no custom allocator.
		sts = pSession->Init(impl, &ver);
		MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
	}
	return sts;
}

// Release resources (device/display)
void Release(){};

void mfxGetTime(mfxTime *timestamp)
{
	clock_gettime(CLOCK_MONOTONIC, timestamp);
}

double TimeDiffMsec(mfxTime tfinish, mfxTime tstart)
{
	UNUSED_PARAMETER(tfinish);
	UNUSED_PARAMETER(tstart);
	//TODO, unused so far it seems
	return 0.0;
}

extern "C" void util_cpuid(int cpuinfo[4], int level)
{
	__get_cpuid(level, (unsigned int *)&cpuinfo[0],
		    (unsigned int *)&cpuinfo[1], (unsigned int *)&cpuinfo[2],
		    (unsigned int *)&cpuinfo[3]);
}

struct vaapi_device {
	int fd;
	VADisplay display;
	const char *driver;
};

void vaapi_open(char *device_path, struct vaapi_device *device)
{
	int fd = open(device_path, O_RDWR);
	if (fd < 0) {
		return;
	}

	VADisplay display = vaGetDisplayDRM(fd);
	if (!display) {
		close(fd);
		return;
	}

	// VA-API is noisy by default.
	vaSetInfoCallback(display, nullptr, nullptr);
	vaSetErrorCallback(display, nullptr, nullptr);

	int major, minor;
	if (vaInitialize(display, &major, &minor) != VA_STATUS_SUCCESS) {
		vaTerminate(display);
		close(fd);
		return;
	}

	const char *driver = vaQueryVendorString(display);
	if (strstr(driver, "Intel i965 driver") != nullptr) {
		blog(LOG_ERROR,
		     "Legacy intel-vaapi-driver detected, incompatible with QSV");
		vaTerminate(display);
		close(fd);
		return;
	}

	device->fd = fd;
	device->display = display;
	device->driver = driver;
}

void vaapi_close(struct vaapi_device *device)
{
	vaTerminate(device->display);
	close(device->fd);
}

static uint32_t vaapi_check_support(VADisplay display, VAProfile profile,
				    VAEntrypoint entrypoint)
{
	bool ret = false;
	VAConfigAttrib attrib[1];
	attrib->type = VAConfigAttribRateControl;

	VAStatus va_status =
		vaGetConfigAttributes(display, profile, entrypoint, attrib, 1);

	uint32_t rc = 0;
	switch (va_status) {
	case VA_STATUS_SUCCESS:
		rc = attrib->value;
		break;
	case VA_STATUS_ERROR_UNSUPPORTED_PROFILE:
	case VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT:
	default:
		break;
	}

	return (rc & VA_RC_CBR || rc & VA_RC_CQP || rc & VA_RC_VBR);
}

bool vaapi_supports_h264(VADisplay display)
{
	bool ret = false;
	ret |= vaapi_check_support(display, VAProfileH264ConstrainedBaseline,
				   VAEntrypointEncSlice);
	ret |= vaapi_check_support(display, VAProfileH264Main,
				   VAEntrypointEncSlice);
	ret |= vaapi_check_support(display, VAProfileH264High,
				   VAEntrypointEncSlice);

	if (!ret) {
		ret |= vaapi_check_support(display,
					   VAProfileH264ConstrainedBaseline,
					   VAEntrypointEncSliceLP);
		ret |= vaapi_check_support(display, VAProfileH264Main,
					   VAEntrypointEncSliceLP);
		ret |= vaapi_check_support(display, VAProfileH264High,
					   VAEntrypointEncSliceLP);
	}

	return ret;
}

bool vaapi_supports_av1(VADisplay display)
{
	bool ret = false;
	// Are there any devices with non-lowpower entrypoints?
	ret |= vaapi_check_support(display, VAProfileAV1Profile0,
				   VAEntrypointEncSlice);
	ret |= vaapi_check_support(display, VAProfileAV1Profile0,
				   VAEntrypointEncSliceLP);
	return ret;
}

bool vaapi_supports_hevc(VADisplay display)
{
	bool ret = false;
	ret |= vaapi_check_support(display, VAProfileHEVCMain,
				   VAEntrypointEncSlice);
	ret |= vaapi_check_support(display, VAProfileHEVCMain,
				   VAEntrypointEncSliceLP);
	return ret;
}

void check_adapters(struct adapter_info *adapters, size_t *adapter_count)
{
	struct dstr full_path;
	struct dirent **namelist;
	int no;
	int adapter_idx;
	const char *base_dir = "/dev/dri/";

	dstr_init(&full_path);
	if ((no = scandir(base_dir, &namelist, 0, alphasort)) > 0) {
		for (int i = 0; i < no; i++) {
			struct adapter_info *adapter;
			struct dirent *dp;
			struct vaapi_device device = {0};

			dp = namelist[i];
			if (strstr(dp->d_name, "renderD") == nullptr)
				goto next_entry;

			adapter_idx = atoi(&dp->d_name[7]) - 128;
			if (adapter_idx >= (ssize_t)*adapter_count ||
			    adapter_idx < 0)
				goto next_entry;

			*adapter_count = adapter_idx + 1;
			dstr_copy(&full_path, base_dir);
			dstr_cat(&full_path, dp->d_name);
			vaapi_open(full_path.array, &device);
			if (!device.display)
				goto next_entry;

			adapter = &adapters[adapter_idx];
			adapter->is_intel = strstr(device.driver, "Intel") !=
					    nullptr;
			// This is currently only used for lowpower coding which is busted on VAAPI anyway.
			adapter->is_dgpu = false;
			adapter->supports_av1 =
				vaapi_supports_av1(device.display);
			adapter->supports_hevc =
				vaapi_supports_hevc(device.display);
			vaapi_close(&device);

		next_entry:
			free(dp);
		}
		free(namelist);
	}
	dstr_free(&full_path);
}
