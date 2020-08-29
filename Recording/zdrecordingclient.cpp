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


#include "zdrecordingclient.h"
#include "zdrecordingdefine.h"
#include "zdobscontext.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QThread>

#include <QDebug>

#define TAG_OUT "<<"
#define TAG_IN  ">>"
#define SEPARATOR "============================================================"
#define RECORDING "===================== Recording Client ====================="

ZDRecordingClient::ZDRecordingClient(QObject *parent) :
    QObject(parent)
{
    qInfo() << SEPARATOR;
    qInfo() << RECORDING;
    qInfo() << SEPARATOR;
    // Local Socket
    mSocket = new QLocalSocket(this);
    connect(mSocket, &QLocalSocket::connected,
            this,    &ZDRecordingClient::onSocketConnected);
    connect(mSocket, &QLocalSocket::disconnected,
            this,    &ZDRecordingClient::onSocketDisconnected);
    connect(mSocket, static_cast<void(QLocalSocket::*)(QLocalSocket::LocalSocketError)>(&QLocalSocket::error),
            this,    &ZDRecordingClient::onSocketError);
    connect(mSocket, &QLocalSocket::readyRead,
            this,    &ZDRecordingClient::onSocketReadyRead);

    // OBS
    mOBSThread = new QThread(this);
    mOBSContext = new ZDTalkOBSContext;
    mOBSContext->moveToThread(mOBSThread);
    connect(mOBSContext, &ZDTalkOBSContext::initialized,
            this,        &ZDRecordingClient::onOBSInitialized);
    connect(mOBSContext, &ZDTalkOBSContext::recordingStarted,
            this,        &ZDRecordingClient::onOBSRecordingStarted);
    connect(mOBSContext, &ZDTalkOBSContext::recordingStopped,
            this,        &ZDRecordingClient::onOBSRecordingStopped);
    connect(mOBSContext, &ZDTalkOBSContext::streamingStarted,
            this,        &ZDRecordingClient::onOBSStreamingStarted);
    connect(mOBSContext, &ZDTalkOBSContext::streamingStopped,
            this,        &ZDRecordingClient::onOBSStreamingStopped);
    connect(mOBSContext, &ZDTalkOBSContext::errorOccurred,
            this,        &ZDRecordingClient::onOBSErrorOccurred);

    connect(this,        &ZDRecordingClient::obsInit,
            mOBSContext, &ZDTalkOBSContext::initialize);
    connect(this,        &ZDRecordingClient::obsRelease,
            mOBSContext, &ZDTalkOBSContext::release);
    connect(this,        &ZDRecordingClient::obsScaleVideo,
            mOBSContext, &ZDTalkOBSContext::scaleVideo);
    connect(this,        &ZDRecordingClient::obsCropVideo,
            mOBSContext, &ZDTalkOBSContext::cropVideo);
    connect(this,        &ZDRecordingClient::obsUpdateVideoConfig,
            mOBSContext, &ZDTalkOBSContext::updateVideoConfig);
    connect(this,        &ZDRecordingClient::obsResetAudioInput,
            mOBSContext, &ZDTalkOBSContext::resetAudioInput);
    connect(this,        &ZDRecordingClient::obsResetAudioOutput,
            mOBSContext, &ZDTalkOBSContext::resetAudioOutput);
    connect(this,        &ZDRecordingClient::obsDownmixMonoInput,
            mOBSContext, &ZDTalkOBSContext::downmixMonoInput);
    connect(this,        &ZDRecordingClient::obsDownmixMonoOutput,
            mOBSContext, &ZDTalkOBSContext::downmixMonoOutput);
    connect(this,        &ZDRecordingClient::obsMuteAudioInput,
            mOBSContext, &ZDTalkOBSContext::muteAudioInput);
    connect(this,        &ZDRecordingClient::obsMuteAudioOutput,
            mOBSContext, &ZDTalkOBSContext::muteAudioOutput);
    connect(this,        &ZDRecordingClient::obsStartRecording,
            mOBSContext, &ZDTalkOBSContext::startRecording);
    connect(this,        &ZDRecordingClient::obsStopRecording,
            mOBSContext, &ZDTalkOBSContext::stopRecording);
    connect(this,        &ZDRecordingClient::obsStartStreaming,
            mOBSContext, &ZDTalkOBSContext::startStreaming);
    connect(this,        &ZDRecordingClient::obsStopStreaming,
            mOBSContext, &ZDTalkOBSContext::stopStreaming);
    connect(this,        &ZDRecordingClient::obsLogStreamStats,
            mOBSContext, &ZDTalkOBSContext::logStreamStats);
}

ZDRecordingClient::~ZDRecordingClient()
{
    if (mOBSThread->isRunning()) {
        qInfo() << "Wait recording thread end...";
        mOBSThread->quit();
        mOBSThread->wait(1000);
        qInfo() << "Recording thread end.";
    }

    delete mOBSContext;
    qInfo() << "Released.";
    qInfo() << SEPARATOR << "\n";
}

void ZDRecordingClient::connectToServer()
{
    qInfo() << "Connect To Server...";
    mSocket->connectToServer(ZDTALK_RECORDING_DESC);
}

// Socket 回调
void ZDRecordingClient::onSocketConnected()
{
    qInfo() << "Socket Connected.";
    if (!mOBSThread->isRunning()) {
        qInfo() << "Start Recording Thread.";
        mOBSThread->start();
    }
}

void ZDRecordingClient::onSocketDisconnected()
{
    // 只要连接断开就退出应用
    qInfo() << "Socket disconnected.";
    QCoreApplication::exit(0);
}

void ZDRecordingClient::onSocketError(
        QLocalSocket::LocalSocketError)
{
    qWarning() << "Socket Error :" << mSocket->errorString();
}

// 接收消息处理
void ZDRecordingClient::onSocketReadyRead()
{
    QByteArray readData = mSocket->readAll();
    QDataStream in(readData);
    in.setVersion(QDataStream::Qt_4_0);

    if (readData.size() < (int)sizeof(quint16)) {
        qWarning() << TAG_IN << "Received Data Is Small. Skip.";
        return;
    }

    do {
        quint16 dataSize;
        quint8 event;
        in >> dataSize >> event;
        // qDebug() << TAG_IN << "Received Data Size:" << dataSize;
        qDebug() << TAG_IN << "Received Event:" << event;

        switch (event) {
        case EventInitialize:
        {
            QString configPath, windowTitle;
            QSize screenSize;
            QRect cropRegion;
            in >> configPath >> windowTitle >> screenSize >> cropRegion;
            qInfo() << TAG_IN << "Init:" << configPath << windowTitle
                    << screenSize << cropRegion;
            emit obsInit(configPath, windowTitle, screenSize, cropRegion);
        }
            break;
        case EventScaleVideo:
        {
            QSize size;
            in >> size;
            qInfo() << TAG_IN << "Scale:" << size;
            emit obsScaleVideo(size);
        }
            break;
        case EventCropVideo:
        {
            QRect rect;
            in >> rect;
            qInfo() << TAG_IN << "Crop:" << rect;
            emit obsCropVideo(rect);
        }
            break;
        case EventUpdateVideoConfig:
        {
            bool cursor, compatibility;
            in >> cursor >> compatibility;
            qInfo() << TAG_IN << "Update Video Config:" << cursor << compatibility;
            emit obsUpdateVideoConfig(cursor, compatibility);
        }
            break;
        case EventResetAudioInput:
        {
            QString deviceId, deviceDesc;
            in >> deviceId >> deviceDesc;
            qInfo() << TAG_IN << "Reset Audio Input:" << deviceId << deviceDesc;
            emit obsResetAudioInput(deviceId, deviceDesc);
        }
            break;
        case EventResetAudioOutput:
        {
            QString deviceId, deviceDesc;
            in >> deviceId >> deviceDesc;
            qInfo() << TAG_IN << "Reset Audio Output:" << deviceId << deviceDesc;
            emit obsResetAudioOutput(deviceId, deviceDesc);
        }
            break;
        case EventDownmixMonoInput:
        {
            bool enable;
            in >> enable;
            qInfo() << TAG_IN << "Downmix Input:" << enable;
            emit obsDownmixMonoInput(enable);
        }
            break;
        case EventDownmixMonoOutput:
        {
            bool enable;
            in >> enable;
            qInfo() << TAG_IN << "Downmix output:" << enable;
            emit obsDownmixMonoOutput(enable);
        }
            break;
        case EventMuteAudioInput:
        {
            bool enable;
            in >> enable;
            qInfo() << TAG_IN << "Mute Input:" << enable;
            emit obsMuteAudioInput(enable);
        }
            break;
        case EventMuteAudioOutput:
        {
            bool enable;
            in >> enable;
            qInfo() << TAG_IN << "Mute Output:" << enable;
            emit obsMuteAudioOutput(enable);
        }
            break;
        case EventStartRecording:
        {
            QString path;
            in >> path;
            qInfo() << TAG_IN << "Start Recording:" << path;
            emit obsStartRecording(path);
        }
            break;
        case EventStopRecording:
        {
            bool force;
            in >> force;
            qInfo() << TAG_IN << "Stop Recording:" << force;
            emit obsStopRecording(force);
        }
            break;
        case EventStartStreaming:
        {
            QString s, k;
            in >> s >> k;
            qInfo() << TAG_IN << "Start Streaming:" << s << k;
            emit obsStartStreaming(s, k);
        }
            break;
        case EventStopStreaming:
        {
            bool force;
            in >> force;
            qInfo() << TAG_IN << "Stop Streaming:" << force;
            emit obsStopStreaming(force);
        }
            break;
        default:
            break;
        }

        if (in.atEnd())
            qDebug() << "Read Received Data Reach End.";
        else
            qDebug() << "Still Some Data Not Read.";

    } while (!in.atEnd());
}

void ZDRecordingClient::sendMessageToServer(int event, int param1,
                                            const QString &param2)
{
    if (!mSocket->isWritable()) {
        qWarning() << TAG_OUT << "Socket Is Not Writable.";
        return;
    }

    QByteArray block;
    QDataStream out(&block, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_4_0);
    out << (quint16)0;
    out << (quint8)event;
    if (param1 != ErrorNone)
        out << (quint8)param1;
    if (!param2.isEmpty())
        out << param2;
    out.device()->seek(0);
    out << (quint16)(block.size() - sizeof(quint16));

    mSocket->write(block);
    mSocket->flush();
}

// OBS 回调
void ZDRecordingClient::onOBSInitialized()
{
    qInfo() << TAG_OUT << "Initialized.";
    sendMessageToServer(EventInitialized);
}

void ZDRecordingClient::onOBSRecordingStarted()
{
    qInfo() << TAG_OUT << "Recording Started.";
    sendMessageToServer(EventRecordingStarted);
}

void ZDRecordingClient::onOBSRecordingStopped(const QString &path)
{
    qInfo() << TAG_OUT << "Recording Finished." << path;
    sendMessageToServer(EventRecordingStopped, ErrorNone, path);
}

void ZDRecordingClient::onOBSStreamingStarted()
{
    qInfo() << TAG_OUT << "Streaming Started.";
    sendMessageToServer(EventStreamingStarted);
}

void ZDRecordingClient::onOBSStreamingStopped()
{
    qInfo() << TAG_OUT << "Streaming Finished.";
    sendMessageToServer(EventStreamingStopped);
}

void ZDRecordingClient::onOBSErrorOccurred(const int type, const QString &msg)
{
    qCritical() << "!!!!!!!!!!!!!!!!!";
    qCritical() << TAG_OUT << "Error Occured :" << type << msg;
    qCritical() << "!!!!!!!!!!!!!!!!!";
    sendMessageToServer(EventErrorOccurred, type, msg);
}
