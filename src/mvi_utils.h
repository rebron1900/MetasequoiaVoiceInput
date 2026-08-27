//
// MetasequoiaVoiceInput Utils
//
#pragma once

#include <string>
#include <Windows.h>

// Convert UTF-8 std::string to std::wstring
namespace mvi_utils
{
std::wstring utf8_to_wstring(const std::string &str);
int GetTaskbarHeight();
RECT GetMonitorCoordinates();
RECT GetMainMonitorCoordinates();
std::wstring resolve_asset_audio_path(const std::string &filename);
std::wstring GetExecutableDirectory();
FLOAT GetForegroundWindowScale();
} // namespace mvi_utils
