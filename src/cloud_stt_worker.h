#pragma once
#include "stt_service.h"
#include <string>

class CloudSttWorker : public SttService
{
  public:
    CloudSttWorker(std::string api_token, std::string api_url);
    ~CloudSttWorker();

    std::string recognize(const std::vector<float> &pcm) override;

  private:
    std::string api_token_;
    std::string api_url_ = "https://api.siliconflow.cn/v1/audio/transcriptions";
    std::string model_ = "TeleAI/TeleSpeechASR";
};
