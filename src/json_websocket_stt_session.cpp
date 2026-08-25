#include "json_websocket_stt_session.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

namespace
{
constexpr int kSampleRate = 16000;
constexpr auto kConnectTimeout = std::chrono::seconds(10);
constexpr auto kFinalResultTimeout = std::chrono::seconds(10);

void EnsureNetworkInitialized()
{
    static std::once_flag initialized;
    std::call_once(initialized, []() { ix::initNetSystem(); });
}

std::string FloatPcmToPcm16(const float *samples, size_t count)
{
    std::string bytes(count * sizeof(int16_t), '\0');
    for (size_t index = 0; index < count; ++index)
    {
        const float sample = std::clamp(samples[index], -1.0f, 1.0f);
        const int16_t converted = static_cast<int16_t>(std::round(sample * 32767.0f));
        std::memcpy(bytes.data() + index * sizeof(converted), &converted, sizeof(converted));
    }
    return bytes;
}
} // namespace

JsonWebSocketSttSession::JsonWebSocketSttSession(std::string endpoint, std::string token, std::string language)
    : endpoint_(std::move(endpoint)), token_(std::move(token)), language_(std::move(language))
{
}

JsonWebSocketSttSession::~JsonWebSocketSttSession()
{
    Cancel();
}

bool JsonWebSocketSttSession::Start(PartialCallback on_partial, std::string *error_message)
{
    if (started_)
    {
        return SetError("streaming session is already started", error_message);
    }

    EnsureNetworkInitialized();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        connected_ = false;
        connection_failed_ = false;
        received_final_ = false;
        final_text_.clear();
        protocol_error_.clear();
    }

    socket_ = std::make_unique<ix::WebSocket>();
    socket_->setUrl(endpoint_);
    socket_->setPingInterval(20);
    socket_->setHandshakeTimeout(10);
    if (!token_.empty())
    {
        socket_->setExtraHeaders({{"Authorization", "Bearer " + token_}});
    }

    on_partial_ = std::move(on_partial);
    socket_->setOnMessageCallback([this](const ix::WebSocketMessagePtr &message) {
        if (message->type == ix::WebSocketMessageType::Open)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            connected_ = true;
            connected_cv_.notify_all();
            return;
        }
        if (message->type == ix::WebSocketMessageType::Message)
        {
            if (!message->binary)
            {
                OnMessage(message->str);
            }
            return;
        }
        if (message->type == ix::WebSocketMessageType::Error)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            connection_failed_ = true;
            protocol_error_ = message->errorInfo.reason;
            connected_cv_.notify_all();
            final_cv_.notify_all();
            return;
        }
        if (message->type == ix::WebSocketMessageType::Close)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!received_final_ && protocol_error_.empty())
            {
                protocol_error_ = "streaming ASR server closed the connection before final result";
            }
            final_cv_.notify_all();
        }
    });
    socket_->start();

    {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool ready = connected_cv_.wait_for(lock, kConnectTimeout, [this]() { return connected_ || connection_failed_; });
        if (!ready || !connected_)
        {
            const std::string reason = protocol_error_.empty() ? "streaming ASR connection timed out" : protocol_error_;
            lock.unlock();
            Cancel();
            return SetError(reason, error_message);
        }
    }

    bool connection_failed = false;
    std::string connection_error;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        connection_failed = connection_failed_;
        connection_error = protocol_error_;
    }
    if (connection_failed)
    {
        Cancel();
        return SetError(connection_error.empty() ? "streaming ASR connection failed" : connection_error, error_message);
    }

    const nlohmann::json start_event = {
        {"type", "start"},
        {"sample_rate", kSampleRate},
        {"channels", 1},
        {"format", "pcm_s16le"},
        {"language", language_},
    };
    const ix::WebSocketSendInfo send_info = socket_->send(start_event.dump());
    if (!send_info.success)
    {
        Cancel();
        return SetError("unable to send streaming start event: " + send_info.errorStr, error_message);
    }

    started_ = true;
    stopped_ = false;
    return true;
}

bool JsonWebSocketSttSession::PushAudio(const std::vector<float> &samples, std::string *error_message)
{
    if (GetConnectionError(error_message))
    {
        return false;
    }
    if (!started_ || stopped_ || socket_ == nullptr)
    {
        return SetError("streaming session is not active", error_message);
    }
    if (samples.empty())
    {
        return true;
    }

    const ix::WebSocketSendInfo send_info = socket_->sendBinary(FloatPcmToPcm16(samples.data(), samples.size()));
    if (!send_info.success)
    {
        return SetError("unable to send streaming audio: " + send_info.errorStr, error_message);
    }
    return true;
}

std::string JsonWebSocketSttSession::Finish(std::string *error_message)
{
    if (GetConnectionError(error_message))
    {
        return "";
    }
    if (!started_ || socket_ == nullptr)
    {
        SetError("streaming session was not started", error_message);
        return "";
    }

    if (!stopped_.exchange(true))
    {
        const ix::WebSocketSendInfo send_info = socket_->send(nlohmann::json{{"type", "stop"}}.dump());
        if (!send_info.success)
        {
            SetError("unable to send streaming stop event: " + send_info.errorStr, error_message);
            return "";
        }
    }

    std::unique_lock<std::mutex> lock(mutex_);
    final_cv_.wait_for(lock, kFinalResultTimeout, [this]() { return received_final_ || !protocol_error_.empty(); });
    const std::string result = final_text_;
    if (!protocol_error_.empty())
    {
        SetError(protocol_error_, error_message);
    }
    else if (!received_final_)
    {
        SetError("streaming ASR timed out waiting for final result", error_message);
    }
    return result;
}

void JsonWebSocketSttSession::Cancel()
{
    stopped_ = true;
    started_ = false;
    if (socket_ != nullptr)
    {
        socket_->stop();
        socket_.reset();
    }
}

void JsonWebSocketSttSession::OnMessage(const std::string &payload)
{
    try
    {
        const nlohmann::json event = nlohmann::json::parse(payload);
        const std::string type = event.value("type", "");
        if (type == "partial")
        {
            const std::string text = event.value("text", "");
            if (!text.empty() && on_partial_)
            {
                on_partial_(text);
            }
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (type == "final")
        {
            final_text_ = event.value("text", "");
            received_final_ = true;
            final_cv_.notify_all();
        }
        else if (type == "error")
        {
            protocol_error_ = event.value("message", "streaming ASR server returned an error");
            final_cv_.notify_all();
        }
    }
    catch (const std::exception &e)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        protocol_error_ = std::string("invalid streaming ASR response: ") + e.what();
        final_cv_.notify_all();
    }
}

bool JsonWebSocketSttSession::GetConnectionError(std::string *error_message)
{
    std::string error;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!connection_failed_)
        {
            return false;
        }
        error = protocol_error_;
    }
    return SetError(error.empty() ? "streaming ASR connection failed" : error, error_message);
}

bool JsonWebSocketSttSession::SetError(const std::string &message, std::string *error_message)
{
    if (error_message != nullptr)
    {
        *error_message = message;
    }
    printf("[STREAMING ASR] %s\n", message.c_str());
    fflush(stdout);
    return false;
}
