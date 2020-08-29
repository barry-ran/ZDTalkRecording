/******************************************************************************
    Copyright (C) 2020 by Zaodao(Dalian) Education Technology Co., Ltd..

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "zdobscontext.h"
#include "zdrecordingdefine.h"
#include "platform.h"

#ifdef _WIN32
#define IS_WIN32 1
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <Dwmapi.h>
#include <psapi.h>
#include <util/windows/win-version.h>
#else
#define IS_WIN32 0
#endif

#include <string.h>
#include <iostream>
#include <string>
#include <mutex>
#include <fstream>

// OBS
#include <util/util.hpp>
#include <util/platform.h>
#include <libavcodec/avcodec.h>

// Qt
#include <QCoreApplication>
#include <QFileInfo>
#include <QSize>
#include <QDir>
#include <QDebug>

using namespace std;

#define ZDTALK_TAG                               "ZDTalk"
#define ZDTALK_OBS_DEFAULT_LOCALE                "en-US"

#define ZDTALK_AUDIO_SAMPLES_PER_SECOND          44100
#define ZDTALK_AUDIO_SPEAKERS		             SPEAKERS_STEREO
#define ZDTALK_AUDIO_BITRATE                     128
#define ZDTALK_AUDIO_MONITOR_ID                  "default"

#define ZDTALK_AUDIO_ENCODER_ID                  "ffmpeg_aac"
#define ZDTALK_AUDIO_ENCODER_NAME                "aac"
#define ZDTALK_AUDIO_OUTPUT_SOURCE_ID            "wasapi_output_capture"
#define ZDTALK_AUDIO_INPUT_SOURCE_ID             "wasapi_input_capture"
#define ZDTALK_AUDIO_OUTPUT_DEVICE_ID            "default"
#define ZDTALK_AUDIO_INPUT_DEVICE_ID             "default"
#define ZDTALK_AUDIO_OUTPUT_INDEX                1
#define ZDTALK_AUDIO_INPUT_INDEX                 3
#define ZDTALK_AUDIO_INPUT_FILTER_NOISE_SUPPRESS "noise_suppress_filter"

#define ZDTALK_VIDEO_FPS                         15
#define ZDTALK_VIDEO_FORMAT                      VIDEO_FORMAT_I420
#define ZDTALK_VIDEO_COLORSPACE                  VIDEO_CS_601
#define ZDTALK_VIDEO_RANGE                       VIDEO_RANGE_PARTIAL
#define ZDTALK_VIDEO_SCALE_TYPE                  OBS_SCALE_BICUBIC
#define ZDTALK_VIDEO_BITRATE                     150
#define ZDTALK_VIDEO_CAPTURE_SOURCE_ID           "window_capture"
#define ZDTALK_VIDEO_OUTPUT_INDEX                0

#define ZDTALK_VIDEO_CROP_FILTER_ID              "crop_filter"
#define ZDTALK_TRANSITION_ID                     "fade_transition"

#define ZDTALK_RECORDING_OUTPUT_ID               "ffmpeg_output"
#define ZDTALK_STREAMING_OUTPUT_ID               "rtmp_output"

/*
static const string encoders[] = {
    "ffmpeg_aac",
    "mf_aac",
    "libfdk_aac",
    "CoreAudio_AAC",
};
*/

static const double scaled_vals[] =
{
    1.0,
    1.25,
    (1.0 / 0.75),
    1.5,
    (1.0 / 0.6),
    1.75,
    2.0,
    2.25,
    2.5,
    2.75,
    3.0,
    0.0
};

static log_handler_t DefLogHandler;

static DisplayRatio CalcDisplayRatio(const int &width, const int &height)
{
    if (height > width || height <= 0 || width <= 0)
        return DisplayRatio_Unhandle;

    float ratio = height * 1.f / width;

    for (int i = 0; i < DisplayRatio_Count; ++i)
    {
        if (fRatioEqual(ratio, display_ratios[i]))
            return static_cast<DisplayRatio>(i);
    }

    return DisplayRatio_Unhandle;
}

#ifdef _WIN32
static void SetAeroEnabled(bool enable)
{
    static HRESULT(WINAPI *func)(UINT) = nullptr;
    static bool failed = false;

    if (!func) {
        if (failed)
            return;

        HMODULE dwm = LoadLibraryW(L"dwmapi");
        if (!dwm) {
            failed = true;
            return;
        }

        func = reinterpret_cast<decltype(func)>(GetProcAddress(dwm,
                        "DwmEnableComposition"));
        if (!func) {
            failed = true;
            return;
        }
    }

    func(enable ? DWM_EC_ENABLECOMPOSITION : DWM_EC_DISABLECOMPOSITION);
}

static uint32_t GetWindowsVersion()
{
    static uint32_t ver = 0;

    if (ver == 0) {
        struct win_version_info ver_info;

        get_win_ver(&ver_info);
        ver = (ver_info.major << 8) | ver_info.minor;
    }

    return ver;
}
#endif

static void LogHandler(int level, const char *format, va_list args, void *param)
{
    UNUSED_PARAMETER(level);
    UNUSED_PARAMETER(param);

    char str[4096];

#ifndef _WIN32
    va_list args2;
    va_copy(args2, args);
#endif

    vsnprintf_s(str, sizeof(str), format, args);

    qInfo() << str;
}

static void AddFilterToAudioInput(const char *id)
{
    if (!id || *id == '\0')
        return;

    obs_source_t *source = obs_get_output_source(ZDTALK_AUDIO_INPUT_INDEX);
    if (!source)
        return;

    obs_source_t *existing_filter;
    string name = obs_source_get_display_name(id);
    if (name.empty())
        name = id;
    existing_filter = obs_source_get_filter_by_name(source, name.c_str());
    if (existing_filter) {
        obs_source_release(existing_filter);
        obs_source_release(source);
        return;
    }

    obs_source_t *filter = obs_source_create(id, name.c_str(), nullptr, nullptr);
    if (filter) {
        const char *sourceName = obs_source_get_name(source);

        blog(LOG_INFO, "added filter '%s' (%s) to source '%s'", name.c_str(),
             id, sourceName);

        obs_source_filter_add(source, filter);
        obs_source_release(filter);
    }

    obs_source_release(source);
}

static inline bool HasAudioDevices(const char *source_id)
{
    const char *output_id = source_id;
    obs_properties_t *props = obs_get_source_properties(output_id);
    size_t count = 0;

    if (!props) {
        return false;
    }

    obs_property_t *devices = obs_properties_get(props, "device_id");
    if (devices) {
        count = obs_property_list_item_count(devices);
    }

    obs_properties_destroy(props);

    return count != 0;
}

static void ResetAudioDevice(const char *sourceId, const char *deviceId,
                             const char *deviceDesc, int channel)
{
    bool disable = deviceId && strcmp(deviceId, "disabled") == 0;
    obs_source_t *source;
    obs_data_t *settings;

    source = obs_get_output_source(channel);
    if (source) {
        if (disable) {
            obs_set_output_source(channel, nullptr);
        } else {
            settings = obs_source_get_settings(source);
            const char *oldId = obs_data_get_string(settings,
                                                    "device_id");
            if (strcmp(oldId, deviceId) != 0) {
                obs_data_set_string(settings, "device_id",
                                    deviceId);
            }
            obs_data_set_bool(settings, "use_device_timing", false);
            obs_source_update(source, settings);
            obs_data_release(settings);
        }

    } else if (!disable) {
        settings = obs_data_create();
        obs_data_set_string(settings, "device_id", deviceId);
        obs_data_set_bool(settings, "use_device_timing", false);
        source = obs_source_create(sourceId, deviceDesc, settings, nullptr);
        obs_data_release(settings);

        obs_set_output_source(channel, source);
    }

    obs_source_release(source);
}

static bool CreateAACEncoder(OBSEncoder &res, string &id, const char *name,
                             size_t idx, obs_data_t *setting)
{
    const char *id_ = ZDTALK_AUDIO_ENCODER_ID;
    id = id_;
    res = obs_audio_encoder_create(id_, name, setting, idx, nullptr);

    if (res) {
        obs_encoder_release(res);
        return true;
    }

    return false;
}

static void AddSource(void *_data, obs_scene_t *scene)
{
    obs_source_t *source = (obs_source_t *)_data;
    obs_scene_add(scene, source);
    obs_source_release(source);
}

static bool FindSceneItemAndScale(obs_scene_t *scene, obs_sceneitem_t *item,
                                  void *param)
{
    UNUSED_PARAMETER(scene);

    ZDTalkOBSContext *obs = static_cast<ZDTalkOBSContext *>(param);
    QSize baseSize = obs->getBaseSize(), orgSize = obs->getOriginalSize();

    float width = baseSize.width();
    float height = baseSize.height();
    float sratio = height / orgSize.height();
    bool align_width = false;
    float base_ratio = baseSize.width() * 1.f / baseSize.height();
    float orginal_ratio = orgSize.width() * 1.f / orgSize.height();
    if (base_ratio >= orginal_ratio)
        width = height * orginal_ratio;
    else {
        height = width / orginal_ratio;
        sratio = width / orgSize.width();
        align_width = true;
    }

    vec2 scale;
    scale.x = scale.y = sratio;
    obs_sceneitem_set_scale(item, &scale);

    vec2 pos;
    if (align_width)
        vec2_set(&pos, 0, (baseSize.height() - height) / 2);
    else
        vec2_set(&pos, (baseSize.width() - width) / 2, 0);
    obs_sceneitem_set_pos(item, &pos);

    blog(LOG_INFO, "scene scale %f %f", scale.x, scale.y);
    return true;
}

// --- Recording/Streaming Status Signal Callback ------------------------------------
#define RECORDING_STARTED \
    "==== Recording Started ==============================================="
#define RECORDING_STOPPING \
    "==== Recording Stopping ================================================"
#define RECORDING_STOPPED \
    "==== Recording Stopped ================================================"
#define STREAMING_STARTED \
    "==== Streaming Started ==============================================="
#define STREAMING_STOPPING \
    "==== Streaming Stopping ================================================"
#define STREAMING_STOPPED \
    "==== Streaming Stopped ================================================"
static void RecordingStarted(void *data, calldata_t *params)
{
    UNUSED_PARAMETER(params);
    blog(LOG_INFO, RECORDING_STARTED);
    ZDTalkOBSContext *handler = static_cast<ZDTalkOBSContext *>(data);
    QMetaObject::invokeMethod(handler, "recordingStarted");
}

static void RecordingStopping(void *data, calldata_t *params)
{
    UNUSED_PARAMETER(data);
    UNUSED_PARAMETER(params);
    blog(LOG_INFO, RECORDING_STOPPING);
}

static void RecordingStopped(void *data, calldata_t *params)
{
    blog(LOG_INFO, RECORDING_STOPPED);

    QString msg;
    int code = (int)calldata_int(params, "code");
    switch (code)
    {
    case OBS_OUTPUT_SUCCESS:
        blog(LOG_INFO, "recording finished!");
        UNUSED_PARAMETER(data);
        break;
    case OBS_OUTPUT_NO_SPACE:
        msg = "磁盘存储空间不足！";
        break;
    case OBS_OUTPUT_UNSUPPORTED:
        msg = "格式不支持！";
        break;
    default:
        {
            const char *last_error = calldata_string(params, "last_error");
            blog(LOG_ERROR, "record error, code=%d,error=%s", code, last_error);
        }
        msg = QString("发生未指定错误 (Code:%1)！").arg(code);
        break;
    }

    if (!msg.isEmpty()) {
        ZDTalkOBSContext *handler = static_cast<ZDTalkOBSContext *>(data);
        QMetaObject::invokeMethod(handler, "errorOccurred",
                                  Q_ARG(int, ErrorRecording),
                                  Q_ARG(QString, msg));
    } else {
        ZDTalkOBSContext *handler = static_cast<ZDTalkOBSContext *>(data);
        QMetaObject::invokeMethod(handler, "recordingStopped",
                                  Q_ARG(QString, handler->getRecordFilePath()));
    }
}

static void StreamingStarted(void *data, calldata_t *params)
{
    UNUSED_PARAMETER(params);
    blog(LOG_INFO, STREAMING_STARTED);
    ZDTalkOBSContext *handler = static_cast<ZDTalkOBSContext *>(data);
    QMetaObject::invokeMethod(handler, "streamingStarted");
}

static void StreamingStopping(void *data, calldata_t *params)
{
    UNUSED_PARAMETER(data);
    UNUSED_PARAMETER(params);
    blog(LOG_INFO, STREAMING_STOPPING);
}

static void StreamingStopped(void *data, calldata_t *params)
{
    blog(LOG_INFO, STREAMING_STOPPED);

    QString msg;
    int code = (int)calldata_int(params, "code");
    switch (code)
    {
    case OBS_OUTPUT_SUCCESS:
        blog(LOG_INFO, "streaming finished!");
        UNUSED_PARAMETER(data);
        break;
    case OBS_OUTPUT_BAD_PATH:
        msg = "无效的地址！";
        break;
    case OBS_OUTPUT_CONNECT_FAILED:
        msg = "无法连接到服务器！";
        break;
    case OBS_OUTPUT_INVALID_STREAM:
        msg = "无法访问流密钥或无法连接服务器！";
        break;
    case OBS_OUTPUT_ERROR:
        msg = "连接服务器时发生意外错误！";
        break;
    case OBS_OUTPUT_DISCONNECTED:
        msg = "已与服务器断开连接！";
        break;
    default:
        {
            const char *last_error = calldata_string(params, "last_error");
            blog(LOG_ERROR, "stream error, code=%d,error=%s", code, last_error);
        }
        msg = QString("发生未指定错误 (Code:%1)！").arg(code);
        break;
    }

    if (!msg.isEmpty()) {
        ZDTalkOBSContext *handler = static_cast<ZDTalkOBSContext *>(data);
        QMetaObject::invokeMethod(handler, "errorOccurred",
                                  Q_ARG(int, ErrorStreaming),
                                  Q_ARG(QString, msg));
    } else {
        ZDTalkOBSContext *handler = static_cast<ZDTalkOBSContext *>(data);
        QMetaObject::invokeMethod(handler, "streamingStopped");
    }
}

#define OBS_INIT_BEGIN \
    "==== OBS Init Begin ==============================================="
#define OBS_INIT_END \
    "==== OBS Init End ==============================================="
#define OBS_STARTUP_SEPARATOR \
    "==== OBS Start Complete ==============================================="
#define OBS_RELEASE_BEGIN_SEPARATOR \
    "==== OBS Release Begin ==============================================="
#define OBS_RELEASE_END_SEPARATOR \
    "==== OBS Release End ==============================================="
#define OBS_SEPARATOR "---------------------------------------------"

// --- ZDTalkOBSContext Member Functions ---------------------------------------
ZDTalkOBSContext::ZDTalkOBSContext(QObject *parent) : QObject(parent),
    filePath(nullptr),
    liveServer(nullptr),
    liveKey(nullptr),
    rtmpService(nullptr),
    recordOutput(nullptr),
    streamOutput(nullptr),
    h264Streaming(nullptr),
    scene(nullptr),
    transition(nullptr),
    captureSource(nullptr),
    properties(nullptr),
    recordWhenStreaming(false),
    outputType(OutputMP4)
{
#ifdef _WIN32
    DisableAudioDucking(true);
#endif
    for (size_t i = 0; i < MAX_AUDIO_MIXES; i++)
        aacTrack[i] = nullptr;
    base_get_log_handler(&DefLogHandler, nullptr);
    base_set_log_handler(LogHandler, nullptr);
}

ZDTalkOBSContext::~ZDTalkOBSContext()
{
#ifdef _WIN32
    DisableAudioDucking(false);
#endif

    if (obs_initialized())
        release();

    obs_shutdown();

    blog(LOG_INFO, "memory leaks: %ld", bnum_allocs());
    base_set_log_handler(nullptr, nullptr);
}

void ZDTalkOBSContext::release()
{
    blog(LOG_INFO, OBS_RELEASE_BEGIN_SEPARATOR);

    signalRecordingStarted.Disconnect();
    signalRecordingStopping.Disconnect();
    signalRecordingStopped.Disconnect();
    signalStreamingStarted.Disconnect();
    signalStreamingStopping.Disconnect();
    signalStreamingStopped.Disconnect();

    for (int i = 0; i < MAX_AUDIO_MIXES; ++i)
        obs_set_output_source(i, nullptr);

    auto cb = [] (void *unused, obs_source_t *source)
    {
        obs_source_remove(source);
        UNUSED_PARAMETER(unused);
        return true;
    };
    obs_enum_sources(cb, nullptr);

    obs_scene_release(scene);
    obs_properties_destroy(properties);

    scene        = nullptr;
    properties   = nullptr;

    rtmpService    = nullptr;
    h264Streaming  = nullptr;

    for (size_t i = 0; i < MAX_AUDIO_MIXES; ++i)
        aacTrack[i] = nullptr;

    streamOutput = nullptr;
    recordOutput = nullptr;

    free(filePath);
    free(liveServer);
    free(liveKey);

    blog(LOG_INFO, OBS_RELEASE_END_SEPARATOR);
}

void ZDTalkOBSContext::initialize(const QString &configPath,
                                  const QString &title,
                                  const QSize &screenSize,
                                  const QRect &sourceRegion)
{
    blog(LOG_INFO, OBS_INIT_BEGIN);

    if (configPath.isEmpty() || title.isEmpty() ||
            screenSize.isEmpty() || sourceRegion.isEmpty()) {
        blog(LOG_ERROR, "init param invalid, window title=%s, path=%s, "
                        "screen:%dx%d, orignal:(%d,%d %dx%d)",
             title.toStdString().c_str(),
             configPath.toStdString().c_str(),
             screenSize.width(), screenSize.height(),
             sourceRegion.left(), sourceRegion.top(),
             sourceRegion.width(), sourceRegion.height());
        emit errorOccurred(ErrorInitializing, QStringLiteral("参数错误"));
        return;
    }

    int screenWidth = screenSize.width();
    int screenHeight = screenSize.height();

    // 根据传入的屏幕分辨率计算最终输出的分辨率
    uint32_t out_cx = 0;
    uint32_t out_cy = 0;
    DisplayRatio ratio = CalcDisplayRatio(screenWidth, screenHeight);
    if (ratio != DisplayRatio_Unhandle) {
        for (int j = 0; j < DISPLAY_RES_COUNT_PER_RATIO; ++j)
        {
            QPair<int, int> res =
                    display_resolutions[ratio * DISPLAY_RES_COUNT_PER_RATIO + j];
            if (screenWidth >= res.first && screenHeight >= res.second) {
                out_cx = res.first;
                out_cy = res.second;
                break;
            }
        }
    }

    if (out_cy == 0 || out_cy == 0) {
        int i = 0;
        out_cx = screenWidth;
        out_cy = screenHeight;
        while (((out_cx * out_cy) > (1280 * 720)) && scaled_vals[i] > 0.0) {
            double scale = scaled_vals[i++];
            out_cx = uint32_t(double(screenWidth) / scale);
            out_cy = uint32_t(double(screenHeight) / scale);
        }

        out_cx = out_cx & ~3;
        out_cy = out_cy & ~3;
    }

    this->baseWidth    = screenWidth;
    this->baseHeight   = screenHeight;
    this->outputWidth  = out_cx;
    this->outputHeight = out_cy;
    this->orgWidth     = sourceRegion.width();
    this->orgHeight    = sourceRegion.height();
    blog(LOG_INFO, "final resolution => org=%dx%d, base=%dx%d, output=%dx%d",
         orgWidth, orgHeight, baseWidth, baseHeight, outputWidth, outputHeight);

    // 全局设置
    if (!obs_initialized()) {

        blog(LOG_INFO, "obs version %u", obs_get_version());

        if (!obs_startup(ZDTALK_OBS_DEFAULT_LOCALE,
                         configPath.toStdString().c_str(), nullptr)) {
            blog(LOG_ERROR, "startup fail");
            emit errorOccurred(ErrorInitializing, QStringLiteral("启动失败"));
            return;
        }

        // 加载模块
        QString appPath = QCoreApplication::applicationDirPath() + "/obs";
        obs_set_app_path(appPath.toStdString().c_str());
        QString pluginsPath = appPath + "/obs-plugins";
        QString modulePath = appPath + "/data/obs-plugins/%module%";
        obs_add_module_path(pluginsPath.toStdString().c_str(),
                            modulePath.toStdString().c_str());
        blog(LOG_INFO, OBS_SEPARATOR);
        obs_load_all_modules();
        blog(LOG_INFO, OBS_SEPARATOR);
        obs_log_loaded_modules();

        blog(LOG_INFO, OBS_STARTUP_SEPARATOR);
    }

    // 音视频配置
    if (!resetAudio()) {
        blog(LOG_ERROR, "reset audio fail");
        emit errorOccurred(ErrorInitializing, QStringLiteral("音频设置失败"));
        return;
    }

    // 视频配置
    int ret = resetVideo();
    if (ret != OBS_VIDEO_SUCCESS) {
        switch (ret) {
        case OBS_VIDEO_MODULE_NOT_FOUND:
            blog(LOG_ERROR, "failed to initialize video: graphics module not found");
            break;
        case OBS_VIDEO_NOT_SUPPORTED:
            blog(LOG_ERROR, "unsupported");
            break;
        case OBS_VIDEO_INVALID_PARAM:
            blog(LOG_ERROR, "failed to initialize video: invalid parameters");
            break;
        default:
            blog(LOG_ERROR, "unknown");
            break;
        }
        blog(LOG_ERROR, "reset video fail");
        emit errorOccurred(ErrorInitializing, QStringLiteral("视频设置失败"));
        return;
    }

    // 加载音频监视器
#if defined(_WIN32) || defined(__APPLE__)
    obs_set_audio_monitoring_device(ZDTALK_TAG "-audio-monitor-default",
                                    ZDTALK_AUDIO_MONITOR_ID);
    blog(LOG_INFO, "audio monitoring device:\tname: %s\tid: %s",
         ZDTALK_TAG "-AudioMonitorDefault",
         ZDTALK_AUDIO_MONITOR_ID);
#endif

    // 初始化推流服务
    if (!initService()) {
        emit errorOccurred(ErrorInitializing, QStringLiteral("初始化服务失败"));
        return;
    }
    // 初始化输出相关(推流/本地录制/编码器)
    if (!resetOutputs()) {
        emit errorOccurred(ErrorInitializing, QStringLiteral("初始化组件失败"));
        return;
    }

    // 设置录制/推流信号槽
    signalRecordingStarted.Connect(obs_output_get_signal_handler(recordOutput),
                          "start", RecordingStarted, this);
    signalRecordingStopping.Connect(obs_output_get_signal_handler(recordOutput),
                           "stopping", RecordingStopping, this);
    signalRecordingStopped.Connect(obs_output_get_signal_handler(recordOutput),
                          "stop", RecordingStopped, this);
    signalStreamingStarted.Connect(obs_output_get_signal_handler(streamOutput),
                          "start", StreamingStarted, this);
    signalStreamingStopping.Connect(obs_output_get_signal_handler(streamOutput),
                           "stopping", StreamingStopping, this);
    signalStreamingStopped.Connect(obs_output_get_signal_handler(streamOutput),
                          "stop", StreamingStopped, this);

#ifdef _WIN32
    uint32_t winVer = GetWindowsVersion();
    blog(LOG_INFO, "windows version %x", winVer);
    if (winVer > 0 && winVer < 0x602) {
        blog(LOG_INFO, "set aero enable");
        SetAeroEnabled(true);
    }
#endif

    // 清除各个音频输出
    for (int i = 0; i < MAX_AUDIO_MIXES; ++i)
        obs_set_output_source(i, nullptr);

    // 场景过度 - 淡出
    size_t idx = 0;
    const char *id;
    while (obs_enum_transition_types(idx++, &id)) {
        if (!obs_is_source_configurable(id)) {
            if (strcmp(id, ZDTALK_TRANSITION_ID) == 0) {
                const char *name = obs_source_get_display_name(id);
                obs_source_t *tr = obs_source_create_private(id, name, NULL);
                blog(LOG_INFO, "transition saved");
                transition = tr;
                break;
            }
        }
    }
    if (!transition) {
        blog(LOG_ERROR, "not find transition");
        emit errorOccurred(ErrorInitializing, QStringLiteral("创建转换器失败"));
        return;
    }
    obs_set_output_source(ZDTALK_VIDEO_OUTPUT_INDEX, transition);
    obs_source_release(transition);

    // 创建场景
    scene = obs_scene_create(ZDTALK_TAG "-Scene");
    if (!scene) {
        blog(LOG_ERROR, "create scene fail");
        emit errorOccurred(ErrorInitializing, QStringLiteral("创建场景失败"));
        return;
    }

    if (HasAudioDevices(ZDTALK_AUDIO_OUTPUT_SOURCE_ID))
        ResetAudioDevice(ZDTALK_AUDIO_OUTPUT_SOURCE_ID,
                         ZDTALK_AUDIO_OUTPUT_DEVICE_ID,
                         ZDTALK_TAG "-Default Desktop Audio",
                         ZDTALK_AUDIO_OUTPUT_INDEX);
    if (HasAudioDevices(ZDTALK_AUDIO_INPUT_SOURCE_ID))
        ResetAudioDevice(ZDTALK_AUDIO_INPUT_SOURCE_ID,
                         ZDTALK_AUDIO_INPUT_DEVICE_ID,
                         ZDTALK_TAG "-Default Mic/Aux",
                         ZDTALK_AUDIO_INPUT_INDEX);

    // 设置降噪
    AddFilterToAudioInput(ZDTALK_AUDIO_INPUT_FILTER_NOISE_SUPPRESS);

    // !!!must set
    obs_source_t *s = obs_get_output_source(0);
    obs_transition_set(s, obs_scene_get_source(scene));
    obs_source_release(s);

    // 创建窗口捕获源
    captureSource = obs_source_create(ZDTALK_VIDEO_CAPTURE_SOURCE_ID,
                               ZDTALK_TAG "-Capture", NULL, nullptr);
    if (captureSource) {
        obs_scene_atomic_update(scene, AddSource, captureSource);
    } else {
        blog(LOG_ERROR, "create source fail");
        emit errorOccurred(ErrorInitializing, QStringLiteral("创建源失败"));
        return;
    }

    // update source
    obs_data_t *setting = obs_data_create();
    obs_data_t *curSetting = obs_source_get_settings(captureSource);
    obs_data_apply(setting, curSetting);
    obs_data_release(curSetting);

    // 添加窗口捕获源的剪裁过滤器
    addFilterToSource(captureSource, ZDTALK_VIDEO_CROP_FILTER_ID);

    blog(LOG_INFO, OBS_SEPARATOR);
    blog(LOG_INFO, "exe desc %s", title.toStdString().c_str());
    properties = obs_source_properties(captureSource);
    obs_property_t *property = obs_properties_first(properties);
    while (property) {
        const char *name = obs_property_name(property);
        if (strcmp(name, "window") == 0) {
            size_t count = obs_property_list_item_count(property);
            const char *string = nullptr;
            for (size_t i = 0; i < count; i++) {
                const char *name = obs_property_list_item_name(property, i);
                blog(LOG_INFO, "window list item name=%s", name);
                if (title == QString::fromUtf8(name)) {
                    string = obs_property_list_item_string(property, i);
                    blog(LOG_INFO, "!!!Found item=%s", string);
                    break;
                }
            }
            if (string) {
                obs_data_set_string(setting, name, string);
                obs_source_update(captureSource, setting);
                break;
            } else {
                blog(LOG_INFO, "find application window fail");
                obs_data_release(setting);
                emit errorOccurred(ErrorInitializing, QStringLiteral("查找应用窗口失败"));
                return;
            }
        }
        obs_property_next(&property);
    }
    obs_data_release(setting);
    blog(LOG_INFO, OBS_SEPARATOR);

    // 视频裁剪
    cropVideo(sourceRegion);
    // 场景放缩
    obs_scene_enum_items(scene, FindSceneItemAndScale, (void *)this);

    blog(LOG_INFO, OBS_INIT_END);

    emit initialized();
}

void ZDTalkOBSContext::addFilterToSource(obs_source_t *source, const char *id)
{
    if (!id || *id == '\0')
        return;

    obs_source_t *existing_filter;
    std::string name = obs_source_get_display_name(id);
    existing_filter = obs_source_get_filter_by_name(source, name.c_str());
    if (existing_filter) {
        blog(LOG_WARNING, "filter %s exists.", id);
        obs_source_release(existing_filter);
        return;
    }

    name = ZDTALK_TAG ZDTALK_VIDEO_CROP_FILTER_ID;
    obs_source_t *filter = obs_source_create(id, name.c_str(), nullptr, nullptr);
    if (filter) {
        const char *sourceName = obs_source_get_name(source);
        blog(LOG_INFO, "add filter '%s' (%s) to source '%s'",
             name.c_str(), id, sourceName);
        obs_source_filter_add(source, filter);
        obs_source_release(filter);
    }
}

void ZDTalkOBSContext::cropVideo(const QRect &rect)
{
    if (!captureSource) return;

    if (rect.isEmpty()) {
        blog(LOG_WARNING, "source crop rect(%d %d %d %d) is empty.",
             rect.left(), rect.top(), rect.right(), rect.bottom());
        return;
    }

    bool relative = false;
    std::string name = ZDTALK_TAG ZDTALK_VIDEO_CROP_FILTER_ID;
    obs_source_t *existing_filter =
            obs_source_get_filter_by_name(captureSource, name.c_str());
    if (existing_filter) {
        obs_data_t *settings = obs_source_get_settings(existing_filter);
        obs_data_set_bool(settings, "relative", relative);
        obs_data_set_int(settings, "left", rect.left());
        obs_data_set_int(settings, "top", rect.top());
        if (relative) {
            obs_data_set_int(settings, "right", rect.right());
            obs_data_set_int(settings, "bottom", rect.bottom());
            blog(LOG_INFO, "update source crop [Left:%d Top:%d Right:%d Bottom:%d]",
                 rect.left(), rect.top(), rect.right(), rect.bottom());
        } else {
            obs_data_set_int(settings, "cx", rect.width());
            obs_data_set_int(settings, "cy", rect.height());
            blog(LOG_INFO, "update source crop [Left:%d Top:%d Width:%d Height:%d]",
                 rect.left(), rect.top(), rect.width(), rect.height());
        }
        obs_source_update(existing_filter, settings);
        obs_data_release(settings);
        obs_source_release(existing_filter);

        scaleVideo(QSize(rect.width(), rect.height()));
    }
}

bool ZDTalkOBSContext::initService()
{
    if (!rtmpService) {
        rtmpService = obs_service_create("rtmp_custom", ZDTALK_TAG "-RtmpService",
                                         nullptr, nullptr);
        if (!rtmpService) {
            blog(LOG_ERROR, "create service fail");
            return false;
        }
        obs_service_release(rtmpService);
    }
    return true;
}

bool ZDTalkOBSContext::resetOutputs()
{
    if (!streamOutput) {
        streamOutput = obs_output_create(ZDTALK_STREAMING_OUTPUT_ID,
                                         ZDTALK_TAG "-AdvRtmpOutput",
                                         nullptr, nullptr);
        if (!streamOutput) {
            blog(LOG_ERROR, "create stream output fail");
            return false;
        }
        obs_output_release(streamOutput);
    }

    if (!recordOutput) {
        recordOutput = obs_output_create(ZDTALK_RECORDING_OUTPUT_ID,
                                         ZDTALK_TAG "-AdvFFmpegOutput",
                                         nullptr, nullptr);
        if (!recordOutput) {
            blog(LOG_ERROR, "create record output fail");
            return false;
        }
        obs_output_release(recordOutput);
    }

    if (!h264Streaming) {
        OBSData streamEncSettings = getStreamEncSettings();
        h264Streaming = obs_video_encoder_create("obs_x264",
                                                 ZDTALK_TAG "-StreamingH264",
                                                 streamEncSettings, nullptr);
        if (!h264Streaming) {
            blog(LOG_ERROR, "create streaming encoder fail");
            return false;
        }
        obs_encoder_release(h264Streaming);

        // 禁用放缩
        obs_encoder_set_scaled_size(h264Streaming, 0, 0);
        obs_encoder_set_video(h264Streaming, obs_get_video());
        obs_output_set_video_encoder(streamOutput, h264Streaming);
        obs_service_apply_encoder_settings(rtmpService, streamEncSettings, nullptr);
        obs_output_set_service(streamOutput, rtmpService);
    }

    for (int i = 0; i < MAX_AUDIO_MIXES; i++) {
        std::string name = ZDTALK_TAG "-Track";
        name += std::to_string(i + 1);
        obs_data_t *setting = obs_data_create();
        obs_data_set_int(setting, "bitrate", 128);
        if (!aacTrack[i]) {
            if (!CreateAACEncoder(aacTrack[i], aacEncoderID[i], name.c_str(), i,
                                  setting)) {
                blog(LOG_ERROR, "create audio encoder %d", i);
                obs_data_release(setting);
                return false;
            }
        }
        if (i == 0) {
            obs_service_apply_encoder_settings(rtmpService, nullptr, setting);
            obs_output_set_audio_encoder(streamOutput, aacTrack[0], 0);
        }
        obs_data_release(setting);

        obs_encoder_set_audio(aacTrack[i], obs_get_audio());
    }

    return true;
}

bool ZDTalkOBSContext::resetAudio()
{
    struct obs_audio_info oai;
    oai.samples_per_sec = ZDTALK_AUDIO_SAMPLES_PER_SECOND;
    oai.speakers        = ZDTALK_AUDIO_SPEAKERS;
    return obs_reset_audio(&oai);
}

int ZDTalkOBSContext::resetVideo()
{
    struct obs_video_info ovi;
    ovi.fps_num         = ZDTALK_VIDEO_FPS; /* 帧率 */
    ovi.fps_den         = 1;
    ovi.graphics_module = DL_D3D11;
    ovi.base_width      = this->baseWidth;
    ovi.base_height     = this->baseHeight;
    ovi.output_width    = this->outputWidth;
    ovi.output_height   = this->outputHeight;
    ovi.output_format   = ZDTALK_VIDEO_FORMAT;
    ovi.colorspace      = ZDTALK_VIDEO_COLORSPACE;
    ovi.range           = ZDTALK_VIDEO_RANGE;
    ovi.adapter         = 0; /* 显示适配器索引 */
    ovi.gpu_conversion  = true;
    ovi.scale_type      = ZDTALK_VIDEO_SCALE_TYPE;

    int ret = obs_reset_video(&ovi);

    if (IS_WIN32 && ret != OBS_VIDEO_SUCCESS) {
        if (strcmp(ovi.graphics_module, DL_OPENGL) != 0) {
            blog(LOG_WARNING, "failed to initialize obs video (%d) "
                              "with graphics_module='%s', retrying "
                              "with graphics_module='%s'",
                 ret, ovi.graphics_module, DL_OPENGL);
            ovi.graphics_module = DL_OPENGL;
            ret = obs_reset_video(&ovi);
        }
    }

    return ret;
}

/**
 * @brief 设置音频输入、输出设备
 *        输出设备序号： 1， 2
 *        输入设备序号： 3， 4， 5
 */
void ZDTalkOBSContext::resetAudioInput(const QString &/*deviceId*/,
                                       const QString &deviceDesc)
{
    obs_properties_t *input_props =
            obs_get_source_properties(ZDTALK_AUDIO_INPUT_SOURCE_ID);
    bool find = false;
    const char *currentDeviceId = nullptr;

    obs_source_t *source =
            obs_get_output_source(ZDTALK_AUDIO_INPUT_INDEX);
    if (source) {
        obs_data_t *settings = nullptr;
        settings = obs_source_get_settings(source);
        if (settings)
            currentDeviceId = obs_data_get_string(settings, "device_id");
        obs_data_release(settings);
        obs_source_release(source);
    }

    if (input_props) {
        obs_property_t *inputs = obs_properties_get(input_props, "device_id");
        size_t count = obs_property_list_item_count(inputs);
        for (size_t i = 0; i < count; i++) {
            const char *name = obs_property_list_item_name(inputs, i);
            const char *id = obs_property_list_item_string(inputs, i);
            if (QString(name).contains(deviceDesc)) {
                blog(LOG_INFO, "reset audio input use %s", name);
                ResetAudioDevice(ZDTALK_AUDIO_INPUT_SOURCE_ID, id, name,
                                 ZDTALK_AUDIO_INPUT_INDEX);
                find = true;
                break;
            }
        }
        obs_properties_destroy(input_props);
    }

    if (!find) {
        if (QString(currentDeviceId) !=
                QString(ZDTALK_AUDIO_INPUT_DEVICE_ID)) {
            blog(LOG_INFO, "reset audio input use \"default\"");
            ResetAudioDevice(ZDTALK_AUDIO_INPUT_SOURCE_ID,
                             ZDTALK_AUDIO_INPUT_DEVICE_ID,
                             ZDTALK_TAG "-Default Mic/Aux",
                             ZDTALK_AUDIO_INPUT_INDEX);
        }
    }

    AddFilterToAudioInput(ZDTALK_AUDIO_INPUT_FILTER_NOISE_SUPPRESS);
}

void ZDTalkOBSContext::resetAudioOutput(const QString &/*deviceId*/,
                                        const QString &deviceDesc)
{
    obs_properties_t *output_props =
            obs_get_source_properties(ZDTALK_AUDIO_OUTPUT_SOURCE_ID);
    bool find = false;
    const char *currentDeviceId = nullptr;

    obs_source_t *source =
            obs_get_output_source(ZDTALK_AUDIO_OUTPUT_INDEX);
    if (source) {
        obs_data_t *settings = nullptr;
        settings = obs_source_get_settings(source);
        if (settings)
            currentDeviceId = obs_data_get_string(settings, "device_id");
        obs_data_release(settings);
        obs_source_release(source);
    }

    if (output_props) {
        obs_property_t *outputs = obs_properties_get(output_props, "device_id");
        size_t count = obs_property_list_item_count(outputs);
        for (size_t i = 0; i < count; i++) {
            const char *name = obs_property_list_item_name(outputs, i);
            const char *id = obs_property_list_item_string(outputs, i);
            if (QString(name).contains(deviceDesc)) {
                blog(LOG_INFO, "reset audio output use %s", name);
                ResetAudioDevice(ZDTALK_AUDIO_OUTPUT_SOURCE_ID, id, name,
                                 ZDTALK_AUDIO_OUTPUT_INDEX);
                find = true;
                break;
            }
        }
        obs_properties_destroy(output_props);
    }

    if (!find) {
        if (QString(currentDeviceId) !=
                QString(ZDTALK_AUDIO_OUTPUT_DEVICE_ID)) {
            blog(LOG_INFO, "reset audio output use \"default\"");
            ResetAudioDevice(ZDTALK_AUDIO_OUTPUT_SOURCE_ID,
                             ZDTALK_AUDIO_OUTPUT_DEVICE_ID,
                             ZDTALK_TAG "-Default Desktop Audio",
                             ZDTALK_AUDIO_OUTPUT_INDEX);
        }
    }
}

void ZDTalkOBSContext::downmixMonoInput(bool enable)
{
    obs_source_t *source =
            obs_get_output_source(ZDTALK_AUDIO_INPUT_INDEX);
    if (source) {
        uint32_t flags = obs_source_get_flags(source);
        bool forceMonoActive = (flags & OBS_SOURCE_FLAG_FORCE_MONO) != 0;
        blog(LOG_INFO, "audio input force mono %d", forceMonoActive);
        if (forceMonoActive != enable) {
            if (enable)
                flags |= OBS_SOURCE_FLAG_FORCE_MONO;
            else
                flags &= ~OBS_SOURCE_FLAG_FORCE_MONO;
            obs_source_set_flags(source, flags);
        }
        obs_source_release(source);
    }
}

void ZDTalkOBSContext::downmixMonoOutput(bool enable)
{
    obs_source_t *source =
            obs_get_output_source(ZDTALK_AUDIO_OUTPUT_INDEX);
    if (source) {
        uint32_t flags = obs_source_get_flags(source);
        bool forceMonoActive = (flags & OBS_SOURCE_FLAG_FORCE_MONO) != 0;
        blog(LOG_INFO, "audio output force mono %d", forceMonoActive);
        if (forceMonoActive != enable) {
            if (enable)
                flags |= OBS_SOURCE_FLAG_FORCE_MONO;
            else
                flags &= ~OBS_SOURCE_FLAG_FORCE_MONO;
            obs_source_set_flags(source, flags);
        }
        obs_source_release(source);
    }
}

void ZDTalkOBSContext::muteAudioInput(bool mute)
{
    obs_source_t *source =
            obs_get_output_source(ZDTALK_AUDIO_INPUT_INDEX);
    if (source) {
        bool old = obs_source_muted(source);
//        if (old != mute) {
        blog(LOG_INFO, "mute audio input from:%d to:%d", old, mute);
        obs_source_set_muted(source, mute);
//        }
        obs_source_release(source);
    }
}

void ZDTalkOBSContext::muteAudioOutput(bool mute)
{
    obs_source_t *source =
            obs_get_output_source(ZDTALK_AUDIO_OUTPUT_INDEX);
    if (source) {
        bool old = obs_source_muted(source);
//        if (old != mute) {
        blog(LOG_INFO, "mute audio output from:%d to:%d", old, mute);
        obs_source_set_muted(source, mute);
//        }
        obs_source_release(source);
    }
}

OBSData ZDTalkOBSContext::getStreamEncSettings()
{
    obs_data_t *settings = obs_data_create();
    obs_data_set_string(settings, "preset", "medium");
    obs_data_set_string(settings, "tune", "stillimage");
    obs_data_set_string(settings, "x264opts", "");
    obs_data_set_string(settings, "rate_control", "CRF");
    obs_data_set_int(settings, "crf", 22);
    obs_data_set_bool(settings, "vfr", false);
    obs_data_set_string(settings, "profile", "main");
    obs_data_set_int(settings, "keyint_sec", 10);

    OBSData dataRet(settings);
    obs_data_release(settings);
    return dataRet;
}

bool ZDTalkOBSContext::setupRecord()
{
    obs_data_t *settings = obs_data_create();

    obs_data_set_string(settings, "url", filePath);
    if (outputType == OutputMP4) {
        obs_data_set_string(settings, "muxer_settings", "movflags=faststart");
        obs_data_set_string(settings, "format_name", "mp4");
        obs_data_set_string(settings, "format_mime_type", "video/mp4");
        obs_data_set_string(settings, "video_encoder", "libx264");
        obs_data_set_int(settings, "video_encoder_id", AV_CODEC_ID_H264);
        obs_data_set_string(settings, "video_settings", "profile=main x264-params=crf=22");
    } else {
        obs_data_set_string(settings, "format_name", "flv");
        obs_data_set_string(settings, "format_mime_type", "video/x-flv");
        obs_data_set_string(settings, "video_encoder", "flv");
        obs_data_set_int(settings, "video_encoder_id", AV_CODEC_ID_FLV1);
        obs_data_set_int(settings, "video_bitrate", ZDTALK_VIDEO_BITRATE);
    }
    obs_data_set_int(settings, "gop_size", ZDTALK_VIDEO_FPS * 10);
    obs_data_set_int(settings, "audio_bitrate", ZDTALK_AUDIO_BITRATE);
    obs_data_set_string(settings, "audio_encoder", ZDTALK_AUDIO_ENCODER_NAME);
    obs_data_set_int(settings, "audio_encoder_id", AV_CODEC_ID_AAC);
    obs_data_set_string(settings, "audio_settings", NULL);

    obs_data_set_int(settings, "scale_width", outputWidth);
    obs_data_set_int(settings, "scale_height", outputHeight);

    //obs_output_set_mixer(fileOutput, 0);
    obs_output_set_media(recordOutput, obs_get_video(), obs_get_audio());
    obs_output_update(recordOutput, settings);

    obs_data_release(settings);

    return true;
}

bool ZDTalkOBSContext::setupStream()
{
    OBSData settings = obs_data_create();
    obs_data_release(settings);

    obs_data_set_string(settings, "server", liveServer);
    obs_data_set_string(settings, "key", liveKey);
    obs_data_set_bool(settings, "use_auth", false);
    obs_service_update(rtmpService, settings);

    obs_output_set_reconnect_settings(streamOutput, 0, 0);

    return true;
}

void ZDTalkOBSContext::startRecording(const QString &output)
{
    if (output.isEmpty()) {
        blog(LOG_ERROR, "record parameter invalid, outputPath=%s",
             output.toStdString().c_str());
        emit errorOccurred(ErrorRecording, QStringLiteral("参数错误"));
        return;
    }

    if (QString(filePath).compare(output) != 0) {
        if (filePath) free(filePath);
        filePath = _strdup(output.toStdString().c_str());
        blog(LOG_INFO, "record output file path %s", filePath);
    }

    setupRecord();

    if (!obs_output_start(recordOutput)) {
        blog(LOG_ERROR, "record start failed.");
        emit errorOccurred(ErrorRecording, QStringLiteral("启动失败"));
        return;
    }
}

void ZDTalkOBSContext::stopRecording(bool force)
{
    if (obs_output_active(recordOutput)) {
        if (force) {
            obs_output_force_stop(recordOutput);
        } else {
            obs_output_stop(recordOutput);
        }
    }
}

void ZDTalkOBSContext::startStreaming(const QString &server, const QString &key)
{
    if (server.isEmpty() || key.isEmpty()) {
        blog(LOG_ERROR, "stream parameter invalid, server=%s, key=%s",
             server.toStdString().c_str(), key.toStdString().c_str());
        emit errorOccurred(ErrorStreaming, QStringLiteral("参数错误"));
        return;
    }

    if (QString(liveServer).compare(server) != 0 ||
            QString(liveKey).compare(key) != 0) {
        if (liveServer) free(liveServer);
        if (liveKey) free(liveKey);
        liveServer = _strdup(server.toStdString().c_str());
        liveKey = _strdup(key.toStdString().c_str());
        blog(LOG_INFO, "stream url server:%s key:%s", liveServer, liveKey);
    }

    setupStream();

    if (!obs_output_start(streamOutput)) {
        blog(LOG_ERROR, "stream start fail");
        emit errorOccurred(ErrorStreaming, QStringLiteral("启动失败"));
        return;
    }

    if (recordWhenStreaming)
        startRecording(QString(filePath));

    firstTotal = obs_output_get_total_frames(streamOutput);
    firstDropped = obs_output_get_frames_dropped(streamOutput);
    blog(LOG_INFO, "first total:%d, dropped:%d", firstTotal, firstDropped);
}

void ZDTalkOBSContext::stopStreaming(bool force)
{
    if (obs_output_active(streamOutput)) {
        if (force) {
            obs_output_force_stop(streamOutput);
        } else {
            obs_output_stop(streamOutput);
        }
    }

    if (recordWhenStreaming)
        stopRecording(force);
}

void ZDTalkOBSContext::updateVideoConfig(bool cursor, bool compatibility)
{
    if (!captureSource) return;

    blog(LOG_INFO, "update source cursor=%d, compatibility=%d",
         cursor, compatibility);
    obs_data_t *settings = obs_source_get_settings(captureSource);
    obs_data_set_bool(settings, "cursor", cursor);
    obs_data_set_bool(settings, "compatibility", compatibility);
    //obs_data_set_bool(settings, "use_wildcards", useWildcards);
    obs_source_update(captureSource, settings);
    obs_data_release(settings);
}

void ZDTalkOBSContext::scaleVideo(const QSize &size)
{
    if (orgWidth == size.width() && orgHeight == size.height())
        return;
    orgWidth = size.width();
    orgHeight = size.height();

    blog(LOG_INFO, "scale scene to %dx%d", orgWidth, orgHeight);

    if (scene)
        obs_scene_enum_items(scene, FindSceneItemAndScale, (void *)this);
    else
        blog(LOG_WARNING, "obs scene is not created, cannot scale");
    /*
    if (!sceneitem)
        return;

    vec2 sc;
    sc.x = baseWidth * 1.f / w;
    sc.y = baseHeight * 1.f / h;
    blog(LOG_INFO, "Scale scene %f, %f", sc.x, sc.y);
    obs_sceneitem_set_scale(sceneitem, &sc);


    //obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_STRETCH);

    obs_sceneitem_t *item = sceneitem;
    obs_bounds_type boundsType = obs_sceneitem_get_bounds_type(item);
    obs_source_t *source = obs_sceneitem_get_source(item);
    obs_sceneitem_crop crop;
    vec2 scale, size, pos;

    obs_sceneitem_get_scale(item, &scale);
    obs_sceneitem_get_crop(item, &crop);
    obs_sceneitem_get_pos(item, &pos);

    if (boundsType != OBS_BOUNDS_NONE) {
        obs_sceneitem_get_bounds(item, &size);

    } else {
        size.x = float(obs_source_get_width(source) -
                crop.left - crop.right) * scale.x;
        size.y = float(obs_source_get_height(source) -
                crop.top - crop.bottom) * scale.y;
    }


    blog(LOG_INFO, "items bound type:%d", boundsType);
    blog(LOG_INFO, "item pos:%f %f", pos.x, pos.y);
    blog(LOG_INFO, "item crop:top->%d left->%d right->%d bottom->%d",
         crop.top, crop.left, crop.right, crop.bottom);
    blog(LOG_INFO, "item scale:%f %f", scale.x, scale.y);
    blog(LOG_INFO, "item size:%f %f", size.x, size.y);
    */
}

void ZDTalkOBSContext::logStreamStats()
{
    if (!streamOutput) return;

    uint64_t totalBytes = obs_output_get_total_bytes(streamOutput);
    uint64_t curTime = os_gettime_ns();
    uint64_t bytesSent = totalBytes;

    if (bytesSent < lastBytesSent)
        bytesSent = 0;
    if (bytesSent == 0)
        lastBytesSent = 0;

    uint64_t bitsBetween = (bytesSent - lastBytesSent) * 8;
    long double timePassed = (long double)(curTime - lastBytesSentTime) /
                             1000000000.0l;
    long double kbps = (long double)bitsBetween /
                       timePassed / 1000.0l;

    if (timePassed < 0.01l)
        kbps = 0.0l;

    long double num = 0;
    int total = obs_output_get_total_frames(streamOutput);
    int dropped = obs_output_get_frames_dropped(streamOutput);
    if (total < firstTotal || dropped < firstDropped) {
        firstTotal   = 0;
        firstDropped = 0;
    }
    total   -= firstTotal;
    dropped -= firstDropped;
    num = total ? (long double)dropped / (long double)total * 100.0l : 0.0l;

    blog(LOG_INFO, "obs stream stat, bitrate:%.2lf kb/s, frames:%d / %d (%.2lf%%)",
         kbps, dropped, total, num);

    lastBytesSent     = bytesSent;
    lastBytesSentTime = curTime;
}
