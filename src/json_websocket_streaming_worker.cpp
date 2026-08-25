#include "json_websocket_streaming_worker.h"

#include "json_websocket_stt_session.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace
{
constexpr size_t kSampleRate = 16000;
constexpr int kMinimumChunkMs = 20;
constexpr int kMaximumChunkMs = 200;

size_t GetChunkSamples(int chunk_ms)
{
    const int safe_chunk_ms = std::clamp(chunk_ms, kMinimumChunkMs, kMaximumChunkMs);
    return (kSampleRate * static_cast<size_t>(safe_chunk_ms)) / 1000;
}
}

JsonWebSocketStreamingWorker::JsonWebSocketStreamingWorker(std::string endpoint, std::string token, std::string language, int chunk_ms)
    : endpoint_(std::move(endpoint)), token_(std::move(token)), language_(std::move(language)), chunk_samples_(GetChunkSamples(chunk_ms))
{
}

std::string JsonWebSocketStreamingWorker::recognize(const std::vector<float> &pcm)
{
    if (pcm.empty())
    {
        return "";
    }

    JsonWebSocketSttSession session(endpoint_, token_, language_);
    std::string error;
    if (!session.Start([](const std::string &partial) {
            printf("[STREAMING ASR] Partial: %s\n", partial.c_str());
            fflush(stdout);
        },
        &error))
    {
        printf("[STREAMING ASR] Start failed: %s\n", error.c_str());
        fflush(stdout);
        return "";
    }

    for (size_t offset = 0; offset < pcm.size(); offset += chunk_samples_)
    {
        const size_t count = std::min(chunk_samples_, pcm.size() - offset);
        std::vector<float> chunk(pcm.begin() + static_cast<std::ptrdiff_t>(offset), pcm.begin() + static_cast<std::ptrdiff_t>(offset + count));
        if (!session.PushAudio(chunk, &error))
        {
            printf("[STREAMING ASR] Audio push failed: %s\n", error.c_str());
            fflush(stdout);
            session.Cancel();
            return "";
        }
    }

    const std::string text = session.Finish(&error);
    if (!error.empty())
    {
        printf("[STREAMING ASR] Finish failed: %s\n", error.c_str());
        fflush(stdout);
    }
    return text;
}
