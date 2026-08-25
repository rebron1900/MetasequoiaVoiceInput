#include "mvi_utils.h"

#include <filesystem>
#include <windows.h>

// Convert UTF-8 std::string to std::wstring
std::wstring mvi_utils::utf8_to_wstring(const std::string &str)
{
    if (str.empty())
        return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

/**
 * @brief Get the Taskbar Height
 *
 * @return int
 */
int mvi_utils::GetTaskbarHeight()
{
    APPBARDATA abd{};
    abd.cbSize = sizeof(abd);

    if (!SHAppBarMessage(ABM_GETTASKBARPOS, &abd))
        return 0;

    RECT &r = abd.rc;

    switch (abd.uEdge)
    {
    case ABE_BOTTOM:
    case ABE_TOP:
        return r.bottom - r.top; // 高度
    case ABE_LEFT:
    case ABE_RIGHT:
        return r.right - r.left; // 宽度（竖向任务栏）
    }

    return 0;
}

FLOAT GetWindowScale(HWND hwnd)
{
    UINT dpi = GetDpiForWindow(hwnd);
    FLOAT scale = dpi / 96.0f;
    return scale;
}

FLOAT mvi_utils::GetForegroundWindowScale()
{
    HWND hwnd = GetForegroundWindow();
    FLOAT scale = GetWindowScale(hwnd);
    return scale;
}

/**
 * @brief 获取当前的窗口所在的显示器的坐标信息
 *
 * @return MonitorCoordinates
 */
RECT mvi_utils::GetMonitorCoordinates()
{
    RECT coordinates = {0};
    HWND hwnd = GetForegroundWindow();
    FLOAT scale = GetWindowScale(hwnd);
    HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!hMonitor)
    {
        return coordinates;
    }

    MONITORINFO monitorInfo = {sizeof(monitorInfo)};
    if (GetMonitorInfo(hMonitor, &monitorInfo))
    {
        int width = (monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left);
        int height = (monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top);
        coordinates.left = monitorInfo.rcMonitor.left;
        coordinates.top = monitorInfo.rcMonitor.top;
        coordinates.right = coordinates.left + width;
        coordinates.bottom = coordinates.top + height;
    }
    else
    {
    }
    return coordinates;
}

RECT mvi_utils::GetMainMonitorCoordinates()
{
    RECT coordinates{};

    HMONITOR hPrimary = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);

    if (GetMonitorInfo(hPrimary, &mi))
    {
        coordinates.left = mi.rcMonitor.left;
        coordinates.top = mi.rcMonitor.top;
        coordinates.right = mi.rcMonitor.right;
        coordinates.bottom = mi.rcMonitor.bottom;
    }

    return coordinates;
}

std::wstring mvi_utils::GetExecutableDirectory()
{
    std::wstring path(MAX_PATH, L'\0');
    while (true)
    {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0)
        {
            return L"";
        }
        if (length < path.size())
        {
            path.resize(length);
            return std::filesystem::path(path).parent_path().wstring();
        }
        path.resize(path.size() * 2);
    }
}

std::wstring mvi_utils::resolve_asset_audio_path(const std::string &filename)
{
    const std::wstring directory = GetExecutableDirectory();
    if (directory.empty())
    {
        return L"";
    }
    return (std::filesystem::path(directory) / L"audios" / utf8_to_wstring(filename)).wstring();
}
