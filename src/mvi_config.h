#pragma once

#include <map>
#include <string>

namespace mvi_config
{
struct ApiConfig
{
    std::string provider;
    std::string token;
    std::string endpoint;
    std::string model;
    std::string prompt;
    std::string model_type;
    int chunk_ms = 40;
};

struct RuntimeConfig
{
    ApiConfig asr;
    ApiConfig polish;
    std::map<std::string, ApiConfig> asr_profiles;
    std::map<std::string, ApiConfig> polish_profiles;
    std::string active_asr_profile = "default";
    std::string active_polish_profile = "default";
    std::string language = "zh-cn";
    bool polish_text = false;
    bool notification_sound = true;
    std::string activation_key = "right_alt";
    bool debug_logging = true;
    std::string log_file = "logs/metasequoia-voice-input.log";
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
