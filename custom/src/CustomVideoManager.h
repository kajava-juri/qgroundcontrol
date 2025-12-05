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

#include "VideoReceiver.h"
#include "QGCCorePlugin.h"

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

    // Stream indices
    static constexpr int STREAM_RGB = 0;
    static constexpr int STREAM_THERMAL = 1;
    static constexpr int STREAM_COUNT = 2;

signals:
    void streamStateChanged(int streamIndex, bool active);
    void streamDecodingChanged(int streamIndex, bool decoding);

private:
    void _setupReceiver(int streamIndex, QQuickItem* widget);
    void _startReceiver(int streamIndex);
    void _stopReceiver(int streamIndex);
    void _initAfterQmlIsReady();

    struct StreamInfo {
        VideoReceiver* receiver = nullptr;
        void* sink = nullptr;
        QQuickItem* widget = nullptr;
        QString uri;
        bool active = false;
        bool decoding = false;
    };

    StreamInfo _streams[STREAM_COUNT];
    QQuickWindow* _mainWindow = nullptr;
    bool _initialized = false;
};