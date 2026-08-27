#include "cloud_stt_worker.h"
#include "mvi_logger.h"
#include "wav_writer.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <vector>

// Helper for CURL write callback
static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    ((std::string *)userp)->append((char *)contents, size * nmemb);
    return size * nmemb;
}

CloudSttWorker::CloudSttWorker(std::string api_token, std::string api_url, std::string model) : api_token_(std::move(api_token)), api_url_(std::move(api_url)), model_(std::move(model))
{
    curl_global_init(CURL_GLOBAL_ALL);
}

CloudSttWorker::~CloudSttWorker()
{
    curl_global_cleanup();
}

std::string CloudSttWorker::recognize(const std::vector<float> &pcm)
{
    // 1. Convert PCM to WAV in memory
    std::vector<uint8_t> wav_data = WavWriter::create_wav(pcm);

    // 2. Prepare CURL request
    CURL *curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if (curl)
    {
        // Headers
        struct curl_slist *headers = NULL;
        std::string auth_header = "Authorization: Bearer " + api_token_;
        headers = curl_slist_append(headers, auth_header.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // Multipart form data
        curl_mime *mime;
        curl_mimepart *part;

        mime = curl_mime_init(curl);

        // "file" part: WAV data
        part = curl_mime_addpart(mime);
        curl_mime_name(part, "file");
        curl_mime_filename(part, "audio.wav"); // Filename is required by some servers
        curl_mime_data(part, reinterpret_cast<const char *>(wav_data.data()), wav_data.size());
        curl_mime_type(part, "audio/wav");

        // "model" part
        part = curl_mime_addpart(mime);
        curl_mime_name(part, "model");
        curl_mime_data(part, model_.c_str(), CURL_ZERO_TERMINATED);

        curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
        curl_easy_setopt(curl, CURLOPT_URL, api_url_.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        // Timeout
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

        res = curl_easy_perform(curl);
        long response_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        mvi_logger::Write("STT-HTTP", "request result curl=" + std::to_string(static_cast<int>(res)) + " http=" + std::to_string(response_code) + " response_bytes=" + std::to_string(readBuffer.size()));

        if (res != CURLE_OK)
        {
            mvi_logger::Write("STT-HTTP", std::string("curl error: ") + curl_easy_strerror(res));
        }
        else
        {
            try
            {
                const auto json = nlohmann::json::parse(readBuffer);
                if (json.contains("text") && json["text"].is_string())
                {
                    const std::string text = json["text"].get<std::string>();
                    mvi_logger::Write("STT-HTTP", "text field present, length=" + std::to_string(text.size()));
                    return text;
                }
                if (json.contains("error"))
                {
                    mvi_logger::Write("STT-HTTP", "API error: " + json["error"].dump());
                }
                else
                {
                    mvi_logger::Write("STT-HTTP", "unexpected response: " + readBuffer.substr(0, 300));
                }
            }
            catch (const std::exception &e)
            {
                mvi_logger::Write("STT-HTTP", std::string("JSON parse error: ") + e.what() + " raw=" + readBuffer.substr(0, 300));
            }
        }

        // Cleanup
        curl_mime_free(mime);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }

    return "";
}
