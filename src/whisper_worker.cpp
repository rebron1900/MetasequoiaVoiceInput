#include "whisper_worker.h"
#include "whisper.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>

WhisperWorker::WhisperWorker(const std::string &model_path)
{
    printf("[WHISPER] Loading model: %s\n", model_path.c_str());
    fflush(stdout);
    ctx_ = whisper_init_from_file(model_path.c_str());
    if (ctx_ == nullptr)
    {
        throw std::runtime_error("无法加载 Whisper 模型: " + model_path);
    }
    printf("[WHISPER] Model loaded.\n");
    fflush(stdout);
}

WhisperWorker::~WhisperWorker()
{
    if (ctx_ != nullptr)
    {
        whisper_free(ctx_);
    }
}

std::string WhisperWorker::recognize(const std::vector<float> &pcm)
{
    if (pcm.empty())
    {
        printf("[WHISPER] Empty audio.\n");
        fflush(stdout);
        return "";
    }

    printf("[WHISPER] Recognizing %zu samples.\n", pcm.size());
    fflush(stdout);
    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH);
    params.n_threads = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    params.language = "zh";
    params.no_timestamps = true;
    params.single_segment = true;
    params.beam_search.beam_size = 5;

    const int result_code = whisper_full(ctx_, params, pcm.data(), pcm.size());
    if (result_code != 0)
    {
        printf("[WHISPER] whisper_full failed: %d\n", result_code);
        fflush(stdout);
        return "";
    }

    std::string result;
    const int segment_count = whisper_full_n_segments(ctx_);
    printf("[WHISPER] Segments: %d.\n", segment_count);
    for (int i = 0; i < segment_count; ++i)
    {
        const char *segment_text = whisper_full_get_segment_text(ctx_, i);
        result += segment_text;
        printf("[WHISPER] Segment %d: %s\n", i, segment_text);
    }
    printf("[WHISPER] Result length: %zu.\n", result.size());
    fflush(stdout);
    return result;
}
