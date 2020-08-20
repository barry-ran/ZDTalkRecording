#include <windows.h>
#include <time.h>
#include <dbghelp.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <inttypes.h>

#include <QString>
#include <QFileInfo>
#include "platform.h"

typedef BOOL (WINAPI *ENUMERATELOADEDMODULES64)(HANDLE process,
        PENUMLOADED_MODULES_CALLBACK64 enum_loaded_modules_callback,
        PVOID user_context);
typedef DWORD (WINAPI *SYMSETOPTIONS)(DWORD sym_options);
typedef BOOL (WINAPI *SYMINITIALIZE)(HANDLE process, PCTSTR user_search_path,
        BOOL invade_process);
typedef BOOL (WINAPI *SYMCLEANUP)(HANDLE process);
typedef BOOL (WINAPI *STACKWALK64)(DWORD machine_type, HANDLE process,
        HANDLE thread, LPSTACKFRAME64 stack_frame,
        PVOID context_record,
        PREAD_PROCESS_MEMORY_ROUTINE64 read_memory_routine,
        PFUNCTION_TABLE_ACCESS_ROUTINE64 function_table_access_routine,
        PGET_MODULE_BASE_ROUTINE64 get_module_base_routine,
        PTRANSLATE_ADDRESS_ROUTINE64 translate_address);
typedef BOOL (WINAPI *SYMREFRESHMODULELIST)(HANDLE process);

typedef PVOID (WINAPI *SYMFUNCTIONTABLEACCESS64)(HANDLE process,
        DWORD64 addr_base);
typedef DWORD64 (WINAPI *SYMGETMODULEBASE64)(HANDLE process, DWORD64 addr);
typedef BOOL (WINAPI *SYMFROMADDR)(HANDLE process, DWORD64 address,
        PDWORD64 displacement, PSYMBOL_INFOW symbol);
typedef BOOL (WINAPI *SYMGETMODULEINFO64)(HANDLE process, DWORD64 addr,
        PIMAGEHLP_MODULE64 module_info);

typedef DWORD64 (WINAPI *SYMLOADMODULE64)(HANDLE process, HANDLE file,
        PSTR image_name, PSTR module_name, DWORD64 base_of_dll,
        DWORD size_of_dll);

typedef BOOL (WINAPI *MINIDUMPWRITEDUMP)(HANDLE process, DWORD process_id,
        HANDLE file, MINIDUMP_TYPE dump_type,
        PMINIDUMP_EXCEPTION_INFORMATION exception_param,
        PMINIDUMP_USER_STREAM_INFORMATION user_stream_param,
        PMINIDUMP_CALLBACK_INFORMATION callback_param);

typedef HINSTANCE (WINAPI *SHELLEXECUTEA)(HWND hwnd, LPCTSTR operation,
        LPCTSTR file, LPCTSTR parameters, LPCTSTR directory,
        INT show_flags);

struct stack_trace {
    CONTEXT       context;
    DWORD64       instruction_ptr;
    STACKFRAME64  frame;
    DWORD         image_type;
};

struct exception_handler_data {
    SYMINITIALIZE                         sym_initialize;
    SYMCLEANUP                            sym_cleanup;
    SYMSETOPTIONS                         sym_set_options;
    SYMFUNCTIONTABLEACCESS64              sym_function_table_access64;
    SYMGETMODULEBASE64                    sym_get_module_base64;
    SYMFROMADDR                           sym_from_addr;
    SYMGETMODULEINFO64                    sym_get_module_info64;
    SYMREFRESHMODULELIST                  sym_refresh_module_list;
    STACKWALK64                           stack_walk64;
    ENUMERATELOADEDMODULES64              enumerate_loaded_modules64;
    MINIDUMPWRITEDUMP                     minidump_write_dump;

    HMODULE                               dbghelp;
    SYMBOL_INFOW                          *sym_info;
    PEXCEPTION_POINTERS                   exception;
    SYSTEMTIME                            time_info;
    HANDLE                                process;

    struct stack_trace                    main_trace;

    QString                               str;
    QString                               os_info;
    QString                               cpu_info;
    QString                               module_name;
    QString                               module_list;
};

static inline void exception_handler_data_free(
        struct exception_handler_data *data)
{
    LocalFree(data->sym_info);
    FreeLibrary(data->dbghelp);
}

static inline void *get_proc(HMODULE module, const char *func)
{
    return (void*)GetProcAddress(module, func);
}

#define GET_DBGHELP_IMPORT(target, str, func) \
    do { \
        data->target = (func)get_proc(data->dbghelp, str); \
        if (!data->target) \
            return false; \
    } while (false)

static inline bool get_dbghelp_imports(struct exception_handler_data *data)
{
    data->dbghelp = LoadLibraryW(L"DbgHelp");
    if (!data->dbghelp)
        return false;

    GET_DBGHELP_IMPORT(sym_initialize, "SymInitialize", SYMINITIALIZE);
    GET_DBGHELP_IMPORT(sym_cleanup, "SymCleanup", SYMCLEANUP);
    GET_DBGHELP_IMPORT(sym_set_options, "SymSetOptions", SYMSETOPTIONS);
    GET_DBGHELP_IMPORT(sym_function_table_access64,
                       "SymFunctionTableAccess64", SYMFUNCTIONTABLEACCESS64);
    GET_DBGHELP_IMPORT(sym_get_module_base64, "SymGetModuleBase64",
                       SYMGETMODULEBASE64);
    GET_DBGHELP_IMPORT(sym_from_addr, "SymFromAddrW", SYMFROMADDR);
    GET_DBGHELP_IMPORT(sym_get_module_info64, "SymGetModuleInfo64",
                       SYMGETMODULEINFO64);
    GET_DBGHELP_IMPORT(sym_refresh_module_list, "SymRefreshModuleList",
                       SYMREFRESHMODULELIST);
    GET_DBGHELP_IMPORT(stack_walk64, "StackWalk64", STACKWALK64);
    GET_DBGHELP_IMPORT(enumerate_loaded_modules64, "EnumerateLoadedModulesW64",
                       ENUMERATELOADEDMODULES64);
    GET_DBGHELP_IMPORT(minidump_write_dump, "MiniDumpWriteDump",
                       MINIDUMPWRITEDUMP);

    return true;
}

static inline void init_instruction_data(struct stack_trace *trace)
{
    trace->instruction_ptr = trace->context.Eip;
    trace->frame.AddrPC.Offset = trace->instruction_ptr;
    trace->frame.AddrFrame.Offset = trace->context.Ebp;
    trace->frame.AddrStack.Offset = trace->context.Esp;
    trace->image_type = IMAGE_FILE_MACHINE_I386;

    trace->frame.AddrFrame.Mode = AddrModeFlat;
    trace->frame.AddrPC.Mode = AddrModeFlat;
    trace->frame.AddrStack.Mode = AddrModeFlat;
}

static bool sym_initialize_called = false;

static inline void init_sym_info(struct exception_handler_data *data)
{
    data->sym_set_options(
            SYMOPT_UNDNAME |
            SYMOPT_FAIL_CRITICAL_ERRORS |
            SYMOPT_LOAD_ANYTHING);

    if (!sym_initialize_called)
         data->sym_initialize(data->process, NULL, true);
    else
        data->sym_refresh_module_list(data->process);

    data->sym_info = (SYMBOL_INFOW *)LocalAlloc(LPTR,
                                                sizeof(*data->sym_info) + 256);
    data->sym_info->SizeOfStruct = sizeof(SYMBOL_INFO);
    data->sym_info->MaxNameLen = 256;
}

static inline void init_version_info(struct exception_handler_data *data)
{
    DWORD dwMajorVersion, dwMinorVersion, dwBuildNumber;
    if (!GetNtVersionNumbers(dwMajorVersion, dwMinorVersion, dwBuildNumber))
        GetVersionNumbers(dwMajorVersion, dwMinorVersion, dwBuildNumber);

    data->os_info = QString::number(dwMajorVersion) + "." +
                    QString::number(dwMinorVersion) + "." +
                    QString::number(dwBuildNumber) + " ";
    data->os_info += IsOS64Bit() ? "64bit" : "32bit";
}

static inline void init_cpu_info(struct exception_handler_data *data)
{
    data->cpu_info = CPUInfo();
}

static BOOL CALLBACK enum_all_modules(PCTSTR module_name, DWORD64 module_base,
        ULONG module_size, struct exception_handler_data *data)
{
    if (data->main_trace.instruction_ptr >= module_base &&
            data->main_trace.instruction_ptr <  module_base + module_size) {

        data->module_name = QString::fromWCharArray(module_name);
    }

    QString info = QString::number(module_base) + "-" +
                   QString::number(module_base+module_size) + " " +
                   QString::fromWCharArray(module_name) + "\r\n";
    data->module_list += info;
    return true;
}

static inline void init_module_info(struct exception_handler_data *data)
{
    data->enumerate_loaded_modules64(data->process,
            (PENUMLOADED_MODULES_CALLBACK64)enum_all_modules,
            data);
}

static inline void write_header(struct exception_handler_data *data)
{
    char date_time[80];
    time_t now = time(0);
    struct tm ts;
    ts = *localtime(&now);
    strftime(date_time, sizeof(date_time), "%Y-%m-%d, %X", &ts);

    QString info("Unhandled exception: %1\r\n"
                 "Date/Time: %2\r\n"
                 "Fault address: %3 (%4)\r\n"
                 "Windows version: %5\r\n"
                 "CPU: %6\r\n\r\n");
    data->str += info.arg(data->exception->ExceptionRecord->ExceptionCode, 0, 16)
                 .arg(date_time)
                 .arg(data->main_trace.instruction_ptr, 0, 16)
                 .arg(data->module_name)
                 .arg(data->os_info)
                 .arg(data->cpu_info);
}

struct module_info {
    DWORD64 addr;
    char name_utf8[MAX_PATH];
};

static BOOL CALLBACK enum_module(PCTSTR module_name, DWORD64 module_base,
        ULONG module_size, struct module_info *info)
{
    if (info->addr >= module_base &&
        info->addr <  module_base + module_size) {

        sprintf_s(info->name_utf8, MAX_PATH,
                QString::fromWCharArray(module_name).toUtf8().constData());
        return false;
    }

    return true;
}

static inline void get_module_name(struct exception_handler_data *data,
        struct module_info *info)
{
    data->enumerate_loaded_modules64(data->process,
            (PENUMLOADED_MODULES_CALLBACK64)enum_module, info);
}

static inline bool walk_stack(struct exception_handler_data *data,
        HANDLE thread, struct stack_trace *trace)
{
    struct module_info module_info = {0};
    DWORD64 func_offset;
    char sym_name[256];
    char *p;

    bool success = data->stack_walk64(trace->image_type,
            data->process, thread, &trace->frame, &trace->context,
            NULL, data->sym_function_table_access64,
            data->sym_get_module_base64, NULL);
    if (!success)
        return false;

    module_info.addr = trace->frame.AddrPC.Offset;
    get_module_name(data, &module_info);

    if (!!module_info.name_utf8[0]) {
        p = strrchr(module_info.name_utf8, '\\');
        p = p ? (p + 1) : module_info.name_utf8;
    } else {
        strcpy(module_info.name_utf8, "<unknown>");
        p = module_info.name_utf8;
    }

    success = !!data->sym_from_addr(data->process,
            trace->frame.AddrPC.Offset, &func_offset,
            data->sym_info);

    if (success)
        sprintf_s(sym_name, 256,
                  QString::fromWCharArray(data->sym_info->Name).toUtf8().constData());

#define SUCCESS_FORMAT \
    "%08.8I64X %08.8I64X %08.8I64X %08.8I64X " \
    "%08.8I64X %08.8I64X %s!%s+0x%I64x\r\n"
#define FAIL_FORMAT \
    "%08.8I64X %08.8I64X %08.8I64X %08.8I64X " \
    "%08.8I64X %08.8I64X %s!0x%I64x\r\n"

    trace->frame.AddrStack.Offset &= 0xFFFFFFFFF;
    trace->frame.AddrPC.Offset &= 0xFFFFFFFFF;
    trace->frame.Params[0] &= 0xFFFFFFFF;
    trace->frame.Params[1] &= 0xFFFFFFFF;
    trace->frame.Params[2] &= 0xFFFFFFFF;
    trace->frame.Params[3] &= 0xFFFFFFFF;

    if (success && (data->sym_info->Flags & SYMFLAG_EXPORT) == 0) {
        char dst[512];
        sprintf_s(dst, 512, SUCCESS_FORMAT,
                  trace->frame.AddrStack.Offset,
                  trace->frame.AddrPC.Offset,
                  trace->frame.Params[0],
                trace->frame.Params[1],
                trace->frame.Params[2],
                trace->frame.Params[3],
                p, sym_name, func_offset);
        data->str += dst;
    } else {
        char dst[512];
        sprintf_s(dst, 512, FAIL_FORMAT,
                  trace->frame.AddrStack.Offset,
                  trace->frame.AddrPC.Offset,
                  trace->frame.Params[0],
                trace->frame.Params[1],
                trace->frame.Params[2],
                trace->frame.Params[3],
                p, trace->frame.AddrPC.Offset);
        data->str += dst;
    }

    return true;
}

#define TRACE_TOP \
    "Stack    EIP      Arg0     " \
    "Arg1     Arg2     Arg3     Address\r\n"

static inline void write_thread_trace(struct exception_handler_data *data,
        THREADENTRY32 *entry, bool first_thread)
{
    bool crash_thread = entry->th32ThreadID == GetCurrentThreadId();
    struct stack_trace trace = {0};
    struct stack_trace *ptrace;
    HANDLE thread;

    if (first_thread != crash_thread)
        return;

    if (entry->th32OwnerProcessID != GetCurrentProcessId())
        return;

    thread = OpenThread(THREAD_ALL_ACCESS, false, entry->th32ThreadID);
    if (!thread)
        return;

    trace.context.ContextFlags = CONTEXT_ALL;
    GetThreadContext(thread, &trace.context);
    init_instruction_data(&trace);

    char dst[256];
    sprintf_s(dst, 256, "\r\nThread %lX%s\r\n"TRACE_TOP,
            entry->th32ThreadID,
            crash_thread ? " (Crashed)" : "");
    data->str += dst;

    ptrace = crash_thread ? &data->main_trace : &trace;

    while (walk_stack(data, thread, ptrace));

    CloseHandle(thread);
}

static inline void write_thread_traces(struct exception_handler_data *data)
{
    THREADENTRY32 entry = {0};
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,
            GetCurrentProcessId());
    bool success;

    if (snapshot == INVALID_HANDLE_VALUE)
        return;

    entry.dwSize = sizeof(entry);

    success = !!Thread32First(snapshot, &entry);
    while (success) {
        write_thread_trace(data, &entry, true);
        success = !!Thread32Next(snapshot, &entry);
    }

    success = !!Thread32First(snapshot, &entry);
    while (success) {
        write_thread_trace(data, &entry, false);
        success = !!Thread32Next(snapshot, &entry);
    }

    CloseHandle(snapshot);
}

static inline void write_module_list(struct exception_handler_data *data)
{
    data->str += "\r\nLoaded modules:\r\n";
    data->str += "Base Address      Module\r\n";
    data->str += data->module_list;
}

/* ------------------------------------------------------------------------- */

static inline void handle_exception(struct exception_handler_data *data,
        PEXCEPTION_POINTERS exception)
{
    if (!get_dbghelp_imports(data))
        return;

    data->exception = exception;
    data->process = GetCurrentProcess();
    data->main_trace.context = *exception->ContextRecord;
    GetSystemTime(&data->time_info);

    init_sym_info(data);
    init_version_info(data);
    init_cpu_info(data);
    init_instruction_data(&data->main_trace);
    init_module_info(data);

    write_header(data);
    write_thread_traces(data);
    write_module_list(data);
}

QString ThreadTracesHandler(PEXCEPTION_POINTERS exception)
{
    struct exception_handler_data data = {0};
    static bool inside_handler = false;

    QString ret;

    // if (IsDebuggerPresent())
    //     return ret;

    if (inside_handler)
        return ret;

    inside_handler = true;

    handle_exception(&data, exception);
    ret = data.str;
    exception_handler_data_free(&data);

    inside_handler = false;

    return ret;
}
