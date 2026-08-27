#pragma once

#include <functional>
#include <string>
#include <vector>

class StreamingSttSession
{
  public:
    using PartialCallback = std::function<void(const std::string &)>;

    virtual ~StreamingSttSession() = default;
    virtual bool Start(PartialCallback on_partial, std::string *error_message) = 0;
    virtual bool PushAudio(const std::vector<float> &samples, std::string *error_message) = 0;
    virtual std::string Finish(std::string *error_message) = 0;
    virtual void Cancel() = 0;
};
