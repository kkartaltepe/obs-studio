#include "common_utils.h"

// ATTENTION: If D3D surfaces are used, DX9_D3D or DX11_D3D must be set in project settings or hardcoded here

#ifdef DX9_D3D
#include "common_directx.h"
#elif DX11_D3D
#include "common_directx11.h"
#include "common_directx9.h"
#endif

#include <intrin.h>
#include <Windows.h>
#include <VersionHelpers.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <intrin.h>
#include <wrl/client.h>

/* =======================================================
 * Windows implementation of OS-specific utility functions
 */

mfxStatus Initialize(mfxIMPL impl, mfxVersion ver, MFXVideoSession *pSession,
		     mfxFrameAllocator *pmfxAllocator, mfxHDL *deviceHandle,
		     bool bCreateSharedHandles, bool dx9hack)
{
	bCreateSharedHandles; // (Hugh) Currently unused
	pmfxAllocator;        // (Hugh) Currently unused

	mfxStatus sts = MFX_ERR_NONE;

	// If mfxFrameAllocator is provided it means we need to setup DirectX device and memory allocator
	if (pmfxAllocator && !dx9hack) {
		// Initialize Intel Media SDK Session
		sts = pSession->Init(impl, &ver);
		MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

		// Create DirectX device context
		if (deviceHandle == NULL || *deviceHandle == NULL) {
			sts = CreateHWDevice(*pSession, deviceHandle, NULL,
					     bCreateSharedHandles);
			MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
		}

		if (deviceHandle == NULL || *deviceHandle == NULL)
			return MFX_ERR_DEVICE_FAILED;

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

	} else if (pmfxAllocator && dx9hack) {
		// Initialize Intel Media SDK Session
		sts = pSession->Init(impl, &ver);
		MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

		// Create DirectX device context
		if (deviceHandle == NULL || *deviceHandle == NULL) {
			sts = DX9_CreateHWDevice(*pSession, deviceHandle, NULL,
						 false);
			MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
		}
		if (*deviceHandle == NULL)
			return MFX_ERR_DEVICE_FAILED;

		// Provide device manager to Media SDK
		sts = pSession->SetHandle(MFX_HANDLE_D3D9_DEVICE_MANAGER,
					  *deviceHandle);
		MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

		pmfxAllocator->pthis =
			*pSession; // We use Media SDK session ID as the allocation identifier
		pmfxAllocator->Alloc = dx9_simple_alloc;
		pmfxAllocator->Free = dx9_simple_free;
		pmfxAllocator->Lock = dx9_simple_lock;
		pmfxAllocator->Unlock = dx9_simple_unlock;
		pmfxAllocator->GetHDL = dx9_simple_gethdl;

		// Since we are using video memory we must provide Media SDK with an external allocator
		sts = pSession->SetFrameAllocator(pmfxAllocator);
		MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	} else {
		// Initialize Intel Media SDK Session
		sts = pSession->Init(impl, &ver);
		MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
	}
	return sts;
}

void Release()
{
#if defined(DX9_D3D) || defined(DX11_D3D)
	CleanupHWDevice();
	DX9_CleanupHWDevice();
#endif
}

void mfxGetTime(mfxTime *timestamp)
{
	QueryPerformanceCounter(timestamp);
}

double TimeDiffMsec(mfxTime tfinish, mfxTime tstart)
{
	static LARGE_INTEGER tFreq = {0};

	if (!tFreq.QuadPart)
		QueryPerformanceFrequency(&tFreq);

	double freq = (double)tFreq.QuadPart;
	return 1000.0 * ((double)tfinish.QuadPart - (double)tstart.QuadPart) /
	       freq;
}

/* (Hugh) Functions currently unused */
#if 0
void ClearYUVSurfaceVMem(mfxMemId memId)
{
#if defined(DX9_D3D) || defined(DX11_D3D)
    ClearYUVSurfaceD3D(memId);
#endif
}

void ClearRGBSurfaceVMem(mfxMemId memId)
{
#if defined(DX9_D3D) || defined(DX11_D3D)
    ClearRGBSurfaceD3D(memId);
#endif
}
#endif

static HANDLE get_lib(const char *lib)
{
	HMODULE mod = GetModuleHandleA(lib);
	if (mod)
		return mod;

	mod = LoadLibraryA(lib);
	if (!mod)
		blog(LOG_INFO, "Failed to load %s", lib);
	return mod;
}

typedef HRESULT(WINAPI *CREATEDXGIFACTORY1PROC)(REFIID, void **);

bool is_intel_gpu_primary()
{
	HMODULE dxgi = get_lib("DXGI.dll");
	CREATEDXGIFACTORY1PROC create_dxgi;
	IDXGIFactory1 *factory;
	IDXGIAdapter *adapter;
	DXGI_ADAPTER_DESC desc;
	HRESULT hr;

	if (!dxgi) {
		return false;
	}
	create_dxgi = (CREATEDXGIFACTORY1PROC)GetProcAddress(
		dxgi, "CreateDXGIFactory1");

	if (!create_dxgi) {
		blog(LOG_INFO, "Failed to load D3D11/DXGI procedures");
		return false;
	}

	hr = create_dxgi(&IID_IDXGIFactory1, &factory);
	if (FAILED(hr)) {
		blog(LOG_INFO, "CreateDXGIFactory1 failed");
		return false;
	}

	hr = factory->lpVtbl->EnumAdapters(factory, 0, &adapter);
	factory->lpVtbl->Release(factory);
	if (FAILED(hr)) {
		blog(LOG_INFO, "EnumAdapters failed");
		return false;
	}

	hr = adapter->lpVtbl->GetDesc(adapter, &desc);
	adapter->lpVtbl->Release(adapter);
	if (FAILED(hr)) {
		blog(LOG_INFO, "GetDesc failed");
		return false;
	}

	/*check whether adapter 0 is Intel*/
	if (desc.VendorId == 0x8086) {
		return true;
	} else {
		return false;
	}
}

bool prefer_current_or_igpu_enc(int *iGPUIndex)
{
	IDXGIAdapter *pAdapter;
	bool hasIGPU = false;
	bool hasDGPU = false;
	bool hasCurrent = false;

	HMODULE hDXGI = LoadLibrary(L"dxgi.dll");
	if (hDXGI == NULL) {
		return false;
	}

	typedef HRESULT(WINAPI * LPCREATEDXGIFACTORY)(REFIID riid,
						      void **ppFactory);

	LPCREATEDXGIFACTORY pCreateDXGIFactory =
		(LPCREATEDXGIFACTORY)GetProcAddress(hDXGI,
						    "CreateDXGIFactory1");
	if (pCreateDXGIFactory == NULL) {
		pCreateDXGIFactory = (LPCREATEDXGIFACTORY)GetProcAddress(
			hDXGI, "CreateDXGIFactory");

		if (pCreateDXGIFactory == NULL) {
			FreeLibrary(hDXGI);
			return false;
		}
	}

	IDXGIFactory *pFactory = NULL;
	if (FAILED((*pCreateDXGIFactory)(__uuidof(IDXGIFactory),
					 (void **)(&pFactory)))) {
		FreeLibrary(hDXGI);
		return false;
	}

	LUID luid;
	bool hasLuid = false;
	// obs_enter_graphics();
	{
		ID3D11Device *pDevice = (ID3D11Device *)gs_get_device_obj();
		Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
		if (SUCCEEDED(pDevice->QueryInterface<IDXGIDevice>(
			    dxgiDevice.GetAddressOf()))) {
			Microsoft::WRL::ComPtr<IDXGIAdapter> dxgiAdapter;
			if (SUCCEEDED(dxgiDevice->GetAdapter(
				    dxgiAdapter.GetAddressOf()))) {
				DXGI_ADAPTER_DESC desc;
				hasLuid =
					SUCCEEDED(dxgiAdapter->GetDesc(&desc));
				if (hasLuid) {
					luid = desc.AdapterLuid;
				}
			}
		}
	}
	// obs_leave_graphics();

	// Check for i+I cases (Intel discrete + Intel integrated graphics on the same system). Default will be integrated.
	for (int adapterIndex = 0;
	     SUCCEEDED(pFactory->EnumAdapters(adapterIndex, &pAdapter));
	     ++adapterIndex) {
		DXGI_ADAPTER_DESC AdapterDesc = {};
		const HRESULT hr = pAdapter->GetDesc(&AdapterDesc);
		pAdapter->Release();

		if (SUCCEEDED(hr) && (AdapterDesc.VendorId == 0x8086)) {
			if (hasLuid &&
			    (AdapterDesc.AdapterLuid.LowPart == luid.LowPart) &&
			    (AdapterDesc.AdapterLuid.HighPart ==
			     luid.HighPart)) {
				hasCurrent = true;
				*iGPUIndex = adapterIndex;
				break;
			}

			if (AdapterDesc.DedicatedVideoMemory <=
			    512 * 1024 * 1024) {
				hasIGPU = true;
				if (iGPUIndex != NULL) {
					*iGPUIndex = adapterIndex;
				}
			} else {
				hasDGPU = true;
			}
		}
	}

	pFactory->Release();
	FreeLibrary(hDXGI);

	return hasCurrent || (hasIGPU && hasDGPU);
}

bool is_windows8_or_greater()
{
	return IsWindows8OrGreater();
}

void util_cpuid(int cpuinfo[4], int level) {
	__cpuid(level, cpuinfo);
}
