# tools 目录说明

当前项目主要依赖 sherpa-onnx 工具链。

建议将以下文件放在 `third_party/sherpa-onnx.../bin/`，程序会自动搜索：

- `sherpa-onnx-offline.exe`
- `sherpa-onnx-offline-punctuation.exe`
- 运行所需 DLL（如 `onnxruntime.dll` 等）

本目录可保留项目辅助脚本（如 `funasr_asr.py`），但 **当前主流程不依赖 whisper-cli**。
