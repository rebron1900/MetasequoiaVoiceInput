#include "mvi_logger.h"
#include "mvi_utils.h"
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <windows.h>

namespace
{
std::mutex g_mutex;
std::ofstream g_file;
}

void mvi_logger::Initialize(const std::string &path, bool enabled)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!enabled) return;
    std::filesystem::path log_path = std::filesystem::u8path(path);
    if (log_path.is_relative()) log_path = std::filesystem::path(mvi_utils::GetExecutableDirectory()) / log_path;
    std::error_code error;
    std::filesystem::create_directories(log_path.parent_path(), error);
    g_file.open(log_path, std::ios::app);
}

void mvi_logger::Write(const std::string &category, const std::string &message)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    std::ostringstream line;
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    line << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") << " [" << category << "] " << message;
    const std::string text = line.str();
    OutputDebugStringW((mvi_utils::utf8_to_wstring(text + "\n")).c_str());
    if (g_file.is_open())
    {
        g_file << text << '\n';
        g_file.flush();
    }
}
