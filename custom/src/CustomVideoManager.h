/****************************************************************************
 *
 * Custom Multi-Video Manager for data collection streams
 * Independent of QGC's built-in camera/thermal system
 *
 ****************************************************************************/

#pragma once

#include <QObject>
#include <QtQuick/QQuickWindow>
#include <QQuickItem>
#include <QLoggingCategory>
#include <QVariant>

#include <glib.h>

#include "VideoReceiver.h"
#include "QGCCorePlugin.h"

typedef struct _GstElement GstElement;
typedef struct _GstBus GstBus;
typedef struct _GstMessage GstMessage;

Q_DECLARE_LOGGING_CATEGORY(CustomVideoManagerLog)

class QGCApplication;

/**
 * @brief Manages custom video streams independent of drone camera system
 *
 * This class provides a standalone video management system that doesn't
 * interfere with QGC's built-in camera/thermal video handling.
 * Perfect for additional data collection cameras.
 */
class CustomVideoManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList videoReplaySegments READ videoReplaySegments NOTIFY videoReplaySegmentsChanged)


public:
    struct FrameMetadata {
        quint64 frameIndex = 0;
        quint64 timestampNs = 0;
    };

    struct VideoStreamMetadata {
        QString videoPath;
        qint64 offsetMs = 0;  // Offset from tlog start (can be negative if video starts after tlog)
        bool isDirectory = false;
        QList<FrameMetadata> frameMetadata;
    };

    explicit CustomVideoManager(QObject* parent = nullptr);
    ~CustomVideoManager() override;

    void init(QQuickWindow* mainWindow);
    void clearAllStreamInfo();
    void restartAllStreamsToBeginning();
    Q_INVOKABLE void reinitializeWidgets(bool gridMode = false);  // Re-find widgets after mode change

    friend class FinishCustomVideoInitialization;

    /**
     * Start/stop individual streams
     */
    Q_INVOKABLE void startStream(int streamIndex);
    Q_INVOKABLE void stopStream(int streamIndex);
    Q_INVOKABLE void restartStream(int streamIndex);

    /**
     * Configure stream URIs
     */
    Q_INVOKABLE void setStreamUri(int streamIndex, const QString& uri);
    Q_INVOKABLE QString getStreamUri(int streamIndex) const;
    Q_INVOKABLE void setStreamId(int streamIndex, int streamId);
    Q_INVOKABLE int getStreamId(int streamIndex) const;
    Q_INVOKABLE void setStreamReady(int streamIndex, bool ready);
    Q_INVOKABLE bool isStreamReady(int streamIndex) const;


    /**
     * Check stream status
     */
    Q_INVOKABLE bool isStreamActive(int streamIndex) const;
    Q_INVOKABLE bool isStreamDecoding(int streamIndex) const;

    static std::map<int, std::string> StreamNames;

    // Stream indices
    static constexpr int STREAM_BR = 0; // bottom right pip
    static constexpr int STREAM_TR = 1; // top right pip
    static constexpr int STREAM_DRONE_CAMERA = 2;  // drone camera footage in replay mode
    // number of sinks managed by this class (not including replay streams)
    static constexpr int STREAM_COUNT = 2;
    // number of streams managed in replay mode (including drone camera)
    static constexpr int REPLAY_STREAM_COUNT = STREAM_COUNT + 1;

    QVariantList videoReplaySegments() const;  // For QML display of replay segments

signals:
    void streamStateChanged(int streamIndex, bool active);
    void streamDecodingChanged(int streamIndex, bool decoding);
    void streamUriChanged(int streamIndex, const QString& uri);
    void replayModeChanged(bool active);
    void videoReplaySegmentsChanged();

private:
    enum StopReason {
        None,
        UriChange,
        WidgetReinit,
        CollectionEnd,
        CommLost,
        ReplayModeEnter
    };
    struct ReplayStreamInfo;
    void _setupReceiver(int streamIndex, QQuickItem* widget);
    void _startReceiver(int streamIndex);
    void _stopReceiver(int streamIndex);
    void _initAfterQmlIsReady();
    void _restartVideo(int streamIndex);
    void _setActiveVehicle(Vehicle* vehicle);
    void _communicationLostChanged(bool communicationLost);
    void _checkDelayedVideos();  // Check if any delayed videos are ready to start
    bool _openReplayStream(int streamIndex, const VideoStreamMetadata& streamMeta, QQuickItem* widget);
    static gboolean _onBusMessage(GstBus* bus, GstMessage* message, gpointer user_data);
    static void _onReplayAppSrcNeedData(GstElement* appsrc, guint length, gpointer user_data);
    static gboolean _onReplayAppSrcSeekData(GstElement* appsrc, guint64 offset, gpointer user_data);
    //void _updateVideoReplaySegments();
    GstElement* _getVideoFilePipeline(const QString& videoPath, void* sink, int streamIndex);
    GstElement* _getVideoFramesPipeline(const VideoStreamMetadata& streamMeta, void* sink, int streamIndex);
    QString _resolveFramePath(const QString& directoryPath, quint64 frameIndex) const;

public:

    bool enterReplayMode(const VideoStreamMetadata& rgbStream, const VideoStreamMetadata& thermalStream);
    bool enterReplayMode(const VideoStreamMetadata& rgbStream, const VideoStreamMetadata& thermalStream, const VideoStreamMetadata& droneCameraStream);
    Q_INVOKABLE bool enterReplayMode(const QString& rgbVideoPath, const QString& thermalVideoPath,
                                      const QString& droneVideoPath = QString(),
                                      qint64 rgbOffsetMs = 0, qint64 thermalOffsetMs = 0, qint64 droneOffsetMs = 0);
    Q_INVOKABLE void exitReplayMode();
    Q_INVOKABLE void seekToPosition(quint32 tlogTimeSecs);  // For manual slider seeking
    Q_INVOKABLE void updateReplayTime(quint32 tlogTimeSecs);  // Update cached time (no seeking)
    Q_INVOKABLE void startReplayPlayback();  // Start playing videos
    Q_INVOKABLE void pauseReplayPlayback();  // Pause videos  // Seek based on tlog playhead position

private:

    bool _updateVideoUri(VideoReceiver *receiver, const QString &uri);
    struct StreamInfo {
        QString name;
        QString uri;
        VideoReceiver* receiver = nullptr;
        void* sink = nullptr;
        bool active = false;
        bool decoding = false;
        QTimer* decodingTimeoutTimer = nullptr;
        bool allowAutoRestart = true;
        int restartAttempts = 0;  // For exponential backoff in noisy WiFi
        QMetaObject::Connection restartConnection;  // Connection for deferred restart
        StopReason pendingStopReason = StopReason::None;  // Reason for pending stop (if any)
        int streamId = -1; // Stream ID provided by data collector
        bool ready = false; // True when data collector has sent signal that stream can be played
    };

    // Replay per-stream state
    struct ReplayStreamInfo {
        GstElement* pipeline = nullptr;   // playbin instance
        void* sink = nullptr;             // GstGLQt6 sink (same widget as live)
        VideoReceiver* receiver = nullptr; // Keep receiver alive for sink/thread lifecycle
        bool loaded = false;
        QString videoPath;
        bool isFrameSequence = false;
        VideoStreamMetadata frameStreamMeta;
        int nextFrameMetadataIndex = 0;
        quint64 baseFrameTimestampNs = 0;
        qint64 defaultFrameDurationNs = 33333333;  // ~30fps fallback for last frame
        bool frameEosSent = false;
        qint64 offsetMs = 0;              // Offset from tlog start (negative if video starts after tlog)
        bool readyToPlay = false;         // False if waiting for tlog to catch up to video start
        qint64 lastSeekTimeMs = -1000;    // Last video time we seeked to (ms) - initialized to force first seek
        qint64 durationMs = 0;
    };

    struct ReplayState {
        std::array<ReplayStreamInfo, REPLAY_STREAM_COUNT> streams;
        bool active = false;
        bool isPlaying = false;  // Track if replay is currently playing
        double playbackSpeed = 1.0;
        quint32 currentTlogTimeSecs = 0;  // Cached from seekToPosition calls
        QTimer* delayedVideoTimer = nullptr;  // Timer for checking delayed videos (negative offsets)
    };

    ReplayState _replay;


    // StreamInfo _streams[STREAM_COUNT];
    std::array<StreamInfo, STREAM_COUNT> _streams{{
        {"BR"},      // Empty - wait for VIDEO_STREAM_INFORMATION
        {"TR"},   // Empty - wait for VIDEO_STREAM_INFORMATION
    }};
    std::map<int, QQuickItem*> _streamWidgets;  // Map stream index to current widget (for replay streams, use same widget as live stream)

    QQuickWindow* _mainWindow = nullptr;
    bool _initialized = false;
    bool _initAfterQmlIsReadyDone = false;
    Vehicle* _activeVehicle = nullptr;
};