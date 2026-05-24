# VoiceFlow AI

VoiceFlow AI 是一个基于 Qt 的 Windows 桌面端实时语音转文本工具。  
当前版本采用 **sherpa-onnx + Paraformer 中文 ONNX** 作为 ASR 后端，并支持 **DeepSeek API 智能文本优化**。

## 当前主流程

麦克风录音  
→ VAD 自动分句  
→ chunk WAV  
→ sherpa-onnx-offline.exe + Paraformer 识别  
→ 文本清洗 / 简繁归一化 / 标点恢复  
→ TranscriptAssembler 聚合显示到左侧“原始识别文本”

## DeepSeek AI 智能文本优化

- 项目支持通过 DeepSeek API 对语音识别文本进行智能整理；
- 用户可以在软件中点击“打开配置”填写自己的 DeepSeek API Key；
- 配置保存到本地 `config/config.json`；
- `config/config.json` 不提交到 Git；
- 也可以通过环境变量 `DEEPSEEK_API_KEY` 配置（优先级更高）；
- 点击“AI智能优化”后，程序读取左侧原始识别文本，调用 DeepSeek API，并将结果显示到右侧优化后文本；
- 默认模型为 `deepseek-v4-flash`，可在配置窗口中修改。

### 自定义提示词优化

- 除默认“AI智能优化”外，用户可以点击“自定义提示词”按钮；
- 用户可输入自己的文本处理要求；
- 程序会将用户提示词和左侧原始识别文本一起发送给 DeepSeek API；
- 结果显示在右侧“优化后文本”区域；
- 示例提示词包括：整理成会议纪要、提取要点、改写成正式文稿、翻译成英文、改写得更口语化；
- 用户的 API Key 由用户自己在配置窗口中填写；
- 项目不会内置或提交任何真实 API Key。

### 文本操作

VoiceFlow AI 支持对识别和优化结果进行基础文本操作：

- 复制：优先复制优化后文本，如果没有优化结果则复制原始识别文本；
- 导出：用户可选择路径导出为 `txt` 或 `md` 文件；
- 保存：自动保存到 `results/` 目录；
- 清空：清空当前页面的原始识别文本和优化后文本。

说明：导出和保存均使用 UTF-8 编码，避免中文乱码。

### 配置说明

1. 在软件中点击“打开配置”；
2. 填写 API Key；
3. 点击“测试连接”；
4. 连接成功后即可使用“AI智能优化”。

或者手动复制：

```powershell
copy config\config.example.json config\config.json
```

然后填写：

```json
{
  "deepseek": {
    "api_key": "你的 DeepSeek API Key"
  }
}
```

## 目录约定

```text
config/
  config.example.json
  config.json                    # 本地生成，不提交

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
