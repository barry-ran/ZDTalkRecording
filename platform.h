#pragma once

#include <Windows.h>

#include <QString>
#include <QScreen>

#define WINDOWS_NORMAL_DPI 96
#define WINDOWS_MAIN_WINDOW_WIDTH  1280
#define WINDOWS_MAIN_WINDOW_HEIGHT 720

#ifdef Q_OS_WIN
#define NORMAL_DPI WINDOWS_NORMAL_DPI
#define MAIN_WINDOW_WIDTH  WINDOWS_MAIN_WINDOW_WIDTH
#define MAIN_WINDOW_HEIGHT WINDOWS_MAIN_WINDOW_HEIGHT
#else
#define NORMAL_DPI WINDOWS_NORMAL_DPI
#define MAIN_WINDOW_WIDTH  WINDOWS_MAIN_WINDOW_WIDTH
#define MAIN_WINDOW_HEIGHT WINDOWS_MAIN_WINDOW_HEIGHT
#endif

BOOL IsOS64Bit();
BOOL GetNtVersionNumbers(DWORD &dwMajorVer, DWORD &dwMinorVer,
                         DWORD &dwBuildNumber);
void GetVersionNumbers(DWORD &dwMajorVer, DWORD &dwMinorVer,
                       DWORD &dwBuildNumber);

// Computer Information
QString OSInfo();
QString CPUInfo();
QString MemoryInfo();
QString DisplayCardInfo(const QScreen *);

double GetAvailableMemory();

#ifdef _WIN32
bool DisableAudioDucking(bool disable);
#endif
