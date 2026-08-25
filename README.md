# Metasequoia Voice Input(水杉记言)

English | [简体中文](README.zh-CN.md)

This is a voice input module for [MetasequoiaImeTsf](https://github.com/metasequoiaime/MetasequoiaImeTsf). However, it can be used as a standalone voice input tool for any application without other MetasequoiaIME components.

## How to run

Download release exe file from [releases](https://github.com/metasequoiaime/MetasequoiaVoiceInput/releases).

Extract the release archive to any writable directory. The application reads `config.toml` beside `MetasequoiaVoiceInput.exe`; no files need to be copied to `%LOCALAPPDATA%`.

Right-click the tray icon and choose **Settings** to configure API tokens and behavior in the tabbed WinUI-style settings panel. Restart the app after saving settings.

Then, run `MetasequoiaVoiceInput.exe`.

## Usage

- **Hotkeys**:
  - RAlt pressed to start recording, release to stop recording and send text to active application
  - RAlt + Space: Lock recording
  - Ctrl + F9: Toggle recording

## Configuration

Use **Settings** from the tray menu. The configuration file is stored beside the executable:

```text
<application directory>\config.toml
```

The tabbed settings panel groups ASR, text processing, and general behavior. API endpoints, ASR and polishing tokens, language, cue sounds, text processing, and text output method are all saved there. `SendInput` is the default. `Ctrl+V` output offers compatibility with some applications but replaces the current clipboard contents. Restart the app after saving.

For low-latency ASR, select **JSON WebSocket Streaming ASR** and point it to a compatible service. The protocol uses start JSON, PCM16 binary audio frames, stop JSON, and partial/final/error response events; see [JSON WebSocket Streaming ASR](docs/json-websocket-streaming-asr.md).

Below is the generated configuration template:

```toml
# 自动语音识别（ASR）配置
[asr_api]
# API 基础地址
endpoint = "https://api.siliconflow.cn/v1/audio/transcriptions"
# 服务提供商（如：azure、openai、deepgram 等）
provider = "siliconflow"
# API token. Configure it in the settings window.
token = ""

# 文本润色配置
[polish_api]
# API 基础地址
endpoint = "https://api.siliconflow.cn/v1/chat/completions"
# 服务提供商（如：azure、openai、deepgram 等）
provider = "siliconflow"
# API token. Configure it in the settings window.
token = ""

# 基础设置
[settings]
# 偏好语言
language = "zh-cn"
# 触发时是否播放提示音
notification_sound = true
# 上屏前是否要先进行文本润色
polish_text = false
# ASR transport: cloud_siliconflow (HTTP file transcription) or json_websocket_streaming
stt_provider = "cloud_siliconflow"
# Streaming PCM16 binary frame size, 20-200 ms
streaming_chunk_ms = 40
# Text output: send_input (default) or clipboard_paste (Ctrl+V)
output_method = "send_input"
```

You can also change these config in settings window:

![](https://i.imgur.com/Q3Jct2Z.png)

![](https://i.imgur.com/9j2IV9X.png)

![](https://i.imgur.com/1F47neV.png)

## Notice

- Only implemented dark-mode UI now

## License

GPL-3.0.
