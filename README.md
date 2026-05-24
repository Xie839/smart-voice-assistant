# VoiceFlow AI

VoiceFlow AI 是一个基于 Qt 的 Windows 桌面端实时语音转文本工具。  
当前版本采用 **sherpa-onnx + Paraformer 中文 ONNX** 作为唯一 ASR 后端。

## 当前主流程

麦克风录音  
→ VAD 自动分句  
→ chunk WAV  
→ `sherpa-onnx-offline.exe` + Paraformer 识别  
→ 文本清洗 / 简繁归一化 / 标点恢复  
→ TranscriptAssembler 聚合显示到左侧“原始识别文本”

## 当前进度

1. Qt 主界面（实时输入工作流）已完成。
2. 麦克风录音与 WAV 保存已完成（完整录音保存到 `temp/recordings/`）。
3. VAD 自动分句已完成（chunk 保存到 `temp/chunks/`）。
4. sherpa-onnx / Paraformer 中文识别已完成。
5. sherpa-onnx 离线标点恢复已接入，失败时回退规则标点。
6. TranscriptAssembler 已接入，实时结果不再按 chunk 机械换行。
7. ASR 支持小并发（默认 `maxConcurrentAsr = 2`），并按 `sequenceId` 有序输出，避免乱序。

## 关键参数（默认）

- `preRollMs = 500`
- `speechStartMs = 120`
- `speechEndSilenceMs = 1000`
- `minSpeechMs = 400`
- `maxSegmentMs = 15000`
- `DISPLAY_PARAGRAPH_GAP_MS = 1600`（TranscriptAssembler 段落换行阈值）

## 目录约定

```text
third_party/
  sherpa-onnx-.../bin/sherpa-onnx-offline.exe
  sherpa-onnx-.../bin/sherpa-onnx-offline-punctuation.exe

models/
  sherpa-onnx/paraformer-zh/model.int8.onnx
  sherpa-onnx/paraformer-zh/tokens.txt
  sherpa-onnx/punctuation/...

temp/
  chunks/
  recordings/
  asr/
```

## 技术栈

- Qt 6.8.3
- CMake
- MinGW 64-bit
- Qt Widgets
- Qt Multimedia
- sherpa-onnx
- Paraformer 中文 ONNX 模型
- VAD 自动分句
- 标点恢复
- 文本后处理
- TranscriptAssembler 文本聚合

## 第三方依赖说明

1. sherpa-onnx 是第三方开源离线语音识别部署框架。  
2. Paraformer 中文 ONNX 模型是第三方预训练模型。  
3. `sherpa-onnx-offline.exe` 与 `sherpa-onnx-offline-punctuation.exe` 为第三方工具。  
4. 本项目没有训练语音识别模型，也不声称模型原创。  

## 本项目原创实现

- 桌面端交互与工作流设计
- 麦克风录音模块封装
- VAD 自动分句
- chunk 管理
- sherpa-onnx 调用封装
- ASR 小并发与按序输出
- 文本清洗、标点恢复与聚合显示

## 运行说明

1. 确保 `third_party` 下存在 sherpa 可执行文件。  
2. 确保 `models/sherpa-onnx/paraformer-zh/` 下存在 `model.int8.onnx` 和 `tokens.txt`。  
3. 如果标点模型缺失，主识别仍可运行，但会回退到规则标点。  
4. 如果 sherpa 工具或主模型缺失，程序会报错提示，不会崩溃。  

## 历史说明

项目早期尝试过其他离线 ASR 路线；当前主分支运行依赖已经统一到 sherpa-onnx / Paraformer。
