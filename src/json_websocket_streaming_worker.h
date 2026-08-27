#pragma once

#include "stt_service.h"

#include <string>

class JsonWebSocketStreamingWorker final : public SttService
{
  public:
    JsonWebSocketStreamingWorker(std::string endpoint, std::string token, std::string language, int chunk_ms);

    std::string recognize(const std::vector<float> &pcm) override;

  private:
    std::string endpoint_;
    std::string token_;
    std::string language_;
    size_t chunk_samples_;
};
