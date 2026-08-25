# Metasequoia Voice Input（水杉记言）

[English](README.md) | 简体中文

水杉记言是一款语音输入模块，最初为 [MetasequoiaImeTsf](https://github.com/metasequoiaime/MetasequoiaImeTsf) 设计，但也可以**独立运行**，作为通用语音输入工具用于任何应用程序，而无需安装其他输入法组件。

---

## 运行方法

1. 从 GitHub 的 Releases 页面下载最新版本：

[https://github.com/metasequoiaime/MetasequoiaVoiceInput/releases](https://github.com/metasequoiaime/MetasequoiaVoiceInput/releases)

2. 解压发布包到任意可写目录。程序从 `MetasequoiaVoiceInput.exe` 同级目录读取 `config.toml`，无需复制文件到 `%LOCALAPPDATA%`。

3. 右键点击托盘图标，选择“设置”，在 WinUI 风格的设置面板中填写 API Token 和其他参数。设置按“语音识别 / 文本处理 / 常规”分组展示；保存后重启程序生效。

4. 运行：

```
MetasequoiaVoiceInput.exe
```

---

## 使用方法

### 快捷键

- **RAlt 按下开始录音，松开停止录音并将识别结果发送到当前活动应用**
- **RAlt + Space：锁定录音（无需持续按住）**
- **Ctrl + F9：切换录音状态**

支持在任意输入场景下快速语音转文字。

---

## 配置说明

优先通过托盘菜单的“设置”打开配置面板。配置文件位于程序目录：

```text
<程序目录>\config.toml
```

设置面板会创建并保存该文件。所有 API endpoint、ASR/润色 Token、语言、提示音和文本处理开关都可在其中修改；保存后重启应用生效。

下面是生成的配置示例：

```toml
# 自动语音识别（ASR）配置
[asr_api]
# API 基础地址
endpoint = "https://api.siliconflow.cn/v1/audio/transcriptions"
# 服务提供商（如：azure、openai、deepgram 等）
provider = "siliconflow"
# API Token（在设置面板中填写）
token = ""

# 文本润色配置
[polish_api]
# API 基础地址
endpoint = "https://api.siliconflow.cn/v1/chat/completions"
# 服务提供商（如：azure、openai、deepgram 等）
provider = "siliconflow"
# API Token（在设置面板中填写）
token = ""

# 基础设置
[settings]
# 偏好语言
language = "zh-cn"
# 触发时是否播放提示音
notification_sound = true
# 上屏前是否进行文本润色
polish_text = false
# ASR 传输方式：cloud_siliconflow（HTTP 文件转写）或 json_websocket_streaming（统一 JSON WebSocket 流式协议）
stt_provider = "cloud_siliconflow"
# 流式模式每个 PCM16 binary frame 的时长，范围 20-200
streaming_chunk_ms = 40
# 上屏方式：send_input（默认）或 clipboard_paste（Ctrl+V 粘贴）
output_method = "send_input"
```

---

## 图形界面设置

右键点击托盘图标后选择“设置”，即可打开 WinUI 风格的设置窗口。窗口按“语音识别”“文本处理”“常规”和“关于”分组，避免手动查找或编辑配置文件。默认使用 SendInput 上屏；“Ctrl+V 粘贴”兼容部分应用，但会覆盖当前剪贴板内容。

流式识别可选择“JSON WebSocket 流式 ASR”。它使用统一协议：start JSON、PCM16 binary 音频帧、stop JSON，以及 partial/final/error 响应。完整协议见 [JSON WebSocket Streaming ASR](docs/json-websocket-streaming-asr.md)。

---

## 特性

- 支持本地 Whisper 模型
- 支持 SiliconFlow 云端识别
- 支持文本润色（LLM 优化表达）
- 支持锁定录音模式
- 支持提示音反馈
- 可独立运行，无需输入法环境

## 注意事项

- UI 目前只适配了暗色模式

---

## 许可证

本项目采用 **GPL-3.0 License** 开源协议。
