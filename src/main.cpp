#include <iostream>
#include <string>
#include <cstdio>
#include <memory>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <atomic>
#include <stdexcept>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <cmath>
#include <algorithm>
#include "vad.h"
#include "audio_capture.h"
#include "cloud_stt_worker.h"
#include "json_websocket_streaming_worker.h"
#include "whisper_worker.h"
#include "send_input.h"
#include "mvi_utils.h"
#include "mvi_config.h"
#include "mvi_logger.h"
#include "wave_overlay.h"
#include "cue_player.h"
#include "text_polisher.h"
#include "window_webview2.h"
#include <fmt/format.h>
#include <fmt/xchar.h>
#include <windows.h>

std::string g_cloud_token;
std::string g_language = "zh-cn";
std::string g_activation_key = "right_alt";
bool g_polish_text = false;
bool g_notification_sound = true;
TextOutputMethod g_output_method = TextOutputMethod::SendInput;

namespace
{
constexpr UINT WM_APP_TOGGLE_RECORD = WM_APP + 1; // Ctrl + F9
constexpr UINT WM_APP_RALT_RECORD_START = WM_APP + 2;
constexpr UINT WM_APP_RALT_RECORD_STOP = WM_APP + 3;
constexpr UINT WM_APP_EXIT = WM_APP + 4;
constexpr UINT WM_APP_RALT_RECORD_LOCK = WM_APP + 5;
constexpr int k_ralt_min_record_ms = 250;
constexpr int k_sample_rate = 16000;

std::atomic<bool> g_ralt_pressed{false};
std::atomic<bool> g_lctrl_pressed{false};
std::atomic<bool> g_rctrl_pressed{false};
std::atomic<bool> g_f9_pressed{false};
std::atomic<bool> g_ralt_lock_mode{false};
std::atomic<uint64_t> g_audio_callback_calls{0};
std::atomic<uint64_t> g_audio_callback_frames{0};
std::atomic<float> g_audio_peak_rms{0.0f};
DWORD g_main_thread_id = 0;

void force_release_ralt_key()
{
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = VK_RMENU;
    in.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(in));
}

bool is_ctrl_pressed()
{
    return g_lctrl_pressed.load() || g_rctrl_pressed.load();
}

LRESULT CALLBACK keyboard_hook_proc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION)
    {
        const auto *kb = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
        if (kb != nullptr && (kb->vkCode == VK_LCONTROL || kb->vkCode == VK_RCONTROL))
        {
            const bool is_key_down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
            const bool is_key_up = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

            if (is_key_down)
            {
                if (kb->vkCode == VK_LCONTROL)
                {
                    bool expected = false;
                    g_lctrl_pressed.compare_exchange_strong(expected, true);
                }
                else
                {
                    bool expected = false;
                    g_rctrl_pressed.compare_exchange_strong(expected, true);
                }
            }
            else if (is_key_up)
            {
                if (kb->vkCode == VK_LCONTROL)
                {
                    bool expected = true;
                    g_lctrl_pressed.compare_exchange_strong(expected, false);
                }
                else
                {
                    bool expected = true;
                    g_rctrl_pressed.compare_exchange_strong(expected, false);
                }
            }
        }
        else if (kb != nullptr && kb->vkCode == VK_F9 && g_activation_key == "ctrl_f9")
        {
            const bool is_key_down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
            const bool is_key_up = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

            if (is_key_down)
            {
                bool expected = false;
                if (g_f9_pressed.compare_exchange_strong(expected, true))
                {
                    if (is_ctrl_pressed())
                    {
                        PostThreadMessage(g_main_thread_id, WM_APP_TOGGLE_RECORD, 0, 0);
                        return 1; // 吞下组合键中的 F9
                    }
                }
            }
            else if (is_key_up)
            {
                bool expected = true;
                g_f9_pressed.compare_exchange_strong(expected, false);
                if (is_ctrl_pressed())
                {
                    return 1; // Ctrl 按住时吞掉 F9 抬起
                }
            }
        }
        else if (kb != nullptr && kb->vkCode == VK_RMENU && g_activation_key == "right_alt")
        {
            const bool is_key_down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
            const bool is_key_up = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

            if (is_key_down)
            {
                bool expected = false;
                if (g_ralt_pressed.compare_exchange_strong(expected, true))
                {
                    if (g_ralt_lock_mode.load())
                    {
                        PostThreadMessage(g_main_thread_id, WM_APP_RALT_RECORD_STOP, 0, 0);
                    }
                    else
                    {
                        PostThreadMessage(g_main_thread_id, WM_APP_RALT_RECORD_START, 0, 0);
                    }
                }
                return 1; // 吞下 RAlt，避免前台应用触发菜单行为
            }
            else if (is_key_up)
            {
                bool expected = true;
                if (g_ralt_pressed.compare_exchange_strong(expected, false))
                {
                    if (!g_ralt_lock_mode.load())
                    {
                        PostThreadMessage(g_main_thread_id, WM_APP_RALT_RECORD_STOP, 0, 0);
                    }
                }
                return 1;
            }
        }
        else if (kb != nullptr && kb->vkCode == VK_SPACE)
        {
            const bool is_key_down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
            const bool is_key_up = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

            if (is_key_down && g_ralt_pressed.load())
            {
                if (!g_ralt_lock_mode.load())
                {
                    PostThreadMessage(g_main_thread_id, WM_APP_RALT_RECORD_LOCK, 0, 0);
                }
                return 1; // RAlt 组合状态下吞掉 Space，避免输入空格
            }

            if (is_key_up && g_ralt_pressed.load())
            {
                return 1;
            }
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
} // namespace

int main()
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    auto single_instance_mutex = std::unique_ptr<void, decltype(&CloseHandle)>(CreateMutexW(nullptr, TRUE, L"Local\\MetasequoiaVoiceInput_SingleInstance"), &CloseHandle);
    if (single_instance_mutex == nullptr)
    {
        OutputDebugString(fmt::format(L"[mvi]: Failed to create single instance mutex.").c_str());
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        OutputDebugString(fmt::format(L"[mvi]: Single instance already exists, MetasequoiaVoiceInput is already running.").c_str());
        return 0;
    }

    const HINSTANCE app_instance = GetModuleHandleW(nullptr);

    const HRESULT com_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool need_com_uninitialize = (com_hr == S_OK || com_hr == S_FALSE);
    if (FAILED(com_hr) && com_hr != RPC_E_CHANGED_MODE)
    {
        printf("FATAL ERROR: CoInitializeEx failed: 0x%08lx\n", static_cast<unsigned long>(com_hr));
        fflush(stdout);
        return 1;
    }

    // Set console code page to UTF-8 so console can display UTF-8 characters correctly
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    const mvi_config::RuntimeConfig runtime_config = mvi_config::LoadRuntimeConfig();
    mvi_logger::Initialize(runtime_config.log_file, runtime_config.debug_logging);
    mvi_logger::Write("INIT", "Configuration loaded; provider=" + runtime_config.stt_provider + ", endpoint=" + runtime_config.asr.endpoint + ", token_present=" + (runtime_config.asr.token.empty() ? "no" : "yes"));
    g_cloud_token = runtime_config.asr.token;
    g_language = runtime_config.language;
    g_activation_key = runtime_config.activation_key;
    g_polish_text = runtime_config.polish_text;
    g_notification_sound = runtime_config.notification_sound;
    g_output_method = ParseTextOutputMethod(runtime_config.output_method);

    if (!mvi_config::GetLastLoadError().empty())
    {
        const std::string message = mvi_config::GetLastLoadError();
        MessageBoxA(nullptr, message.c_str(), "MetasequoiaVoiceInput - Configuration Error", MB_ICONERROR);
        if (need_com_uninitialize)
        {
            CoUninitialize();
        }
        return 1;
    }

    const bool safe_asr_endpoint = runtime_config.stt_provider == "local_whisper"
        ? true
        : runtime_config.stt_provider == "json_websocket_streaming"
            ? mvi_config::IsSafeStreamingEndpoint(runtime_config.asr.endpoint)
            : mvi_config::IsSafeApiEndpoint(runtime_config.asr.endpoint);
    if (!safe_asr_endpoint || (runtime_config.polish_text && !mvi_config::IsSafeApiEndpoint(runtime_config.polish.endpoint)))
    {
        const char *message = "config.toml contains an unsafe API endpoint.";
        MessageBoxA(nullptr, message, "MetasequoiaVoiceInput - Configuration Error", MB_ICONERROR);
        if (need_com_uninitialize)
        {
            CoUninitialize();
        }
        return 1;
    }

    printf("--- METASEQUOIA VOICE INPUT START ---\n");
    fflush(stdout);

    try
    {
        VadSegmenter vad;
        std::unique_ptr<SttService> stt;
        if (runtime_config.stt_provider == "local_whisper")
        {
            std::filesystem::path model_path = std::filesystem::u8path(runtime_config.asr.model);
            if (model_path.is_relative()) model_path = std::filesystem::path(mvi_utils::GetExecutableDirectory()) / model_path;
            stt = std::make_unique<WhisperWorker>(model_path.u8string());
            printf("[INIT] Local Whisper ASR Ready.\n");
        }
        else if (runtime_config.stt_provider == "json_websocket_streaming")
        {
            stt = std::make_unique<JsonWebSocketStreamingWorker>(runtime_config.asr.endpoint, runtime_config.asr.token, runtime_config.language, runtime_config.asr.chunk_ms);
            printf("[INIT] JSON WebSocket streaming ASR Ready.\n");
        }
        else
        {
            stt = std::make_unique<CloudSttWorker>(runtime_config.asr.token, runtime_config.asr.endpoint, runtime_config.asr.model);
            printf("[INIT] Cloud HTTP STT Ready.\n");
        }
        std::unique_ptr<TextPolisher> text_polisher;

        if (g_polish_text)
        {
            text_polisher = std::make_unique<TextPolisher>(runtime_config.polish.token, runtime_config.language, runtime_config.polish.endpoint, runtime_config.polish.model, runtime_config.polish.prompt);
            printf("[INIT] Text polishing enabled.\n");
        }
        else
        {
            printf("[INIT] Text polishing disabled.\n");
        }
        fflush(stdout);

        printf("[INIT] Initializing Audio...\n");
        fflush(stdout);
        AudioCapture audio;
        WaveOverlay wave_overlay;
        if (!wave_overlay.init(GetModuleHandleW(nullptr)))
        {
            throw std::runtime_error("WaveOverlay init failed");
        }
        //
        // STT queue + thread, 把耗时的操作放在这个线程里，避免在 audio callback 里做耗时操作导致丢音频
        //
        std::mutex stt_mutex;
        std::condition_variable stt_cv;
        std::deque<std::vector<float>> stt_queue;
        std::atomic<bool> stt_stop = false;
        std::mutex record_mutex;
        std::vector<float> recorded_samples;
        std::thread stt_thread([&]() {
            while (!stt_stop)
            {
                std::vector<float> samples;
                {
                    std::unique_lock<std::mutex> lock(stt_mutex);
                    stt_cv.wait(lock, [&]() { return stt_stop || !stt_queue.empty(); });
                    if (stt_stop) break;
                    samples = std::move(stt_queue.front());
                    stt_queue.pop_front();
                }

                try
                {
                    printf("[STT] Begin recognition: %zu samples.\n", samples.size());
                    fflush(stdout);
                    mvi_logger::Write("STT", "begin recognition samples=" + std::to_string(samples.size()));
                    const auto start = std::chrono::steady_clock::now();
                    const std::string text = stt->recognize(samples);
                    const auto end = std::chrono::steady_clock::now();
                    const double elapsed_seconds = std::chrono::duration<double>(end - start).count();
                    printf("[STT] Time: %.3fs, result_length=%zu.\n", elapsed_seconds, text.size());
                    fflush(stdout);
                    mvi_logger::Write("STT", "recognition complete seconds=" + std::to_string(elapsed_seconds) + " result_length=" + std::to_string(text.size()));
                    if (text.empty())
                    {
                        printf("[STT] Empty recognition result; output skipped.\n");
                        fflush(stdout);
                        continue;
                    }

                    printf("[STT] Recognized: %s\n", text.c_str());
                    std::string final_text = text;
                    if (text_polisher != nullptr)
                    {
                        const auto polish_start = std::chrono::steady_clock::now();
                        final_text = text_polisher->polish(text);
                        const auto polish_end = std::chrono::steady_clock::now();
                        printf("[POLISH] Time: %.3fs, result_length=%zu.\n", std::chrono::duration<double>(polish_end - polish_start).count(), final_text.size());
                        fflush(stdout);
                    }
                    if (final_text.empty())
                    {
                        printf("[OUTPUT] Empty final text; output skipped.\n");
                        fflush(stdout);
                        continue;
                    }
                    mvi_logger::Write("OUTPUT", "sending text length=" + std::to_string(final_text.size()));
                    send_text(mvi_utils::utf8_to_wstring(final_text), g_output_method);
                }
                catch (const std::exception &e)
                {
                    printf("[STT ERROR] %s\n", e.what());
                    fflush(stdout);
                    mvi_logger::Write("STT ERROR", e.what());
                }
                catch (...)
                {
                    printf("[STT ERROR] Unknown exception.\n");
                    fflush(stdout);
                    mvi_logger::Write("STT ERROR", "unknown exception");
                }
            }
        });

        auto audio_callback_vad = [&](const float *data, size_t count) {
            try
            {
                ++g_audio_callback_calls;
                g_audio_callback_frames += count;
                double sum_sq = 0.0;
                for (size_t i = 0; i < count; ++i)
                {
                    sum_sq += data[i] * data[i];
                }
                const float rms = count > 0 ? static_cast<float>(std::sqrt(sum_sq / static_cast<double>(count))) : 0.0f;
                float previous_peak = g_audio_peak_rms.load();
                while (rms > previous_peak && !g_audio_peak_rms.compare_exchange_weak(previous_peak, rms)) {}
                wave_overlay.set_input_level(std::min(1.0f, rms * 8.0f));

                vad.process(data, count);
                if (vad.should_flush())
                {
                    auto samples = vad.take_audio();
                    mvi_logger::Write("VAD", "auto flush samples=" + std::to_string(samples.size()));
                    if (samples.empty())
                    {
                        mvi_logger::Write("VAD", "auto flush skipped because audio was empty");
                    }
                    else
                    {
                        std::lock_guard<std::mutex> lock(stt_mutex);
                        stt_queue.push_back(std::move(samples));
                        stt_cv.notify_one();
                    }
                }
            }
            catch (const std::exception &e)
            {
                printf("[CALLBACK ERROR] %s\n", e.what());
                fflush(stdout);
            }
            catch (...)
            {
                printf("[CALLBACK ERROR] Unknown exception\n");
                fflush(stdout);
            }
        };

        auto audio_callback_raw = [&](const float *data, size_t count) {
            try
            {
                ++g_audio_callback_calls;
                g_audio_callback_frames += count;
                double sum_sq = 0.0;
                for (size_t i = 0; i < count; ++i)
                {
                    sum_sq += data[i] * data[i];
                }
                const float rms = count > 0 ? static_cast<float>(std::sqrt(sum_sq / static_cast<double>(count))) : 0.0f;
                float previous_peak = g_audio_peak_rms.load();
                while (rms > previous_peak && !g_audio_peak_rms.compare_exchange_weak(previous_peak, rms)) {}
                wave_overlay.set_input_level(std::min(1.0f, rms * 8.0f));

                std::lock_guard<std::mutex> lock(record_mutex);
                recorded_samples.insert(recorded_samples.end(), data, data + count);
            }
            catch (const std::exception &e)
            {
                printf("[CALLBACK ERROR] %s\n", e.what());
                fflush(stdout);
            }
            catch (...)
            {
                printf("[CALLBACK ERROR] Unknown exception\n");
                fflush(stdout);
            }
        };

        // 从 app data 目录中去取
        const std::wstring start_cue_path = mvi_utils::resolve_asset_audio_path("start.mp3");
        const std::wstring end_cue_path = mvi_utils::resolve_asset_audio_path("end.mp3");
        CuePlayer cue_player;

        if (start_cue_path.empty())
        {
            printf("[AUDIO] start.mp3 not found in assets.\n");
            fflush(stdout);
        }
        if (end_cue_path.empty())
        {
            printf("[AUDIO] end.mp3 not found in assets.\n");
            fflush(stdout);
        }

        cue_player.init(start_cue_path, end_cue_path);

        g_main_thread_id = GetCurrentThreadId();

        MSG init_msg{};
        PeekMessage(&init_msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

        window_webview2::TrayMenuConfig tray_config{};
        tray_config.main_thread_id = g_main_thread_id;
        tray_config.msg_toggle_record = WM_APP_TOGGLE_RECORD;
        tray_config.msg_exit = WM_APP_EXIT;
        tray_config.app_name = L"MetasequoiaVoiceInput";
        tray_config.default_width = 300;
        tray_config.default_height = 360;
        if (!window_webview2::InitializeTrayUi(app_instance, tray_config))
        {
            throw std::runtime_error("Tray UI init failed");
        }

        HHOOK keyboard_hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_hook_proc, GetModuleHandleW(nullptr), 0);
        if (keyboard_hook == nullptr)
        {
            throw std::runtime_error(fmt::format("SetWindowsHookExW failed: {}", GetLastError()));
        }

        bool audio_started = false;
        bool toggle_mode_active = false;
        bool ralt_mode_active = false;
        bool ralt_lock_active = false;
        auto ralt_record_start_time = std::chrono::steady_clock::now();

        printf("Hold RAlt to record one segment. Release RAlt to transcribe and send text.\n");
        printf("While holding RAlt, press Space to lock recording. Press RAlt again to stop and transcribe.\n");
        fflush(stdout);

        MSG msg{};
        bool should_exit = false;
        while (!should_exit && GetMessage(&msg, nullptr, 0, 0) > 0)
        {
            switch (msg.message)
            {
            case WM_APP_TOGGLE_RECORD: {
                if (ralt_mode_active)
                {
                    printf("[AUDIO] Busy: RAlt hold-to-record is active.\n");
                    fflush(stdout);
                    break;
                }

                if (!toggle_mode_active)
                {
                    g_audio_callback_calls = 0;
                    g_audio_callback_frames = 0;
                    g_audio_peak_rms = 0.0f;
                    if (!audio.start(audio_callback_vad))
                    {
                        printf("[AUDIO] Failed to start capture.\n");
                        fflush(stdout);
                    }
                    else
                    {
                        audio_started = true;
                        toggle_mode_active = true;
                        wave_overlay.show();
                        wave_overlay.set_listening(true);
                        if (g_notification_sound)
                        {
                            cue_player.play_start();
                        }
                        printf("[AUDIO] Started (Ctrl+F9 toggle mode).\n");
                        fflush(stdout);
                    }
                }
                else
                {
                    audio.stop();
                    mvi_logger::Write("MIC", "toggle stop callbacks=" + std::to_string(g_audio_callback_calls.load()) + " frames=" + std::to_string(g_audio_callback_frames.load()) + " peak_rms=" + std::to_string(g_audio_peak_rms.load()));
                    audio_started = false;
                    toggle_mode_active = false;
                    wave_overlay.set_listening(false);
                    wave_overlay.set_input_level(0.0f);
                    wave_overlay.hide();
                    if (g_notification_sound)
                    {
                        cue_player.play_end();
                    }
                    auto samples = vad.take_audio();
                    if (!samples.empty())
                    {
                        {
                            std::lock_guard<std::mutex> lock(stt_mutex);
                            stt_queue.push_back(std::move(samples));
                        }
                        stt_cv.notify_one();
                    }
                    printf("[AUDIO] Stopped (Ctrl+F9 toggle mode).\n");
                    fflush(stdout);
                }
                break;
            }
            case WM_APP_RALT_RECORD_START: {
                mvi_logger::Write("HOOK", "receive RAlt START toggle=" + std::to_string(toggle_mode_active) + " ralt=" + std::to_string(ralt_mode_active) + " audio=" + std::to_string(audio_started));
                if (toggle_mode_active)
                {
                    printf("[AUDIO] Busy: Ctrl+F9 toggle mode is active.\n");
                    fflush(stdout);
                    break;
                }

                if (ralt_mode_active || audio_started)
                {
                    mvi_logger::Write("HOOK", "RAlt START dropped because recording is already active");
                    break;
                }

                {
                    std::lock_guard<std::mutex> lock(record_mutex);
                    recorded_samples.clear();
                }
                g_audio_callback_calls = 0;
                g_audio_callback_frames = 0;
                g_audio_peak_rms = 0.0f;

                if (!audio.start(audio_callback_raw))
                {
                    printf("[AUDIO] Failed to start capture.\n");
                    fflush(stdout);
                }
                else
                {
                    audio_started = true;
                    ralt_mode_active = true;
                    ralt_lock_active = false;
                    g_ralt_lock_mode = false;
                    wave_overlay.show();
                    wave_overlay.set_listening(true);
                    ralt_record_start_time = std::chrono::steady_clock::now();
                    cue_player.play_start();
                    printf("[AUDIO] Recording (RAlt hold mode)...\n");
                    fflush(stdout);
                }
                break;
            }
            case WM_APP_RALT_RECORD_LOCK: {
                if (!ralt_mode_active || ralt_lock_active)
                {
                    break;
                }

                ralt_lock_active = true;
                g_ralt_lock_mode = true;
                printf("[AUDIO] Locked recording (RAlt+Space). Press RAlt again to stop.\n");
                fflush(stdout);
                break;
            }
            case WM_APP_RALT_RECORD_STOP: {
                mvi_logger::Write("HOOK", "receive RAlt STOP active=" + std::to_string(ralt_mode_active));
                if (!ralt_mode_active)
                {
                    mvi_logger::Write("HOOK", "RAlt STOP dropped because no active recording");
                    break;
                }

                const bool was_locked = ralt_lock_active;
                audio.stop();
                audio_started = false;
                ralt_mode_active = false;
                ralt_lock_active = false;
                g_ralt_lock_mode = false;
                wave_overlay.set_listening(false);
                wave_overlay.set_input_level(0.0f);
                wave_overlay.hide();
                cue_player.play_end();
                if (was_locked)
                {
                    printf("[AUDIO] Stopped (RAlt lock mode).\n");
                }
                else
                {
                    printf("[AUDIO] Stopped (RAlt hold mode).\n");
                }
                fflush(stdout);

                std::vector<float> samples;
                {
                    std::lock_guard<std::mutex> lock(record_mutex);
                    samples = std::move(recorded_samples);
                    recorded_samples.clear();
                }

                const auto elapsed = std::chrono::steady_clock::now() - ralt_record_start_time;
                const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
                mvi_logger::Write("MIC", "RAlt stop duration_ms=" + std::to_string(elapsed_ms) + " samples=" + std::to_string(samples.size()) + " callbacks=" + std::to_string(g_audio_callback_calls.load()) + " frames=" + std::to_string(g_audio_callback_frames.load()) + " peak_rms=" + std::to_string(g_audio_peak_rms.load()));
                const size_t min_samples = static_cast<size_t>((k_sample_rate * k_ralt_min_record_ms) / 1000);

                if (elapsed_ms < k_ralt_min_record_ms || samples.size() < min_samples)
                {
                    printf("[AUDIO] Ignored short RAlt recording (%lldms, %zu samples).\n", elapsed_ms, samples.size());
                    fflush(stdout);
                    break;
                }

                if (!samples.empty())
                {
                    {
                        std::lock_guard<std::mutex> lock(stt_mutex);
                        stt_queue.push_back(std::move(samples));
                    }
                    stt_cv.notify_one();
                }
                else
                {
                    printf("[AUDIO] No audio captured.\n");
                    fflush(stdout);
                    mvi_logger::Write("MIC", "RAlt stop had no captured audio");
                }
                break;
            }
            case WM_APP_EXIT:
                should_exit = true;
                break;
            default:
                TranslateMessage(&msg);
                DispatchMessage(&msg);
                break;
            }
        }

        printf("Stopping...\n");
        fflush(stdout);

        if (audio_started)
        {
            audio.stop();
            wave_overlay.set_listening(false);
            wave_overlay.set_input_level(0.0f);
            wave_overlay.hide();
            cue_player.play_end();
        }

        cue_player.shutdown();
        g_ralt_pressed = false;
        g_ralt_lock_mode = false;
        force_release_ralt_key();

        UnhookWindowsHookEx(keyboard_hook);
        window_webview2::ShutdownTrayUi(app_instance);
        wave_overlay.shutdown();

        // 通知 stt 线程停止
        stt_stop = true;
        stt_cv.notify_one();
        if (stt_thread.joinable())
        {
            stt_thread.join();
        }
        printf("Stopped.\n");
        fflush(stdout);
    }
    catch (const std::exception &e)
    {
        window_webview2::ShutdownTrayUi(app_instance);
        printf("FATAL ERROR: %s\n", e.what());
        fflush(stdout);
        MessageBoxA(nullptr, e.what(), "MetasequoiaVoiceInput - Fatal Error", MB_ICONERROR);
        if (need_com_uninitialize)
        {
            CoUninitialize();
        }
        return 1;
    }

    if (need_com_uninitialize)
    {
        CoUninitialize();
    }

    return 0;
}
