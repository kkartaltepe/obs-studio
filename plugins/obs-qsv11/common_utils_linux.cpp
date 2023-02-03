#include "common_utils.h"
#include <time.h>
#include <cpuid.h>
#include <util/c99defs.h>
#include <util/bmem.h>
#include <fcntl.h>
#include <va/va.h>
#include <va/va_drm.h>
#include <va/va_drmcommon.h>

#define DEVICE_MGR_TYPE MFX_HANDLE_VA_DISPLAY

mfxStatus simple_alloc(mfxHDL pthis, mfxFrameAllocRequest *request,
		       mfxFrameAllocResponse *response)
{
	if (request->Type & MFX_MEMTYPE_SYSTEM_MEMORY)
		return MFX_ERR_UNSUPPORTED;

	MFXVideoSession *session = (MFXVideoSession *)pthis;
	VADisplay display;
	mfxStatus sts = session->GetHandle(DEVICE_MGR_TYPE, &display);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	// Thanks intel https://ffmpeg.org/doxygen/5.1/hwcontext__vaapi_8c_source.html#l00109
	// though earlier comments suggest the driver ignores rt_format so we could choose whatever.
	unsigned int rt_format;
	int32_t pix_format;
	switch (request->Info.FourCC) {
	case MFX_FOURCC_NV12:
		rt_format = VA_RT_FORMAT_YUV420;
		pix_format = VA_FOURCC_NV12;
		break;
		/*
		 * These end up not really having valid surface formats in va-api
	case MFX_FOURCC_RGB4:
		rt_format = VA_RT_FORMAT_RGB32;
		pix_format = VA_FOURCC_RGBX;
		break;
	case MFX_FOURCC_YUY2:
		rt_format = VA_RT_FORMAT_YUV422;
		pix_format = VA_FOURCC_YUY2;
		break;
	case MFX_FOURCC_P8:
		rt_format = VA_RT_FORMAT_YUV400;
		pix_format = VA_FOURCC_Y8;
		break;
		*/
	case MFX_FOURCC_P010:
		rt_format = VA_RT_FORMAT_YUV420_10;
		pix_format = VA_FOURCC_P010;
		break;
	}

	int num_attrs = 2;
	VASurfaceAttrib attrs[2] = {
		{
			.type = VASurfaceAttribMemoryType, // maybe not supported on old drivers? We need to map these with libdrm.
			.flags = VA_SURFACE_ATTRIB_SETTABLE,
			.value =
				{
					.type = VAGenericValueTypeInteger,
					.value =
						{.i = VA_SURFACE_ATTRIB_MEM_TYPE_VA |
						      VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2},
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
	VASurfaceID *surfaces =
		(VASurfaceID *)bmalloc(sizeof(mfxMemId) * num_surfaces);
	if (vaCreateSurfaces(display, rt_format, request->Info.Width,
			     request->Info.Height, surfaces, num_surfaces,
			     attrs, num_attrs) != VA_STATUS_SUCCESS)
		return MFX_ERR_MEMORY_ALLOC;

	if (sizeof(VASurfaceID) < sizeof(mfxMemId)) {
		for (int i = num_surfaces; i > 1; i--) {
			// Weird casts since surfaces are 32bit and we are storing into a void *.
			((mfxMemId *)surfaces)[i] =
				(mfxMemId)(uintptr_t)surfaces[i];
		}
	}

	response->mids = (mfxMemId *)surfaces;
	response->NumFrameActual = request->NumFrameSuggested;
	return sts;
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
	*handle = (mfxHDL)mid;
	return MFX_ERR_NONE;
}
mfxStatus simple_free(mfxHDL pthis, mfxFrameAllocResponse *response)
{
	MFXVideoSession *session = (MFXVideoSession *)pthis;
	VADisplay display;
	mfxStatus sts = session->GetHandle(DEVICE_MGR_TYPE, &display);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	if (vaDestroySurfaces(display, (VASurfaceID *)response->mids,
			      response->NumFrameActual) != VA_STATUS_SUCCESS)
		return MFX_ERR_MEMORY_ALLOC;
	bfree(response->mids);

	return MFX_ERR_NONE;
}

mfxStatus simple_copytex(mfxHDL pthis, mfxMemId mid, mfxU32 tex_handle,
			 mfxU64 lock_key, mfxU64 *next_key)
{
	UNUSED_PARAMETER(pthis);
	UNUSED_PARAMETER(mid);
	UNUSED_PARAMETER(tex_handle);
	UNUSED_PARAMETER(lock_key);
	UNUSED_PARAMETER(next_key);


	MFXVideoSession *session = (MFXVideoSession *)pthis;
	VADisplay display;
	mfxStatus sts = session->GetHandle(DEVICE_MGR_TYPE, &display);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	/* LOL unsupported on intel, of course.

	// Get the mid DRM buffer and va blit into it from tex_handle dma-buf.
	VADRMPRIMESurfaceDescriptor surfDesc = {
		.fourcc = VA_FOURCC_NV12, // P010?
		.width = width,
		.height = height,
		.num_objects = 2, // combined planes? Not possible on opengl (or vulkan?)
		.objects = {{.fd = XXX, .size = 0, .drm_format_modifier = XXX},
			    {.fd = XXX, .size = 0, .drm_format_modifier = XXX}},
		.num_layers = 2, // 1 for combined planes.
		.layers = {
			{.drm_format = XXX,
			 .num_planes = 1,
			 .object_index = {0},
			 .offset = {XXX},
			 .pitch = {XXX}},
			{.drm_format = XXX,
			 .num_planes = 1,
			 .object_index = {0},
			 .offset = {XXX},
			 .pitch = {XXX}},
		};

	int num_attrs = 2;
	VASurfaceAttrib attrs[2] = {
		{
			.type = VASurfaceAttribMemoryType, // maybe not supported on old drivers? We need to map these with libdrm.
			.flags = VA_SURFACE_ATTRIB_SETTABLE,
			.value =
				{
					.type = VAGenericValueTypeInteger,
					.value =
						{.i = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2},
				},
		},
		{
			.type = VASurfaceAttribExternalBufferDescriptor,
			.flags = VA_SURFACE_ATTRIB_SETTABLE,
			.value =
				{
					.type = VAGenericValueTypePointer,
					.value = {.p = &surfDesc},
				},
		}};

	unsigned int num_surfaces = 1;
	VASurfaceID surface;
	if (vaCreateSurfaces(display, rt_format, width, height, &surface,
			     num_surfaces, attrs,
			     num_attrs) != VA_STATUS_SUCCESS)
		return MFX_ERR_MEMORY_ALLOC;

	VAImage tex_image;
	if(vaDeriveImage(display, surface, &tex_image) != VA_STATUS_SUCCESS) {
		vaDestroySurfaces(display, &surface, num_surfaces);
		return MFX_ERR_MEMORY_ALLOC;
	}

	// I hope this is fast... Also no cross texture format support so much sadness if the source isnt nv12.
	// https://github.com/intel/media-driver/issues/401
	vaPutImage(display, va_surf, tex_image, 0, 0, width, height, 0, 0,
		   width, height);
	vaDestroyImage(display, tex_image);
	vaDestroySurfaces(display, &surface, num_surfaces);
	*/

	VADRMPRIMESurfaceDescriptor surfDesc = {0};
	VASurfaceID va_surf = (VASurfaceID)(uintptr_t)mid; // reverse weird casts.
	if (vaExportSurfaceHandle(display, va_surf,
				  VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
				  VA_EXPORT_SURFACE_WRITE_ONLY,
				  &surfDesc) != VA_STATUS_SUCCESS)
		return MFX_ERR_UNSUPPORTED;

	return MFX_ERR_NONE;
}

// Initialize Intel Media SDK Session, device/display and memory manager
mfxStatus Initialize(mfxIMPL impl, mfxVersion ver, MFXVideoSession *pSession,
		     mfxFrameAllocator *pmfxAllocator, mfxHDL *deviceHandle,
		     bool bCreateSharedHandles, bool dx9hack)
{
	UNUSED_PARAMETER(deviceHandle);
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
				implDRM = "/dev/dri/card0";
				break;
			case MFX_IMPL_HARDWARE2:
				implDRM = "/dev/dri/card1";
				break;
			case MFX_IMPL_HARDWARE3:
				implDRM = "/dev/dri/card2";
				break;
			case MFX_IMPL_HARDWARE4:
				implDRM = "/dev/dri/card3";
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
			*pSession; // We use Media SDK session ID as the allocation identifier
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

//void mfxInitTime();  might need this for Windows
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
