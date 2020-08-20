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
