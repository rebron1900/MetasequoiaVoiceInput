# JSON WebSocket Streaming ASR Protocol

`json_websocket_streaming` is a provider for an ASR service that implements the following bidirectional WebSocket protocol. It is intended for a local gateway or an HTTPS/WSS-hosted proxy that adapts a vendor-specific streaming ASR service.

## Connection

- Remote services must use `wss://`.
- Development services may use `ws://localhost`, `ws://127.0.0.1`, or `ws://[::1]`.
- When configured, the client sends `Authorization: Bearer <token>` during the WebSocket handshake.

## Client events

After connecting, the client sends this text frame:

```json
{
  "type": "start",
  "sample_rate": 16000,
  "channels": 1,
  "format": "pcm_s16le",
  "language": "zh-cn"
}
```

While recording, the application buffers PCM locally. After recording stops, it sends binary WebSocket frames containing little-endian signed 16-bit PCM. The configured chunk size defaults to 40 ms.

When recording stops, it sends this text frame:

```json
{ "type": "stop" }
```

## Server events

The server must send text WebSocket frames containing JSON:

```json
{ "type": "partial", "text": "intermediate transcription" }
{ "type": "final", "text": "final transcription" }
{ "type": "error", "message": "human-readable failure" }
```

`partial` events are logged by the application. Exactly one `final` event completes the recording session and is then passed through optional text processing and the selected output method.

The client waits up to 10 seconds for `final` after sending `stop`.
