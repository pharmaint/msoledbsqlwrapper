// msoledbsqlwrapper.cpp : Defines the exported functions for the DLL.
//

#include "framework.h"
#include <windows.h>
#include <shlwapi.h>
#include <iostream>
#include <fstream>
#include <string>
#include <iomanip>
#include <chrono>
#include <tlhelp32.h>
#include <olectl.h>
#include <psapi.h>   // Added for GetModuleBaseNameA
#include <vector>     // Added for std::vector
#include <regex>      // Added for registry-based regex matching

// Must include the MSOLEDBSQL header file.
// This is typically found in the Windows SDK: C:\Program Files (x86)\Windows Kits\10\Include\[version]\um\msoledbsql.h
#include <msoledbsql.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "psapi.lib") // Added for GetModuleBaseNameA
#pragma comment(lib, "advapi32.lib") // For registry functions

// --- Global Variables ---
// Handle to the original msoledbsql.dll
HMODULE hOriginalDll = NULL;
// Path to the original dll
wchar_t originalDllPath[MAX_PATH];

// --- Registry-controlled Settings ---
bool g_loggingEnabled = false;
std::string g_serverRegex;
std::string g_dbaseRegex;
const wchar_t* REGISTRY_KEY_PATH = L"SOFTWARE\\msoledbsqlwrapper";

// --- Function Pointers to original DLL exports ---
// We will resolve the addresses of the functions in the original DLL.
using DllGetClassObject_t = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
DllGetClassObject_t pfnDllGetClassObject = nullptr;

// A generic function pointer type for passthrough functions
using FARPROC_t = FARPROC;

// --- Logging ---
// Log file paths
const std::string logFileLoad = "C:\\Users\\Public\\Documents\\dll_load.log";
const std::string logFileWrapper = "C:\\Users\\Public\\Documents\\oledb_wrapper.log";

// Function to get the current process name
std::string GetCurrentProcessName() {
    char processName[MAX_PATH] = "unknown";
    HANDLE hProcess = GetCurrentProcess();
    if (hProcess) {
        GetModuleBaseNameA(hProcess, NULL, processName, sizeof(processName));
    }
    return std::string(processName);
}

// Logging function
void WriteToLog(const std::string& logFile, const std::string& message) {
    if (!g_loggingEnabled) return; // Honor registry setting

    std::ofstream log(logFile, std::ios_base::app | std::ios_base::out);
    if (log.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm buf;
        localtime_s(&buf, &in_time_t);
        log << std::put_time(&buf, "%Y-%m-%d %X") << ": "
            << "PID: " << GetCurrentProcessId() << ", "
            << "Process: " << GetCurrentProcessName() << " - "
            << message << std::endl;
    }
}

// Function to load settings from the registry
void LoadRegistrySettings() {
    HKEY hKey;
    // KEY_WOW64_64KEY forces access to the 64-bit registry view from a 32-bit application.
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, REGISTRY_KEY_PATH, 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
        // 1. Check Logging setting
        DWORD loggingValue = 0;
        DWORD loggingValueSize = sizeof(loggingValue);
        if (RegQueryValueExW(hKey, L"LoggingEnabled", NULL, NULL, (LPBYTE)&loggingValue, &loggingValueSize) == ERROR_SUCCESS) {
            if (loggingValue == 1) {
                g_loggingEnabled = true;
            }
        }

        // Helper to convert wstring to string
        auto wstringToString = [](const std::wstring& wstr) -> std::string {
            if (wstr.empty()) return std::string();
            int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
            std::string strTo(size_needed, 0);
            WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
            return strTo;
            };

        // 2. Read Server Regex
        wchar_t serverRegexBuf[256];
        DWORD serverRegexBufSize = sizeof(serverRegexBuf);
        if (RegQueryValueExW(hKey, L"ServerRegex", NULL, NULL, (LPBYTE)serverRegexBuf, &serverRegexBufSize) == ERROR_SUCCESS) {
            g_serverRegex = wstringToString(serverRegexBuf);
        }

        // 3. Read Database Regex
        wchar_t dbaseRegexBuf[256];
        DWORD dbaseRegexBufSize = sizeof(dbaseRegexBuf);
        if (RegQueryValueExW(hKey, L"DbaseRegex", NULL, NULL, (LPBYTE)dbaseRegexBuf, &dbaseRegexBufSize) == ERROR_SUCCESS) {
            g_dbaseRegex = wstringToString(dbaseRegexBuf);
        }

        RegCloseKey(hKey);
    }
    // Defaults are already set (logging=false, regexes=empty)
}

// --- Wrapper for IClassFactory ---
// This class wraps the original IClassFactory to intercept object creation.
class WrapperClassFactory : public IClassFactory {
private:
    IClassFactory* m_pOriginalFactory;
    volatile LONG m_cRef;

public:
    WrapperClassFactory(IClassFactory* pOriginalFactory) : m_pOriginalFactory(pOriginalFactory), m_cRef(1) {
        m_pOriginalFactory->AddRef();
    }

    virtual ~WrapperClassFactory() {
        m_pOriginalFactory->Release();
    }

    // --- IUnknown Methods ---
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) {
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppvObject = this;
            AddRef();
            return S_OK;
        }
        *ppvObject = NULL;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() {
        return InterlockedIncrement(&m_cRef);
    }

    STDMETHODIMP_(ULONG) Release() {
        LONG cRef = InterlockedDecrement(&m_cRef);
        if (cRef == 0) {
            delete this;
        }
        return cRef;
    }

    // --- IClassFactory Methods ---
    STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObject);

    STDMETHODIMP LockServer(BOOL fLock) {
        return m_pOriginalFactory->LockServer(fLock);
    }
};

// --- Wrapper for OLE DB Interfaces ---
// This class will wrap the main OLE DB provider object.
// It specifically implements IDBProperties to intercept property setting.
class OLEDBWrapper : public IDBProperties {
private:
    IUnknown* m_pOriginalUnk; // Pointer to the real OLE DB object
    volatile LONG m_cRef;

public:
    OLEDBWrapper(IUnknown* pOriginalUnk) : m_pOriginalUnk(pOriginalUnk), m_cRef(1) {
        m_pOriginalUnk->AddRef();
        WriteToLog(logFileWrapper, "Wrapper object created.");
    }

    virtual ~OLEDBWrapper() {
        m_pOriginalUnk->Release();
        WriteToLog(logFileWrapper, "Wrapper object destroyed.");
    }

    // --- IUnknown Methods ---
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) {
        // If the requested interface is one we are wrapping, return ourselves.
        if (riid == IID_IUnknown || riid == IID_IDBProperties) {
            *ppvObject = static_cast<IDBProperties*>(this);
            AddRef();
            return S_OK;
        }
        // For any other interface, delegate to the original object.
        return m_pOriginalUnk->QueryInterface(riid, ppvObject);
    }

    STDMETHODIMP_(ULONG) AddRef() {
        return InterlockedIncrement(&m_cRef);
    }

    STDMETHODIMP_(ULONG) Release() {
        LONG cRef = InterlockedDecrement(&m_cRef);
        if (cRef == 0) {
            delete this;
        }
        return cRef;
    }

    // --- IDBProperties Methods ---
    STDMETHODIMP GetProperties(ULONG cPropertyIDSets, const DBPROPIDSET rgPropertyIDSets[], ULONG* pcPropertySets, DBPROPSET** prgPropertySets) {
        WriteToLog(logFileWrapper, "GetProperties called.");
        IDBProperties* pProps = NULL;
        HRESULT hr = m_pOriginalUnk->QueryInterface(IID_IDBProperties, (void**)&pProps);
        if (SUCCEEDED(hr)) {
            hr = pProps->GetProperties(cPropertyIDSets, rgPropertyIDSets, pcPropertySets, prgPropertySets);
            pProps->Release();
        }
        return hr;
    }

    STDMETHODIMP GetPropertyInfo(ULONG cPropertyIDSets, const DBPROPIDSET rgPropertyIDSets[], ULONG* pcPropertyInfoSets, DBPROPINFOSET** prgPropertyInfoSets, OLECHAR** ppDescBuffer) {
        WriteToLog(logFileWrapper, "GetPropertyInfo called.");
        IDBProperties* pProps = NULL;
        HRESULT hr = m_pOriginalUnk->QueryInterface(IID_IDBProperties, (void**)&pProps);
        if (SUCCEEDED(hr)) {
            hr = pProps->GetPropertyInfo(cPropertyIDSets, rgPropertyIDSets, pcPropertyInfoSets, prgPropertyInfoSets, ppDescBuffer);
            pProps->Release();
        }
        return hr;
    }

    STDMETHODIMP SetProperties(ULONG cPropertySets, DBPROPSET rgPropertySets[]) {
        WriteToLog(logFileWrapper, "SetProperties called (v3 - Registry Controlled).");

        // 1. Extract Server and Database names from the incoming properties
        std::wstring serverName;
        std::wstring dbaseName;
        for (ULONG i = 0; i < cPropertySets; ++i) {
            if (rgPropertySets[i].guidPropertySet == DBPROPSET_SQLSERVERDBINIT || rgPropertySets[i].guidPropertySet == DBPROPSET_DBINIT) {
                for (ULONG j = 0; j < rgPropertySets[i].cProperties; ++j) {
                    if (rgPropertySets[i].rgProperties[j].dwPropertyID == DBPROP_INIT_DATASOURCE && rgPropertySets[i].rgProperties[j].vValue.vt == VT_BSTR) {
                        serverName = rgPropertySets[i].rgProperties[j].vValue.bstrVal;
                    }
                    else if (rgPropertySets[i].rgProperties[j].dwPropertyID == DBPROP_INIT_CATALOG && rgPropertySets[i].rgProperties[j].vValue.vt == VT_BSTR) {
                        dbaseName = rgPropertySets[i].rgProperties[j].vValue.bstrVal;
                    }
                }
            }
        }

        // 2. Perform Regex checks to see if we should inject the property
        bool shouldInject = true;
        auto wstringToString = [](const std::wstring& wstr) -> std::string {
            if (wstr.empty()) return std::string();
            int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
            std::string strTo(size_needed, 0);
            WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
            return strTo;
            };
        std::string serverNameStr = wstringToString(serverName);
        std::string dbaseNameStr = wstringToString(dbaseName);

        if (!g_serverRegex.empty()) {
            try {
                std::regex server_regex(g_serverRegex);
                if (!std::regex_search(serverNameStr, server_regex)) {
                    shouldInject = false;
                    WriteToLog(logFileWrapper, "Server name '" + serverNameStr + "' did not match regex. Skipping injection.");
                }
            }
            catch (const std::regex_error& e) {
                WriteToLog(logFileWrapper, "Invalid server regex in registry: " + std::string(e.what()));
                shouldInject = false;
            }
        }

        if (shouldInject && !g_dbaseRegex.empty()) {
            try {
                std::regex dbase_regex(g_dbaseRegex);
                if (!std::regex_search(dbaseNameStr, dbase_regex)) {
                    shouldInject = false;
                    WriteToLog(logFileWrapper, "Database name '" + dbaseNameStr + "' did not match regex. Skipping injection.");
                }
            }
            catch (const std::regex_error& e) {
                WriteToLog(logFileWrapper, "Invalid database regex in registry: " + std::string(e.what()));
                shouldInject = false;
            }
        }

        // 3. --- Deep Copy of Property Sets ---
        std::vector<DBPROPSET> newPropSets(cPropertySets);
        for (ULONG i = 0; i < cPropertySets; ++i) {
            newPropSets[i].guidPropertySet = rgPropertySets[i].guidPropertySet;
            newPropSets[i].cProperties = rgPropertySets[i].cProperties;
            newPropSets[i].rgProperties = new DBPROP[rgPropertySets[i].cProperties];
            for (ULONG j = 0; j < rgPropertySets[i].cProperties; ++j) {
                DBPROP& src = rgPropertySets[i].rgProperties[j];
                DBPROP& dst = newPropSets[i].rgProperties[j];
                dst.dwPropertyID = src.dwPropertyID;
                dst.dwOptions = src.dwOptions;
                dst.dwStatus = src.dwStatus;
                dst.colid = src.colid;
                VariantInit(&dst.vValue);
                VariantCopy(&dst.vValue, &src.vValue);
            }
        }

        // 4. If conditions are met, inject the property into our deep-copied set
        if (shouldInject) {
            WriteToLog(logFileWrapper, "Conditions met. Proceeding with MultiSubnetFailover injection.");
            bool multiSubnetSet = false;
            ULONG sqlServerInitPropSetIndex = (ULONG)-1;
            for (ULONG i = 0; i < newPropSets.size(); ++i) {
                if (newPropSets[i].guidPropertySet == DBPROPSET_SQLSERVERDBINIT) {
                    sqlServerInitPropSetIndex = i;
                    for (ULONG j = 0; j < newPropSets[i].cProperties; ++j) {
                        if (newPropSets[i].rgProperties[j].dwPropertyID == SSPROP_INIT_MULTISUBNETFAILOVER) {
                            WriteToLog(logFileWrapper, "MultiSubnetFailover already present. Overwriting to VARIANT_TRUE.");
                            VariantClear(&(newPropSets[i].rgProperties[j].vValue));
                            newPropSets[i].rgProperties[j].vValue.vt = VT_BOOL;
                            newPropSets[i].rgProperties[j].vValue.boolVal = VARIANT_TRUE;
                            multiSubnetSet = true;
                            break;
                        }
                    }
                    if (multiSubnetSet) break;
                }
            }

            if (!multiSubnetSet) {
                WriteToLog(logFileWrapper, "MultiSubnetFailover not found. Injecting property as VARIANT_TRUE.");
                DBPROP multiSubnetProp;
                multiSubnetProp.dwPropertyID = SSPROP_INIT_MULTISUBNETFAILOVER;
                multiSubnetProp.dwOptions = DBPROPOPTIONS_REQUIRED;
                multiSubnetProp.dwStatus = DBPROPSTATUS_OK;
                multiSubnetProp.colid = DB_NULLID;
                multiSubnetProp.vValue.vt = VT_BOOL;
                multiSubnetProp.vValue.boolVal = VARIANT_TRUE;
                if (sqlServerInitPropSetIndex != (ULONG)-1) {
                    ULONG cProps = newPropSets[sqlServerInitPropSetIndex].cProperties;
                    DBPROP* newProps = new DBPROP[cProps + 1];
                    for (ULONG k = 0; k < cProps; ++k) newProps[k] = newPropSets[sqlServerInitPropSetIndex].rgProperties[k];
                    delete[] newPropSets[sqlServerInitPropSetIndex].rgProperties;
                    newProps[cProps] = multiSubnetProp;
                    newPropSets[sqlServerInitPropSetIndex].rgProperties = newProps;
                    newPropSets[sqlServerInitPropSetIndex].cProperties++;
                }
                else {
                    DBPROPSET newSet;
                    newSet.guidPropertySet = DBPROPSET_SQLSERVERDBINIT;
                    newSet.cProperties = 1;
                    newSet.rgProperties = new DBPROP[1];
                    newSet.rgProperties[0] = multiSubnetProp;
                    newPropSets.push_back(newSet);
                }
            }
        }
        else {
            WriteToLog(logFileWrapper, "Conditions not met. Skipping MultiSubnetFailover injection.");
        }

        // 5. Call the original SetProperties with the (potentially modified) deep-copied properties
        HRESULT hr = E_FAIL;
        IDBProperties* pProps = NULL;
        if (SUCCEEDED(m_pOriginalUnk->QueryInterface(IID_IDBProperties, (void**)&pProps))) {
            hr = pProps->SetProperties(newPropSets.size(), newPropSets.data());
            pProps->Release();
        }

        // 6. --- Cleanup ---
        for (auto& propSet : newPropSets) {
            for (ULONG i = 0; i < propSet.cProperties; ++i) {
                VariantClear(&(propSet.rgProperties[i].vValue));
            }
            delete[] propSet.rgProperties;
        }

        return hr;
    }
};

// --- Implementation of WrapperClassFactory::CreateInstance ---
STDMETHODIMP WrapperClassFactory::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObject) {
    WriteToLog(logFileWrapper, "ClassFactory::CreateInstance called.");

    // Create the original object first.
    IUnknown* pOriginalUnk = NULL;
    // We request IUnknown initially, then the wrapper can handle specific interface queries.
    HRESULT hr = m_pOriginalFactory->CreateInstance(pUnkOuter, IID_IUnknown, (void**)&pOriginalUnk);

    if (SUCCEEDED(hr)) {
        // Create our wrapper and return it to the application.
        OLEDBWrapper* pWrapper = new OLEDBWrapper(pOriginalUnk);
        hr = pWrapper->QueryInterface(riid, ppvObject);
        // The QI above added a ref, so we can release our local one.
        pWrapper->Release();
        // We don't need the original object pointer anymore, wrapper holds a ref.
        pOriginalUnk->Release();
    }
    else {
        *ppvObject = NULL;
    }
    return hr;
}


// --- DLL Entry Point ---
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
    {
        LoadRegistrySettings(); // Load configuration on attach
        WriteToLog(logFileLoad, "DLL_PROCESS_ATTACH"); // Log this after loading settings

        // Get the path to the original DLL
        GetSystemWow64DirectoryW(originalDllPath, MAX_PATH);
        PathAppendW(originalDllPath, L"\\msoledbsql.original.dll");

        // Load the original DLL
        hOriginalDll = LoadLibraryW(originalDllPath);

        if (hOriginalDll) {
            // Get pointers to the functions we want to proxy
            pfnDllGetClassObject = (DllGetClassObject_t)GetProcAddress(hOriginalDll, "DllGetClassObject");
        }
        else {
            WriteToLog(logFileLoad, "DLL_PROCESS_ATTACH: FAILED to load msoledbsql.original.dll");
            return FALSE; // Prevents the DLL from loading if the original can't be found
        }
    }
    break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
    {
        WriteToLog(logFileLoad, "DLL_PROCESS_DETACH");
        if (hOriginalDll) {
            FreeLibrary(hOriginalDll);
            hOriginalDll = NULL;
        }
    }
    break;
    }
    return TRUE;
}

// --- Exported Functions ---

// The main exported function we are intercepting
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    WriteToLog(logFileWrapper, "DllGetClassObject: Intercepted request via file hijack.");

    if (!pfnDllGetClassObject) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    IClassFactory* pOriginalFactory = NULL;
    HRESULT hr = pfnDllGetClassObject(rclsid, IID_IClassFactory, (void**)&pOriginalFactory);

    if (SUCCEEDED(hr)) {
        WriteToLog(logFileWrapper, "DllGetClassObject: Returning our wrapper factory to the application.");
        *ppv = new WrapperClassFactory(pOriginalFactory);
        pOriginalFactory->Release(); // Wrapper factory holds its own reference
        return S_OK;
    }
    else {
        WriteToLog(logFileWrapper, "DllGetClassObject: Failed to get original class factory.");
        *ppv = NULL;
        return hr;
    }
}

// Other standard COM exports that must be passed through
STDAPI DllCanUnloadNow(void) {
    FARPROC_t pfn = GetProcAddress(hOriginalDll, "DllCanUnloadNow");
    if (pfn) return ((HRESULT(WINAPI*)())pfn)();
    return S_FALSE;
}

STDAPI DllRegisterServer(void) {
    FARPROC_t pfn = GetProcAddress(hOriginalDll, "DllRegisterServer");
    if (pfn) return ((HRESULT(WINAPI*)())pfn)();
    return E_FAIL;
}

STDAPI DllUnregisterServer(void) {
    FARPROC_t pfn = GetProcAddress(hOriginalDll, "DllUnregisterServer");
    if (pfn) return ((HRESULT(WINAPI*)())pfn)();
    return E_FAIL;
}

// Passthrough for the export discovered from dumpbin
// Must use extern "C" to ensure correct name decoration for the linker.
// We rename it to avoid conflict with the msoledbsql.h header.
// The .def file will export this function with its original name.
extern "C" HRESULT WINAPI OpenSqlFilestream_Passthrough(
    LPCWSTR FilestreamPath,
    SQL_FILESTREAM_DESIRED_ACCESS DesiredAccess,
    ULONG OpenOptions,
    LPBYTE FilestreamTransactionContext,
    SIZE_T FilestreamTransactionContextLength,
    PLARGE_INTEGER AllocationSize,
    LPHANDLE FileHandle)
{
    using OpenSqlFilestream_t = HRESULT(WINAPI*)(LPCWSTR, SQL_FILESTREAM_DESIRED_ACCESS, ULONG, LPBYTE, SIZE_T, PLARGE_INTEGER, LPHANDLE);
    static OpenSqlFilestream_t pfnOpenSqlFilestream = nullptr;
    if (!pfnOpenSqlFilestream) {
        pfnOpenSqlFilestream = (OpenSqlFilestream_t)GetProcAddress(hOriginalDll, "OpenSqlFilestream");
    }

    if (pfnOpenSqlFilestream) {
        return pfnOpenSqlFilestream(FilestreamPath, DesiredAccess, OpenOptions, FilestreamTransactionContext, FilestreamTransactionContextLength, AllocationSize, FileHandle);
    }

    // Should not happen if the original DLL is loaded correctly
    return E_FAIL;
}

