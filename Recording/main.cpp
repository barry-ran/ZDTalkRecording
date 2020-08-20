#include <QCoreApplication>
#include <QTextCodec>
#include <QDateTime>
#include <QThread>
#include <QDir>
#include <QTextStream>
#include <QFileInfo>
#include <QDebug>

#include <Windows.h>
#include <DbgHelp.h>
#include <tchar.h>
#include <iostream>

#include "CrashRpt.h"
#include "strconv.h"

#include "utils/log/zdlogger.h"
#include "zdrecordingversion.h"
#include "zdrecordingdefine.h"
#include "zdrecordingclient.h"

#define LOG_CRASHED     "!!! Crashed !!!"

QString g_logFilePath;
QString g_crashFilePath;
QString g_crashDirPath;

// [本地 Crash 处理]
static void PreventSetUnhandledExceptionFilter()
{
    QString dllname("kernel32.dll");
    void* addr = (void*)GetProcAddress(LoadLibrary(dllname.toStdWString().c_str()),
                                       "SetUnhandledExceptionFilter");
    if (addr && !IsBadReadPtr(addr, sizeof(void *)))
    {
        unsigned char code[16];
        int size = 0;

        code[size++] = 0x33;
        code[size++] = 0xC0;
        code[size++] = 0xC2;
        code[size++] = 0x04;
        code[size++] = 0x00;

        DWORD dwOldFlag, dwTempFlag;
        int ret = VirtualProtectEx(GetCurrentProcess(), addr, size,
                                   PAGE_EXECUTE_READWRITE, &dwOldFlag);
        if (ret == 0) return;
        WriteProcessMemory(GetCurrentProcess(), addr, code, size, nullptr);
        VirtualProtectEx(GetCurrentProcess(), addr, size, dwOldFlag, &dwTempFlag);
    }
}

static void LoadDebugPrivilege()
{
    const DWORD flags = TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY;
    TOKEN_PRIVILEGES tp;
    HANDLE token;
    LUID val;

    if (!OpenProcessToken(GetCurrentProcess(), flags, &token))
        return;

    if (!!LookupPrivilegeValue(nullptr, SE_DEBUG_NAME, &val)) {
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = val;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        AdjustTokenPrivileges(token, false, &tp, sizeof(tp), nullptr, nullptr);
    }

    CloseHandle(token);
}

extern QString ThreadTracesHandler(PEXCEPTION_POINTERS exception);
long __stdcall CrashHandler(_EXCEPTION_POINTERS *excp)
{
    static bool inside_handler = false;

    if (inside_handler)
        return EXCEPTION_EXECUTE_HANDLER;

    inside_handler = true;
    qCritical(LOG_CRASHED);

    // 写 Thread Traces 文件
    QString strCurrentTime =
            QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss");
    QString info = ThreadTracesHandler(excp);
    if (!info.isEmpty())
    {
        QString filePath = g_crashDirPath + QDir::separator() +
                           "threadtraces-rec" + strCurrentTime + ".txt";
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly)) {
            qWarning() << "Tread Traces File Can not Open!";
            return EXCEPTION_EXECUTE_HANDLER;
        }
        QTextStream ts(&file);
        ts.setCodec("UTF-8");
        ts << info;
        file.close();
    } else {
        qWarning() << "No Tread Traces Information!";
    }

    // 写 Dump 文件
//    QString crashDmpFile = QString("%1/crash-%2.dmp").arg(g_crashDirPath)
//                .arg(strCurrentTime);
//    HANDLE hDumpFile = CreateFile(crashDmpFile.toStdWString().c_str(), GENERIC_WRITE, 0, nullptr,
//                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
//    if (hDumpFile != INVALID_HANDLE_VALUE) {
//        MINIDUMP_EXCEPTION_INFORMATION dumpInfo;
//        dumpInfo.ExceptionPointers = excp;
//        dumpInfo.ThreadId = GetCurrentThreadId();
//        dumpInfo.ClientPointers = TRUE;

//        MINIDUMP_TYPE mdt = (MINIDUMP_TYPE)(MiniDumpNormal);

//        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hDumpFile,
//                          mdt, &dumpInfo, nullptr, nullptr);
//        CloseHandle(hDumpFile);
//    }

    return EXCEPTION_EXECUTE_HANDLER;
}
// [本地 Crash 处理]

// [CrashRpt 处理]
static int CALLBACK CrashCallback(CR_CRASH_CALLBACK_INFOA *pInfo)
{
    UNREFERENCED_PARAMETER(pInfo);

    return CR_CB_DODEFAULT;
}

static BOOL WINAPI PreCrashCallback(LPVOID lpvState)
{
    UNREFERENCED_PARAMETER(lpvState);
    static bool inside_handler = false;

    if (inside_handler)
        return FALSE;

    inside_handler = true;
    qCritical(LOG_CRASHED);

    return TRUE;
}
// [CrashRpt 处理]

// [Qt 日志代理]
static void LogHandler(QtMsgType type, const QMessageLogContext &,
                       const QString &msg)
{
    QString label = "UNKNOWN";
    switch (type)
    {
    case QtInfoMsg:
        label = QString("INFO");
        break;
    case QtDebugMsg:
        label = QString("DEBUG");
        break;
    case QtWarningMsg:
        label = QString("WARNING");
        break;
    case QtCriticalMsg:
        label = QString("ERROR");
        break;
    case QtFatalMsg:
        label = QString("FATAL");
        break;
    default:
        break;
    }

    QString text = QStringLiteral("[%1][%2:%3] %4")
                   .arg(QDateTime::currentDateTime().toString("hh:mm:ss.zzz"))
                   .arg(quintptr(QThread::currentThreadId()))
                   .arg(label)
                   .arg(msg);
    ZDLogger::getInstance()->writeLog(text);
}
// [Qt 日志代理]

static void PrintHelpDoc()
{
    std::cout << "--help, -h: Get list of available commands.\n"
              << "--version, -v: Get current version.\n\n"
              << "--log, -l: Log Output File Path.\n"
              << "--crash, -c: Crash Output File Path.\n"
              << "--user, -u: User Name.\n"
              << "--id, -i: User OpenId.\n";
    exit(0);
}

static void PrintVersion()
{
    std::cout << "ZDRecording - "
              << ZDRECORDING_VERSION_STR << "\n";
    exit(0);
}

static inline bool arg_is(const char *arg, const char *long_form,
                          const char *short_form)
{
    return (long_form  && strcmp(arg, long_form)  == 0) ||
           (short_form && strcmp(arg, short_form) == 0);
}

static bool UpdateFilePath(const char *arg, QString &filePath)
{
    if (!arg || arg[0] == '\0')
        return false;

    QString path = QString::fromLocal8Bit(arg);
    if (!QFileInfo(path).absoluteDir().exists())
        return false;

    filePath = path;
    return true;
}

static bool UpdateUserInfo(const char *arg, QString &info)
{
    if (!arg || arg[0] == '\0')
        return false;

    info = QString::fromLocal8Bit(arg);
    return true;
}

int main(int argc, char *argv[])
{
    SetErrorMode(SEM_FAILCRITICALERRORS);

    // 参数处理，没有日志及崩溃日志路径，则退出
    QString userName, openId;
    for (int i = 1; i < argc; ++i) {
        if (arg_is(argv[i], "--log", "-l"))
            UpdateFilePath(argv[++i], g_logFilePath);
        else if (arg_is(argv[i], "--crash", "-c"))
            UpdateFilePath(argv[++i], g_crashFilePath);
        else if (arg_is(argv[i], "--user", "-u"))
            UpdateUserInfo(argv[++i], userName);
        else if (arg_is(argv[i], "--id", "-i"))
            UpdateUserInfo(argv[++i], openId);
        else if (arg_is(argv[i], "--version", "-v"))
            PrintVersion();
        else if (arg_is(argv[i], "--help", "-h"))
            PrintHelpDoc();
    }
    if (g_logFilePath.isEmpty() || g_crashFilePath.isEmpty())
        PrintHelpDoc();
    g_crashDirPath = QFileInfo(g_crashFilePath).absoluteDir().absolutePath();

    // 日志处理
    ZDLogger::getInstance()->openLogFile(g_logFilePath);
    qInstallMessageHandler(LogHandler);

    // CrashRpt 设置
    strconv_t strconv;
    MINIDUMP_TYPE mdt = (MINIDUMP_TYPE)(MiniDumpNormal);
    CR_INSTALL_INFOA info = {0};
    info.cb = sizeof(CR_INSTALL_INFOA);
    info.pszAppName = ZDTALK_RECORDING_DESC;
    info.pszAppVersion = ZDRECORDING_VERSION_STR;
    info.uMiniDumpType = mdt;
    info.pfnCrashCallback = PreCrashCallback;
    info.dwFlags |= CR_INST_ALL_POSSIBLE_HANDLERS;
    info.dwFlags |= CR_INST_NO_GUI;
    // info.dwFlags |= CR_INST_NO_MINIDUMP;
    info.pszDebugHelpDLL = NULL;
    info.pszEmailTo = "jiangguopeng@izaodao.com";
    info.pszEmailSubject = "Recording Crash Report";
    info.pszErrorReportSaveDir =
            strconv.utf82a(g_crashDirPath.toUtf8().constData());

    CrAutoInstallHelper cr_install_helper(&info);
    if (cr_install_helper.m_nInstallStatus == 0) {
        crSetCrashCallbackA(CrashCallback, nullptr);
        crAddFile2A(strconv.utf82a(g_logFilePath.toUtf8().constData()),
                    NULL, "Log File", CR_AF_MAKE_FILE_COPY);
        if (!userName.isEmpty())
            crAddPropertyA("UserName",
                           strconv.utf82a(userName.toStdString().c_str()));
        if (!openId.isEmpty())
            crAddPropertyA("OpenId",
                           strconv.utf82a(openId.toUtf8().constData()));
    } else {
        char lastErr[1024] = {0};
        crGetLastErrorMsgA(lastErr, 1024);
        qWarning() << "CrashRpt install failed."
                   << cr_install_helper.m_nInstallStatus << lastErr;

        qInfo() << "Use default exception handler.";
        LoadDebugPrivilege();
        SetUnhandledExceptionFilter(CrashHandler);
        PreventSetUnhandledExceptionFilter();
    }

    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
    QCoreApplication a(argc, argv);
    ZDRecordingClient client;
    client.connectToServer();
    return a.exec();
}
