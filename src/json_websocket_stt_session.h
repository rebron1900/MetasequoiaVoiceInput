#pragma once

#include "streaming_stt_session.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>

namespace ix
{
class WebSocket;
}

class JsonWebSocketSttSession final : public StreamingSttSession
{
  public:
    JsonWebSocketSttSession(std::string endpoint, std::string token, std::string language);
    ~JsonWebSocketSttSession() override;

    bool Start(PartialCallback on_partial, std::string *error_message) override;
    bool PushAudio(const std::vector<float> &samples, std::string *error_message) override;
    std::string Finish(std::string *error_message) override;
    void Cancel() override;

  private:
    void OnMessage(const std::string &payload);
    bool GetConnectionError(std::string *error_message);
    bool SetError(const std::string &message, std::string *error_message);

    std::string endpoint_;
    std::string token_;
    std::string language_;
    std::unique_ptr<ix::WebSocket> socket_;
    PartialCallback on_partial_;

    std::mutex mutex_;
    std::condition_variable connected_cv_;
    std::condition_variable final_cv_;
    bool connected_ = false;
    bool connection_failed_ = false;
    std::string final_text_;
    std::string protocol_error_;
    std::atomic<bool> started_{false};
    std::atomic<bool> stopped_{false};
    bool received_final_ = false;
};
