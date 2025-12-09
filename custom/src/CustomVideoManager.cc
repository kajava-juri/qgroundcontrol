/****************************************************************************
 *
 * Custom Multi-Video Manager Implementation
 *
 ****************************************************************************/

#include "CustomVideoManager.h"
#include "QGCApplication.h"
#include "QGCCorePlugin.h"
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"
#include "VideoSettings.h"
#include "QtQuick/QQuickWindow"

#include <QDebug>

QGC_LOGGING_CATEGORY(CustomVideoManagerLog, "CustomVideoManager")

// Helper to create all video sinks from render thread (like upstream FinishVideoInitialization)
class FinishCustomVideoInitialization : public QRunnable {
public:
    FinishCustomVideoInitialization(CustomVideoManager* mgr)
        : _mgr(mgr) {}

    void run() override {
        // We're on render thread - create all sinks sequentially here (like upstream)
        qCWarning(CustomVideoManagerLog) << "FinishCustomVideoInitialization::run() on render thread";
        _mgr->_initAfterQmlIsReady();
    }

private:
    CustomVideoManager* _mgr;
};

std::map<int, std::string> CustomVideoManager::StreamNames;

CustomVideoManager::CustomVideoManager(QObject* parent)
    : QObject(parent)
{
    qCWarning(CustomVideoManagerLog) << "CustomVideoManager created";
}

void CustomVideoManager::_initAfterQmlIsReady()
{
    qCWarning(CustomVideoManagerLog) << "_initAfterQmlIsReady - searching for video widgets";

    // Try to find widgets - they may or may not be loaded yet
    QQuickItem* rgbWidget = _mainWindow->findChild<QQuickItem*>("customRgbVideo");
    QQuickItem* thermalWidget = _mainWindow->findChild<QQuickItem*>("customThermalVideo");

    qCWarning(CustomVideoManagerLog) << "Found widgets - RGB:" << rgbWidget
                                      << "Thermal:" << thermalWidget;

    // If widgets not found, schedule another render job to retry
    if (!rgbWidget || !thermalWidget) {
        qCWarning(CustomVideoManagerLog) << "Widgets not ready yet, scheduling retry render job";
        _mainWindow->scheduleRenderJob(
            new FinishCustomVideoInitialization(this),
            QQuickWindow::BeforeSynchronizingStage
        );
        return;
    }

    // Widgets found - set up BOTH receivers sequentially in this same render job (like upstream)
    qCWarning(CustomVideoManagerLog) << "Both widgets found, initializing receivers sequentially in same render job";

    // Setup RGB receiver
    qCWarning(CustomVideoManagerLog) << "Setting up RGB receiver...";
    _setupReceiver(STREAM_RGB, rgbWidget);
    qCWarning(CustomVideoManagerLog) << "RGB receiver setup complete";

    // Setup Thermal receiver immediately after (no delay - same render job!)
    qCWarning(CustomVideoManagerLog) << "Setting up Thermal receiver...";
    _setupReceiver(STREAM_THERMAL, thermalWidget);
    qCWarning(CustomVideoManagerLog) << "Thermal receiver setup complete";

    // Don't start receivers here - wait for VIDEO_STREAM_INFORMATION with URIs
    qCWarning(CustomVideoManagerLog) << "Receivers initialized, waiting for VIDEO_STREAM_INFORMATION messages";
}


CustomVideoManager::~CustomVideoManager()
{
    qCWarning(CustomVideoManagerLog) << "CustomVideoManager destroyed";

    // Clean up receivers
    for (int i = 0; i < STREAM_COUNT; i++) {
        if (_streams[i].receiver) {
            _stopReceiver(i);
            if (_streams[i].sink) {
                QGCCorePlugin::instance()->releaseVideoSink(_streams[i].sink);
            }
            delete _streams[i].receiver;
        }
    }
}

void CustomVideoManager::init(QQuickWindow *mainWindow)
{

    if (_initialized) {
        qCWarning(CustomVideoManagerLog) << "CustomVideoManager already initialized";
        return;
    }
    if (!mainWindow) {
        qCCritical(CustomVideoManagerLog) << "init failed - mainWindow is NULL";
        return;
    }

    StreamNames = {
        {STREAM_RGB, "customRgbVideo"},
        {STREAM_THERMAL, "customThermalVideo"}
    };

    for (const auto& pair : StreamNames) {
        qCWarning(CustomVideoManagerLog) << "Stream index/name:" << pair.first << "/" << QString::fromStdString(pair.second);
    }

    _mainWindow = mainWindow;


    qCWarning(CustomVideoManagerLog) << "CustomVideoManager init - scheduling render job test map ended";

    // Schedule initialization on render thread (like upstream VideoManager does)
    _mainWindow->scheduleRenderJob(
        new FinishCustomVideoInitialization(this),
        QQuickWindow::BeforeSynchronizingStage
    );

    _initialized = true;
}

void CustomVideoManager::_setupReceiver(int streamIndex, QQuickItem* widget)
{
    if (streamIndex < 0 || streamIndex >= STREAM_COUNT) {
        qCWarning(CustomVideoManagerLog) << "Invalid stream index:" << streamIndex;
        return;
    }
    qCWarning(CustomVideoManagerLog) << "_setupReceiver called for stream" << streamIndex;
    // Create receiver
    VideoReceiver *receiver = QGCCorePlugin::instance()->createVideoReceiver(this);
    if (!receiver) {
        qCCritical(CustomVideoManagerLog) << "Failed to create receiver for stream" << streamIndex;
        return;
    }
    qCWarning(CustomVideoManagerLog) << "Receiver created for stream" << streamIndex;
    // Assign name
    receiver->setName(
        QString::fromStdString(StreamNames[streamIndex])
    );

    qCWarning(CustomVideoManagerLog) << "Setting widget for receiver of stream" << streamIndex;
    receiver->setWidget(widget);
    StreamInfo& stream = _streams[streamIndex];
    stream.receiver = receiver;

    // Create video sink directly (we're already on render thread from FinishCustomVideoInitialization)
    qCWarning(CustomVideoManagerLog) << "Creating video sink for stream" << streamIndex;
    void *sink = QGCCorePlugin::instance()->createVideoSink(widget, receiver);
    if (!sink) {
        qCCritical(CustomVideoManagerLog) << "createVideoSink failed for stream" << streamIndex;
    } else {
        qCWarning(CustomVideoManagerLog) << "Video sink created for stream" << streamIndex;
        stream.sink = sink;
        receiver->setSink(sink);
    }

    qCWarning(CustomVideoManagerLog) << "Connecting signals for stream" << streamIndex;
    // Connect signals (no duplicates)
    connect(stream.receiver, &VideoReceiver::streamingChanged, this,
        [this, streamIndex](bool active) {
            qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex << "streaming changed:" << active;
            _streams[streamIndex].active = active;
            emit streamStateChanged(streamIndex, active);
    });

    connect(stream.receiver, &VideoReceiver::decodingChanged, this,
        [this, streamIndex](bool decoding) {
            qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex << "decoding changed:" << decoding;
            _streams[streamIndex].decoding = decoding;
            emit streamDecodingChanged(streamIndex, decoding);
            
            // Stop the timeout timer if decoding started successfully
            if (decoding && _streams[streamIndex].decodingTimeoutTimer) {
                _streams[streamIndex].decodingTimeoutTimer->stop();
            }
    });

    connect(stream.receiver, &VideoReceiver::onStartComplete, this,
        [this, streamIndex](VideoReceiver::STATUS status) {
            qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex << "onStartComplete, status:" << status;
            if (status == VideoReceiver::STATUS_OK) {
                qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex << "starting decoding";
                _streams[streamIndex].receiver->startDecoding(_streams[streamIndex].sink);
                
                // Start a timer - if decoding doesn't start in 5 seconds, restart the stream
                if (!_streams[streamIndex].decodingTimeoutTimer) {
                    _streams[streamIndex].decodingTimeoutTimer = new QTimer(this);
                    _streams[streamIndex].decodingTimeoutTimer->setSingleShot(true);
                    connect(_streams[streamIndex].decodingTimeoutTimer, &QTimer::timeout, this, [this, streamIndex]() {
                        if (!_streams[streamIndex].decoding && _streams[streamIndex].receiver->started()) {
                            qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex << "decoding timeout - restarting to resync with RTP stream";
                            _restartVideo(streamIndex);
                        }
                    });
                }
                _streams[streamIndex].decodingTimeoutTimer->start(5000); // 5 second timeout
            } else {
                qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex << "start FAILED with status:" << status;
            }
    });

    (void) connect(stream.receiver, &VideoReceiver::onStopComplete, this, [this, streamIndex](VideoReceiver::STATUS status) {
        qCDebug(CustomVideoManagerLog) << "Stop complete" << _streams[streamIndex].receiver->name() << _streams[streamIndex].receiver->uri()  << ", status:" << status;
        _streams[streamIndex].receiver->setStarted(false);
        if (status == VideoReceiver::STATUS_INVALID_URL) {
            qCDebug(CustomVideoManagerLog) << "Invalid video URL. Not restarting";
        } else {
            QTimer::singleShot(1000, _streams[streamIndex].receiver, [this, streamIndex]() {
                qCDebug(CustomVideoManagerLog) << "Restarting video receiver" << _streams[streamIndex].receiver->name() << _streams[streamIndex].receiver->uri();
                _startReceiver(streamIndex);
            });
        }
    });

    // Log when receiver gets timeout
    connect(stream.receiver, &VideoReceiver::timeout, this,
        [this, streamIndex]() {
            qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex << "TIMEOUT - no data received";
    });

    qCWarning(CustomVideoManagerLog) << "Custom stream initialized:" << streamIndex << "URI:" << stream.uri;
}


void CustomVideoManager::_startReceiver(int streamIndex)
{
    qCWarning(CustomVideoManagerLog) << "_startReceiver called for stream" << streamIndex;

    if (streamIndex < 0 || streamIndex >= STREAM_COUNT) {
        qCWarning(CustomVideoManagerLog) << "Invalid stream index:" << streamIndex;
        return;
    }

    StreamInfo& stream = _streams[streamIndex];

    if (!stream.receiver) {
        qCWarning(CustomVideoManagerLog) << "No receiver for stream" << streamIndex << "- was _setupReceiver called?";
        return;
    }

    if (stream.receiver->started()) {
        qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex << "already started";
        return;
    }

    if (stream.uri.isEmpty()) {
        qCWarning(CustomVideoManagerLog) << "No URI set for stream" << streamIndex << "- URI is empty!";
        return;
    }

    qCWarning(CustomVideoManagerLog) << "Starting stream" << streamIndex << "URI:" << stream.uri
                                      << "receiver:" << stream.receiver
                                      << "sink:" << stream.sink;
    stream.receiver->setUri(stream.uri);
    stream.receiver->start(5000);  // 5 second timeout
    qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex << "start() called successfully";
}

void CustomVideoManager::_stopReceiver(int streamIndex)
{
    if (streamIndex < 0 || streamIndex >= STREAM_COUNT) {
        return;
    }

    StreamInfo& stream = _streams[streamIndex];
    if (stream.receiver && stream.receiver->started()) {
        qCWarning(CustomVideoManagerLog) << "Stopping stream" << streamIndex;
        stream.receiver->stop();
        qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex << "stop() called";
       stream.receiver->stop();
    }
}

void CustomVideoManager::startStream(int streamIndex)
{
    _startReceiver(streamIndex);
}

void CustomVideoManager::stopStream(int streamIndex)
{
    _stopReceiver(streamIndex);
}

void CustomVideoManager::_restartVideo(int streamIndex)
{
    StreamInfo& stream = _streams[streamIndex];
    qCDebug(CustomVideoManagerLog) << "Restart video receiver" << stream.receiver->name();

    if (!stream.receiver) {
        qCDebug(CustomVideoManagerLog) << "VideoReceiver is NULL";
        return;
    }

    if (stream.receiver->started()) {
        _stopReceiver(streamIndex);
        // onStopComplete Signal Will Restart It
    } else {
        _startReceiver(streamIndex);
    }
}

void CustomVideoManager::restartStream(int streamIndex)
{
    _stopReceiver(streamIndex);
    _startReceiver(streamIndex);
}

void CustomVideoManager::setStreamUri(int streamIndex, const QString& uri)
{
    qCWarning(CustomVideoManagerLog) << "setStreamUri called for stream" << streamIndex << "URI:" << uri;
    if (streamIndex < 0 || streamIndex >= STREAM_COUNT) {
        return;
    }

    qCDebug(CustomVideoManagerLog) << "Current URI for stream" << streamIndex << "is" << _streams[streamIndex].uri;
    // Only update if different
    if (_streams[streamIndex].uri == uri) {
        qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex << "URI unchanged, skipping";
        return;
    }

    qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex << "URI changing from" << _streams[streamIndex].uri << "to" << uri;
    
    // Update the URI first
    _streams[streamIndex].uri = uri;
    emit streamUriChanged(streamIndex, uri);  // Notify QML
    
    // If receiver is already running, stop it - onStopComplete will restart with new URI
    if (_streams[streamIndex].receiver && _streams[streamIndex].receiver->started()) {
        qCWarning(CustomVideoManagerLog) << "qqqqStream" << streamIndex << "is running, stopping to apply new URI";
        _stopReceiver(streamIndex);
        // onStopComplete handler will automatically restart with the new URI after 1 second
    } else {
        // Not running, start it now with the new URI
        qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex << "not running, starting with new URI";
        _startReceiver(streamIndex);
    }
}

QString CustomVideoManager::getStreamUri(int streamIndex) const
{
    if (streamIndex < 0 || streamIndex >= STREAM_COUNT) {
        return QString();
    }

    return _streams[streamIndex].uri;
}

bool CustomVideoManager::isStreamActive(int streamIndex) const
{
    if (streamIndex < 0 || streamIndex >= STREAM_COUNT) {
        return false;
    }

    return _streams[streamIndex].active;
}

bool CustomVideoManager::isStreamDecoding(int streamIndex) const
{
    if (streamIndex < 0 || streamIndex >= STREAM_COUNT) {
        return false;
    }

    return _streams[streamIndex].decoding;
}

bool CustomVideoManager::_updateVideoUri(VideoReceiver *receiver, const QString &uri)
{
    if (!receiver) {
        qCDebug(CustomVideoManagerLog) << "VideoReceiver is NULL";
        return false;
    }

    if ((uri == receiver->uri()) && !receiver->uri().isNull()) {
        return false;
    }

    qCDebug(CustomVideoManagerLog) << "New Video URI" << uri;

    receiver->setUri(uri);

    return true;
}
