/******************************************************************************
    Copyright (C) 2022 by Georges Basile Stavracas Neto <georges.stavracas@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "gl-gbm.h"
#include "gl-egl-common.h"

#include <fcntl.h>
#include <gbm.h>
#include <glad/glad_egl.h>

// #include <xf86drm.h>
#ifdef __OpenBSD__
#define DRM_DIR_NAME "/dev"
#define DRM_DEV_NAME "%s/drm%d"
#define DRM_CONTROL_DEV_NAME "%s/drmC%d"
#define DRM_RENDER_DEV_NAME "%s/drmR%d"
#else
#define DRM_DIR_NAME "/dev/dri"
#define DRM_DEV_NAME "%s/card%d"
#define DRM_CONTROL_DEV_NAME "%s/controlD%d"
#define DRM_RENDER_DEV_NAME "%s/renderD%d"
#define DRM_PROC_NAME "/proc/dri/" /* For backward Linux compatibility */
#endif

static const EGLint ctx_attribs[] = {
#ifdef _DEBUG
	EGL_CONTEXT_OPENGL_DEBUG,
	EGL_TRUE,
#endif
	EGL_CONTEXT_OPENGL_PROFILE_MASK,
	EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
	EGL_CONTEXT_MAJOR_VERSION,
	3,
	EGL_CONTEXT_MINOR_VERSION,
	3,
	EGL_NONE,
};

static const EGLint config_attribs[] = {
	EGL_SURFACE_TYPE, EGL_WINDOW_BIT,  EGL_RENDERABLE_TYPE,
	EGL_OPENGL_BIT,   EGL_BUFFER_SIZE, 32,
	EGL_STENCIL_SIZE, EGL_DONT_CARE,   EGL_DEPTH_SIZE,
	EGL_DONT_CARE,    EGL_ALPHA_SIZE,  8,
	EGL_NONE,
};

struct gl_windowinfo {
	uint32_t width;
	uint32_t height;

	struct gbm_surface *gbm_surface;
	EGLSurface *egl_surface;
};

struct gl_platform {
	struct gbm_device *gbm;
	EGLDisplay egl_display;
	EGLConfig egl_config;
	EGLint gbm_format;
	EGLContext egl_context;
};

static bool egl_make_current(EGLDisplay display, EGLSurface surface,
			     EGLContext context)
{
	if (eglBindAPI(EGL_OPENGL_API) == EGL_FALSE) {
		blog(LOG_ERROR, "[gbm] eglBindAPI failed");
		return false;
	}

	if (!eglMakeCurrent(display, surface, surface, context)) {
		blog(LOG_ERROR, "[gbm] eglMakeCurrent failed");
		return false;
	}

	if (surface != EGL_NO_SURFACE)
		glDrawBuffer(GL_BACK);

	return true;
}

static void egl_context_destroy(struct gl_platform *plat)
{
	egl_make_current(plat->egl_display, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroyContext(plat->egl_display, plat->egl_context);
}

static bool create_egl_context(struct gl_platform *plat, const EGLint *attribs)
{
	EGLBoolean result;
	EGLint n_configs;

	if (eglBindAPI(EGL_OPENGL_API) == EGL_FALSE) {
		blog(LOG_ERROR, "[gbm] eglBindAPI failed");
		return false;
	}

	result = eglChooseConfig(plat->egl_display, config_attribs,
				 &plat->egl_config, 1, &n_configs);
	if (result != EGL_TRUE || n_configs == 0) {
		blog(LOG_ERROR, "[gbm] eglChooseConfig failed");
		return false;
	}

	plat->egl_context = eglCreateContext(
		plat->egl_display, plat->egl_config, EGL_NO_CONTEXT, attribs);
	if (plat->egl_context == EGL_NO_CONTEXT) {
		blog(LOG_ERROR, "[gbm] eglCreateContext failed");
		return false;
	}

	if (!eglGetConfigAttrib(plat->egl_display, plat->egl_config,
				EGL_NATIVE_VISUAL_ID, &plat->gbm_format)) {
		return false;
	}

	return egl_make_current(plat->egl_display, EGL_NO_SURFACE,
				plat->egl_context);
}

struct gl_windowinfo *
gl_gbm_windowinfo_create(const struct gs_init_data *init_data)
{
	struct gl_windowinfo *wi;

	if (init_data->format != GS_BGRA && init_data->format != GS_BGRX) {
		blog(LOG_ERROR,
		     "Only GS_BGRA and GS_BGRX are supported for the DRM renderer");
		return NULL;
	}

	wi = bmalloc(sizeof(struct gl_windowinfo));
	wi->width = init_data->cx;
	wi->height = init_data->cy;

	return wi;
}

static void gl_gbm_windowinfo_destroy(struct gl_windowinfo *wi)
{
	bfree(wi);
}

static struct gl_platform *gl_gbm_platform_create(gs_device_t *device,
						  uint32_t adapter)
{
	UNUSED_PARAMETER(device);
	struct gl_platform *plat;
	const EGLint *attribs;
	char drm_device[64];
	EGLint major;
	EGLint minor;
	int card_fd;

	snprintf(drm_device, sizeof(drm_device), DRM_DEV_NAME, DRM_DIR_NAME,
		 adapter);

	card_fd = open(drm_device, O_RDWR | O_CLOEXEC);
	if (card_fd == -1) {
		blog(LOG_ERROR, "[gbm] Failed to open DRI device");
		return NULL;
	}

	blog(LOG_INFO, "[gbm] Opening DRM device %s", drm_device);

#define EGL_PLATFORM_GBM_KHR 0x31D7
	plat = bmalloc(sizeof(struct gl_platform));
	plat->gbm = gbm_create_device(card_fd);
	plat->egl_display = eglGetPlatformDisplay(
		EGL_PLATFORM_GBM_KHR, (EGLNativeDisplayType)plat->gbm, NULL);
	if (plat->egl_display == EGL_NO_DISPLAY) {
		blog(LOG_ERROR, "[gbm] eglGetDisplay failed");
		goto fail_display_init;
	}

	if (eglInitialize(plat->egl_display, &major, &minor) == EGL_FALSE) {
		blog(LOG_ERROR, "[gbm] eglInitialize failed");
		goto fail_display_init;
	}

	blog(LOG_INFO, "[gbm] Initialized EGL %d.%d", major, minor);

	attribs = ctx_attribs;
	if (major > 1 || (major == 1 && minor < 5)) {
		blog(LOG_ERROR, "[gbm] EGL 1.5 or higher is required.");
		goto fail_context_create;
	}

	if (!create_egl_context(plat, attribs))
		goto fail_context_create;

	if (!gladLoadGL()) {
		blog(LOG_ERROR, "[gbm] Failed to load OpenGL entry functions.");
		goto fail_load_gl;
	}

	if (!gladLoadEGL()) {
		blog(LOG_ERROR, "[gbm] Unable to load EGL entry functions.");
		goto fail_load_egl;
	}

	goto success;

fail_load_egl:
fail_load_gl:
	egl_context_destroy(plat);
fail_context_create:
	eglTerminate(plat->egl_display);
fail_display_init:
	bfree(plat);
	plat = NULL;
success:
	return plat;
}

static void gl_gbm_platform_destroy(struct gl_platform *plat)
{
	if (plat) {
		egl_context_destroy(plat);
		eglTerminate(plat->egl_display);
		bfree(plat);
	}
}

static bool gl_gbm_platform_init_swapchain(struct gs_swap_chain *swap)
{
	struct gl_windowinfo *wi = swap->wi;
	struct gl_platform *plat = swap->device->plat;

	wi->gbm_surface = NULL;
	wi->egl_surface = EGL_NO_SURFACE;

	errno = 0;
	uint64_t modifiers = 0x0;
	wi->gbm_surface = gbm_surface_create_with_modifiers2(
		plat->gbm, wi->width, wi->height, plat->gbm_format, NULL, 0,
		GBM_BO_USE_RENDERING);
	if (!wi->gbm_surface) {
		blog(LOG_ERROR,
		     "[gbm] Failed to initialize swapchain: failed to create gbm_surface");
		return false;
	}

	wi->egl_surface = eglCreatePlatformWindowSurface(
		plat->egl_display, plat->egl_config, (void *)wi->gbm_surface,
		NULL);

	if (wi->egl_surface == EGL_NO_SURFACE) {
		blog(LOG_ERROR,
		     "[gbm] Failed to initialize swapchain: failed to create EGLSurface");
		return false;
	}

	return true;
}

static void gl_gbm_platform_cleanup_swapchain(struct gs_swap_chain *swap)
{
	struct gl_windowinfo *wi = swap->wi;
	struct gl_platform *plat = swap->device->plat;

	if (wi->gbm_surface) {
		gbm_surface_destroy(wi->gbm_surface);
		wi->gbm_surface = NULL;
	}

	if (wi->egl_surface != EGL_NO_SURFACE) {
		eglDestroySurface(plat->egl_display, swap->wi->egl_surface);
		wi->egl_surface = EGL_NO_SURFACE;
	}
}

static void gl_gbm_device_enter_context(gs_device_t *device)
{
	struct gl_platform *plat = device->plat;
	EGLSurface egl_surface = EGL_NO_SURFACE;

	if (device->cur_swap)
		egl_surface = device->cur_swap->wi->egl_surface;

	egl_make_current(plat->egl_display, egl_surface, plat->egl_context);
}

static void gl_gbm_device_leave_context(gs_device_t *device)
{
	struct gl_platform *plat = device->plat;

	egl_make_current(plat->egl_display, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

static void *gl_gbm_device_get_device_obj(gs_device_t *device)
{
	return device->plat->egl_context;
}

static void gl_gbm_getclientsize(const struct gs_swap_chain *swap,
				 uint32_t *width, uint32_t *height)
{
	if (width)
		*width = swap->wi->width;
	if (height)
		*height = swap->wi->height;
}

static void gl_gbm_clear_context(gs_device_t *device)
{
	struct gl_platform *plat = device->plat;

	egl_make_current(plat->egl_display, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

static void gl_gbm_update(gs_device_t *device)
{
	struct gs_swap_chain *swap = device->cur_swap;

	assert(swap != NULL);

	gl_gbm_platform_cleanup_swapchain(swap);

	swap->wi->width = swap->info.cx;
	swap->wi->height = swap->info.cy;

	gl_gbm_platform_init_swapchain(swap);
}

static void gl_gbm_device_load_swapchain(gs_device_t *device,
					 gs_swapchain_t *swap)
{
	struct gl_platform *plat = device->plat;

	if (device->cur_swap == swap)
		return;

	device->cur_swap = swap;

	if (swap)
		egl_make_current(plat->egl_display, swap->wi->egl_surface,
				 plat->egl_context);
	else
		egl_make_current(plat->egl_display, EGL_NO_SURFACE,
				 EGL_NO_CONTEXT);
}

static void gl_gbm_device_present(gs_device_t *device)
{
	struct gl_platform *plat = device->plat;
	struct gl_windowinfo *wi = device->cur_swap->wi;

	if (eglSwapBuffers(plat->egl_display, wi->egl_surface) == EGL_FALSE)
		blog(LOG_ERROR, "[gbm] eglSwapBuffers failed");
}

static struct gs_texture *gl_gbm_device_texture_create_from_dmabuf(
	gs_device_t *device, unsigned int width, unsigned int height,
	uint32_t drm_format, enum gs_color_format color_format,
	uint32_t n_planes, const int *fds, const uint32_t *strides,
	const uint32_t *offsets, const uint64_t *modifiers)
{
	struct gl_platform *plat = device->plat;

	return gl_egl_create_dmabuf_image(plat->egl_display, width, height,
					  drm_format, color_format, n_planes,
					  fds, strides, offsets, modifiers);
}

static bool gl_gbm_device_query_dmabuf_capabilities(
	gs_device_t *device, enum gs_dmabuf_flags *dmabuf_flags,
	uint32_t **drm_formats, size_t *n_formats)
{
	struct gl_platform *plat = device->plat;

	return gl_egl_query_dmabuf_capabilities(plat->egl_display, dmabuf_flags,
						drm_formats, n_formats);
}

static bool gl_gbm_device_query_dmabuf_modifiers_for_format(
	gs_device_t *device, uint32_t drm_format, uint64_t **modifiers,
	size_t *n_modifiers)
{
	struct gl_platform *plat = device->plat;

	return gl_egl_query_dmabuf_modifiers_for_format(
		plat->egl_display, drm_format, modifiers, n_modifiers);
}

static struct gs_texture *gl_gbm_device_texture_create_from_pixmap(
	gs_device_t *device, uint32_t width, uint32_t height,
	enum gs_color_format color_format, uint32_t target, void *pixmap)
{
	UNUSED_PARAMETER(device);
	UNUSED_PARAMETER(width);
	UNUSED_PARAMETER(height);
	UNUSED_PARAMETER(color_format);
	UNUSED_PARAMETER(target);
	UNUSED_PARAMETER(pixmap);
	return NULL;
}

static const gs_swapchain_image_t *
gl_gbm_egl_swapchain_acquire_image(gs_swapchain_t *swap)
{
	return NULL;
}

static void
gl_gbm_egl_swapchain_release_image(gs_swapchain_t *swap,
				   const gs_swapchain_image_t *image)
{
}

static const struct gl_winsys_vtable gl_gbm_winsys_vtable = {
	.windowinfo_create = gl_gbm_windowinfo_create,
	.windowinfo_destroy = gl_gbm_windowinfo_destroy,
	.platform_create = gl_gbm_platform_create,
	.platform_destroy = gl_gbm_platform_destroy,
	.platform_init_swapchain = gl_gbm_platform_init_swapchain,
	.platform_cleanup_swapchain = gl_gbm_platform_cleanup_swapchain,
	.device_enter_context = gl_gbm_device_enter_context,
	.device_leave_context = gl_gbm_device_leave_context,
	.device_get_device_obj = gl_gbm_device_get_device_obj,
	.getclientsize = gl_gbm_getclientsize,
	.clear_context = gl_gbm_clear_context,
	.update = gl_gbm_update,
	.device_load_swapchain = gl_gbm_device_load_swapchain,
	.device_present = gl_gbm_device_present,
	.device_texture_create_from_dmabuf =
		gl_gbm_device_texture_create_from_dmabuf,
	.device_query_dmabuf_capabilities =
		gl_gbm_device_query_dmabuf_capabilities,
	.device_query_dmabuf_modifiers_for_format =
		gl_gbm_device_query_dmabuf_modifiers_for_format,
	.device_texture_create_from_pixmap =
		gl_gbm_device_texture_create_from_pixmap,
	.swapchain_acquire_image = gl_gbm_egl_swapchain_acquire_image,
	.swapchain_release_image = gl_gbm_egl_swapchain_release_image,
};

const struct gl_winsys_vtable *gl_gbm_get_winsys_vtable(void)
{
	return &gl_gbm_winsys_vtable;
}
