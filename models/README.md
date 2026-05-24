# models 目录说明

当前项目主识别后端为 **sherpa-onnx / Paraformer 中文 ONNX**。  
请准备以下模型文件：

```text
models/sherpa-onnx/paraformer-zh/model.int8.onnx
models/sherpa-onnx/paraformer-zh/tokens.txt
models/sherpa-onnx/punctuation/...
```

说明：

- `model.int8.onnx` 和 `tokens.txt` 为主 ASR 必需文件。
- `punctuation` 目录用于离线标点恢复；缺失时程序会回退规则标点。
- 大模型和二进制文件不建议提交到 Git 仓库。
