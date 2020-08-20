#include "platform.h"

BOOL IsOS64Bit()
{
    BOOL b64Bit = FALSE;

#ifdef _WIN64
    // 64-bit applications always run under 64-bit Windows
    return TRUE;
#endif

    // Check for 32-bit applications

    typedef BOOL (WINAPI *PFNISWOW64PROCESS)(HANDLE, PBOOL);

    HMODULE hKernel32 = LoadLibraryW(L"kernel32.dll");
    if(hKernel32!=NULL)
    {
        PFNISWOW64PROCESS pfnIsWow64Process =
            (PFNISWOW64PROCESS)GetProcAddress(hKernel32, "IsWow64Process");
        if(pfnIsWow64Process==NULL)
        {
            // If there is no IsWow64Process() API, than Windows is 32-bit for sure
            FreeLibrary(hKernel32);
            return FALSE;
        }

        pfnIsWow64Process(GetCurrentProcess(), &b64Bit);
        FreeLibrary(hKernel32);
    }

    return b64Bit;
}


BOOL GetNtVersionNumbers(DWORD &dwMajorVer, DWORD &dwMinorVer, DWORD &dwBuildNumber)
{
    BOOL bRet = FALSE;
    HMODULE hModNtdll = NULL;
    if (hModNtdll = LoadLibraryW(L"ntdll.dll"))
    {
        typedef void (WINAPI *pfRTLGETNTVERSIONNUMBERS)(DWORD*, DWORD*, DWORD*);
        pfRTLGETNTVERSIONNUMBERS pfRtlGetNtVersionNumbers;
        pfRtlGetNtVersionNumbers = (pfRTLGETNTVERSIONNUMBERS)GetProcAddress(hModNtdll, "RtlGetNtVersionNumbers");
        if (pfRtlGetNtVersionNumbers)
        {
            pfRtlGetNtVersionNumbers(&dwMajorVer, &dwMinorVer, &dwBuildNumber);
            dwBuildNumber &= 0x0ffff;
            bRet = TRUE;
        }

        FreeLibrary(hModNtdll);
        hModNtdll = NULL;
    }

    return bRet;
}

void GetVersionNumbers(DWORD &dwMajorVer, DWORD &dwMinorVer, DWORD &dwBuildNumber)
{
    OSVERSIONINFO osver = { sizeof(OSVERSIONINFO) };
    GetVersionEx(&osver);
    dwMajorVer = osver.dwMajorVersion;
    dwMinorVer = osver.dwMinorVersion;
    dwBuildNumber = osver.dwBuildNumber;
    /*
    std::string os_name;
    if (osver.dwMajorVersion == 5 && osver.dwMinorVersion == 0)
        os_name = "Windows 2000";
    else if (osver.dwMajorVersion == 5 && osver.dwMinorVersion == 1)
        os_name = "Windows XP";
    else if (osver.dwMajorVersion == 6 && osver.dwMinorVersion == 0)
        os_name = "Windows 2003";
    else if (osver.dwMajorVersion == 5 && osver.dwMinorVersion == 2)
        os_name = "Windows vista";
    else if (osver.dwMajorVersion == 6 && osver.dwMinorVersion == 1)
        os_name = "Windows 7";
    else if (osver.dwMajorVersion == 6 && osver.dwMinorVersion == 2)
        os_name = "Windows 10";
    */
}

#include <QSettings>
#include <QScreen>
#include <d3d11.h>
#include <dxgi.h>
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")

const static D3D_FEATURE_LEVEL featureLevels[] =
{
    D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_1,
    D3D_FEATURE_LEVEL_10_0,
    D3D_FEATURE_LEVEL_9_3,
};

#define REG_CPU "HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"

QString OSInfo()
{
    return QSysInfo::prettyProductName();
}

QString CPUInfo()
{
#ifdef Q_OS_WIN
    QSettings cpu(REG_CPU, QSettings::NativeFormat);
    QString cpuString = cpu.value("ProcessorNameString").toString();
    return cpuString;
#else
    return "未知";
#endif
}

QString MemoryInfo()
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof (statex);
    GlobalMemoryStatusEx(&statex);

    double total = statex.ullTotalPhys * 1.0 / GB;
    return QString::number(total, 'f', 1) + "G";
#else
    return "未知";
#endif
}

QString DisplayCardInfo(const QScreen *screen)
{
    QString d3dLevelString = "未知";

#ifdef Q_OS_WIN
    IDXGIFactory *pFactory;
    IDXGIAdapter *pAdapter;
    std::vector <IDXGIAdapter*>vAdapters;

    int iAdapterNum = 0, index = -1;

    HRESULT hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)(&pFactory));
    if (FAILED(hr))
        return d3dLevelString;

    while (pFactory->EnumAdapters(iAdapterNum, &pAdapter) != DXGI_ERROR_NOT_FOUND)
    {
        vAdapters.push_back(pAdapter);
        ++iAdapterNum;
    }

    for (size_t i = 0; i < vAdapters.size(); i++)
    {
        IDXGIOutput *pOutput;
        std::vector<IDXGIOutput*> vOutputs;
        int iOutputNum = 0;
        while (vAdapters[i]->EnumOutputs(iOutputNum, &pOutput) != DXGI_ERROR_NOT_FOUND)
        {
            vOutputs.push_back(pOutput);
            iOutputNum++;
        }

        for (size_t n = 0; n < vOutputs.size(); n++)
        {
            DXGI_OUTPUT_DESC outputDesc;
            vOutputs[n]->GetDesc(&outputDesc);

            QString devName = QString::fromWCharArray(outputDesc.DeviceName);
            if (devName == screen->name()) {
                index = i;
                i = vAdapters.size() + 1;
                break;
            }
        }
        vOutputs.clear();
    }

    if (index == -1) {
        vAdapters.clear();
        return d3dLevelString;
    }

    D3D_FEATURE_LEVEL levelUsed = D3D_FEATURE_LEVEL_9_3;
    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    hr = D3D11CreateDevice(vAdapters[index], D3D_DRIVER_TYPE_UNKNOWN,
                           NULL, createFlags, featureLevels,
                           sizeof(featureLevels) / sizeof(D3D_FEATURE_LEVEL),
                           D3D11_SDK_VERSION, NULL,
                           &levelUsed, NULL);
    if (!FAILED(hr))
    {
        switch (levelUsed)
        {
        case D3D_FEATURE_LEVEL_11_0:
            d3dLevelString = "11";
            break;
        case D3D_FEATURE_LEVEL_10_1:
            d3dLevelString = "10.1";
            break;
        case D3D_FEATURE_LEVEL_10_0:
            d3dLevelString = "10";
            break;
        case D3D_FEATURE_LEVEL_9_3:
            d3dLevelString = "9.3";
            break;
        default:
            break;
        }
    }

    vAdapters.clear();

#endif

    return d3dLevelString;
}

double GetAvailableMemory()
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof (statex);
    GlobalMemoryStatusEx(&statex);
    return statex.ullAvailPhys * 1.0 / MB;
#else
    return 1.0;
#endif
}

#ifdef _WIN32
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <util/windows/WinHandle.hpp>
#include <util/windows/HRError.hpp>
#include <util/windows/ComPtr.hpp>
bool DisableAudioDucking(bool disable)
{
    ComPtr<IMMDeviceEnumerator>   devEnum;
    ComPtr<IMMDevice>             device;
    ComPtr<IAudioSessionManager2> sessionManager2;
    ComPtr<IAudioSessionControl>  sessionControl;
    ComPtr<IAudioSessionControl2> sessionControl2;

    HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                      nullptr, CLSCTX_INPROC_SERVER,
                                      __uuidof(IMMDeviceEnumerator),
                                      (void **)&devEnum);
    if (FAILED(result))
        return false;

    result = devEnum->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(result))
        return false;

    result = device->Activate(__uuidof(IAudioSessionManager2),
                              CLSCTX_INPROC_SERVER, nullptr,
                              (void **)&sessionManager2);
    if (FAILED(result))
        return false;

    result = sessionManager2->GetAudioSessionControl(nullptr, 0,
                                                     &sessionControl);
    if (FAILED(result))
        return false;

    result = sessionControl->QueryInterface(&sessionControl2);
    if (FAILED(result))
        return false;

    result = sessionControl2->SetDuckingPreference(disable);
    return SUCCEEDED(result);
}
#endif
