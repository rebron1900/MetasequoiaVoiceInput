#pragma once

#include <string>

class TextPolisher
{
  public:
    TextPolisher(std::string api_token, std::string language, std::string api_url, std::string model, std::string prompt);
    std::string polish(const std::string &original_text) const;

  private:
    std::string api_token_;
    std::string language_;
    std::string api_url_ = "https://api.siliconflow.cn/v1/chat/completions";
    std::string model_ = "Qwen/Qwen3-8B";
    std::string prompt_;
};
