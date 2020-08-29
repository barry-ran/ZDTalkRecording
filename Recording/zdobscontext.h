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

#pragma once

#define DL_OPENGL "libobs-opengl.dll"
#define DL_D3D9   ""
#define DL_D3D11  "libobs-d3d11.dll"

#include "obs.h"
#include "obs.hpp"

#include <string>

#include <QObject>
#include <QSize>
#include <QRect>

class ZDTalkOBSContext : public QObject
{
    Q_OBJECT
public:
    explicit ZDTalkOBSContext(QObject *parent = nullptr);
	~ZDTalkOBSContext();

    const char * getRecordFilePath() { return filePath; }
    const QSize getBaseSize() { return QSize(baseWidth, baseHeight); }
    const QSize getOriginalSize() { return QSize(orgWidth, orgHeight); }

signals:
    void initialized();
    void recordingStarted();
    void recordingStopped(const QString &);
    void streamingStarted();
    void streamingStopped();
    void errorOccurred(const int, const QString &);

public slots:
    void initialize(const QString &configPath, const QString &windowTitle,
                    const QSize &screenSize, const QRect &sourceRect);
    void release();

    /* 主界面最大化/恢复时，用于切换场景的分辨率 （弃用）*/
    void scaleVideo(const QSize &);
    void updateVideoConfig(bool cursor = true, bool compatibility = false);
    void cropVideo(const QRect &);

    /* 试麦切换设备后，需要重新设置音频设备 */
    void resetAudioInput(const QString &deviceId, const QString &deviceDesc);
    void resetAudioOutput(const QString &deviceId, const QString &deviceDesc);

    /* 处理单声道问题 */
    void downmixMonoInput(bool enable);
    void downmixMonoOutput(bool enable);

    /* 静音状态与 RTC 保持一致 */
    void muteAudioInput(bool);
    void muteAudioOutput(bool);

    void startRecording(const QString &output);
    void stopRecording(bool force);

    void startStreaming(const QString &server, const QString &key);
    void stopStreaming(bool force);

    void logStreamStats();

private:
    bool resetAudio();
    int  resetVideo();

    OBSData getStreamEncSettings();
    bool initService();
    bool resetOutputs();

    bool setupRecord();
    bool setupStream();

    void addFilterToSource(obs_source_t *, const char *);

    char *filePath;
    char *liveServer;
    char *liveKey;

    OBSService rtmpService;

    OBSOutput recordOutput;
    OBSOutput streamOutput;

    OBSEncoder h264Streaming;
    OBSEncoder aacTrack[MAX_AUDIO_MIXES];
    std::string aacEncoderID[MAX_AUDIO_MIXES];

    obs_scene_t *scene;            // "default"
    obs_source_t *transition;
    obs_source_t *captureSource;
    obs_properties_t *properties;  // ZDTalk window properties

    OBSSignal signalRecordingStarted;
    OBSSignal signalRecordingStopping;
    OBSSignal signalRecordingStopped;
    OBSSignal signalStreamingStarted;
    OBSSignal signalStreamingStopping;
    OBSSignal signalStreamingStopped;

    bool recordWhenStreaming;

    int baseWidth;    // 场景画布分辨率
    int baseHeight;
    int outputWidth;  // 输出文件分辨率
    int outputHeight;
    int orgWidth;     // 窗口原始分辨率
    int orgHeight;

    uint64_t lastBytesSent;
    uint64_t lastBytesSentTime;
    int      firstTotal;
    int      firstDropped;

    int      outputType;
};
