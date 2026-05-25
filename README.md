# VoiceFlow AI

## 实时流式识别说明

项目已接入 sherpa-onnx online streaming 实时流式识别后端。这里的 `online` 指本地 streaming 识别，不是云服务，不需要联网。

实时语音输入默认会优先尝试使用 streaming，以降低用户说话过程中的等待感。如果 streaming 模型、DLL 或运行时依赖不可用，程序会自动回退到稳定的 offline 分段识别方案。本地文件转写仍然使用 offline/exe 流程。

如需强制关闭 streaming，可设置：

```powershell
$env:VOICEFLOW_ENABLE_STREAMING_ASR="0"
.\VoiceFlowAI.exe
```

如需明确强制尝试 streaming，可设置：

```powershell
$env:VOICEFLOW_ENABLE_STREAMING_ASR="1"
.\VoiceFlowAI.exe
```

当前版本为了减少首次点击“开始输入”时的等待，会在程序启动后约 500ms 异步预初始化 streaming recognizer。模型扫描、DLL 检查、运行时加载、recognizer 创建和静音 smoke test 都放在后台执行，不阻塞 UI。

streaming recognizer 会在首次初始化成功后常驻复用；后续“停止 → 再开始”只会销毁并重建当前 stream/session，不会重复扫描 DLL、加载模型、LoadLibrary 或运行 smoke test。点击停止时会立即停止接收新的音频帧，并丢弃当前临时 stream，recognizer 会保留到程序退出时再释放。

partial result 仅作为临时状态展示，不写入历史记录，不触发保存，也不进入 TranscriptAssembler；final result 才会进入正式文本聚合流程。

---

## Windows ONNX Runtime DLL 版本冲突说明

streaming 后端需要以下 DLL 版本匹配：

```text
sherpa-onnx-c-api.dll
onnxruntime.dll
onnxruntime_providers_shared.dll
```

它们必须来自同一个 sherpa-onnx release。

如果日志中出现类似错误：

```text
requested API version
Current ORT Version
```

通常说明程序加载到了旧的或不匹配的 `onnxruntime.dll`。

程序会优先选择同一目录中同时包含这三个 DLL 的 sherpa runtime 目录，并在创建 recognizer 前显式加载这些 DLL；实际路径会写入：

```text
logs/perf-YYYYMMDD.log
```

---

## 性能诊断日志

项目支持语音识别全链路耗时诊断，用于定位从 VAD 检测到语音结束、chunk WAV 写入、ASR 排队、sherpa-onnx 进程执行、JSON 解析、标点恢复、文本聚合到 UI 显示的耗时。

Debug 模式默认打印性能日志；Release 模式可通过环境变量开启：

```powershell
$env:VOICEFLOW_PERF_LOG="1"
.\VoiceFlowAI.exe
```

性能日志会同时输出到控制台和应用目录下的日志文件：

```text
logs/perf-YYYYMMDD.log
```

日志会记录 VAD 静音等待、chunk WAV 写入、ASR 入队和队列等待、sherpa-onnx 进程耗时、stdout/stderr 字节数、JSON 解析、标点恢复、UI 更新、本地文件转写和 ffmpeg 转换耗时。

日志不会打印 API Key、Authorization header、完整识别文本、完整用户文本或完整自定义 prompt。

---

## 性能优化说明

当前实时识别延迟主要由 VAD 静音等待和 ASR 后端识别耗时组成。

默认 `speechEndSilenceMs` 已调整为：

```text
800ms
```

用于在响应速度和句子完整性之间折中。该值越小，用户说完后的响应越快，但过小可能导致一句话被自然停顿切碎。

实时语音输入会优先复用常驻 streaming recognizer，避免每次点击“开始输入”都重新加载模型。offline/exe 仍作为 fallback 保留，因此日志中的 `backend=offline-exe` 和 `sherpa_process_elapsed_ms` 主要用于定位回退链路的耗时。

过短 chunk 会被跳过并在性能日志中标记：

```text
chunk_skipped_too_short
```

用于避免噪声片段或过短音频触发 sherpa 进程。

---

## 目录约定

```text
config/
  config.example.json
  config.json                    # 本地生成，不提交

third_party/
  sherpa-onnx-.../               # 第三方 sherpa-onnx 运行包，不提交真实二进制

tools/
  ffmpeg/bin/ffmpeg.exe          # 可选，本地音视频转写使用

models/
  sherpa-onnx/
    paraformer-zh/
      model.int8.onnx
      tokens.txt
    punctuation/
      ...
    streaming-zh/
      encoder.int8.onnx
      decoder.int8.onnx
      tokens.txt

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
- Qt Network
- sherpa-onnx
- Paraformer 中文 ONNX 模型
- VAD 自动分句
- 标点恢复
- 文本后处理
- TranscriptAssembler 文本聚合

## 第三方依赖说明

1. sherpa-onnx 是第三方开源离线语音识别部署框架；
2. Paraformer 中文 ONNX 模型是第三方预训练模型；
3. `sherpa-onnx-offline.exe` 与 `sherpa-onnx-offline-punctuation.exe` 是第三方工具；
4. DeepSeek API 是第三方在线模型服务；
5. 本项目没有训练 sherpa/Paraformer/DeepSeek 模型，也不声称模型原创。

## 本项目原创实现

- 桌面端交互与工作流设计
- 麦克风录音模块封装
- VAD 自动分句
- chunk 管理
- sherpa-onnx 调用封装
- ASR 小并发与按序输出
- 文本清洗、标点恢复与聚合显示
- DeepSeek 配置管理、异步请求封装与结果展示

## 运行说明

1. 确保 `third_party` 下存在 sherpa 可执行文件；
2. 确保 `models/sherpa-onnx/paraformer-zh/` 下存在 `model.int8.onnx` 和 `tokens.txt`；
3. 如标点模型缺失，主识别仍可运行，但会回退规则标点；
4. `config/config.json` 缺失时会自动从 `config/config.example.json` 初始化；
5. 若配置了 `DEEPSEEK_API_KEY` 环境变量，程序将优先使用该 Key。

## 本地音视频文件转写

- 本地文件转写除 WAV 外，支持 `mp3 / m4a / aac / flac / ogg / mp4 / mov / avi / mkv / wmv`。
- 对于非 WAV 文件，程序会先调用 ffmpeg 提取/转换音频，再复用现有 sherpa-onnx 识别流程。
- 转换参数统一为：`16kHz + 单声道 + 16-bit PCM WAV`，便于与当前离线 ASR 对齐。
- 转写完成后可继续使用 AI 智能优化、自定义提示词、保存、导出与历史记录。

### ffmpeg 查找优先级

1. `tools/ffmpeg/bin/ffmpeg.exe`
2. `third_party/ffmpeg/bin/ffmpeg.exe`
3. 应用程序目录下的 `ffmpeg.exe`
4. 系统 PATH 中的 `ffmpeg`

推荐随项目打包内置 ffmpeg，优先命中第 1 或第 2 路径。

## 界面说明（当前版本）

主界面分为两个核心页面：

1. 实时语音输入  
   - 麦克风输入  
   - VAD 自动分句  
   - sherpa-onnx 本地识别  
   - AI 文本优化  
   - 底部左侧按钮：`开始输入`、`停止`

2. 本地文件转写  
   - 选择本地文件  
   - 开始转写  
   - 识别结果显示到左侧原始识别文本  
   - 支持 AI 优化  
   - 底部左侧按钮：`选择文件`、`开始转写`

说明：当前语音识别模型固定为 `sherpa-onnx / Paraformer` 中文 ONNX，不再提供前端模型切换控件。
