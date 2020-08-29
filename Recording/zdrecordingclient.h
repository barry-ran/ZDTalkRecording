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

#ifndef ZDRECORDINGCLIENT_H
#define ZDRECORDINGCLIENT_H

#include <QObject>
#include <QLocalSocket>
class QThread;
class ZDTalkOBSContext;

class ZDRecordingClient : public QObject
{
    Q_OBJECT
public:
    explicit ZDRecordingClient(QObject *parent = 0);
    ~ZDRecordingClient();

    void connectToServer();

signals:
    // Client -> Recording Thread
    void obsInit(const QString &configPath, const QString &windowTitle,
                 const QSize &screenSize, const QRect &sourceRect);
    void obsRelease();
    void obsScaleVideo(const QSize &);
    void obsCropVideo(const QRect &);
    void obsUpdateVideoConfig(bool cursor = true, bool compatibility = false);
    void obsResetAudioInput(const QString &deviceId, const QString &deviceDesc);
    void obsResetAudioOutput(const QString &deviceId, const QString &deviceDesc);
    void obsDownmixMonoInput(bool enable);
    void obsDownmixMonoOutput(bool enable);
    void obsMuteAudioInput(bool);
    void obsMuteAudioOutput(bool);
    void obsStartRecording(const QString &output);
    void obsStopRecording(bool force);
    void obsStartStreaming(const QString &server, const QString &key);
    void obsStopStreaming(bool force);
    void obsLogStreamStats();

private slots:
    // Socket
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketError(QLocalSocket::LocalSocketError socketError);
    void onSocketReadyRead();

public slots:
    // Recording Callback -> Client
    void onOBSInitialized();
    void onOBSRecordingStarted();
    void onOBSRecordingStopped(const QString &);
    void onOBSStreamingStarted();
    void onOBSStreamingStopped();
    void onOBSErrorOccurred(const int, const QString &);

private:
    void sendMessageToServer(int event, int param1 = 0,
                             const QString &param2 = QStringLiteral(""));

private:
    QLocalSocket     *mSocket     = nullptr;
    QThread          *mOBSThread  = nullptr;
    ZDTalkOBSContext *mOBSContext = nullptr;
};

#endif // ZDRECORDINGCLIENT_H
