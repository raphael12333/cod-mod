#include <std_include.hpp>

#include "loader/loader.hpp"
#include "loader/component_loader.hpp"

#include <utils/io.hpp>
#include <utils/string.hpp>

#include <DbgHelp.h>

#include <version.hpp>

const char* get_current_date()
{
    auto now = std::chrono::system_clock::now();
    auto current_time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};

    (void)localtime_s(&local_time, &current_time);

    std::stringstream ss;
    ss << std::put_time(&local_time, "%Y%m%d_%H%M%S");

    const auto result = ss.str();
    return utils::string::va("%s", result.data());
}

LONG WINAPI exception_handler(PEXCEPTION_POINTERS exception_info)
{
    if (exception_info->ExceptionRecord->ExceptionCode == 0x406D1388)
    {
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    if (exception_info->ExceptionRecord->ExceptionCode < 0x80000000 ||
        exception_info->ExceptionRecord->ExceptionCode == 0xE06D7363)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    MINIDUMP_EXCEPTION_INFORMATION exception_information =
    {
        GetCurrentThreadId(), exception_info, FALSE
    };

    const auto type = MiniDumpIgnoreInaccessibleMemory
        | MiniDumpWithHandleData
        | MiniDumpScanMemory
        | MiniDumpWithProcessThreadData
        | MiniDumpWithFullMemoryInfo
        | MiniDumpWithThreadInfo;

    CreateDirectoryA("minidumps", nullptr);
    const auto* file_name = utils::string::va("minidumps\\cod-mod_%s_%s.dmp", SHORTVERSION, get_current_date());
    constexpr auto file_share = FILE_SHARE_READ | FILE_SHARE_WRITE;

    const auto file_handle = CreateFileA(file_name, GENERIC_WRITE | GENERIC_READ, file_share, nullptr, CREATE_ALWAYS, NULL, nullptr);

    if (!MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
        file_handle, static_cast<MINIDUMP_TYPE>(type),
        &exception_information, nullptr, nullptr))
    {
        char buf[4096]{};
        sprintf_s(buf, "An exception 0x%08X occurred at location 0x%p\n",
            exception_info->ExceptionRecord->ExceptionCode,
            exception_info->ExceptionRecord->ExceptionAddress);
        //game::show_error(buf);
        MessageBoxA(nullptr, buf, "cod-mod", MB_ICONINFORMATION);
    }

    CloseHandle(file_handle);
    TerminateProcess(GetCurrentProcess(), exception_info->ExceptionRecord->ExceptionCode);

    return EXCEPTION_CONTINUE_SEARCH;
}

[[noreturn]] void WINAPI exit_hook(const int code)
{
    component_loader::pre_destroy();
    std::exit(code);
}

BOOL WINAPI system_parameters_info_a(const UINT uiAction, const UINT uiParam, const PVOID pvParam, const UINT fWinIni)
{
    component_loader::post_unpack();
    return SystemParametersInfoA(uiAction, uiParam, pvParam, fWinIni);
}

FARPROC WINAPI get_proc_address(const HMODULE hModule, const LPCSTR lpProcName)
{
    if (lpProcName == "GlobalMemoryStatusEx"s)
    {
        component_loader::post_unpack();
    }

    return GetProcAddress(hModule, lpProcName);
}

FARPROC load_binary()
{
    loader loader;
    utils::nt::library self;

    loader.set_import_resolver([self](const std::string& library, const std::string& function) -> void*
        {
            if (function == "ExitProcess")
            {
                return exit_hook;
            }

            if (function == "SystemParametersInfoA")
            {
                return system_parameters_info_a;
            }

            if (function == "GetProcAddress")
            {
                return get_proc_address;
            }

            return component_loader::load_import(library, function);
        });

    std::string binary;
    binary = "CoDMP.exe";

    std::string data;
    if (!utils::io::read_file(binary, &data))
    {
        throw std::runtime_error(
            "Failed to read game binary! Please select the correct path in the launcher settings.");
    }

    return loader.load(self, data);
}

void remove_crash_file()
{
    const utils::nt::library self;
    auto name = self.get_name();
    name = std::filesystem::path(name).replace_extension("").generic_string();

    utils::io::remove_file("__" + name);
    utils::io::remove_file("__codmp");
}

void limit_parallel_dll_loading()
{
    const utils::nt::library self;
    const auto registry_path = R"(Software\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\)" + self.
        get_name();

    HKEY key = nullptr;
    if (RegCreateKeyA(HKEY_LOCAL_MACHINE, registry_path.data(), &key) == ERROR_SUCCESS)
    {
        RegCloseKey(key);
    }

    key = nullptr;
    if (RegOpenKeyExA(
        HKEY_LOCAL_MACHINE, registry_path.data(), 0,
        KEY_ALL_ACCESS, &key) != ERROR_SUCCESS)
    {
        return;
    }

    DWORD value = 1;
    RegSetValueExA(key, "MaxLoaderThreads", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));

    RegCloseKey(key);
}

void enable_dpi_awareness()
{
    const utils::nt::library user32{ "user32.dll" };
    const auto set_dpi = user32
        ? user32.get_proc<BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT)>("SetProcessDpiAwarenessContext")
        : nullptr;
    if (set_dpi)
    {
        set_dpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
}

void apply_environment()
{
    char* buffer{};
    std::size_t size{};
    if (_dupenv_s(&buffer, &size, "GHOSTS_INSTALL") != 0 || buffer == nullptr)
    {
        return;
    }

    const auto _ = gsl::finally([&]
        {
            std::free(buffer);
        });

    SetCurrentDirectoryA(buffer);
    SetDllDirectoryA(buffer);
}

int main()
{
    AddVectoredExceptionHandler(0, exception_handler);
    SetProcessDEPPolicy(PROCESS_DEP_ENABLE);

    FARPROC entry_point;
    //enable_dpi_awareness();

    // This requires admin privilege, but I suppose many
    // people will start with admin rights if it crashes.
    //limit_parallel_dll_loading();

    std::srand(static_cast<std::uint32_t>(time(nullptr)) ^ ~(GetTickCount() * GetCurrentProcessId()));

    {
        auto premature_shutdown = true;
        const auto _ = gsl::finally([&premature_shutdown]()
            {
                if (premature_shutdown)
                {
                    component_loader::pre_destroy();
                }
            });

        try
        {
            //apply_environment();

            remove_crash_file();

            if (!component_loader::post_start()) return 0;

            entry_point = load_binary();
            if (!entry_point)
            {
                throw std::runtime_error("Unable to load binary into memory");
            }

            if (!component_loader::post_load()) return 0;

            premature_shutdown = false;
        }
        catch (std::exception& ex)
        {
            //game::show_error(ex.what());
            MessageBoxA(nullptr, ex.what(), "cod-mod", MB_ICONINFORMATION);
            return 1;
        }
    }

    return static_cast<int>(entry_point());
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, PSTR, int)
{
    return main();
}