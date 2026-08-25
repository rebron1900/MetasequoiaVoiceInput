#include "mvi_config.h"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <windows.h>

#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>

namespace
{
thread_local std::string g_last_load_error;

mvi_config::RuntimeConfig DefaultConfig()
{
    mvi_config::RuntimeConfig config;
    config.asr.provider = "siliconflow";
    config.asr.endpoint = "https://api.siliconflow.cn/v1/audio/transcriptions";
    config.polish.provider = "siliconflow";
    config.polish.endpoint = "https://api.siliconflow.cn/v1/chat/completions";
    return config;
}

std::filesystem::path GetExecutableDirectory()
{
    std::wstring path(MAX_PATH, L'\0');
    while (true)
    {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0)
        {
            return {};
        }
        if (length < path.size())
        {
            path.resize(length);
            return std::filesystem::path(path).parent_path();
        }
        path.resize(path.size() * 2);
    }
}

std::filesystem::path GetLegacyConfigPath()
{
    wchar_t local_app_data[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, static_cast<DWORD>(std::size(local_app_data)));
    if (length == 0 || length >= std::size(local_app_data))
    {
        return {};
    }
    return std::filesystem::path(local_app_data) / L"MetasequoiaVoiceInput" / L"config.toml";
}

bool CopyFileIfMissing(const std::filesystem::path &source, const std::filesystem::path &destination, std::string *error_message)
{
    if (std::filesystem::exists(destination) || !std::filesystem::exists(source))
    {
        return true;
    }

    std::error_code error;
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, error);
    if (error)
    {
        if (error_message != nullptr)
        {
            *error_message = std::string("无法创建 config.toml: ") + error.message();
        }
        return false;
    }
    return true;
}

bool CopyLegacyConfigIfNeeded(const std::filesystem::path &config_path, std::string *error_message)
{
    return CopyFileIfMissing(GetLegacyConfigPath(), config_path, error_message);
}

bool CopyTemplateConfigIfNeeded(const std::filesystem::path &config_path, std::string *error_message)
{
    return CopyFileIfMissing(config_path.parent_path() / L"templates" / L"config.toml", config_path, error_message);
}

void LoadApiConfig(const toml::table &table, const char *section, mvi_config::ApiConfig &config)
{
    if (const auto provider = table[section]["provider"]; provider.is_string())
    {
        config.provider = provider.value_or(config.provider);
    }
    if (const auto token = table[section]["token"]; token.is_string())
    {
        config.token = token.value_or("");
    }
    if (const auto endpoint = table[section]["endpoint"]; endpoint.is_string())
    {
        config.endpoint = endpoint.value_or(config.endpoint);
    }
}

void AssignStringIfPresent(const nlohmann::json &object, const char *key, std::string &target)
{
    if (object.contains(key) && object[key].is_string())
    {
        target = object[key].get<std::string>();
    }
}

void AssignBoolIfPresent(const nlohmann::json &object, const char *key, bool &target)
{
    if (object.contains(key) && object[key].is_boolean())
    {
        target = object[key].get<bool>();
    }
}

bool IsSafeEndpoint(const std::string &endpoint)
{
    if (endpoint.empty())
    {
        return false;
    }

    static const std::regex https_url(R"(^https://[^\s/]+(?:/[^\s]*)?$)", std::regex::icase);
    static const std::regex localhost_url(R"(^http://(localhost|127\.0\.0\.1|\[::1\])(?::[0-9]+)?(?:/[^\s]*)?$)", std::regex::icase);
    return std::regex_match(endpoint, https_url) || std::regex_match(endpoint, localhost_url);
}

bool IsSafeStreamingEndpoint(const std::string &endpoint)
{
    static const std::regex wss_url(R"(^wss://[^\s/]+(?:/[^\s]*)?$)", std::regex::icase);
    static const std::regex localhost_ws_url(R"(^ws://(localhost|127\.0\.0\.1|\[::1\])(?::[0-9]+)?(?:/[^\s]*)?$)", std::regex::icase);
    return std::regex_match(endpoint, wss_url) || std::regex_match(endpoint, localhost_ws_url);
}

bool ValidateEndpoints(const mvi_config::RuntimeConfig &config, std::string *error_message)
{
    if (config.stt_provider == "json_websocket_streaming")
    {
        if (!IsSafeStreamingEndpoint(config.asr.endpoint))
        {
            if (error_message != nullptr)
            {
                *error_message = "流式 ASR 地址必须使用 WSS，或使用 localhost WS 开发地址";
            }
            return false;
        }
    }
    else if (!IsSafeEndpoint(config.asr.endpoint))
    {
        if (error_message != nullptr)
        {
            *error_message = "ASR API 地址必须使用 HTTPS，或使用 localhost 开发地址";
        }
        return false;
    }
    if (!IsSafeEndpoint(config.polish.endpoint))
    {
        if (error_message != nullptr)
        {
            *error_message = "文本处理 API 地址必须使用 HTTPS，或使用 localhost 开发地址";
        }
        return false;
    }
    return true;
}

toml::table ToToml(const mvi_config::RuntimeConfig &config)
{
    toml::table table;
    table.insert_or_assign("asr_api", toml::table{{"provider", config.asr.provider}, {"token", config.asr.token}, {"endpoint", config.asr.endpoint}});
    table.insert_or_assign("polish_api", toml::table{{"provider", config.polish.provider}, {"token", config.polish.token}, {"endpoint", config.polish.endpoint}});
    table.insert_or_assign("settings", toml::table{{"language", config.language}, {"polish_text", config.polish_text}, {"notification_sound", config.notification_sound}, {"stt_provider", config.stt_provider}, {"output_method", config.output_method}, {"streaming_chunk_ms", config.streaming_chunk_ms}});
    return table;
}
} // namespace

std::string mvi_config::GetConfigPath()
{
    const std::filesystem::path directory = GetExecutableDirectory();
    if (directory.empty())
    {
        return "";
    }
    return (directory / L"config.toml").u8string();
}

mvi_config::RuntimeConfig mvi_config::LoadRuntimeConfig()
{
    g_last_load_error.clear();
    RuntimeConfig config = DefaultConfig();
    const std::string config_path = GetConfigPath();
    if (config_path.empty())
    {
        return config;
    }

    try
    {
        const std::filesystem::path config_file = std::filesystem::u8path(config_path);
        if (!CopyLegacyConfigIfNeeded(config_file, &g_last_load_error) || !CopyTemplateConfigIfNeeded(config_file, &g_last_load_error))
        {
            return config;
        }
        if (!std::filesystem::exists(config_file))
        {
            return config;
        }

        const toml::table table = toml::parse_file(config_path);
        LoadApiConfig(table, "asr_api", config.asr);
        LoadApiConfig(table, "polish_api", config.polish);

        if (const auto language = table["settings"]["language"]; language.is_string())
        {
            config.language = language.value_or(config.language);
        }
        if (const auto polish_text = table["settings"]["polish_text"]; polish_text.is_boolean())
        {
            config.polish_text = polish_text.value_or(config.polish_text);
        }
        if (const auto notification_sound = table["settings"]["notification_sound"]; notification_sound.is_boolean())
        {
            config.notification_sound = notification_sound.value_or(config.notification_sound);
        }
        if (const auto stt_provider = table["settings"]["stt_provider"]; stt_provider.is_string())
        {
            config.stt_provider = stt_provider.value_or(config.stt_provider);
        }
        if (const auto output_method = table["settings"]["output_method"]; output_method.is_string())
        {
            config.output_method = output_method.value_or(config.output_method);
        }
        if (const auto streaming_chunk_ms = table["settings"]["streaming_chunk_ms"]; streaming_chunk_ms.is_integer())
        {
            config.streaming_chunk_ms = streaming_chunk_ms.value_or(config.streaming_chunk_ms);
        }
    }
    catch (const std::exception &e)
    {
        g_last_load_error = std::string("无法读取 config.toml: ") + e.what();
        return config;
    }

    if (config.stt_provider != "cloud_siliconflow" && config.stt_provider != "json_websocket_streaming")
    {
        g_last_load_error = "未知的 ASR 服务类型";
        return DefaultConfig();
    }
    if (config.output_method != "send_input" && config.output_method != "clipboard_paste")
    {
        g_last_load_error = "未知的上屏方式";
        return DefaultConfig();
    }
    if (config.streaming_chunk_ms < 20 || config.streaming_chunk_ms > 200)
    {
        g_last_load_error = "流式音频分片必须在 20 到 200 毫秒之间";
        return DefaultConfig();
    }
    if (!ValidateEndpoints(config, &g_last_load_error))
    {
        return DefaultConfig();
    }

    return config;
}

std::string mvi_config::GetLastLoadError()
{
    return g_last_load_error;
}

bool mvi_config::IsSafeApiEndpoint(const std::string &endpoint)
{
    return IsSafeEndpoint(endpoint);
}

bool mvi_config::IsSafeStreamingEndpoint(const std::string &endpoint)
{
    return ::IsSafeStreamingEndpoint(endpoint);
}

std::string mvi_config::ReadConfigAsJson()
{
    const RuntimeConfig config = LoadRuntimeConfig();
    const nlohmann::json root = {
        {"asr_api", {{"provider", config.asr.provider}, {"token", config.asr.token}, {"endpoint", config.asr.endpoint}}},
        {"polish_api", {{"provider", config.polish.provider}, {"token", config.polish.token}, {"endpoint", config.polish.endpoint}}},
        {"settings", {{"language", config.language}, {"polish_text", config.polish_text}, {"notification_sound", config.notification_sound}, {"stt_provider", config.stt_provider}, {"output_method", config.output_method}, {"streaming_chunk_ms", config.streaming_chunk_ms}}},
    };
    return root.dump();
}

bool mvi_config::WriteConfigFromJson(const std::string &config_json, std::string *error_message)
{
    const auto set_error = [error_message](const std::string &message) {
        if (error_message != nullptr)
        {
            *error_message = message;
        }
    };

    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(config_json);
    }
    catch (const std::exception &e)
    {
        set_error(std::string("invalid json: ") + e.what());
        return false;
    }

    if (!root.is_object())
    {
        set_error("json root is not object");
        return false;
    }

    RuntimeConfig config = LoadRuntimeConfig();
    if (!GetLastLoadError().empty())
    {
        set_error(GetLastLoadError());
        return false;
    }
    if (root.contains("asr_api") && root["asr_api"].is_object())
    {
        const nlohmann::json &asr = root["asr_api"];
        AssignStringIfPresent(asr, "provider", config.asr.provider);
        AssignStringIfPresent(asr, "token", config.asr.token);
        AssignStringIfPresent(asr, "endpoint", config.asr.endpoint);
    }
    if (root.contains("polish_api") && root["polish_api"].is_object())
    {
        const nlohmann::json &polish = root["polish_api"];
        AssignStringIfPresent(polish, "provider", config.polish.provider);
        AssignStringIfPresent(polish, "token", config.polish.token);
        AssignStringIfPresent(polish, "endpoint", config.polish.endpoint);
    }
    if (root.contains("settings") && root["settings"].is_object())
    {
        const nlohmann::json &settings = root["settings"];
        AssignStringIfPresent(settings, "language", config.language);
        AssignBoolIfPresent(settings, "polish_text", config.polish_text);
        AssignBoolIfPresent(settings, "notification_sound", config.notification_sound);
        AssignStringIfPresent(settings, "stt_provider", config.stt_provider);
        AssignStringIfPresent(settings, "output_method", config.output_method);
        if (settings.contains("streaming_chunk_ms") && settings["streaming_chunk_ms"].is_number_integer())
        {
            config.streaming_chunk_ms = settings["streaming_chunk_ms"].get<int>();
        }
    }

    if (config.output_method != "send_input" && config.output_method != "clipboard_paste")
    {
        set_error("未知的上屏方式");
        return false;
    }

    if (config.stt_provider != "cloud_siliconflow" && config.stt_provider != "json_websocket_streaming")
    {
        set_error("未知的 ASR 服务类型");
        return false;
    }

    if (config.streaming_chunk_ms < 20 || config.streaming_chunk_ms > 200)
    {
        set_error("流式音频分片必须在 20 到 200 毫秒之间");
        return false;
    }

    if (!ValidateEndpoints(config, error_message))
    {
        return false;
    }

    const std::string config_path = GetConfigPath();
    if (config_path.empty())
    {
        set_error("unable to determine executable directory");
        return false;
    }

    try
    {
        const std::filesystem::path destination = std::filesystem::u8path(config_path);
        const std::filesystem::path temporary = destination.wstring() + L".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output.is_open())
            {
                set_error("open temporary config file failed");
                return false;
            }
            output << ToToml(config);
            output.close();
            if (!output)
            {
                set_error("write temporary config file failed");
                return false;
            }
        }

        if (std::filesystem::exists(destination))
        {
            if (!ReplaceFileW(destination.c_str(), temporary.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr))
            {
                const DWORD replace_error = GetLastError();
                std::filesystem::remove(temporary);
                set_error("replace config file failed: " + std::to_string(replace_error));
                return false;
            }
        }
        else
        {
            std::error_code error;
            std::filesystem::rename(temporary, destination, error);
            if (error)
            {
                std::filesystem::remove(temporary);
                set_error(std::string("create config file failed: ") + error.message());
                return false;
            }
        }
    }
    catch (const std::exception &e)
    {
        set_error(std::string("write config exception: ") + e.what());
        return false;
    }

    if (error_message != nullptr)
    {
        error_message->clear();
    }
    return true;
}
