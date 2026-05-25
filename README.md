# VoiceFlow AI 智能语音助手

VoiceFlow AI 是一个基于 Qt/C++ 开发的 Windows 桌面端智能语音助手，主要用于将实时语音或本地音视频文件转换为可编辑文本，并支持 AI 智能优化、自定义提示词、复制导出、保存和历史记录管理等功能。

项目当前采用 `sherpa-onnx + Paraformer 中文 ONNX` 作为本地语音识别后端，支持实时语音输入和本地文件转写；同时接入 DeepSeek API，用于对识别结果进行文本润色、摘要整理、会议纪要生成等智能处理。

---

## Demo 视频与可运行版本

Demo 视频完整展示了实时语音输入、本地文件转写、AI 智能优化、自定义提示词、复制导出保存和历史记录等核心功能。

- Demo 视频：[B站链接](https://www.bilibili.com/video/BV1ugGR6MErF/)


---

## 项目背景与用户需求

在日常学习、办公、会议记录、访谈整理和内容写作场景中，用户经常需要将语音内容快速转换成文本。如果完全依赖键盘输入或人工听写，不仅效率较低，而且容易遗漏关键信息。因此，用户需要一个能够完成语音转文字、文本整理和结果保存的桌面端工具。

本项目主要面向两类核心用户需求。

第一类是 **实时语音转文字需求**。在办公记录、课堂学习、会议讨论或个人写作过程中，用户可能希望直接通过语音输入内容，而不是完全依赖键盘打字。这样可以把说话内容实时转换成可编辑文本，提高文本输入效率，也方便用户在输入过程中及时查看和修改。因此，系统提供了实时语音输入功能，可以通过麦克风采集语音，并将语音内容实时转换为文字。

第二类是 **已有录音文件转写需求**。有些情况下，用户可能不会直接在电脑端实时输入，而是先通过手机、录音笔、会议软件或其他设备录制一段完整的音频或视频。例如课堂录音、会议录音、采访录音等。录制完成后，用户希望能够一次性把整段音频转换成文本，方便后续整理、归档和编辑。因此，系统提供了本地文件转写功能，可以选择已经录制好的音频或视频文件，并自动完成音频预处理和语音转文字。

在完成语音转文字之后，真实识别结果往往还会比较口语化，可能存在停顿、重复或表达不够规范的问题。因此，本项目进一步加入了 AI 智能优化和自定义提示词功能，可以将原始识别文本整理成更加清晰、规范、适合阅读和保存的文本。最终形成从“语音输入”到“文本生成”，再到“文本优化”和“结果管理”的完整流程。

---
---

## 技术栈

| 模块 | 技术 |
|---|---|
| 桌面端框架 | Qt 6.8.3 |
| 构建工具 | CMake |
| 编译环境 | MinGW 64-bit |
| UI 框架 | Qt Widgets |
| 音频采集 | Qt Multimedia |
| 网络请求 | Qt Network |
| 本地语音识别 | sherpa-onnx |
| 中文 ASR 模型 | Paraformer 中文 ONNX |
| 流式识别 | sherpa-onnx online streaming |
| 音视频预处理 | ffmpeg |
| 语音端点检测 | VAD 自动分句 |
| 文本后处理 | 文本清洗、简繁归一、标点恢复 |
| 文本聚合 | TranscriptAssembler |
| AI 文本优化 | DeepSeek API |
| 本地持久化 | JSON / 本地文件 |


---

## 第三方依赖说明

本项目使用了多个第三方工具和模型，包括 sherpa-onnx、Paraformer 中文 ONNX 模型、sherpa-onnx-offline.exe、sherpa-onnx-offline-punctuation.exe、ffmpeg 和 DeepSeek API。

sherpa-onnx 是第三方开源离线语音识别部署框架；Paraformer 中文 ONNX 模型是第三方中文语音识别模型；sherpa-onnx-offline.exe 和 sherpa-onnx-offline-punctuation.exe 是第三方本地识别和标点恢复工具；ffmpeg 是第三方音视频处理工具；DeepSeek API 是第三方在线大模型服务。

本项目没有训练 sherpa、Paraformer 或 DeepSeek 模型，也不声称这些模型为原创。

---

## 本项目原创实现

本项目主要原创实现包括：

```text
桌面端交互与工作流设计
麦克风录音模块封装
VAD 自动分句
chunk 音频片段管理
sherpa-onnx 调用封装
streaming 实时识别接入与 fallback
ASR 小并发与按序输出
文本清洗、简繁归一和标点恢复
TranscriptAssembler 文本聚合显示
DeepSeek 配置管理、异步请求封装和结果展示
本地文件转写流程
ffmpeg 文件预处理封装
历史记录管理
复制、导出、保存、清空等文本操作
性能诊断日志
```

---
## 核心功能

VoiceFlow AI 当前主要包含实时语音输入、本地文件转写、AI 智能优化、自定义提示词优化、文本操作和历史记录管理等功能。

### 实时语音输入

实时语音输入功能用于通过麦克风采集用户语音，并自动转换为文本。系统当前默认优先使用 sherpa-onnx streaming 实时识别方案，用于降低用户说话过程中的等待感；如果 streaming 模型、DLL 或运行时依赖不可用，程序会自动回退到稳定的 offline 分段识别方案。

实时语音输入适用于办公记录、课堂笔记、会议讨论和个人口述写作等场景。

### 本地文件转写

本地文件转写功能用于处理用户已经录制好的音频或视频文件。用户可以选择本地文件，系统会自动进行音频预处理，并调用本地语音识别模型完成转写。

当前支持常见格式：

```text
wav / mp3 / m4a / aac / flac / ogg / mp4 / mov / avi / mkv / wmv
```

对于非 WAV 文件，程序会先调用 ffmpeg 提取或转换音频，将其统一转换为：

```text
16kHz + 单声道 + 16-bit PCM WAV
```

然后再复用本地语音识别流程完成转写。

### AI 智能优化

项目支持通过 DeepSeek API 对语音识别文本进行智能整理。用户可以将左侧原始识别文本发送给 DeepSeek API，生成更加规范、清晰、适合阅读和保存的优化文本。

该功能适用于：

```text
会议纪要生成
课堂笔记整理
访谈内容归纳
口语化文本润色
汇报材料整理
文本摘要生成
```

### 自定义提示词优化

除默认“AI智能优化”外，用户还可以点击“自定义提示词”，输入自己的文本处理要求。系统会结合用户输入的提示词和当前文本调用 DeepSeek API，并将处理结果显示到右侧“优化后文本”区域。

示例提示词：

```text
请整理成会议纪要
请提取要点并分条列出
请改写成正式汇报语气
请翻译成英文
请压缩成 200 字以内摘要
请整理成适合项目汇报的内容
```

### 文本操作

VoiceFlow AI 支持对识别和优化结果进行基础文本操作：

- 复制：优先复制用户选中的文本；如果未选中文本，则优先复制优化后文本；如果优化后文本为空，则复制原始识别文本；
- 导出：用户可选择路径导出为 `txt` 或 `md` 文件；
- 保存：自动保存到 `results/` 目录，并写入历史记录；
- 清空：清空当前页面的原始识别文本和优化后文本。

导出和保存均使用 UTF-8 编码，避免中文乱码。

### 历史记录

系统支持将识别结果和优化结果保存到本地历史记录中。用户可以在历史记录页面查看之前保存的内容，也可以将历史记录重新加载回编辑区继续处理。

历史记录保存在本地，不会自动上传服务器。



## 主流程说明

实时语音输入主流程：

```text
麦克风录音
→ streaming 实时识别 / offline fallback
→ VAD 自动分句
→ 文本清洗 / 简繁归一化 / 标点恢复
→ TranscriptAssembler 聚合
→ 显示到左侧“原始识别文本”
```

本地文件转写主流程：

```text
选择本地音频或视频文件
→ 如果不是 WAV，则调用 ffmpeg 提取 / 转换音频
→ 转换为 16kHz 单声道 PCM WAV
→ sherpa-onnx-offline.exe + Paraformer 中文模型识别
→ 文本清洗 / 标点恢复
→ 显示到左侧“原始识别文本”
```

AI 智能优化主流程：

```text
读取原始识别文本或用户选中文本
→ 结合默认提示词或自定义提示词
→ 调用 DeepSeek API
→ 显示到右侧“优化后文本”
```

---

## 界面说明

主界面左侧为功能导航栏，当前包括：

```text
实时语音输入
本地文件转写
历史记录
设置
```

### 实时语音输入页面

用于通过麦克风实时识别语音。

页面中左侧显示原始识别文本，右侧显示优化后文本，底部提供“开始输入”“停止”“AI智能优化”“自定义提示词”“复制”“导出”“保存”“清空”等按钮。

### 本地文件转写页面

用于选择本地音频或视频文件进行转写。

页面中左侧显示文件转写后的原始识别文本，右侧显示 AI 优化后的文本，底部提供“选择文件”“开始转写”“AI智能优化”“自定义提示词”“复制”“导出”“保存”“清空”等按钮。

### 历史记录页面

用于查看已经保存的识别结果和优化结果。

左侧显示历史记录列表，右侧显示当前选中记录的原始识别文本和优化后文本。用户可以将历史记录加载回编辑区，也可以复制、导出、删除或清空历史记录。

---

## 运行说明

运行项目前，需要准备 sherpa-onnx 可执行程序、Paraformer 中文模型、streaming 模型、可选的标点模型、可选的 ffmpeg 工具，以及 DeepSeek API 配置。

首先确保 `third_party/` 下存在 sherpa-onnx 可执行文件和运行时依赖，例如：

```text
third_party/
  sherpa-onnx-.../
    bin/
      sherpa-onnx.exe
      sherpa-onnx-offline.exe
      sherpa-onnx-offline-punctuation.exe
      *.dll
```

然后确保 Paraformer 中文模型位于：

```text
models/
  sherpa-onnx/
    paraformer-zh/
      model.int8.onnx
      tokens.txt
```

如果使用 streaming 实时识别，请确保 streaming 模型位于：

```text
models/
  sherpa-onnx/
    streaming-zh/
      encoder.int8.onnx
      decoder.int8.onnx
      tokens.txt
```

如果需要标点恢复模型，可以放置在：

```text
models/
  sherpa-onnx/
    punctuation/
      ...
```

如果标点模型缺失，主识别仍可运行，但会回退到规则标点。

如果需要支持 mp3、mp4、m4a、flac 等非 WAV 文件转写，请准备 ffmpeg。推荐目录为：

```text
tools/
  ffmpeg/
    bin/
      ffmpeg.exe
```

ffmpeg 查找优先级如下：

```text
1. tools/ffmpeg/bin/ffmpeg.exe
2. third_party/ffmpeg/bin/ffmpeg.exe
3. 应用程序目录下的 ffmpeg.exe
4. 系统 PATH 中的 ffmpeg
```

---

## DeepSeek API 配置

项目支持通过 DeepSeek API 对语音识别文本进行智能优化。用户可以在软件中点击“打开配置”填写自己的 API Key，也可以手动复制配置模板：

```powershell
copy config\config.example.json config\config.json
```

然后填写：

```json
{
  "deepseek": {
    "api_key": "你的 DeepSeek API Key",
    "model": "deepseek-v4-flash"
  }
}
```

配置文件保存到：

```text
config/config.json
```

该文件不提交到 Git。

也可以通过环境变量配置：

```powershell
$env:DEEPSEEK_API_KEY="你的 DeepSeek API Key"
```

环境变量优先级高于本地配置文件。

---

## streaming 实时识别说明

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
  converted/
  asr/

results/
  voiceflow_result_*.md

data/
  history/
    history.json

logs/
  perf-YYYYMMDD.log
```



## 隐私与安全说明

语音识别默认使用本地 sherpa-onnx 模型完成，实时语音和本地文件转写不会自动上传到云端。只有用户点击 AI 智能优化或自定义提示词时，文本才会发送给 DeepSeek API。

用户需要自行配置自己的 API Key，本项目不会内置或提交任何真实 API Key。

历史记录保存在用户本地。`config/config.json`、`results/`、`data/history/`、模型文件、第三方二进制文件均不应提交到 Git。

---
