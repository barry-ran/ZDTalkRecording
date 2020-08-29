#pragma once

#include <Windows.h>
#include <QString>

BOOL IsOS64Bit();
BOOL GetNtVersionNumbers(DWORD &dwMajorVer, DWORD &dwMinorVer,
                         DWORD &dwBuildNumber);
void GetVersionNumbers(DWORD &dwMajorVer, DWORD &dwMinorVer,
                       DWORD &dwBuildNumber);

QString CPUInfo();

bool DisableAudioDucking(bool disable);
