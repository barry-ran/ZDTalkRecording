#include "platform.h"

#include <QSettings>

#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <util/windows/WinHandle.hpp>
#include <util/windows/HRError.hpp>
#include <util/windows/ComPtr.hpp>

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

#define REG_CPU "HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"

QString CPUInfo()
{
    QSettings cpu(REG_CPU, QSettings::NativeFormat);
    QString cpuString = cpu.value("ProcessorNameString").toString();
    return cpuString;
}

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
