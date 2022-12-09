#include "common_utils.h"
#include <time.h>
#include <cpuid.h>

mfxStatus simple_alloc(mfxHDL pthis, mfxFrameAllocRequest *request,
		       mfxFrameAllocResponse *response)
{
	return MFX_ERR_UNSUPPORTED;
}
mfxStatus simple_lock(mfxHDL pthis, mfxMemId mid, mfxFrameData *ptr)
{
	return MFX_ERR_UNSUPPORTED;
}
mfxStatus simple_unlock(mfxHDL pthis, mfxMemId mid, mfxFrameData *ptr)
{
	return MFX_ERR_UNSUPPORTED;
}
mfxStatus simple_gethdl(mfxHDL pthis, mfxMemId mid, mfxHDL *handle)
{
	return MFX_ERR_UNSUPPORTED;
}
mfxStatus simple_free(mfxHDL pthis, mfxFrameAllocResponse *response)
{
	return MFX_ERR_UNSUPPORTED;
}
mfxStatus simple_copytex(mfxHDL pthis, mfxMemId mid, mfxU32 tex_handle,
			 mfxU64 lock_key, mfxU64 *next_key)
{
	return MFX_ERR_UNSUPPORTED;
}

#if 0
void ClearYUVSurfaceVMem(mfxMemId memId);
void ClearRGBSurfaceVMem(mfxMemId memId);
#endif

// Initialize Intel Media SDK Session, device/display and memory manager
mfxStatus Initialize(mfxIMPL impl, mfxVersion ver, MFXVideoSession *pSession,
		     mfxFrameAllocator *pmfxAllocator, mfxHDL *deviceHandle,
		     bool bCreateSharedHandles, bool dx9hack)
{
	mfxStatus sts = MFX_ERR_NONE;

	// Initialize Intel Media SDK Session
	sts = pSession->Init(impl, &ver);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
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
	//TODO, unused so far it seems
	return 0.0;
}


bool is_windows8_or_greater()
{
	return false;
}

void util_cpuid(int cpuinfo[4], int level)
{
	__get_cpuid(level, (unsigned int *)&cpuinfo[0],
		    (unsigned int *)&cpuinfo[1], (unsigned int *)&cpuinfo[2],
		    (unsigned int *)&cpuinfo[3]);
}
