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
    config.asr.provider = "cloud_siliconflow";
    config.asr.endpoint = "https://api.siliconflow.cn/v1/audio/transcriptions";
    config.asr.model = "TeleAI/TeleSpeechASR";
    config.asr.model_type = "ggml-base";
    config.asr.chunk_ms = 40;
    config.polish.provider = "siliconflow";
    config.polish.endpoint = "https://api.siliconflow.cn/v1/chat/completions";
    config.polish.model = "Qwen/Qwen3-8B";
    config.polish.prompt = "你是语音输入法的文本清洗器，只做最小必要修改。\n- 删除无意义停顿词（嗯、啊、哦、呃等）\n- 删除明显重复\n- 不润色、不扩写、不改写句式\n只输出最终文本。";
    config.asr_profiles.emplace("default", config.asr);
    config.polish_profiles.emplace("default", config.polish);
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
    if (const auto model = table[section]["model"]; model.is_string())
    {
        config.model = model.value_or(config.model);
    }
    if (const auto prompt = table[section]["prompt"]; prompt.is_string())
    {
        config.prompt = prompt.value_or(config.prompt);
    }
    if (const auto model_type = table[section]["model_type"]; model_type.is_string())
    {
        config.model_type = model_type.value_or(config.model_type);
    }
    if (const auto chunk_ms = table[section]["chunk_ms"]; chunk_ms.is_integer())
    {
        config.chunk_ms = chunk_ms.value_or(config.chunk_ms);
    }
}

void LoadApiProfiles(const toml::table &table, const char *section, std::map<std::string, mvi_config::ApiConfig> &profiles)
{
    if (const auto *profiles_table = table[section].as_table())
    {
        for (const auto &[name, value] : *profiles_table)
        {
            if (const auto *profile = value.as_table())
            {
                mvi_config::ApiConfig config;
                if (const auto provider = (*profile)["provider"]; provider.is_string()) config.provider = provider.value_or("");
                if (const auto token = (*profile)["token"]; token.is_string()) config.token = token.value_or("");
                if (const auto endpoint = (*profile)["endpoint"]; endpoint.is_string()) config.endpoint = endpoint.value_or("");
                if (const auto model = (*profile)["model"]; model.is_string()) config.model = model.value_or("");
                if (const auto prompt = (*profile)["prompt"]; prompt.is_string()) config.prompt = prompt.value_or("");
                if (const auto model_type = (*profile)["model_type"]; model_type.is_string()) config.model_type = model_type.value_or("");
                if (const auto chunk_ms = (*profile)["chunk_ms"]; chunk_ms.is_integer()) config.chunk_ms = chunk_ms.value_or(40);
                profiles[std::string(name.str())] = std::move(config);
            }
        }
    }
}

 toml::table ApiConfigTable(const mvi_config::ApiConfig &config)
{
    return toml::table{{"provider", config.provider}, {"token", config.token}, {"endpoint", config.endpoint}, {"model", config.model}, {"prompt", config.prompt}, {"model_type", config.model_type}, {"chunk_ms", config.chunk_ms}};
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

void AssignApiProfilesIfPresent(const nlohmann::json &object, std::map<std::string, mvi_config::ApiConfig> &profiles)
{
    if (!object.is_object()) return;
    for (const auto &[name, value] : object.items())
    {
        if (!value.is_object()) continue;
        mvi_config::ApiConfig profile;
        AssignStringIfPresent(value, "provider", profile.provider);
        AssignStringIfPresent(value, "token", profile.token);
        AssignStringIfPresent(value, "endpoint", profile.endpoint);
        AssignStringIfPresent(value, "model", profile.model);
        AssignStringIfPresent(value, "prompt", profile.prompt);
        AssignStringIfPresent(value, "model_type", profile.model_type);
        if (value.contains("chunk_ms") && value["chunk_ms"].is_number_integer()) profile.chunk_ms = value["chunk_ms"].get<int>();
        profiles[name] = std::move(profile);
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
    if (config.stt_provider == "local_whisper")
    {
        if (config.asr.model.empty())
        {
            if (error_message != nullptr) *error_message = "本地 Whisper 模型路径不能为空";
            return false;
        }
        return !config.polish_text || IsSafeEndpoint(config.polish.endpoint);
    }
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
    table.insert_or_assign("asr_api", ApiConfigTable(config.asr));
    table.insert_or_assign("polish_api", ApiConfigTable(config.polish));
    toml::table asr_profiles;
    for (const auto &[name, profile] : config.asr_profiles) asr_profiles.insert_or_assign(name, ApiConfigTable(profile));
    toml::table polish_profiles;
    for (const auto &[name, profile] : config.polish_profiles) polish_profiles.insert_or_assign(name, ApiConfigTable(profile));
    table.insert_or_assign("asr_profiles", std::move(asr_profiles));
    table.insert_or_assign("polish_profiles", std::move(polish_profiles));
    table.insert_or_assign("settings", toml::table{{"active_asr_profile", config.active_asr_profile}, {"active_polish_profile", config.active_polish_profile}, {"language", config.language}, {"polish_text", config.polish_text}, {"notification_sound", config.notification_sound}, {"activation_key", config.activation_key}, {"debug_logging", config.debug_logging}, {"log_file", config.log_file}, {"stt_provider", config.stt_provider}, {"output_method", config.output_method}, {"streaming_chunk_ms", config.streaming_chunk_ms}});
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
        config.asr_profiles["default"] = config.asr;
        config.polish_profiles["default"] = config.polish;
        LoadApiProfiles(table, "asr_profiles", config.asr_profiles);
        LoadApiProfiles(table, "polish_profiles", config.polish_profiles);

        if (const auto active_asr_profile = table["settings"]["active_asr_profile"]; active_asr_profile.is_string())
        {
            config.active_asr_profile = active_asr_profile.value_or(config.active_asr_profile);
        }
        if (const auto active_polish_profile = table["settings"]["active_polish_profile"]; active_polish_profile.is_string())
        {
            config.active_polish_profile = active_polish_profile.value_or(config.active_polish_profile);
        }
        if (const auto debug_logging = table["settings"]["debug_logging"]; debug_logging.is_boolean())
        {
            config.debug_logging = debug_logging.value_or(config.debug_logging);
        }
        if (const auto log_file = table["settings"]["log_file"]; log_file.is_string())
        {
            config.log_file = log_file.value_or(config.log_file);
        }
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
        if (const auto activation_key = table["settings"]["activation_key"]; activation_key.is_string())
        {
            config.activation_key = activation_key.value_or(config.activation_key);
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
        if (!table["asr_profiles"].is_table())
        {
            config.asr.provider = config.stt_provider;
            config.asr_profiles["default"] = config.asr;
        }
        if (const auto active = config.asr_profiles.find(config.active_asr_profile); active != config.asr_profiles.end())
        {
            config.asr = active->second;
            if (table["asr_profiles"].is_table() && (config.asr.provider == "local_whisper" || config.asr.provider == "cloud_siliconflow" || config.asr.provider == "json_websocket_streaming"))
            {
                config.stt_provider = config.asr.provider;
            }
        }
        if (const auto active = config.polish_profiles.find(config.active_polish_profile); active != config.polish_profiles.end()) config.polish = active->second;
    }
    catch (const std::exception &e)
    {
        g_last_load_error = std::string("无法读取 config.toml: ") + e.what();
        return config;
    }

    if (config.stt_provider != "cloud_siliconflow" && config.stt_provider != "json_websocket_streaming" && config.stt_provider != "local_whisper")
    {
        g_last_load_error = "未知的 ASR 服务类型";
        return DefaultConfig();
    }
    if (config.output_method != "send_input" && config.output_method != "clipboard_paste")
    {
        g_last_load_error = "未知的上屏方式";
        return DefaultConfig();
    }
    if (config.asr.chunk_ms < 20 || config.asr.chunk_ms > 200)
    {
        g_last_load_error = "ASR 音频分片必须在 20 到 200 毫秒之间";
        return DefaultConfig();
    }
    if (config.polish.prompt.empty())
    {
        config.polish.prompt = DefaultConfig().polish.prompt;
        config.polish_profiles[config.active_polish_profile].prompt = config.polish.prompt;
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
    nlohmann::json asr_profiles = nlohmann::json::object();
    for (const auto &[name, profile] : config.asr_profiles) asr_profiles[name] = {{"provider", profile.provider}, {"token", profile.token}, {"endpoint", profile.endpoint}, {"model", profile.model}, {"model_type", profile.model_type}, {"chunk_ms", profile.chunk_ms}};
    nlohmann::json polish_profiles = nlohmann::json::object();
    for (const auto &[name, profile] : config.polish_profiles) polish_profiles[name] = {{"provider", profile.provider}, {"token", profile.token}, {"endpoint", profile.endpoint}, {"model", profile.model}, {"prompt", profile.prompt}};
    const nlohmann::json root = {
        {"asr_api", {{"provider", config.asr.provider}, {"token", config.asr.token}, {"endpoint", config.asr.endpoint}, {"model", config.asr.model}, {"model_type", config.asr.model_type}, {"chunk_ms", config.asr.chunk_ms}}},
        {"polish_api", {{"provider", config.polish.provider}, {"token", config.polish.token}, {"endpoint", config.polish.endpoint}, {"model", config.polish.model}, {"prompt", config.polish.prompt}}},
        {"asr_profiles", asr_profiles},
        {"polish_profiles", polish_profiles},
        {"settings", {{"active_asr_profile", config.active_asr_profile}, {"active_polish_profile", config.active_polish_profile}, {"language", config.language}, {"polish_text", config.polish_text}, {"notification_sound", config.notification_sound}, {"activation_key", config.activation_key}, {"debug_logging", config.debug_logging}, {"log_file", config.log_file}, {"stt_provider", config.stt_provider}, {"output_method", config.output_method}, {"streaming_chunk_ms", config.streaming_chunk_ms}}},
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
        AssignStringIfPresent(asr, "model", config.asr.model);
        AssignStringIfPresent(asr, "model_type", config.asr.model_type);
        if (asr.contains("chunk_ms") && asr["chunk_ms"].is_number_integer()) config.asr.chunk_ms = asr["chunk_ms"].get<int>();
        config.asr_profiles[config.active_asr_profile] = config.asr;
    }
    if (root.contains("polish_api") && root["polish_api"].is_object())
    {
        const nlohmann::json &polish = root["polish_api"];
        AssignStringIfPresent(polish, "provider", config.polish.provider);
        AssignStringIfPresent(polish, "token", config.polish.token);
        AssignStringIfPresent(polish, "endpoint", config.polish.endpoint);
        AssignStringIfPresent(polish, "model", config.polish.model);
        AssignStringIfPresent(polish, "prompt", config.polish.prompt);
        config.polish_profiles[config.active_polish_profile] = config.polish;
    }
    if (root.contains("asr_profiles")) AssignApiProfilesIfPresent(root["asr_profiles"], config.asr_profiles);
    if (root.contains("polish_profiles")) AssignApiProfilesIfPresent(root["polish_profiles"], config.polish_profiles);
    if (root.contains("settings") && root["settings"].is_object())
    {
        const nlohmann::json &settings = root["settings"];
        AssignStringIfPresent(settings, "language", config.language);
        AssignBoolIfPresent(settings, "polish_text", config.polish_text);
        AssignBoolIfPresent(settings, "notification_sound", config.notification_sound);
        AssignStringIfPresent(settings, "activation_key", config.activation_key);
        AssignBoolIfPresent(settings, "debug_logging", config.debug_logging);
        AssignStringIfPresent(settings, "log_file", config.log_file);
        AssignStringIfPresent(settings, "active_asr_profile", config.active_asr_profile);
        AssignStringIfPresent(settings, "active_polish_profile", config.active_polish_profile);
        AssignStringIfPresent(settings, "stt_provider", config.stt_provider);
        AssignStringIfPresent(settings, "output_method", config.output_method);
        if (settings.contains("streaming_chunk_ms") && settings["streaming_chunk_ms"].is_number_integer())
        {
            config.streaming_chunk_ms = settings["streaming_chunk_ms"].get<int>();
        }
    }

    if (const auto active = config.asr_profiles.find(config.active_asr_profile); active != config.asr_profiles.end())
    {
        config.asr = active->second;
        if (config.asr.provider == "local_whisper" || config.asr.provider == "cloud_siliconflow" || config.asr.provider == "json_websocket_streaming") config.stt_provider = config.asr.provider;
    }
    if (const auto active = config.polish_profiles.find(config.active_polish_profile); active != config.polish_profiles.end()) config.polish = active->second;

    if (config.output_method != "send_input" && config.output_method != "clipboard_paste")
    {
        set_error("未知的上屏方式");
        return false;
    }

    if (config.stt_provider != "cloud_siliconflow" && config.stt_provider != "json_websocket_streaming" && config.stt_provider != "local_whisper")
    {
        set_error("未知的 ASR 服务类型");
        return false;
    }

    if (config.asr.chunk_ms < 20 || config.asr.chunk_ms > 200)
    {
        set_error("ASR 音频分片必须在 20 到 200 毫秒之间");
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
