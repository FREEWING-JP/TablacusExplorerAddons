// Tablacus Susie Plug-in Wrapper (C)2017 Gaku
// MIT Lisence
// Visual C++ 2010 Express Edition SP1
// Windows SDK v7.1
// http://www.eonet.ne.jp/~gakana/tablacus/

#include "tspi.h"

// Global Variables:
const TCHAR g_szProgid[] = TEXT("Tablacus.SusiePlugin");
const TCHAR g_szClsid[] = TEXT("{211571E6-E2B9-446F-8F9F-4DFBE338CE8C}");
HINSTANCE	g_hinstDll = NULL;
LONG		g_lLocks = 0;
CteBase		*g_pBase = NULL;
CteSPI		*g_ppObject[MAX_OBJ];
IDispatch	*g_pdispProgressProc = NULL;

TEmethod methodBASE[] = {
	{ 0x60010000, L"Open" },
	{ 0x6001000C, L"Close" },
};

TEmethod methodTSPI[] = {
	{ 0x60010001, L"GetPluginInfo" },
	{ 0x60010002, L"IsSupported" },
	{ 0x60010003, L"GetPictureInfo" },
	{ 0x60010004, L"GetPicture" },
	{ 0x60010005, L"GetPreview" },
	{ 0x60010006, L"GetArchiveInfo" },
	{ 0x60010007, L"GetFileInfo" },
	{ 0x60010008, L"GetFile" },
	{ 0x60010009, L"ConfigurationDlg" },
	{ 0x6001FFFF, L"IsUnicode" },
	{ 0, NULL }
};

// Unit
VOID SafeRelease(PVOID ppObj)
{
	try {
		IUnknown **ppunk = static_cast<IUnknown **>(ppObj);
		if (*ppunk) {
			(*ppunk)->Release();
			*ppunk = NULL;
		}
	} catch (...) {}
}

VOID teGetProcAddress(HMODULE hModule, LPSTR lpName, FARPROC *lpfnA, FARPROC *lpfnW)
{
	*lpfnA = GetProcAddress(hModule, lpName);
	if (lpfnW) {
		char pszProcName[80];
		strcpy_s(pszProcName, 80, lpName);
		strcat_s(pszProcName, 80, "W");
		*lpfnW = GetProcAddress(hModule, (LPCSTR)pszProcName);
	} else if (lpfnW) {
		*lpfnW = NULL;
	}
}

void LockModule(BOOL bLock)
{
	if (bLock) {
		InterlockedIncrement(&g_lLocks);
	} else {
		InterlockedDecrement(&g_lLocks);
	}
}

HRESULT ShowRegError(LSTATUS ls)
{
	LPTSTR lpBuffer = NULL;
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, ls, LANG_USER_DEFAULT, (LPTSTR)&lpBuffer, 0, NULL);
	MessageBox(NULL, lpBuffer, TEXT(PRODUCTNAME), MB_ICONHAND | MB_OK);
	LocalFree(lpBuffer);
	return HRESULT_FROM_WIN32(ls);
}

LSTATUS CreateRegistryKey(HKEY hKeyRoot, LPTSTR lpszKey, LPTSTR lpszValue, LPTSTR lpszData)
{
	HKEY  hKey;
	LSTATUS  lr;
	DWORD dwSize;

	lr = RegCreateKeyEx(hKeyRoot, lpszKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
	if (lr == ERROR_SUCCESS) {
		if (lpszData != NULL) {
			dwSize = (lstrlen(lpszData) + 1) * sizeof(TCHAR);
		} else {
			dwSize = 0;
		}
		lr = RegSetValueEx(hKey, lpszValue, 0, REG_SZ, (LPBYTE)lpszData, dwSize);
		RegCloseKey(hKey);
	}
	return lr;
}

/*
int teBSearch(TEmethod *method, int nSize, int* pMap, LPOLESTR bs)
{
	int nMin = 0;
	int nMax = nSize - 1;
	int nIndex, nCC;

	while (nMin <= nMax) {
		nIndex = (nMin + nMax) / 2;
		nCC = lstrcmpi(bs, method[pMap[nIndex]].name);
		if (nCC < 0) {
			nMax = nIndex - 1;
			continue;
		}
		if (nCC > 0) {
			nMin = nIndex + 1;
			continue;
		}
		return pMap[nIndex];
	}
	return -1;
}
*/
HRESULT teGetDispId(TEmethod *method, int nCount, int* pMap, LPOLESTR bs, DISPID *rgDispId)
{
/*	if (pMap) {
		int nIndex = teBSearch(method, nCount, pMap, bs);
		if (nIndex >= 0) {
			*rgDispId = method[nIndex].id;
			return S_OK;
		}
	} else {*/
		for (int i = 0; method[i].name; i++) {
			if (lstrcmpi(bs, method[i].name) == 0) {
				*rgDispId = method[i].id;
				return S_OK;
			}
		}
//	}
	return DISP_E_UNKNOWNNAME;
}

BSTR GetLPWSTRFromVariant(VARIANT *pv)
{
	switch (pv->vt) {
		case VT_VARIANT | VT_BYREF:
			return GetLPWSTRFromVariant(pv->pvarVal);
		case VT_BSTR:
		case VT_LPWSTR:
			return pv->bstrVal;
		default:
			return NULL;
	}//end_switch
}

int GetIntFromVariant(VARIANT *pv)
{
	if (pv) {
		if (pv->vt == (VT_VARIANT | VT_BYREF)) {
			return GetIntFromVariant(pv->pvarVal);
		}
		if (pv->vt == VT_I4) {
			return pv->lVal;
		}
		if (pv->vt == VT_UI4) {
			return pv->ulVal;
		}
		if (pv->vt == VT_R8) {
			return (int)(LONGLONG)pv->dblVal;
		}
		VARIANT vo;
		VariantInit(&vo);
		if SUCCEEDED(VariantChangeType(&vo, pv, 0, VT_I4)) {
			return vo.lVal;
		}
		if SUCCEEDED(VariantChangeType(&vo, pv, 0, VT_UI4)) {
			return vo.ulVal;
		}
		if SUCCEEDED(VariantChangeType(&vo, pv, 0, VT_I8)) {
			return (int)vo.llVal;
		}
	}
	return 0;
}

int GetIntFromVariantClear(VARIANT *pv)
{
	int i = GetIntFromVariant(pv);
	VariantClear(pv);
	return i;
}

#ifdef _WIN64
LONGLONG GetLLFromVariant(VARIANT *pv)
{
	if (pv) {
		if (pv->vt == (VT_VARIANT | VT_BYREF)) {
			return GetLLFromVariant(pv->pvarVal);
		}
		if (pv->vt == VT_I4) {
			return pv->lVal;
		}
		if (pv->vt == VT_R8) {
			return (LONGLONG)pv->dblVal;
		}
		if (pv->vt == (VT_ARRAY | VT_I4)) {
			LONGLONG ll = 0;
			PVOID pvData;
			if (::SafeArrayAccessData(pv->parray, &pvData) == S_OK) {
				::CopyMemory(&ll, pvData, sizeof(LONGLONG));
				::SafeArrayUnaccessData(pv->parray);
				return ll;
			}
		}
		VARIANT vo;
		VariantInit(&vo);
		if SUCCEEDED(VariantChangeType(&vo, pv, 0, VT_I8)) {
			return vo.llVal;
		}
	}
	return 0;
}
#endif

VOID teSetBool(VARIANT *pv, BOOL b)
{
	if (pv) {
		pv->boolVal = b ? VARIANT_TRUE : VARIANT_FALSE;
		pv->vt = VT_BOOL;
	}
}

VOID teSysFreeString(BSTR *pbs)
{
	if (*pbs) {
		::SysFreeString(*pbs);
		*pbs = NULL;
	}
}

VOID teSetLong(VARIANT *pv, LONG i)
{
	if (pv) {
		pv->lVal = i;
		pv->vt = VT_I4;
	}
}

VOID teSetLL(VARIANT *pv, LONGLONG ll)
{
	if (pv) {
		pv->lVal = static_cast<int>(ll);
		if (ll == static_cast<LONGLONG>(pv->lVal)) {
			pv->vt = VT_I4;
			return;
		}
		pv->dblVal = static_cast<DOUBLE>(ll);
		if (ll == static_cast<LONGLONG>(pv->dblVal)) {
			pv->vt = VT_R8;
			return;
		}
		SAFEARRAY *psa;
		psa = SafeArrayCreateVector(VT_I4, 0, sizeof(LONGLONG) / sizeof(int));
		if (psa) {
			PVOID pvData;
			if (::SafeArrayAccessData(psa, &pvData) == S_OK) {
				::CopyMemory(pvData, &ll, sizeof(LONGLONG));
				::SafeArrayUnaccessData(psa);
				pv->vt = VT_ARRAY | VT_I4;
				pv->parray = psa;
			}
		}
	}
}

BOOL teSetObject(VARIANT *pv, PVOID pObj)
{
	if (pObj) {
		try {
			IUnknown *punk = static_cast<IUnknown *>(pObj);
			if SUCCEEDED(punk->QueryInterface(IID_PPV_ARGS(&pv->pdispVal))) {
				pv->vt = VT_DISPATCH;
				return true;
			}
			if SUCCEEDED(punk->QueryInterface(IID_PPV_ARGS(&pv->punkVal))) {
				pv->vt = VT_UNKNOWN;
				return true;
			}
		} catch (...) {}
	}
	return false;
}

BOOL teSetObjectRelease(VARIANT *pv, PVOID pObj)
{
	if (pObj) {
		try {
			IUnknown *punk = static_cast<IUnknown *>(pObj);
			if (pv) {
				if SUCCEEDED(punk->QueryInterface(IID_PPV_ARGS(&pv->pdispVal))) {
					pv->vt = VT_DISPATCH;
					SafeRelease(&punk);
					return true;
				}
				if SUCCEEDED(punk->QueryInterface(IID_PPV_ARGS(&pv->punkVal))) {
					pv->vt = VT_UNKNOWN;
					SafeRelease(&punk);
					return true;
				}
			}
			SafeRelease(&punk);
		} catch (...) {}
	}
	return false;
}

VOID teSetSZA(VARIANT *pv, LPCSTR lpstr, int nCP)
{
	if (pv) {
		int nLenW = MultiByteToWideChar(nCP, 0, lpstr, -1, NULL, NULL);
		if (nLenW) {
			pv->bstrVal = ::SysAllocStringLen(NULL, nLenW - 1);
			pv->bstrVal[0] = NULL;
			MultiByteToWideChar(nCP, 0, (LPCSTR)lpstr, -1, pv->bstrVal, nLenW);
		} else {
			pv->bstrVal = NULL;
		}
		pv->vt = VT_BSTR;
	}
}

VOID teSetSZ(VARIANT *pv, LPCWSTR lpstr)
{
	if (pv) {
		pv->bstrVal = ::SysAllocString(lpstr);
		pv->vt = VT_BSTR;
	}
}

VOID teSetBSTR(VARIANT *pv, BSTR bs, int nLen)
{
	if (pv) {
		pv->vt = VT_BSTR;
		if (bs) {
			if (nLen < 0) {
				nLen = lstrlen(bs);
			}
			if (::SysStringLen(bs) == nLen) {
				pv->bstrVal = bs;
				return;
			}
		}
		pv->bstrVal = SysAllocStringLen(bs, nLen);
		teSysFreeString(&bs);
	}
}

BOOL FindUnknown(VARIANT *pv, IUnknown **ppunk)
{
	if (pv) {
		if (pv->vt == VT_DISPATCH || pv->vt == VT_UNKNOWN) {
			*ppunk = pv->punkVal;
			return *ppunk != NULL;
		}
		if (pv->vt == (VT_VARIANT | VT_BYREF)) {
			return FindUnknown(pv->pvarVal, ppunk);
		}
		if (pv->vt == (VT_DISPATCH | VT_BYREF) || pv->vt == (VT_UNKNOWN | VT_BYREF)) {
			*ppunk = *pv->ppunkVal;
			return *ppunk != NULL;
		}
	}
	*ppunk = NULL;
	return false;
}

BSTR GetMemoryFromVariant(VARIANT *pv, BOOL *pbDelete, LONG_PTR *pLen)
{
	if (pv->vt == (VT_VARIANT | VT_BYREF)) {
		return GetMemoryFromVariant(pv->pvarVal, pbDelete, pLen);
	}
	BSTR pMemory = NULL;
	*pbDelete = FALSE;
	if (pLen) {
		if (pv->vt == VT_BSTR || pv->vt == VT_LPWSTR) {
			return pv->bstrVal;
		}
	}
	IUnknown *punk;
	if (FindUnknown(pv, &punk)) {
		IStream *pStream;
		if SUCCEEDED(punk->QueryInterface(IID_PPV_ARGS(&pStream))) {
			ULARGE_INTEGER uliSize;
			if (pLen) {
				LARGE_INTEGER liOffset;
				liOffset.QuadPart = 0;
				pStream->Seek(liOffset, STREAM_SEEK_END, &uliSize);
				pStream->Seek(liOffset, STREAM_SEEK_SET, NULL);
			} else {
				uliSize.QuadPart = 2048;
			}
			pMemory = ::SysAllocStringByteLen(NULL, uliSize.LowPart > 2048 ? uliSize.LowPart : 2048);
			if (pMemory) {
				if (uliSize.LowPart < 2048) {
					::ZeroMemory(pMemory, 2048);
				}
				*pbDelete = TRUE;
				ULONG cbRead;
				pStream->Read(pMemory, uliSize.LowPart, &cbRead);
				if (pLen) {
					*pLen = cbRead;
				}
			}
			pStream->Release();
		}
	} else if (pv->vt == (VT_ARRAY | VT_I1) || pv->vt == (VT_ARRAY | VT_UI1) || pv->vt == (VT_ARRAY | VT_I1 | VT_BYREF) || pv->vt == (VT_ARRAY | VT_UI1 | VT_BYREF)) {
		LONG lUBound, lLBound, nSize;
		SAFEARRAY *psa = (pv->vt & VT_BYREF) ? pv->pparray[0] : pv->parray;
		PVOID pvData;
		if (::SafeArrayAccessData(psa, &pvData) == S_OK) {
			SafeArrayGetUBound(psa, 1, &lUBound);
			SafeArrayGetLBound(psa, 1, &lLBound);
			nSize = lUBound - lLBound + 1;
			pMemory = ::SysAllocStringByteLen(NULL, nSize > 2048 ? nSize : 2048);
			if (pMemory) {
				if (nSize < 2048) {
					::ZeroMemory(pMemory, 2048);
				}
				::CopyMemory(pMemory, pvData, nSize);
				if (pLen) {
					*pLen = nSize;
				}
				*pbDelete = TRUE;
			}
			::SafeArrayUnaccessData(psa);
		}
		return pMemory;
	} else if (!pLen) {
		return (BSTR)GetPtrFromVariant(pv);
	}
	return pMemory;
}

HRESULT tePutProperty0(IUnknown *punk, LPOLESTR sz, VARIANT *pv, DWORD grfdex)
{
	HRESULT hr = E_FAIL;
	DISPID dispid, putid;
	DISPPARAMS dispparams;
	IDispatchEx *pdex;
	if SUCCEEDED(punk->QueryInterface(IID_PPV_ARGS(&pdex))) {
		BSTR bs = ::SysAllocString(sz);
		hr = pdex->GetDispID(bs, grfdex, &dispid);
		if SUCCEEDED(hr) {
			putid = DISPID_PROPERTYPUT;
			dispparams.rgvarg = pv;
			dispparams.rgdispidNamedArgs = &putid;
			dispparams.cArgs = 1;
			dispparams.cNamedArgs = 1;
			hr = pdex->InvokeEx(dispid, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYPUTREF, &dispparams, NULL, NULL, NULL);
		}
		::SysFreeString(bs);
		SafeRelease(&pdex);
	}
	return hr;
}

HRESULT tePutProperty(IUnknown *punk, LPOLESTR sz, VARIANT *pv)
{
	return tePutProperty0(punk, sz, pv, fdexNameEnsure);
}

// VARIANT Clean-up of an array
VOID teClearVariantArgs(int nArgs, VARIANTARG *pvArgs)
{
	if (pvArgs && nArgs > 0) {
		for (int i = nArgs ; i-- >  0;){
			VariantClear(&pvArgs[i]);
		}
		delete[] pvArgs;
		pvArgs = NULL;
	}
}

HRESULT Invoke5(IDispatch *pdisp, DISPID dispid, WORD wFlags, VARIANT *pvResult, int nArgs, VARIANTARG *pvArgs)
{
	HRESULT hr;
	// DISPPARAMS
	DISPPARAMS dispParams;
	dispParams.rgvarg = pvArgs;
	dispParams.cArgs = abs(nArgs);
	DISPID dispidName = DISPID_PROPERTYPUT;
	if (wFlags & DISPATCH_PROPERTYPUT) {
		dispParams.cNamedArgs = 1;
		dispParams.rgdispidNamedArgs = &dispidName;
	} else {
		dispParams.rgdispidNamedArgs = NULL;
		dispParams.cNamedArgs = 0;
	}
	try {
		hr = pdisp->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT,
			wFlags, &dispParams, pvResult, NULL, NULL);
	} catch (...) {
		hr = E_FAIL;
	}
	teClearVariantArgs(nArgs, pvArgs);
	return hr;
}

HRESULT Invoke4(IDispatch *pdisp, VARIANT *pvResult, int nArgs, VARIANTARG *pvArgs)
{
	return Invoke5(pdisp, DISPID_VALUE, DISPATCH_METHOD, pvResult, nArgs, pvArgs);
}

VARIANTARG* GetNewVARIANT(int n)
{
	VARIANT *pv = new VARIANTARG[n];
	while (n--) {
		VariantInit(&pv[n]);
	}
	return pv;
}

BOOL teFileTimeToVariantTime(LPFILETIME pft, DOUBLE *pdt)
{
	FILETIME ft;
	if (::FileTimeToLocalFileTime(pft, &ft)) {
		SYSTEMTIME SysTime;
		if (::FileTimeToSystemTime(&ft, &SysTime)) {
			return ::SystemTimeToVariantTime(&SysTime, pdt);
		}
	}
	return FALSE;
}

VOID teSetSusieTime(VARIANT *pv, susie_time_t t)
{
    FILETIME ft;
    LONGLONG ll = Int32x32To64(t, 10000000) + 116444736000000000;
    ft.dwLowDateTime = (DWORD)ll;
    ft.dwHighDateTime = (DWORD)(ll >> 32);
	if (teFileTimeToVariantTime(&ft, &pv->date)) {
		pv->vt = VT_DATE;
	}
}

VOID teSetSusieFileInfoW(IUnknown *punk, SUSIE_FINFOTW *pfinfo)
{
	if (punk) {
		VARIANT v;
		VariantClear(&v);
		teSetSZA(&v, (LPCSTR)pfinfo->method, CP_ACP);
		tePutProperty(punk, L"method", &v);
		VariantClear(&v);
		teSetPtr(&v, pfinfo->position);
		tePutProperty(punk, L"position", &v);
		VariantClear(&v);
		teSetPtr(&v, pfinfo->compsize);
		tePutProperty(punk, L"compsize", &v);
		VariantClear(&v);
		teSetPtr(&v, pfinfo->filesize);
		tePutProperty(punk, L"filesize", &v);
		VariantClear(&v);
		teSetSusieTime(&v, pfinfo->timestamp);
		tePutProperty(punk, L"timestamp", &v);
		VariantClear(&v);
		teSetSZ(&v, pfinfo->path);
		tePutProperty(punk, L"path", &v);
		VariantClear(&v);
		teSetSZ(&v, pfinfo->filename);
		tePutProperty(punk, L"filename", &v);
		VariantClear(&v);
		teSetLong(&v, pfinfo->crc);
		tePutProperty(punk, L"crc", &v);
		VariantClear(&v);
	}
}

VOID teSetSusieFileInfoA(IUnknown *punk, SUSIE_FINFO *pfinfo)
{
	if (punk) {
		VARIANT v;
		VariantClear(&v);
		teSetSZA(&v, (LPCSTR)pfinfo->method, CP_ACP);
		tePutProperty(punk, L"method", &v);
		VariantClear(&v);
		teSetPtr(&v, pfinfo->position);
		tePutProperty(punk, L"position", &v);
		VariantClear(&v);
		teSetPtr(&v, pfinfo->compsize);
		tePutProperty(punk, L"compsize", &v);
		VariantClear(&v);
		teSetPtr(&v, pfinfo->filesize);
		tePutProperty(punk, L"filesize", &v);
		VariantClear(&v);
		teSetSusieTime(&v, pfinfo->timestamp);
		tePutProperty(punk, L"timestamp", &v);
		VariantClear(&v);
		teSetSZA(&v, pfinfo->path, CP_ACP);
		tePutProperty(punk, L"path", &v);
		VariantClear(&v);
		teSetSZA(&v, pfinfo->filename, CP_ACP);
		tePutProperty(punk, L"filename", &v);
		VariantClear(&v);
		teSetLong(&v, pfinfo->crc);
		tePutProperty(punk, L"crc", &v);
		VariantClear(&v);
	}
}

BOOL GetDispatch(VARIANT *pv, IDispatch **ppdisp)
{
	IUnknown *punk;
	if (FindUnknown(pv, &punk)) {
		return SUCCEEDED(punk->QueryInterface(IID_PPV_ARGS(ppdisp)));
	}
	return FALSE;
}

HRESULT teGetProperty(IDispatch *pdisp, LPOLESTR sz, VARIANT *pv)
{
	DISPID dispid;
	HRESULT hr = pdisp->GetIDsOfNames(IID_NULL, &sz, 1, LOCALE_USER_DEFAULT, &dispid);
	if (hr == S_OK) {
		hr = Invoke5(pdisp, dispid, DISPATCH_PROPERTYGET, pv, 0, NULL);
	}
	return hr;
}

int __stdcall tspi_ProgressCallback(int nNum, int nDenom, LONG_PTR lData)
{
	if (g_pdispProgressProc) {
		VARIANT vResult;
		VariantInit(&vResult);
		VARIANTARG *pv = GetNewVARIANT(3);
		teSetLong(&pv[3], nNum);
		teSetLong(&pv[3], nDenom);
		teSetPtr(&pv[0], lData);
		if SUCCEEDED(Invoke4(g_pdispProgressProc, &vResult, 3, pv)) {
			return GetIntFromVariantClear(&vResult);
		}
	}
	return SPI_ALL_RIGHT;
}

VOID teVariantChangeType(__out VARIANTARG * pvargDest,
				__in const VARIANTARG * pvarSrc, __in VARTYPE vt)
{
	VariantInit(pvargDest);
	if FAILED(VariantChangeType(pvargDest, pvarSrc, 0, vt)) {
		pvargDest->llVal = 0;
	}
}

BSTR teWide2Ansi(LPWSTR lpW, int nLenW)
{
	if (lpW) {
		int nLenA = WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)lpW, nLenW, NULL, 0, NULL, NULL);
		BSTR bs = ::SysAllocStringByteLen(NULL, nLenA);
		WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)lpW, nLenW, (LPSTR)bs, nLenA, NULL, NULL);
		LPSTR lp = (LPSTR)bs;
		return bs;
	}
	return NULL;
}

HRESULT teExecMethod(IDispatch *pdisp, LPOLESTR sz, VARIANT *pvResult, int nArg, VARIANTARG *pvArgs)
{
	DISPID dispid;
	HRESULT hr = pdisp->GetIDsOfNames(IID_NULL, &sz, 1, LOCALE_USER_DEFAULT, &dispid);
	if (hr == S_OK) {
		return Invoke5(pdisp, dispid, DISPATCH_METHOD, pvResult, nArg, pvArgs);
	}
	teClearVariantArgs(nArg, pvArgs);
	return hr;
}

// Initialize & Finalize
BOOL WINAPI DllMain(HINSTANCE hinstDll, DWORD dwReason, LPVOID lpReserved)
{
	switch (dwReason) {
		case DLL_PROCESS_ATTACH:
			for (int i = MAX_OBJ; i--;) {
				g_ppObject[i] = NULL;
			}
			g_pBase = new CteBase();
			g_hinstDll = hinstDll;
			break;
		case DLL_PROCESS_DETACH:
			for (int i = MAX_OBJ; i--;) {
				if (g_ppObject[i]) {
					g_ppObject[i]->Close();
					SafeRelease(&g_ppObject[i]);
				}
			}
			SafeRelease(&g_pBase);
			SafeRelease(&g_pdispProgressProc);
			break;
	}
	return TRUE;
}

// DLL Export

STDAPI DllCanUnloadNow(void)
{
	return g_lLocks == 0 ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID *ppv)
{
	static CteClassFactory serverFactory;
	CLSID clsid;
	HRESULT hr = CLASS_E_CLASSNOTAVAILABLE;

	*ppv = NULL;
	CLSIDFromString(g_szClsid, &clsid);
	if (IsEqualCLSID(rclsid, clsid)) {
		hr = serverFactory.QueryInterface(riid, ppv);
	}
	return hr;
}

STDAPI DllRegisterServer(void)
{
	TCHAR szModulePath[MAX_PATH];
	TCHAR szKey[256];

	wsprintf(szKey, TEXT("CLSID\\%s"), g_szClsid);
	LSTATUS lr = CreateRegistryKey(HKEY_CLASSES_ROOT, szKey, NULL, const_cast<LPTSTR>(g_szProgid));
	if (lr != ERROR_SUCCESS) {
		return ShowRegError(lr);
	}
	GetModuleFileName(g_hinstDll, szModulePath, sizeof(szModulePath) / sizeof(TCHAR));
	wsprintf(szKey, TEXT("CLSID\\%s\\InprocServer32"), g_szClsid);
	lr = CreateRegistryKey(HKEY_CLASSES_ROOT, szKey, NULL, szModulePath);
	if (lr != ERROR_SUCCESS) {
		return ShowRegError(lr);
	}
	lr = CreateRegistryKey(HKEY_CLASSES_ROOT, szKey, TEXT("ThreadingModel"), TEXT("Apartment"));
	if (lr != ERROR_SUCCESS) {
		return ShowRegError(lr);
	}
	wsprintf(szKey, TEXT("CLSID\\%s\\ProgID"), g_szClsid);
	lr = CreateRegistryKey(HKEY_CLASSES_ROOT, szKey, NULL, const_cast<LPTSTR>(g_szProgid));
	if (lr != ERROR_SUCCESS) {
		return ShowRegError(lr);
	}
	lr = CreateRegistryKey(HKEY_CLASSES_ROOT, const_cast<LPTSTR>(g_szProgid), NULL, TEXT(PRODUCTNAME));
	if (lr != ERROR_SUCCESS) {
		return ShowRegError(lr);
	}
	wsprintf(szKey, TEXT("%s\\CLSID"), g_szProgid);
	lr = CreateRegistryKey(HKEY_CLASSES_ROOT, szKey, NULL, const_cast<LPTSTR>(g_szClsid));
	if (lr != ERROR_SUCCESS) {
		return ShowRegError(lr);
	}
	return S_OK;
}

STDAPI DllUnregisterServer(void)
{
	TCHAR szKey[64];
	wsprintf(szKey, TEXT("CLSID\\%s"), g_szClsid);
	LSTATUS ls = SHDeleteKey(HKEY_CLASSES_ROOT, szKey);
	if (ls == ERROR_SUCCESS) {
		ls = SHDeleteKey(HKEY_CLASSES_ROOT, g_szProgid);
		if (ls == ERROR_SUCCESS) {
			return S_OK;
		}
	}
	return ShowRegError(ls);
}

//CteSPI

CteSPI::CteSPI(HMODULE hDll, LPWSTR lpLib)
{
	m_cRef = 1;
	m_hDll = hDll;
	m_bsLib = ::SysAllocString(lpLib);

	teGetProcAddress(m_hDll, "GetPluginInfo", (FARPROC *)&GetPluginInfo, (FARPROC *)&GetPluginInfoW);
	teGetProcAddress(m_hDll, "IsSupported", (FARPROC *)&IsSupported, (FARPROC *)&IsSupportedW);
	teGetProcAddress(m_hDll, "GetPictureInfo", (FARPROC *)&GetPictureInfo, (FARPROC *)&GetPictureInfoW);
	teGetProcAddress(m_hDll, "GetPicture", (FARPROC *)&GetPicture, (FARPROC *)&GetPictureW);
	teGetProcAddress(m_hDll, "GetPreview", (FARPROC *)&GetPreview, (FARPROC *)&GetPreviewW);
	teGetProcAddress(m_hDll, "GetArchiveInfo", (FARPROC *)&GetArchiveInfo, (FARPROC *)&GetArchiveInfoW);
	teGetProcAddress(m_hDll, "GetFileInfo", (FARPROC *)&GetFileInfo, (FARPROC *)&GetFileInfoW);
	teGetProcAddress(m_hDll, "GetFile", (FARPROC *)&GetFile, (FARPROC *)&GetFileW);
	teGetProcAddress(m_hDll, "ConfigurationDlg", (FARPROC *)&ConfigurationDlg, NULL);
}

CteSPI::~CteSPI()
{
	Close();
	for (int i = MAX_OBJ; i--;) {
		if (this == g_ppObject[i]) {
			g_ppObject[i] = NULL;
			break;
		}
	}
}

VOID CteSPI::Close()
{
	if (m_hDll) {
		FreeLibrary(m_hDll);
		m_hDll = NULL;
	}
	GetPluginInfo = NULL;
	IsSupported = NULL;
	GetPictureInfo = NULL;
	GetPicture = NULL;
	GetPreview = NULL;
	GetArchiveInfo = NULL;
	GetFileInfo = NULL;
	GetFile = NULL;
	ConfigurationDlg = NULL;
}

STDMETHODIMP CteSPI::QueryInterface(REFIID riid, void **ppvObject)
{
	static const QITAB qit[] =
	{
		QITABENT(CteSPI, IDispatch),
		{ 0 },
	};
	return QISearch(this, qit, riid, ppvObject);
}

STDMETHODIMP_(ULONG) CteSPI::AddRef()
{
	return ::InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CteSPI::Release()
{
	if (::InterlockedDecrement(&m_cRef) == 0) {
		delete this;
		return 0;
	}
	return m_cRef;
}

STDMETHODIMP CteSPI::GetTypeInfoCount(UINT *pctinfo)
{
	*pctinfo = 0;
	return S_OK;
}

STDMETHODIMP CteSPI::GetTypeInfo(UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo)
{
	return E_NOTIMPL;
}

STDMETHODIMP CteSPI::GetIDsOfNames(REFIID riid, LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId)
{
	return teGetDispId(methodTSPI, _countof(methodTSPI), NULL, *rgszNames, rgDispId);
}

STDMETHODIMP CteSPI::Invoke(DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
	int iResult = SPI_NO_FUNCTION;
	int nArg = pDispParams ? pDispParams->cArgs - 1 : -1;
	try {
		switch (dispIdMember) {
			//GetPluginInfo
			case 0x60010001:
				if (nArg >= 0) {
					IUnknown *punk;
					if (FindUnknown(&pDispParams->rgvarg[nArg], &punk)) {
						VARIANT v;
						VariantInit(&v);
						BOOL bLoop = TRUE;
						WCHAR pszBuf[SIZE_BUFF];
						for (int i = 0; bLoop; i++) {
							if (GetPluginInfoW) {
								if (GetPluginInfoW(i, pszBuf, SIZE_BUFF)) {
									teSetSZ(&v, pszBuf);
								}
							} else if (GetPluginInfo) {
								if (GetPluginInfo(i, (LPSTR)pszBuf, SIZE_BUFF)) {
									teSetSZA(&v, (LPCSTR)pszBuf, CP_ACP);
								}
							}
							bLoop = v.vt == VT_BSTR;
							if (bLoop) {
								wsprintf(pszBuf, L"%d", i);
								tePutProperty(punk, pszBuf, &v);
							}
							VariantClear(&v);
						}
					}
					teSetLong(pVarResult, iResult);
				} else if (wFlags == DISPATCH_PROPERTYGET) {
					teSetBool(pVarResult, GetPluginInfoW || GetPluginInfo);
				}
				return S_OK;
			//IsSupported
			case 0x60010002:
				if (nArg >= 1) {
					LPWSTR lpPath = GetLPWSTRFromVariant(&pDispParams->rgvarg[nArg]);
					BOOL bDelete = FALSE;
					BSTR pdw = GetMemoryFromVariant(&pDispParams->rgvarg[nArg - 1], &bDelete, NULL);
					iResult = 0;
					if (IsSupportedW) {
						iResult = IsSupportedW(lpPath, (void *)pdw);
					} else if (IsSupported) {
						BSTR bsPath = teWide2Ansi(lpPath, -1);
						iResult = IsSupported((char *)bsPath, (void *)pdw);
						teSysFreeString(&bsPath);
					}
					if (bDelete) {
						teSysFreeString(&pdw);
					}
					teSetLong(pVarResult, iResult);
				} else if (wFlags == DISPATCH_PROPERTYGET) {
					teSetBool(pVarResult, IsSupportedW || IsSupported);
				}
				return S_OK;
			//GetPictureInfo
			case 0x60010003:
				if (nArg >= 3) {
					BOOL bDelete = FALSE;
					LONG_PTR len = GetPtrFromVariant(&pDispParams->rgvarg[nArg - 1]);
					BSTR lpBuf = GetMemoryFromVariant(&pDispParams->rgvarg[nArg], &bDelete, &len);
					int flag = GetIntFromVariant(&pDispParams->rgvarg[nArg - 2]);
					IUnknown *punk;
					if (FindUnknown(&pDispParams->rgvarg[nArg - 3], &punk)) {
						PictureInfo Info;
						if (GetPictureInfoW) {
							iResult = GetPictureInfoW(lpBuf, len, flag, &Info);
						} else if (GetPictureInfo) {
							if (bDelete) {
								iResult = GetPictureInfo((LPSTR)lpBuf, len, flag, &Info);
							} else {
								BSTR bsBufA = teWide2Ansi(lpBuf, -1);
								iResult = GetPictureInfo((LPSTR)bsBufA, len, flag, &Info);
								teSysFreeString(&bsBufA);
							}
						}
						if (iResult == SPI_ALL_RIGHT) {
							VARIANT v;
							VariantInit(&v);
							teSetLong(&v, Info.left);
							tePutProperty(punk, L"left", &v);
							VariantClear(&v);
							teSetLong(&v, Info.top);
							tePutProperty(punk, L"top", &v);
							VariantClear(&v);
							teSetLong(&v, Info.width);
							tePutProperty(punk, L"width", &v);
							VariantClear(&v);
							teSetLong(&v, Info.height);
							tePutProperty(punk, L"height", &v);
							VariantClear(&v);
							teSetLong(&v, Info.x_density);
							tePutProperty(punk, L"x_density", &v);
							VariantClear(&v);
							teSetLong(&v, Info.y_density);
							tePutProperty(punk, L"y_density", &v);
							VariantClear(&v);
							teSetLong(&v, Info.colorDepth);
							tePutProperty(punk, L"colorDepth", &v);
							VariantClear(&v);
							if (Info.hInfo) {
								teSetSZA(&v, (LPCSTR)Info.hInfo, CP_ACP);
								LocalFree(Info.hInfo);
							}
							tePutProperty(punk, L"hInfo", &v);
							VariantClear(&v);
						}
					}
					if (bDelete) {
						teSysFreeString(&lpBuf);
					}
					teSetLong(pVarResult, iResult);
				} else if (wFlags == DISPATCH_PROPERTYGET) {
					teSetBool(pVarResult, GetPictureInfoW || GetPictureInfo);
				}
				return S_OK;
			//GetPicture
			case 0x60010004:
			//GetPreview
			case 0x60010005:
				if (nArg >= 6) {
					BOOL bDelete = FALSE;
					LONG_PTR len = GetPtrFromVariant(&pDispParams->rgvarg[nArg - 1]);
					BSTR lpBuf = GetMemoryFromVariant(&pDispParams->rgvarg[nArg],  &bDelete, &len);
					int flag = GetIntFromVariant(&pDispParams->rgvarg[nArg - 2]);
					int lData = GetIntFromVariant(&pDispParams->rgvarg[nArg - 6]);
					IUnknown *punkBM;
					if (FindUnknown(&pDispParams->rgvarg[nArg - 4], &punkBM)) {
						IDispatch *pdispInfo = NULL;
						GetDispatch(&pDispParams->rgvarg[nArg - 3], &pdispInfo);
						SafeRelease(&g_pdispProgressProc);
						GetDispatch(&pDispParams->rgvarg[nArg - 5], &g_pdispProgressProc);
						HLOCAL hInfo = NULL;
						HLOCAL hBMData = NULL;
						if (dispIdMember == 0x60010004 && GetPictureW) {
							iResult = GetPictureW(lpBuf, len, flag, &hInfo, &hBMData, tspi_ProgressCallback, lData);
						} else if (dispIdMember == 0x60010005 && GetPreviewW) {
							iResult = GetPreviewW(lpBuf, len, flag, &hInfo, &hBMData, tspi_ProgressCallback, lData);
						} else if (dispIdMember == 0x60010004 && GetPicture) {
							if (bDelete) {
								iResult = GetPicture((LPSTR)lpBuf, len, flag, &hInfo, &hBMData, tspi_ProgressCallback, lData);
							} else {
								BSTR bsBufA = teWide2Ansi(lpBuf, -1);
								iResult = GetPicture((LPSTR)bsBufA, len, flag, &hInfo, &hBMData, tspi_ProgressCallback, lData);
								teSysFreeString(&bsBufA);
							}
						} else if (dispIdMember == 0x60010005 && GetPreview) {
							if (bDelete) {
								iResult = GetPreview((LPSTR)lpBuf, len, flag, &hInfo, &hBMData, NULL, lData);
							} else {
								BSTR bsBufA = teWide2Ansi(lpBuf, -1);
								iResult = GetPreview((LPSTR)bsBufA, len, flag, &hInfo, &hBMData, NULL, lData);
								teSysFreeString(&bsBufA);
							}
						}
						VARIANT v, vX;
						VariantInit(&vX);
						VariantInit(&v);
						if (iResult == SPI_ALL_RIGHT) {
							PBITMAPINFO lpbmi = (PBITMAPINFO)LocalLock(hInfo);
							if (lpbmi) {
								try {
									VOID *lpBits;
									HBITMAP hBM =  CreateDIBSection(NULL, lpbmi, DIB_RGB_COLORS, &lpBits, NULL, 0);
									if (hBM) {
										PBYTE lpbm = (PBYTE)LocalLock(hBMData);
										if (lpbm) {
											try {
												SetDIBits(NULL, hBM, 0, lpbmi->bmiHeader.biHeight, lpbm, lpbmi, DIB_RGB_COLORS);
											} catch (...) {}
										}
										LocalUnlock(hBMData);
										LocalFree(hBMData);
									}
									teSetPtr(&v, hBM);
									tePutProperty(punkBM, L"0",  &v);
									VariantClear(&v);
									if (pdispInfo) {
										if SUCCEEDED(teGetProperty(pdispInfo, L"bmiHeader", &vX)) {
											teSetLong(&v, lpbmi->bmiHeader.biSize);
											tePutProperty(vX.punkVal, L"biSize", &v);
											VariantClear(&v);
											teSetLong(&v, lpbmi->bmiHeader.biWidth);
											tePutProperty(vX.punkVal, L"biWidth", &v);
											VariantClear(&v);
											teSetLong(&v, lpbmi->bmiHeader.biHeight);
											tePutProperty(vX.punkVal, L"biHeight", &v);
											VariantClear(&v);
											teSetLong(&v, lpbmi->bmiHeader.biPlanes);
											tePutProperty(vX.punkVal, L"biPlanes", &v);
											VariantClear(&v);
											teSetLong(&v, lpbmi->bmiHeader.biBitCount);
											tePutProperty(vX.punkVal, L"biBitCount", &v);
											VariantClear(&v);
											teSetLong(&v, lpbmi->bmiHeader.biCompression);
											tePutProperty(vX.punkVal, L"biCompression", &v);
											VariantClear(&v);
											teSetLong(&v, lpbmi->bmiHeader.biSizeImage);
											tePutProperty(vX.punkVal, L"biSizeImage", &v);
											VariantClear(&v);
											teSetLong(&v, lpbmi->bmiHeader.biXPelsPerMeter);
											tePutProperty(vX.punkVal, L"biXPelsPerMeter", &v);
											VariantClear(&v);
											teSetLong(&v, lpbmi->bmiHeader.biYPelsPerMeter);
											tePutProperty(vX.punkVal, L"biYPelsPerMeter", &v);
											VariantClear(&v);
											teSetLong(&v, lpbmi->bmiHeader.biClrUsed);
											tePutProperty(vX.punkVal, L"biClrUsed", &v);
											VariantClear(&v);
											teSetLong(&v, lpbmi->bmiHeader.biClrImportant);
											tePutProperty(vX.punkVal, L"biClrImportant", &v);
											VariantClear(&v);
											VariantClear(&vX);
										}
										if SUCCEEDED(teGetProperty(pdispInfo, L"bmiColors", &vX)) {
											WCHAR pszBuf[9];
											for (DWORD i = 0; i < lpbmi->bmiHeader.biClrUsed; i++) {
												wsprintf(pszBuf, L"%d", i);
												teSetLong(&v, *(LONG *)&lpbmi->bmiColors[i]);
												tePutProperty(vX.punkVal, pszBuf, &v);
												VariantClear(&v);
											}
											VariantClear(&vX);
										}
									}
								} catch (...) {}
								LocalUnlock(hInfo);
								LocalFree(hInfo);
							}
						}
						SafeRelease(&pdispInfo);
						teSetLong(pVarResult, iResult);
					}
					if (bDelete) {
						teSysFreeString(&lpBuf);
					}
				} else if (wFlags == DISPATCH_PROPERTYGET) {
					teSetBool(pVarResult, dispIdMember == 0x60010004 ? GetPictureInfoW || GetPictureInfo : GetPreviewW || GetPreview);
				}
				return S_OK;
			//GetArchiveInfo
			case 0x60010006:
				if (nArg >= 4) {
					BOOL bDelete = FALSE;
					LONG_PTR len = GetPtrFromVariant(&pDispParams->rgvarg[nArg - 1]);
					BSTR lpBuf = GetMemoryFromVariant(&pDispParams->rgvarg[nArg], &bDelete, &len);
					int flag = GetIntFromVariant(&pDispParams->rgvarg[nArg - 2]);
					IDispatch *pList;
					if (GetDispatch(&pDispParams->rgvarg[nArg - 3], &pList)) {
						IDispatch *pdisp;
						if (GetDispatch(&pDispParams->rgvarg[nArg - 4], &pdisp)) {
							HLOCAL hInfo = NULL;
							VARIANT v, vX;
							VariantInit(&v);
							if (GetArchiveInfoW) {
								iResult = GetArchiveInfoW(lpBuf, len, flag, &hInfo);
								if (iResult == SPI_ALL_RIGHT) {
									SUSIE_FINFOTW *pfinfo = (SUSIE_FINFOTW *)hInfo;
									for (int i = 0; pfinfo[i].method[0]; i++) {
										if SUCCEEDED(Invoke5(pdisp, DISPID_VALUE, DISPATCH_METHOD, &v, 0, NULL)) {
											if (v.vt == VT_DISPATCH) {
												teSetSusieFileInfoW(v.pdispVal, &pfinfo[i]);
												teExecMethod(pList, L"push", NULL, -1, &v);
												VariantClear(&v);
											}
										}
									}
								}
							} else if (GetArchiveInfo) {
								if (bDelete) {
									iResult = GetArchiveInfo((LPSTR)lpBuf, len, flag, &hInfo);
								} else {
									BSTR bsBufA = teWide2Ansi(lpBuf, -1);
									iResult = GetArchiveInfo((LPSTR)bsBufA, len, flag, &hInfo);
									teSysFreeString(&bsBufA);
								}
								if (iResult == SPI_ALL_RIGHT) {
									SUSIE_FINFO *pfinfo = (SUSIE_FINFO *)hInfo;
									for (int i = 0; pfinfo[i].method[0]; i++) {
										if SUCCEEDED(Invoke5(pdisp, DISPID_VALUE, DISPATCH_METHOD, &vX, 0, NULL)) {
											if (v.vt == VT_DISPATCH) {
												teSetSusieFileInfoA(v.pdispVal, &pfinfo[i]);
												teExecMethod(pList, L"push", NULL, -1, &v);
												VariantClear(&v);
											}
										}
									}
								}
							}
							pdisp->Release();
						}
						pList->Release();
					}
					if (bDelete) {
						teSysFreeString(&lpBuf);
					}
					teSetLong(pVarResult, iResult);
				} else if (wFlags == DISPATCH_PROPERTYGET) {
					teSetBool(pVarResult, GetArchiveInfoW || GetArchiveInfo);
				}
				return S_OK;
			//GetFileInfo
			case 0x60010007:
				if (nArg >= 4) {
					BOOL bDelete = FALSE;
					LONG_PTR len = GetPtrFromVariant(&pDispParams->rgvarg[nArg - 1]);
					BSTR lpBuf = GetMemoryFromVariant(&pDispParams->rgvarg[nArg], &bDelete, &len);
					BSTR lpfilename = GetLPWSTRFromVariant(&pDispParams->rgvarg[nArg - 2]);
					int flag = GetIntFromVariant(&pDispParams->rgvarg[nArg - 3]);

					IUnknown *punk;
					if (FindUnknown(&pDispParams->rgvarg[nArg - 4], &punk)) {
						HLOCAL hInfo = NULL;
						if (GetFileInfoW) {
							SUSIE_FINFOTW finfo;
							iResult = GetFileInfoW(lpBuf, len, lpfilename, flag, &finfo);
							if (iResult == SPI_ALL_RIGHT) {
								teSetSusieFileInfoW(punk, &finfo);
							}
						} else if (GetArchiveInfo) {
							SUSIE_FINFO finfo;
							BSTR bsFilenameA = teWide2Ansi(lpfilename, -1);
							if (bDelete) {
								iResult = GetFileInfo((LPSTR)lpBuf, len, (LPCSTR)bsFilenameA, flag, &finfo);
							} else {
								BSTR bsBufA = teWide2Ansi(lpBuf, -1);
								iResult = GetFileInfo((LPSTR)bsBufA, len, (LPCSTR)bsFilenameA, flag, &finfo);
								teSysFreeString(&bsBufA);
							}
							teSysFreeString(&bsFilenameA);
							if (iResult == SPI_ALL_RIGHT) {
								teSetSusieFileInfoA(punk, &finfo);
							}
						}
					}
					if (bDelete) {
						teSysFreeString(&lpBuf);
					}
					teSetLong(pVarResult, iResult);
				} else if (wFlags == DISPATCH_PROPERTYGET) {
					teSetBool(pVarResult, GetFileInfoW || GetFileInfo);
				}
				return S_OK;
			//GetFile
			case 0x60010008:
				if (nArg >= 5) {
					BOOL bDelete = FALSE;
					LONG_PTR len = GetPtrFromVariant(&pDispParams->rgvarg[nArg - 1]);
					BSTR lpBuf = GetMemoryFromVariant(&pDispParams->rgvarg[nArg], &bDelete, &len);
					BSTR lpDest = GetLPWSTRFromVariant(&pDispParams->rgvarg[nArg - 2]);
					int flag = GetIntFromVariant(&pDispParams->rgvarg[nArg - 3]);
					HLOCAL hLocal = NULL;
					IUnknown *punk = NULL;
					if (FindUnknown(&pDispParams->rgvarg[nArg - 2], &punk)) {
						lpDest = (BSTR)&hLocal;
					}
					SafeRelease(&g_pdispProgressProc);
					GetDispatch(&pDispParams->rgvarg[nArg - 4], &g_pdispProgressProc);
					LONG_PTR lData = GetPtrFromVariant(&pDispParams->rgvarg[nArg - 5]);
					if (GetFileW) {
						iResult = GetFileW(lpBuf, len, lpDest, flag, NULL, lData);
					} else if (GetFile) {
						if (!punk) {
							lpDest = teWide2Ansi(lpDest, -1);
						}
						if (bDelete) {
							iResult = GetFile((LPSTR)lpBuf, len, (LPSTR)lpDest, flag, tspi_ProgressCallback, lData);
						} else {
							BSTR bsBufA = teWide2Ansi(lpBuf, -1);
							iResult = GetFile((LPSTR)bsBufA, len, (LPSTR)lpDest, flag, tspi_ProgressCallback, lData);
							teSysFreeString(&bsBufA);
						}
						if (!punk) {
							teSysFreeString(&lpDest);
						}
					}
					if (bDelete) {
						teSysFreeString(&lpBuf);
					}
					if (iResult == SPI_ALL_RIGHT && punk && hLocal) {
						PBYTE lpData = (PBYTE)LocalLock(hLocal);
						if (lpData) {
							IStream *pStream = SHCreateMemStream(NULL, NULL);
							if (pStream) {
								pStream->Write(lpData, LocalSize(hLocal), NULL);
								VARIANT v;
								VariantInit(&v);
								teSetObjectRelease(&v, pStream);
								tePutProperty(punk, L"0", &v);
								VariantClear(&v);
							}
							LocalUnlock(hLocal);
							LocalFree(hLocal);
						}
					}
					teSetLong(pVarResult, iResult);
				} else if (wFlags == DISPATCH_PROPERTYGET) {
					teSetBool(pVarResult, GetFileInfoW || GetFileInfo);
				}
				return S_OK;
			case 0x60010009:
			//ConfigurationDlg
				if (nArg >= 1) {
					if (ConfigurationDlg) {
						ConfigurationDlg((HWND)GetPtrFromVariant(&pDispParams->rgvarg[nArg]), GetIntFromVariant(&pDispParams->rgvarg[nArg - 1]));
					}
					teSetLong(pVarResult, iResult);
				} else if (wFlags == DISPATCH_PROPERTYGET) {
					teSetBool(pVarResult, ConfigurationDlg != NULL);
				}
				return S_OK;
			//IsUnicode
			case 0x6001FFFF:
				teSetBool(pVarResult, GetPluginInfoW != NULL);
				return S_OK;
			//this
			case DISPID_VALUE:
				if (pVarResult) {
					teSetObject(pVarResult, this);
				}
				return S_OK;
		}//end_switch
	} catch (...) {
		return DISP_E_EXCEPTION;
	}
	return DISP_E_MEMBERNOTFOUND;
}

//CteBase

CteBase::CteBase()
{
	m_cRef = 1;
}

CteBase::~CteBase()
{
}

STDMETHODIMP CteBase::QueryInterface(REFIID riid, void **ppvObject)
{
	static const QITAB qit[] =
	{
		QITABENT(CteBase, IDispatch),
		{ 0 },
	};
	return QISearch(this, qit, riid, ppvObject);
}

STDMETHODIMP_(ULONG) CteBase::AddRef()
{
	return ::InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CteBase::Release()
{
	if (::InterlockedDecrement(&m_cRef) == 0) {
		delete this;
		return 0;
	}
	return m_cRef;
}

STDMETHODIMP CteBase::GetTypeInfoCount(UINT *pctinfo)
{
	*pctinfo = 0;
	return S_OK;
}

STDMETHODIMP CteBase::GetTypeInfo(UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo)
{
	return E_NOTIMPL;
}

STDMETHODIMP CteBase::GetIDsOfNames(REFIID riid, LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId)
{
	return teGetDispId(methodBASE, _countof(methodBASE), NULL, *rgszNames, rgDispId);
}

STDMETHODIMP CteBase::Invoke(DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
	int nArg = pDispParams ? pDispParams->cArgs - 1 : -1;
	HRESULT hr = S_OK;

	switch (dispIdMember) {
		//Open
		case 0x60010000:
			if (nArg >= 0) {
				LPWSTR lpLib = GetLPWSTRFromVariant(&pDispParams->rgvarg[nArg]);

				int nEmpty = -1;
				CteSPI *pItem;
				for (int i = MAX_OBJ; i--;) {
					pItem = g_ppObject[i];
					if (pItem) {
						if (lstrcmpi(lpLib, pItem->m_bsLib) == 0) {
							teSetObject(pVarResult, pItem);
							return S_OK;
						}
					} else if (nEmpty < 0) {
						nEmpty = i;
					}
				}
				if (nEmpty >= 0) {
					HMODULE hDll = LoadLibrary(lpLib);
					if (hDll) {
						pItem = new CteSPI(hDll, lpLib);
						g_ppObject[nEmpty] = pItem;
						teSetObjectRelease(pVarResult, pItem);
					}
				}
			}
			return S_OK;
		//Close
		case 0x6001000C:
			if (nArg >= 0) {
				LPWSTR lpLib = GetLPWSTRFromVariant(&pDispParams->rgvarg[nArg]);

				for (int i = MAX_OBJ; i--;) {
					if (g_ppObject[i]) {
						if (lstrcmpi(lpLib, g_ppObject[i]->m_bsLib) == 0) {
							g_ppObject[i]->Close();
							SafeRelease(&g_ppObject[i]);
							break;
						}
					}
				}
			}
			return S_OK;
		//this
		case DISPID_VALUE:
			if (pVarResult) {
				teSetObject(pVarResult, this);
			}
			return S_OK;
	}//end_switch
	return DISP_E_MEMBERNOTFOUND;
}

// CteClassFactory

STDMETHODIMP CteClassFactory::QueryInterface(REFIID riid, void **ppvObject)
{
	static const QITAB qit[] =
	{
		QITABENT(CteClassFactory, IClassFactory),
		{ 0 },
	};
	return QISearch(this, qit, riid, ppvObject);
}

STDMETHODIMP_(ULONG) CteClassFactory::AddRef()
{
	LockModule(TRUE);
	return 2;
}

STDMETHODIMP_(ULONG) CteClassFactory::Release()
{
	LockModule(FALSE);
	return 1;
}

STDMETHODIMP CteClassFactory::CreateInstance(IUnknown *pUnkOuter, REFIID riid, void **ppvObject)
{
	*ppvObject = NULL;

	if (pUnkOuter != NULL) {
		return CLASS_E_NOAGGREGATION;
	}
	return g_pBase->QueryInterface(riid, ppvObject);
}

STDMETHODIMP CteClassFactory::LockServer(BOOL fLock)
{
	LockModule(fLock);
	return S_OK;
}
