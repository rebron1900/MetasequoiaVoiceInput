#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "audio_capture.h"
#include "mvi_logger.h"

static ma_device g_device;

static void data_callback(ma_device *device, void *, const void *input, ma_uint32 frameCount)
{
    auto *cap = static_cast<AudioCapture *>(device->pUserData);
    if (cap && input)
    {
        cap->onAudio(static_cast<const float *>(input), frameCount);
    }
}

bool AudioCapture::start(AudioCallback cb)
{
    callback_ = std::move(cb);

    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_f32;
    config.capture.channels = 1;
    config.sampleRate = 16000;
    config.dataCallback = data_callback;
    config.pUserData = this;

    const ma_result init_result = ma_device_init(nullptr, &config, &g_device);
    if (init_result != MA_SUCCESS)
    {
        mvi_logger::Write("MIC", "device init failed code=" + std::to_string(static_cast<int>(init_result)));
        return false;
    }

    const ma_result start_result = ma_device_start(&g_device);
    if (start_result != MA_SUCCESS)
    {
        mvi_logger::Write("MIC", "device start failed code=" + std::to_string(static_cast<int>(start_result)));
        ma_device_uninit(&g_device);
        return false;
    }
    mvi_logger::Write("MIC", "capture started sample_rate=16000 channels=1");
    return true;
}

void AudioCapture::stop()
{
    ma_device_uninit(&g_device);
    mvi_logger::Write("MIC", "capture stopped");
}

void AudioCapture::onAudio(const float *data, size_t count)
{
    if (callback_)
    {
        callback_(data, count);
    }
}