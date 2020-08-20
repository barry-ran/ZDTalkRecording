#ifndef ZDRECORDINGDEFINE_H
#define ZDRECORDINGDEFINE_H

#define ZDTALK_RECORDING_DESC ""

#define ZDTALK_STREAMING_MAX_RETRY_TIMES   3
#define ZDTALK_STREAMING_RETRY_INTERVAL    5

enum ZDRecordingOutputType
{
    OutputMP4,
    OutputFLV
};

enum ZDRecordingEvent
{
    // Client To Server
    EventInitialize,
    EventScaleVideo,
    EventCropVideo,
    EventUpdateVideoConfig,
    EventResetAudioInput,
    EventResetAudioOutput,
    EventDownmixMonoInput,
    EventDownmixMonoOutput,
    EventMuteAudioInput,
    EventMuteAudioOutput,
    EventStartRecording,
    EventStopRecording,
    EventStartStreaming,
    EventStopStreaming,

    // Server To Client
    EventInitialized,
    EventRecordingStarted,
    EventRecordingStopped,
    EventStreamingStarted,
    EventStreamingStopped,
    EventErrorOccurred,
};

enum ZDRecordingErrorType
{
    ErrorNone,
    ErrorInitializing,
    ErrorRecording,
    ErrorStreaming,
    ErrorClient
};

enum ZDRecordingServerError
{
    ErrorAlreadyListening = -40,
    ErrorArgumentInvalid  = -41,
};

inline bool fRatioEqual(const float f1, const float f2)
{
    return fabs(f1 - f2) < 0.0005f;
}

typedef enum {
    DisplayRatio_Unhandle = -1,
    DisplayRatio_5_4,      // 1280*1024
    DisplayRatio_4_3,      // 1280*960
    DisplayRatio_25_16,    // 1600*1024
    DisplayRatio_16_10,    // 2560*1600
    DisplayRatio_5_3,      // 1280*768
    DisplayRatio_16_9,     // 1920*1080
    DisplayRatio_21_9,     // 2560*1080 (21:9)
    DisplayRatio_43_18,    // 3440*1440
    DisplayRatio_Count
} DisplayRatio;

const float display_ratios[DisplayRatio_Count] = {
     4.f / 5,    // 0.8
     3.f / 4,    // 0.75
    16.f / 25,   // 0.64
    10.f / 16,   // 0.625
     3.f / 5,    // 0.6
     9.f / 16,   // 0.5625
    27.f / 64,   // 0.421875
    18.f / 43,   // 0.418605
};

#define DISPLAY_RES_COUNT_PER_RATIO 3

#include <QPair>
const QPair<int, int>
display_resolutions[DisplayRatio_Count * DISPLAY_RES_COUNT_PER_RATIO] {

    // DisplayRatio_5_4
    qMakePair(1280, 1024),
    qMakePair(960 , 768),
    qMakePair(640 , 512),

    // DisplayRatio_4_3
    qMakePair(1280, 960),
    qMakePair(960 , 720),
    qMakePair(640 , 480),

    // DisplayRatio_25_16
    qMakePair(1280, 800),
    qMakePair(960 , 600),
    qMakePair(640 , 400),

    // DisplayRatio_16_10
    qMakePair(1280, 800),
    qMakePair(960 , 600),
    qMakePair(640 , 400),

    // DisplayRatio_5_3
    qMakePair(1280, 768),
    qMakePair(960 , 576),
    qMakePair(640 , 384),

    // DisplayRatio_16_9
    qMakePair(1280, 720),
    qMakePair(960 , 540),
    qMakePair(640 , 360),

    // DisplayRatio_21_9
    qMakePair(1280, 540),
    qMakePair(960 , 405),
    qMakePair(640 , 270),

    // DisplayRatio_43_18
    qMakePair(1280, 540),
    qMakePair(960 , 405),
    qMakePair(640 , 270),
};

#endif // ZDRECORDINGDEFINE_H
