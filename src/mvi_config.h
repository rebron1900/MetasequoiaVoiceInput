#pragma once

#include <string>

namespace mvi_config
{
struct ApiConfig
{
    std::string provider;
    std::string token;
    std::string endpoint;
};

struct RuntimeConfig
{
    ApiConfig asr;
    ApiConfig polish;
    std::string language = "zh-cn";
    bool polish_text = false;
    bool notification_sound = true;
    std::string stt_provider = "cloud_siliconflow";
    std::string output_method = "send_input";
    int streaming_chunk_ms = 40;
};

std::string GetConfigPath();
RuntimeConfig LoadRuntimeConfig();
std::string GetLastLoadError();
bool IsSafeApiEndpoint(const std::string &endpoint);
bool IsSafeStreamingEndpoint(const std::string &endpoint);
std::string ReadConfigAsJson();
bool WriteConfigFromJson(const std::string &config_json, std::string *error_message = nullptr);
} // namespace mvi_config
