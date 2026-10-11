// Desktop Studio COM registration/factory for the OBS-derived camera filter.
// Registration is confined to this user's Desktop Studio CLSID and category.
#include "virtualcam-filter.hpp"
#include <string>
#include <atomic>
HINSTANCE dll_inst = nullptr;
volatile long locks = 0;
static const CLSID cameraClass = {0x71bd0d43,0x5eb7,0x4e61,{0xa6,0x4a,0x25,0xd5,0x44,0x37,0x32,0x97}};
static const wchar_t *classPath = L"Software\\Classes\\CLSID\\{71BD0D43-5EB7-4E61-A64A-25D544373297}";
static const wchar_t *categoryPath = L"Software\\Classes\\CLSID\\{860BB310-5D01-11D0-BD3B-00A0C911CE86}\\Instance\\{71BD0D43-5EB7-4E61-A64A-25D544373297}";
class Factory final : public IClassFactory {
    std::atomic<ULONG> refs{1};
public:
    Factory() { InterlockedIncrement(&locks); }
    ~Factory() { InterlockedDecrement(&locks); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IClassFactory) return E_NOINTERFACE;
        *out = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG value = --refs; if (!value) delete this; return value; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown *outer, REFIID iid, void **out) override {
        if (!out) return E_POINTER; *out = nullptr; if (outer) return CLASS_E_NOAGGREGATION;
        auto *filter = new VCamFilter; const HRESULT result = filter->QueryInterface(iid, out); filter->Release(); return result;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override { if (lock) InterlockedIncrement(&locks); else InterlockedDecrement(&locks); return S_OK; }
};
STDAPI DllCanUnloadNow() { return InterlockedCompareExchange(&locks, 0, 0) ? S_FALSE : S_OK; }
STDAPI DllGetClassObject(REFCLSID cls, REFIID iid, void **out) {
    if (!out) return E_POINTER; *out = nullptr; if (cls != cameraClass) return CLASS_E_CLASSNOTAVAILABLE;
    auto *factory = new Factory; const HRESULT result = factory->QueryInterface(iid, out); factory->Release(); return result;
}
static LSTATUS value(const std::wstring &path, const wchar_t *name, const wchar_t *data) {
    HKEY key{}; LSTATUS result = RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    if (result == ERROR_SUCCESS) { result = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(data), DWORD((wcslen(data)+1)*sizeof(wchar_t))); RegCloseKey(key); }
    return result;
}
STDAPI DllRegisterServer() {
    wchar_t file[32768]; if (!GetModuleFileNameW(dll_inst, file, 32768)) return HRESULT_FROM_WIN32(GetLastError());
    LSTATUS result = value(classPath, nullptr, L"Desktop Studio Virtual Camera");
    if (!result) result = value(std::wstring(classPath)+L"\\InprocServer32", nullptr, file);
    if (!result) result = value(std::wstring(classPath)+L"\\InprocServer32", L"ThreadingModel", L"Both");
    if (!result) result = value(categoryPath, L"FriendlyName", L"Desktop Studio Virtual Camera");
    if (!result) result = value(categoryPath, L"CLSID", L"{71BD0D43-5EB7-4E61-A64A-25D544373297}");
    return HRESULT_FROM_WIN32(result);
}
STDAPI DllUnregisterServer() {
    RegDeleteTreeW(HKEY_CURRENT_USER, categoryPath); RegDeleteTreeW(HKEY_CURRENT_USER, classPath); return S_OK;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) { if (reason == DLL_PROCESS_ATTACH) { dll_inst = instance; DisableThreadLibraryCalls(instance); } return TRUE; }
