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

public:
    explicit CustomVideoManager(QObject* parent = nullptr);
    ~CustomVideoManager() override;

    void init(QQuickWindow* mainWindow);
    void clearAllStreamInfo();
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

    /**
     * Check stream status
     */
    Q_INVOKABLE bool isStreamActive(int streamIndex) const;
    Q_INVOKABLE bool isStreamDecoding(int streamIndex) const;

    static std::map<int, std::string> StreamNames;

    // Stream indices
    static constexpr int STREAM_RGB = 0;
    static constexpr int STREAM_THERMAL = 1;
    static constexpr int STREAM_COUNT = 2;

signals:
    void streamStateChanged(int streamIndex, bool active);
    void streamDecodingChanged(int streamIndex, bool decoding);
    void streamUriChanged(int streamIndex, const QString& uri);
    void replayModeChanged(bool active);

private:

    void _setupReceiver(int streamIndex, QQuickItem* widget);
    void _startReceiver(int streamIndex);
    void _stopReceiver(int streamIndex);
    void _initAfterQmlIsReady();
    void _restartVideo(int streamIndex);
    void _setActiveVehicle(Vehicle* vehicle);
    void _communicationLostChanged(bool communicationLost);
    bool _openReplayStream(int streamIndex, const QString& videoPath, QQuickItem* widget);
    static gboolean _onBusMessage(GstBus* bus, GstMessage* message, gpointer user_data);

public:
    struct VideoStreamMetadata {
        QString videoPath;
        qint64 offsetMs = 0;  // Offset from tlog start (can be negative if video starts after tlog)
    };

    Q_INVOKABLE bool enterReplayMode(const QString& rgbVideoPath, const QString& thermalVideoPath, 
                                      qint64 rgbOffsetMs = 0, qint64 thermalOffsetMs = 0);
    Q_INVOKABLE void exitReplayMode();
    Q_INVOKABLE void seekToPosition(quint32 tlogTimeSecs);  // For manual slider seeking
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
    };

    // Replay per-stream state
    struct ReplayStreamInfo {
        GstElement* pipeline = nullptr;   // playbin instance
        void* sink = nullptr;             // GstGLQt6 sink (same widget as live)
        bool loaded = false;
        QString videoPath;
        qint64 offsetMs = 0;              // Offset from tlog start (negative if video starts after tlog)
        bool readyToPlay = false;         // False if waiting for tlog to catch up to video start
        qint64 lastSeekTimeMs = -1000;    // Last video time we seeked to (ms) - initialized to force first seek
    };

    struct ReplayState {
        std::array<ReplayStreamInfo, STREAM_COUNT> streams;
        bool active = false;
        double playbackSpeed = 1.0;
    };

    ReplayState _replay;


    // StreamInfo _streams[STREAM_COUNT];
    std::array<StreamInfo, STREAM_COUNT> _streams{{
        {"RGB", "", nullptr, nullptr, false, false},      // Empty - wait for VIDEO_STREAM_INFORMATION
        {"Thermal", "", nullptr, nullptr, false, false}   // Empty - wait for VIDEO_STREAM_INFORMATION
    }};
    QQuickWindow* _mainWindow = nullptr;
    bool _initialized = false;
    bool _initAfterQmlIsReadyDone = false;
    Vehicle* _activeVehicle = nullptr;
};