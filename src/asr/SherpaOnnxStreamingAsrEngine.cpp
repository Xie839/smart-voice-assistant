#include "SherpaOnnxStreamingAsrEngine.h"

#include "../utils/PerfTracer.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLibrary>
#include <QSet>
#include <QVector>
#include <QtGlobal>

#include <algorithm>
#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifdef VOICEFLOW_SHERPA_STREAMING_HEADERS
#include <sherpa-onnx/c-api/c-api.h>
#endif

namespace
{
constexpr int kFrameLogIntervalMs = 300;

bool compiledWithSherpaStreamingApi()
{
#ifdef VOICEFLOW_SHERPA_STREAMING_HEADERS
    return true;
#else
    return false;
#endif
}

QString modelKindName(SherpaOnnxStreamingAsrEngine::ModelKind kind)
{
    switch (kind)
    {
    case SherpaOnnxStreamingAsrEngine::ModelKind::Paraformer:
        return "online_paraformer";
    case SherpaOnnxStreamingAsrEngine::ModelKind::Transducer:
        return "online_transducer";
    case SherpaOnnxStreamingAsrEngine::ModelKind::Ctc:
        return "online_ctc";
    default:
        return "unknown";
    }
}
}

struct SherpaOnnxStreamingAsrEngine::Impl
{
    QLibrary library;

#ifdef VOICEFLOW_SHERPA_STREAMING_HEADERS
    using CreateRecognizerFn = const SherpaOnnxOnlineRecognizer *(*)(const SherpaOnnxOnlineRecognizerConfig *);
    using DestroyRecognizerFn = void (*)(const SherpaOnnxOnlineRecognizer *);
    using CreateStreamFn = const SherpaOnnxOnlineStream *(*)(const SherpaOnnxOnlineRecognizer *);
    using DestroyStreamFn = void (*)(const SherpaOnnxOnlineStream *);
    using AcceptWaveformFn = void (*)(const SherpaOnnxOnlineStream *, int32_t, const float *, int32_t);
    using IsReadyFn = int32_t (*)(const SherpaOnnxOnlineRecognizer *, const SherpaOnnxOnlineStream *);
    using DecodeFn = void (*)(const SherpaOnnxOnlineRecognizer *, const SherpaOnnxOnlineStream *);
    using GetResultFn = const SherpaOnnxOnlineRecognizerResult *(*)(const SherpaOnnxOnlineRecognizer *, const SherpaOnnxOnlineStream *);
    using DestroyResultFn = void (*)(const SherpaOnnxOnlineRecognizerResult *);
    using InputFinishedFn = void (*)(const SherpaOnnxOnlineStream *);
    using SetOptionFn = void (*)(const SherpaOnnxOnlineStream *, const char *, const char *);

    CreateRecognizerFn createRecognizer = nullptr;
    DestroyRecognizerFn destroyRecognizer = nullptr;
    CreateStreamFn createStream = nullptr;
    DestroyStreamFn destroyStream = nullptr;
    AcceptWaveformFn acceptWaveform = nullptr;
    IsReadyFn isReady = nullptr;
    DecodeFn decode = nullptr;
    GetResultFn getResult = nullptr;
    DestroyResultFn destroyResult = nullptr;
    InputFinishedFn inputFinished = nullptr;
    SetOptionFn setOption = nullptr;

    const SherpaOnnxOnlineRecognizer *recognizer = nullptr;
    const SherpaOnnxOnlineStream *stream = nullptr;
#endif

#ifdef Q_OS_WIN
    HMODULE onnxruntimeHandle = nullptr;
    HMODULE providersHandle = nullptr;
    HMODULE sherpaHandle = nullptr;
#endif
};

SherpaOnnxStreamingAsrEngine::SherpaOnnxStreamingAsrEngine(QObject *parent)
    : QObject(parent)
{
    m_impl = std::make_unique<Impl>();
}

SherpaOnnxStreamingAsrEngine::~SherpaOnnxStreamingAsrEngine()
{
    resetSession();
#ifdef VOICEFLOW_SHERPA_STREAMING_HEADERS
    if (m_impl && m_impl->recognizer && m_impl->destroyRecognizer)
    {
        m_impl->destroyRecognizer(m_impl->recognizer);
        m_impl->recognizer = nullptr;
    }
#endif
}

bool SherpaOnnxStreamingAsrEngine::isAvailable(QString *reason) const
{
    QStringList reasons;
    QString sdkReason;
    QString modelReason;
    QString runtimeReason;
    const bool sdkReady = findStreamingSdk(&sdkReason);
    const bool modelReady = findStreamingModel(nullptr, &modelReason);
    const QString runtimeDir = findSherpaRuntimeDir(&runtimeReason);

    if (!compiledWithSherpaStreamingApi())
    {
        reasons << "streaming C API 头文件未启用，当前构建只能回退 offline/exe";
    }
    if (!sdkReady)
    {
        reasons << sdkReason;
    }
    if (runtimeDir.isEmpty())
    {
        reasons << runtimeReason;
    }
    if (!modelReady)
    {
        reasons << modelReason;
    }

    if (reason)
    {
        *reason = reasons.join("；");
    }
    const bool available = reasons.isEmpty();
    qWarning() << "[STREAM] backend available check=" << available
               << "reason=" << reasons.join("；");
    return available;
}

bool SherpaOnnxStreamingAsrEngine::initialize(QString *errorMessage)
{
    PerfTracer::startTrace("STREAM", m_activeTraceId, "streaming_model_init_start");

    QString reason;
    if (!isAvailable(&reason))
    {
        if (errorMessage)
        {
            *errorMessage = reason;
        }
        if (!m_unavailableLogged)
        {
            PerfTracer::markTrace("STREAM", m_activeTraceId, "streaming_backend_unavailable", reason);
            PerfTracer::markTrace("ASR", m_activeTraceId, "fallback_backend", "backend=offline-exe");
            m_unavailableLogged = true;
        }
        return false;
    }

#ifndef VOICEFLOW_SHERPA_STREAMING_HEADERS
    if (errorMessage)
    {
        *errorMessage = "streaming C API 头文件未启用";
    }
    return false;
#else
    if (m_initialized)
    {
        return true;
    }

    ModelFiles modelFiles;
    if (!findStreamingModel(&modelFiles, &reason))
    {
        if (errorMessage)
        {
            *errorMessage = reason;
        }
        PerfTracer::markTrace("STREAM", m_activeTraceId, "streaming_backend_unavailable", reason);
        return false;
    }

    qWarning() << "[STREAM] model_dir=" << modelFiles.modelDir;
    qWarning() << "[STREAM] tokens=" << (QFileInfo::exists(modelFiles.tokensPath) ? "exists" : "missing")
               << "path=" << QFileInfo(modelFiles.tokensPath).fileName();
    qWarning() << "[STREAM] encoder=" << (modelFiles.encoderPath.isEmpty() ? "missing" : QFileInfo(modelFiles.encoderPath).fileName());
    qWarning() << "[STREAM] decoder=" << (modelFiles.decoderPath.isEmpty() ? "missing" : QFileInfo(modelFiles.decoderPath).fileName());
    qWarning() << "[STREAM] joiner=" << (modelFiles.joinerPath.isEmpty() ? "missing" : QFileInfo(modelFiles.joinerPath).fileName());
    qWarning() << "[STREAM] detected_model_type=" << modelKindName(modelFiles.kind);
    PerfTracer::markTrace("STREAM", m_activeTraceId, "streaming_model_detected",
                          QString("model_dir=%1, detected_model_type=%2")
                              .arg(modelFiles.modelDir, modelKindName(modelFiles.kind)));

    if (modelFiles.kind == ModelKind::Unknown)
    {
        reason = "无法判断 streaming 模型类型";
        if (errorMessage)
        {
            *errorMessage = reason;
        }
        qWarning() << "[STREAM] backend init fail:" << reason;
        PerfTracer::markTrace("STREAM", m_activeTraceId, "streaming_backend_init_skipped", reason);
        return false;
    }

    m_selectedModelFiles = modelFiles;
    const QString runtimeDir = findSherpaRuntimeDir(&reason);
    if (runtimeDir.isEmpty())
    {
        if (errorMessage)
        {
            *errorMessage = reason;
        }
        PerfTracer::markTrace("STREAM", m_activeTraceId, "streaming_runtime_dir_missing", reason);
        return false;
    }
    m_selectedRuntimeDir = runtimeDir;
    const QDir dllDir(runtimeDir);
    const QString dllPath = dllDir.filePath("sherpa-onnx-c-api.dll");
    qWarning() << "[STREAM] sdk_available=true";
    qWarning() << "[STREAM] sherpa_bin_dir=" << dllDir.absolutePath();
    qWarning() << "[STREAM] dll_check sherpa-onnx-c-api.dll=" << QFileInfo::exists(dllPath);
    qWarning() << "[STREAM] dll_check onnxruntime.dll=" << QFileInfo::exists(dllDir.filePath("onnxruntime.dll"));
    qWarning() << "[STREAM] dll_check onnxruntime_providers_shared.dll="
               << QFileInfo::exists(dllDir.filePath("onnxruntime_providers_shared.dll"));
    if (!QFileInfo::exists(dllPath) ||
        !QFileInfo::exists(dllDir.filePath("onnxruntime.dll")) ||
        !QFileInfo::exists(dllDir.filePath("onnxruntime_providers_shared.dll")))
    {
        reason = "streaming 依赖 DLL 不完整，请检查 sherpa-onnx-c-api.dll、onnxruntime.dll、onnxruntime_providers_shared.dll";
        if (errorMessage)
        {
            *errorMessage = reason;
        }
        PerfTracer::markTrace("STREAM", m_activeTraceId, "streaming_backend_unavailable", reason);
        return false;
    }
    qWarning() << "[STREAM][DLL] selected sherpa_runtime_dir=" << runtimeDir;
    if (!prepareRuntimeDlls(runtimeDir, &reason))
    {
        if (errorMessage)
        {
            *errorMessage = reason;
        }
        PerfTracer::markTrace("STREAM", m_activeTraceId, "streaming_dll_prepare_failed", reason);
        return false;
    }

    qWarning() << "[STREAM] backend init start dll=" << dllPath;
    m_impl->library.setFileName(dllPath);
    if (!m_impl->library.load())
    {
        reason = "加载 sherpa-onnx-c-api.dll 失败：" + m_impl->library.errorString();
        if (errorMessage)
        {
            *errorMessage = reason;
        }
        PerfTracer::markTrace("STREAM", m_activeTraceId, "streaming_backend_unavailable", reason);
        return false;
    }

    auto resolve = [this](auto &fn, const char *name) -> bool
    {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(m_impl->library.resolve(name));
        return fn != nullptr;
    };

    if (!resolve(m_impl->createRecognizer, "SherpaOnnxCreateOnlineRecognizer") ||
        !resolve(m_impl->destroyRecognizer, "SherpaOnnxDestroyOnlineRecognizer") ||
        !resolve(m_impl->createStream, "SherpaOnnxCreateOnlineStream") ||
        !resolve(m_impl->destroyStream, "SherpaOnnxDestroyOnlineStream") ||
        !resolve(m_impl->acceptWaveform, "SherpaOnnxOnlineStreamAcceptWaveform") ||
        !resolve(m_impl->isReady, "SherpaOnnxIsOnlineStreamReady") ||
        !resolve(m_impl->decode, "SherpaOnnxDecodeOnlineStream") ||
        !resolve(m_impl->getResult, "SherpaOnnxGetOnlineStreamResult") ||
        !resolve(m_impl->destroyResult, "SherpaOnnxDestroyOnlineRecognizerResult") ||
        !resolve(m_impl->inputFinished, "SherpaOnnxOnlineStreamInputFinished") ||
        !resolve(m_impl->setOption, "SherpaOnnxOnlineStreamSetOption"))
    {
        reason = "sherpa-onnx-c-api.dll 缺少 online streaming 必要符号";
        if (errorMessage)
        {
            *errorMessage = reason;
        }
        PerfTracer::markTrace("STREAM", m_activeTraceId, "streaming_backend_unavailable", reason);
        m_impl->library.unload();
        return false;
    }

    SherpaOnnxOnlineRecognizerConfig config;
    std::memset(&config, 0, sizeof(config));
    config.feat_config.sample_rate = 16000;
    config.feat_config.feature_dim = 80;
    config.model_config.provider = "cpu";
    config.model_config.num_threads = 2;
    config.decoding_method = "greedy_search";
    config.max_active_paths = 4;
    config.enable_endpoint = 0;

    m_encoderPathUtf8 = modelFiles.encoderPath.toLocal8Bit();
    m_decoderPathUtf8 = modelFiles.decoderPath.toLocal8Bit();
    m_joinerPathUtf8 = modelFiles.joinerPath.toLocal8Bit();
    m_ctcModelPathUtf8 = modelFiles.ctcModelPath.toLocal8Bit();
    m_tokensPathUtf8 = modelFiles.tokensPath.toLocal8Bit();
    config.model_config.tokens = m_tokensPathUtf8.constData();
    if (modelFiles.kind == ModelKind::Paraformer)
    {
        config.model_config.paraformer.encoder = m_encoderPathUtf8.constData();
        config.model_config.paraformer.decoder = m_decoderPathUtf8.constData();
        config.model_config.model_type = "paraformer";
    }
    else if (modelFiles.kind == ModelKind::Transducer)
    {
        config.model_config.transducer.encoder = m_encoderPathUtf8.constData();
        config.model_config.transducer.decoder = m_decoderPathUtf8.constData();
        config.model_config.transducer.joiner = m_joinerPathUtf8.constData();
    }
    else if (modelFiles.kind == ModelKind::Ctc)
    {
        config.model_config.zipformer2_ctc.model = m_ctcModelPathUtf8.constData();
    }

    qWarning() << "[STREAM] build config start"
               << "type=" << modelKindName(modelFiles.kind)
               << "sample_rate=16000"
               << "feature_dim=80";
    PerfTracer::markTrace("STREAM", m_activeTraceId, "streaming_build_config_start",
                          QString("detected_model_type=%1").arg(modelKindName(modelFiles.kind)));

    QElapsedTimer initTimer;
    initTimer.start();
    qWarning() << "[STREAM] create recognizer start";
    m_impl->recognizer = m_impl->createRecognizer(&config);
    const qint64 initMs = initTimer.elapsed();
    if (!m_impl->recognizer)
    {
        reason = "创建 sherpa-onnx online recognizer 失败，请检查 streaming 模型是否匹配当前 SDK";
        if (errorMessage)
        {
            *errorMessage = reason;
        }
        PerfTracer::markTrace("STREAM", m_activeTraceId, "streaming_backend_unavailable", reason);
        qWarning() << "[STREAM] create recognizer fail:" << reason;
        return false;
    }

    qWarning() << "[STREAM] recognizer created successfully";
    m_initialized = true;
    PerfTracer::markTrace("STREAM", m_activeTraceId, "streaming_backend_available",
                          QString("backend=streaming, model_dir=%1").arg(QFileInfo(modelFiles.modelDir).fileName()));
    PerfTracer::markDurationTrace("STREAM", m_activeTraceId, "streaming_model_init_done", initMs);
    return true;
#endif
}

bool SherpaOnnxStreamingAsrEngine::runSmokeTest(QString *errorMessage)
{
#ifndef VOICEFLOW_SHERPA_STREAMING_HEADERS
    if (errorMessage)
    {
        *errorMessage = "当前 sherpa-onnx 开发包未暴露 online streaming C/C++ API，无法启用 streaming。";
    }
    return false;
#else
    if (!m_initialized || !m_impl || !m_impl->recognizer)
    {
        if (errorMessage)
        {
            *errorMessage = "streaming recognizer 尚未初始化";
        }
        return false;
    }
    if (!m_impl->createStream || !m_impl->destroyStream ||
        !m_impl->acceptWaveform || !m_impl->isReady ||
        !m_impl->decode || !m_impl->getResult || !m_impl->destroyResult)
    {
        if (errorMessage)
        {
            *errorMessage = "streaming C API 必要函数未解析完整";
        }
        return false;
    }

    qWarning() << "[STREAM] smoke test start";
    PerfTracer::markTrace("STREAM", m_activeTraceId, "streaming_smoke_test_start");

    qWarning() << "[STREAM] create stream start";
    const SherpaOnnxOnlineStream *testStream = m_impl->createStream(m_impl->recognizer);
    if (!testStream)
    {
        if (errorMessage)
        {
            *errorMessage = "smoke test 创建 online stream 失败";
        }
        qWarning() << "[STREAM] create stream fail";
        return false;
    }
    qWarning() << "[STREAM] create stream success";

    QVector<float> silence(16000 / 2);
    std::fill(silence.begin(), silence.end(), 0.0f);
    m_impl->acceptWaveform(testStream, 16000, silence.constData(), silence.size());

    int decodeSteps = 0;
    while (m_impl->isReady(m_impl->recognizer, testStream) && decodeSteps < 100)
    {
        m_impl->decode(m_impl->recognizer, testStream);
        ++decodeSteps;
    }

    const SherpaOnnxOnlineRecognizerResult *result =
        m_impl->getResult(m_impl->recognizer, testStream);
    if (!result)
    {
        m_impl->destroyStream(testStream);
        if (errorMessage)
        {
            *errorMessage = "smoke test 获取 result 失败";
        }
        qWarning() << "[STREAM] smoke test fail: null result";
        return false;
    }

    const int textLen = result->text ? QString::fromUtf8(result->text).size() : 0;
    m_impl->destroyResult(result);
    m_impl->destroyStream(testStream);
    qWarning() << "[STREAM] smoke test passed text_len=" << textLen
               << "decode_steps=" << decodeSteps;
    PerfTracer::markTrace("STREAM", m_activeTraceId, "streaming_smoke_test_passed",
                          QString("text_len=%1, decode_steps=%2").arg(textLen).arg(decodeSteps));
    return true;
#endif
}

void SherpaOnnxStreamingAsrEngine::startSession()
{
    QElapsedTimer timer;
    timer.start();
    if (!m_initialized)
    {
        emit streamingError("streaming recognizer is not initialized; fallback offline");
        return;
    }

#ifdef VOICEFLOW_SHERPA_STREAMING_HEADERS
    if (!m_impl || !m_impl->recognizer || !m_impl->createStream || !m_impl->destroyStream)
    {
        emit streamingError("streaming 运行时对象未初始化，已回退离线识别");
        return;
    }
    if (m_impl->stream)
    {
        m_impl->destroyStream(m_impl->stream);
        m_impl->stream = nullptr;
    }
    qWarning() << "[STREAM][START] create stream start";
    m_impl->stream = m_impl->createStream(m_impl->recognizer);
    if (!m_impl->stream)
    {
        emit streamingError("创建 sherpa-onnx online stream 失败");
        return;
    }
#endif

    m_sessionActive = true;
    m_sessionStartedAtMs = PerfTracer::nowMs();
    m_lastFrameLogMs = -1;
    m_lastPartialText.clear();
    PerfTracer::startTrace("STREAM", m_activeTraceId, "streaming_session_start");
    qWarning() << "[STREAM][START] create stream done elapsed=" << timer.elapsed() << "ms";
    if (timer.elapsed() > 500)
    {
        qWarning() << "[STREAM][WARN] restart took" << timer.elapsed() << "ms";
    }
}

void SherpaOnnxStreamingAsrEngine::acceptAudioFrame(const QByteArray &pcmData,
                                                    int sampleRate,
                                                    int channels,
                                                    int bitsPerSample,
                                                    qint64 frameStartMs)
{
    if (!m_sessionActive || pcmData.isEmpty())
    {
        return;
    }
    if (sampleRate <= 0 || channels <= 0 || bitsPerSample != 16)
    {
        const QString reason = QString("音频帧格式不支持：sampleRate=%1, channels=%2, bits=%3")
                                   .arg(sampleRate)
                                   .arg(channels)
                                   .arg(bitsPerSample);
        qWarning() << "[STREAM]" << reason;
        PerfTracer::markTrace("STREAM", m_activeTraceId, "streaming_audio_frame_rejected", reason);
        emit streamingError(reason);
        return;
    }

    const QVector<float> samples = pcm16ToFloat32Mono(pcmData, channels, bitsPerSample);
    if (samples.isEmpty())
    {
        return;
    }
    QVector<float> outputSamples = samples;
    int outputSampleRate = sampleRate;
    if (sampleRate != 16000)
    {
        const int outputCount = qMax(1, int(qRound(double(samples.size()) * 16000.0 / double(sampleRate))));
        outputSamples.resize(outputCount);
        for (int i = 0; i < outputCount; ++i)
        {
            const double srcPos = double(i) * double(sampleRate) / 16000.0;
            const int left = qBound(0, int(srcPos), samples.size() - 1);
            const int right = qMin(left + 1, samples.size() - 1);
            const double frac = srcPos - double(left);
            outputSamples[i] = float(samples[left] * (1.0 - frac) + samples[right] * frac);
        }
        outputSampleRate = 16000;
    }

#ifdef VOICEFLOW_SHERPA_STREAMING_HEADERS
    if (!m_impl || !m_impl->recognizer || !m_impl->stream ||
        !m_impl->acceptWaveform || !m_impl->isReady || !m_impl->decode)
    {
        return;
    }
    m_impl->acceptWaveform(m_impl->stream, outputSampleRate, outputSamples.constData(), outputSamples.size());
    while (m_impl->isReady(m_impl->recognizer, m_impl->stream))
    {
        m_impl->decode(m_impl->recognizer, m_impl->stream);
    }

    const QString text = currentResultText();
    if (!text.isEmpty() && text != m_lastPartialText)
    {
        m_lastPartialText = text;
        const qint64 latency = m_sessionStartedAtMs >= 0 ? PerfTracer::nowMs() - m_sessionStartedAtMs : 0;
        PerfTracer::markTrace("STREAM", m_activeTraceId, "streaming_partial_result",
                              QString("text_len=%1, latency=%2 ms").arg(text.size()).arg(latency));
        emit partialResultReady(text);
    }
#endif

    const qint64 now = PerfTracer::nowMs();
    if (m_lastFrameLogMs < 0 || now - m_lastFrameLogMs >= kFrameLogIntervalMs)
    {
        const int bytesPerSample = qMax(1, bitsPerSample / 8);
        const int safeChannels = qMax(1, channels);
        const qint64 frameMs = sampleRate > 0
                                   ? (pcmData.size() / safeChannels / bytesPerSample) * 1000LL / sampleRate
                                   : 0;
        PerfTracer::markTrace("STREAM", m_activeTraceId, "streaming_accept_frame",
                              QString("frame_ms=%1, in_sr=%2, out_sr=%3, channels=%4, samples=%5, frameStartMs=%6")
                                  .arg(frameMs)
                                  .arg(sampleRate)
                                  .arg(outputSampleRate)
                                  .arg(channels)
                                  .arg(outputSamples.size())
                                  .arg(frameStartMs));
        qWarning() << "[STREAM] accept frame:"
                   << "in_sr=" << sampleRate
                   << "out_sr=" << outputSampleRate
                   << "samples=" << outputSamples.size();
        m_lastFrameLogMs = now;
    }
}

void SherpaOnnxStreamingAsrEngine::finishSession()
{
    if (!m_sessionActive)
    {
        return;
    }

#ifdef VOICEFLOW_SHERPA_STREAMING_HEADERS
    if (m_impl && m_impl->stream && m_impl->recognizer &&
        m_impl->inputFinished && m_impl->isReady && m_impl->decode)
    {
        if (m_impl->setOption)
        {
            m_impl->setOption(m_impl->stream, "is_final", "1");
        }
        m_impl->inputFinished(m_impl->stream);
        while (m_impl->isReady(m_impl->recognizer, m_impl->stream))
        {
            m_impl->decode(m_impl->recognizer, m_impl->stream);
        }
    }
#endif

    const QString finalText = currentResultText();
    const qint64 elapsed = m_sessionStartedAtMs >= 0 ? PerfTracer::nowMs() - m_sessionStartedAtMs : 0;
    PerfTracer::markTrace("STREAM", m_activeTraceId, "streaming_final_result",
                          QString("text_len=%1, streaming_total_latency=%2 ms").arg(finalText.size()).arg(elapsed));
    if (!finalText.trimmed().isEmpty())
    {
        AsrResult result;
        result.traceId = m_activeTraceId;
        result.text = finalText;
        result.modelName = "sherpa-onnx online streaming";
        result.elapsedMs = elapsed;
        result.success = true;
        emit finalResultReady(result);
    }
    m_sessionActive = false;
}

void SherpaOnnxStreamingAsrEngine::resetSession()
{
    if (m_sessionActive)
    {
        finishSession();
    }
    PerfTracer::markTrace("STREAM", m_activeTraceId, "streaming_reset_session");
#ifdef VOICEFLOW_SHERPA_STREAMING_HEADERS
    if (m_impl && m_impl->stream && m_impl->destroyStream)
    {
        m_impl->destroyStream(m_impl->stream);
        m_impl->stream = nullptr;
    }
#endif
    m_sessionStartedAtMs = -1;
    m_lastFrameLogMs = -1;
    m_lastPartialText.clear();
}

void SherpaOnnxStreamingAsrEngine::discardSession()
{
    QElapsedTimer timer;
    timer.start();
    PerfTracer::markTrace("STREAM", m_activeTraceId, "streaming_discard_session");
    m_sessionActive = false;
#ifdef VOICEFLOW_SHERPA_STREAMING_HEADERS
    if (m_impl && m_impl->stream && m_impl->destroyStream)
    {
        m_impl->destroyStream(m_impl->stream);
        m_impl->stream = nullptr;
    }
#endif
    m_sessionStartedAtMs = -1;
    m_lastFrameLogMs = -1;
    m_lastPartialText.clear();
    qWarning() << "[STREAM][LIFE] discard stream done elapsed=" << timer.elapsed() << "ms";
}

bool SherpaOnnxStreamingAsrEngine::isSessionActive() const
{
    return m_sessionActive;
}

bool SherpaOnnxStreamingAsrEngine::isInitialized() const
{
    return m_initialized;
}

bool SherpaOnnxStreamingAsrEngine::findStreamingSdk(QString *reason) const
{
    if (!findSherpaRuntimeDir().isEmpty())
    {
        return true;
    }

    if (reason)
    {
        *reason = "未找到可用的 sherpa-onnx-c-api.dll";
    }
    return false;
}

QString SherpaOnnxStreamingAsrEngine::findStreamingDll() const
{
    const QString runtimeDir = findSherpaRuntimeDir();
    return runtimeDir.isEmpty() ? QString() : QDir(runtimeDir).filePath("sherpa-onnx-c-api.dll");
}

QString SherpaOnnxStreamingAsrEngine::findSherpaRuntimeDir(QString *reason) const
{
    for (const QString &baseDir : candidateBaseDirs())
    {
        QStringList candidateDirs;
        const QDir thirdPartyDir(baseDir + "/third_party");
        candidateDirs << thirdPartyDir.filePath("sherpa-onnx-runtime/bin");
        candidateDirs << thirdPartyDir.filePath("sherpa-onnx-runtime/lib");

        if (thirdPartyDir.exists())
        {
            const QFileInfoList candidates = thirdPartyDir.entryInfoList(
                QStringList() << "sherpa-onnx-v*" << "sherpa-onnx-*",
                QDir::Dirs | QDir::NoDotAndDotDot,
                QDir::Name);
            for (const QFileInfo &candidate : candidates)
            {
                const QDir root(candidate.absoluteFilePath());
                candidateDirs << root.filePath("bin");
                candidateDirs << root.filePath("lib");
            }
        }

        candidateDirs << QCoreApplication::applicationDirPath();
        const QStringList pathDirs = QString::fromLocal8Bit(qgetenv("PATH")).split(';', Qt::SkipEmptyParts);
        candidateDirs << pathDirs;

        QSet<QString> seen;
        for (const QString &candidateDir : candidateDirs)
        {
            const QDir dir(candidateDir);
            const QString absolute = dir.absolutePath();
            if (seen.contains(absolute))
            {
                continue;
            }
            seen.insert(absolute);

            const QString sherpaDll = dir.filePath("sherpa-onnx-c-api.dll");
            const QString ortDll = dir.filePath("onnxruntime.dll");
            const QString providersDll = dir.filePath("onnxruntime_providers_shared.dll");
            const QFileInfo sherpaInfo(sherpaDll);
            const QFileInfo ortInfo(ortDll);
            const QFileInfo providersInfo(providersDll);
            qWarning() << "[STREAM][DLL] candidate=" << absolute;
            qWarning() << "[STREAM][DLL] has sherpa-onnx-c-api.dll=" << sherpaInfo.exists()
                       << "size=" << (sherpaInfo.exists() ? sherpaInfo.size() : 0)
                       << "modified=" << (sherpaInfo.exists() ? sherpaInfo.lastModified().toString(Qt::ISODate) : QString());
            qWarning() << "[STREAM][DLL] has onnxruntime.dll=" << ortInfo.exists()
                       << "size=" << (ortInfo.exists() ? ortInfo.size() : 0)
                       << "modified=" << (ortInfo.exists() ? ortInfo.lastModified().toString(Qt::ISODate) : QString());
            qWarning() << "[STREAM][DLL] has onnxruntime_providers_shared.dll=" << providersInfo.exists()
                       << "size=" << (providersInfo.exists() ? providersInfo.size() : 0)
                       << "modified=" << (providersInfo.exists() ? providersInfo.lastModified().toString(Qt::ISODate) : QString());

            if (sherpaInfo.exists() && ortInfo.exists() && providersInfo.exists())
            {
                qWarning() << "[STREAM][DLL] selected sherpa_runtime_dir=" << absolute;
                PerfTracer::markTrace("STREAM", m_activeTraceId, "streaming_runtime_dir_selected",
                                      QString("sherpa_runtime_dir=%1").arg(absolute));
                return absolute;
            }
        }
    }

    if (reason)
    {
        *reason = "未找到同一目录下完整的 sherpa-onnx-c-api.dll、onnxruntime.dll、onnxruntime_providers_shared.dll";
    }
    return QString();
}

bool SherpaOnnxStreamingAsrEngine::findStreamingModel(ModelFiles *files, QString *reason) const
{
    for (const QString &baseDir : candidateBaseDirs())
    {
        const QDir rootDir(baseDir + "/models/sherpa-onnx");
        if (!rootDir.exists())
        {
            continue;
        }

        QStringList candidateDirs;
        candidateDirs << rootDir.filePath("streaming-zh");
        const QFileInfoList modelDirs = rootDir.entryInfoList(
            QStringList() << "sherpa-onnx-streaming*",
            QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &entry : modelDirs)
        {
            candidateDirs << entry.absoluteFilePath();
        }

        for (const QString &candidateDir : candidateDirs)
        {
            const QDir modelDir(candidateDir);
            if (!modelDir.exists())
            {
                continue;
            }

            ModelFiles found;
            found.modelDir = modelDir.absolutePath();
            found.tokensPath = modelDir.filePath("tokens.txt");
            found.encoderPath = firstMatch(modelDir.absolutePath(), QStringList() << "encoder*.onnx");
            found.decoderPath = firstMatch(modelDir.absolutePath(), QStringList() << "decoder*.onnx");
            found.joinerPath = firstMatch(modelDir.absolutePath(), QStringList() << "joiner*.onnx");
            found.ctcModelPath = firstMatch(modelDir.absolutePath(), QStringList() << "model*.onnx");

            const bool hasTokens = QFileInfo::exists(found.tokensPath);
            const bool hasEncoder = !found.encoderPath.isEmpty();
            const bool hasDecoder = !found.decoderPath.isEmpty();
            qWarning() << "[STREAM] model_dir=" << modelDir.absolutePath();
            qWarning() << "[STREAM] encoder exists=" << hasEncoder
                       << "decoder exists=" << hasDecoder
                       << "tokens exists=" << hasTokens;
            PerfTracer::markTrace("STREAM", "realtime", "streaming_model_check",
                                  QString("model_dir=%1, encoder=%2, decoder=%3, tokens=%4")
                                      .arg(modelDir.absolutePath())
                                      .arg(hasEncoder ? "true" : "false")
                                      .arg(hasDecoder ? "true" : "false")
                                      .arg(hasTokens ? "true" : "false"));
            const bool hasParaformer = !found.encoderPath.isEmpty() &&
                                       !found.decoderPath.isEmpty() &&
                                       found.joinerPath.isEmpty();
            const bool hasTransducer = !found.encoderPath.isEmpty() &&
                                       !found.decoderPath.isEmpty() &&
                                       !found.joinerPath.isEmpty();
            const bool hasCtc = !found.ctcModelPath.isEmpty();

            if (hasTokens && (hasParaformer || hasTransducer || hasCtc))
            {
                if (hasParaformer)
                {
                    found.kind = ModelKind::Paraformer;
                }
                else if (hasTransducer)
                {
                    found.kind = ModelKind::Transducer;
                }
                else
                {
                    found.kind = ModelKind::Ctc;
                }
                if (files)
                {
                    *files = found;
                }
                return true;
            }

            QStringList missing;
            if (!hasTokens)
            {
                missing << "tokens.txt";
            }
            if (!hasEncoder && !hasCtc)
            {
                missing << "encoder*.onnx";
            }
            if (!hasDecoder && !hasCtc)
            {
                missing << "decoder*.onnx";
            }
            if (!missing.isEmpty())
            {
                qWarning() << "[STREAM] incomplete model files missing=" << missing.join(", ");
            }
        }
    }

    if (reason)
    {
        *reason = "未找到完整 sherpa-onnx streaming 模型，至少需要 encoder*.onnx、decoder*.onnx、tokens.txt";
    }
    return false;
}

QStringList SherpaOnnxStreamingAsrEngine::candidateBaseDirs() const
{
    QStringList dirs;
    dirs << QDir::currentPath();
    dirs << QCoreApplication::applicationDirPath();

    QDir appDir(QCoreApplication::applicationDirPath());
    dirs << appDir.filePath("..");
    dirs << appDir.filePath("../..");

    QStringList normalized;
    QSet<QString> seen;
    for (const QString &dir : dirs)
    {
        const QString clean = QDir(dir).absolutePath();
        if (!seen.contains(clean))
        {
            seen.insert(clean);
            normalized << clean;
        }
    }
    return normalized;
}

QString SherpaOnnxStreamingAsrEngine::firstMatch(const QString &dirPath, const QStringList &patterns) const
{
    const QDir dir(dirPath);
    const QFileInfoList entries = dir.entryInfoList(patterns, QDir::Files, QDir::Name);
    return entries.isEmpty() ? QString() : entries.first().absoluteFilePath();
}

QVector<float> SherpaOnnxStreamingAsrEngine::pcm16ToFloat32Mono(const QByteArray &pcmData,
                                                                int channels,
                                                                int bitsPerSample) const
{
    if (bitsPerSample != 16 || pcmData.size() < int(sizeof(qint16)))
    {
        return {};
    }

    const int safeChannels = qMax(1, channels);
    const int totalSamples = pcmData.size() / int(sizeof(qint16));
    const int frameCount = totalSamples / safeChannels;
    QVector<float> samples;
    samples.reserve(frameCount);

    const auto *input = reinterpret_cast<const qint16 *>(pcmData.constData());
    for (int frame = 0; frame < frameCount; ++frame)
    {
        qint64 sum = 0;
        for (int channel = 0; channel < safeChannels; ++channel)
        {
            sum += input[frame * safeChannels + channel];
        }
        samples.append(static_cast<float>(sum / safeChannels) / 32768.0f);
    }

    return samples;
}

QString SherpaOnnxStreamingAsrEngine::currentResultText() const
{
#ifdef VOICEFLOW_SHERPA_STREAMING_HEADERS
    if (!m_impl || !m_impl->recognizer || !m_impl->stream || !m_impl->getResult)
    {
        return QString();
    }

    const SherpaOnnxOnlineRecognizerResult *result =
        m_impl->getResult(m_impl->recognizer, m_impl->stream);
    if (!result)
    {
        return QString();
    }

    const QString text = result->text ? QString::fromUtf8(result->text) : QString();
    m_impl->destroyResult(result);
    return text;
#else
    return QString();
#endif
}

bool SherpaOnnxStreamingAsrEngine::prepareRuntimeDlls(const QString &runtimeDir, QString *errorMessage)
{
#ifndef Q_OS_WIN
    Q_UNUSED(runtimeDir);
    Q_UNUSED(errorMessage);
    return true;
#else
    const QDir dir(runtimeDir);
    const QString ortPath = dir.filePath("onnxruntime.dll");
    const QString providersPath = dir.filePath("onnxruntime_providers_shared.dll");
    const QString sherpaPath = dir.filePath("sherpa-onnx-c-api.dll");

    const std::wstring runtimeDirW = QDir::toNativeSeparators(dir.absolutePath()).toStdWString();
    const BOOL setDllOk = SetDllDirectoryW(runtimeDirW.c_str());
    qWarning() << "[STREAM][DLL] SetDllDirectory=" << dir.absolutePath()
               << "success=" << (setDllOk != FALSE);
    PerfTracer::markTrace("STREAM", m_activeTraceId, "streaming_set_dll_directory",
                          QString("dir=%1, success=%2")
                              .arg(dir.absolutePath())
                              .arg(setDllOk != FALSE ? "true" : "false"));
    if (!setDllOk)
    {
        if (errorMessage)
        {
            *errorMessage = "SetDllDirectoryW 失败，无法固定 sherpa runtime DLL 目录";
        }
        return false;
    }

    auto loadOne = [&](const QString &path, const QString &moduleName, HMODULE *outHandle) -> bool
    {
        const std::wstring pathW = QDir::toNativeSeparators(path).toStdWString();
        HMODULE handle = LoadLibraryW(pathW.c_str());
        if (!handle)
        {
            if (errorMessage)
            {
                *errorMessage = QString("LoadLibraryW 失败：%1, error=%2")
                                    .arg(path)
                                    .arg(GetLastError());
            }
            return false;
        }
        if (outHandle)
        {
            *outHandle = handle;
        }
        const QString loadedPath = loadedModulePath(moduleName);
        qWarning() << "[STREAM][DLL] loaded" << moduleName << "=" << loadedPath;
        PerfTracer::markTrace("STREAM", m_activeTraceId, "streaming_dll_loaded",
                              QString("%1=%2").arg(moduleName, loadedPath));
        if (!QDir::toNativeSeparators(loadedPath).startsWith(QDir::toNativeSeparators(dir.absolutePath()), Qt::CaseInsensitive))
        {
            if (errorMessage)
            {
                *errorMessage = QString("实际加载的 %1 不在选定 sherpa runtime 目录：%2").arg(moduleName, loadedPath);
            }
            return false;
        }
        return true;
    };

    if (!loadOne(ortPath, "onnxruntime.dll", &m_impl->onnxruntimeHandle))
    {
        return false;
    }
    if (!loadOne(providersPath, "onnxruntime_providers_shared.dll", &m_impl->providersHandle))
    {
        return false;
    }
    if (!loadOne(sherpaPath, "sherpa-onnx-c-api.dll", &m_impl->sherpaHandle))
    {
        return false;
    }

    return true;
#endif
}

QString SherpaOnnxStreamingAsrEngine::loadedModulePath(const QString &moduleName) const
{
#ifndef Q_OS_WIN
    Q_UNUSED(moduleName);
    return QString();
#else
    const std::wstring moduleNameW = moduleName.toStdWString();
    HMODULE module = GetModuleHandleW(moduleNameW.c_str());
    if (!module)
    {
        return QString();
    }

    wchar_t buffer[MAX_PATH] = {0};
    const DWORD length = GetModuleFileNameW(module, buffer, MAX_PATH);
    if (length == 0)
    {
        return QString();
    }
    return QDir::fromNativeSeparators(QString::fromWCharArray(buffer, int(length)));
#endif
}
