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
#include "Vehicle.h"
#include "MultiVehicleManager.h"

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
    // Only run once to prevent infinite loop
    if (_initAfterQmlIsReadyDone) {
        qCWarning(CustomVideoManagerLog) << "_initAfterQmlIsReady called multiple times";
        return;
    }
    if (!_mainWindow) {
        qCCritical(CustomVideoManagerLog) << "_initAfterQmlIsReady called with NULL mainWindow";
        return;
    }
    _initAfterQmlIsReadyDone = true;

    qCWarning(CustomVideoManagerLog) << "_initAfterQmlIsReady - searching for video widgets";

    // Find widgets
    QQuickItem* rgbWidget = _mainWindow->findChild<QQuickItem*>("customRgbVideo");
    QQuickItem* thermalWidget = _mainWindow->findChild<QQuickItem*>("customThermalVideo");

    qCWarning(CustomVideoManagerLog) << "Found widgets - RGB:" << rgbWidget << "Thermal:" << thermalWidget;

    // Setup receivers
    _setupReceiver(STREAM_RGB, rgbWidget);
    _setupReceiver(STREAM_THERMAL, thermalWidget);

    qCWarning(CustomVideoManagerLog) << "Receivers initialized, waiting for VIDEO_STREAM_INFORMATION messages";
}

void CustomVideoManager::reinitializeWidgets()
{
    if (!_mainWindow) {
        qCCritical(CustomVideoManagerLog) << "reinitializeWidgets called with NULL mainWindow";
        return;
    }

    qCWarning(CustomVideoManagerLog) << "reinitializeWidgets - re-finding video widgets after mode change";

    // Find widgets (they may have been recreated by Loaders)
    QQuickItem* rgbWidget = _mainWindow->findChild<QQuickItem*>("customRgbVideo");
    QQuickItem* thermalWidget = _mainWindow->findChild<QQuickItem*>("customThermalVideo");

    qCWarning(CustomVideoManagerLog) << "Found widgets - RGB:" << rgbWidget << "Thermal:" << thermalWidget;

    // For each stream, update the widget reference and recreate sink without stopping receiver
    for (int i = 0; i < STREAM_COUNT; i++) {
        QQuickItem* newWidget = (i == STREAM_RGB) ? rgbWidget : thermalWidget;

        if (!newWidget) {
            qCWarning(CustomVideoManagerLog) << "Widget not found for stream" << i << "- may be hidden";
            continue;
        }

        if (!_streams[i].receiver) {
            qCWarning(CustomVideoManagerLog) << "No receiver for stream" << i << "- setting up new receiver";
            _setupReceiver(i, newWidget);
            continue;
        }

        bool wasDecoding = _streams[i].decoding;

        qCWarning(CustomVideoManagerLog) << "Stream" << i << "state before reinit: wasDecoding=" << wasDecoding;

        if (wasDecoding) {
            qCWarning(CustomVideoManagerLog) << "Stopping stream" << i << "before widget change";
            _streams[i].receiver->stopDecoding();
        }

        // Release old sink
        if (_streams[i].sink) {
            qCWarning(CustomVideoManagerLog) << "Releasing old sink for stream" << i;
            QGCCorePlugin::instance()->releaseVideoSink(_streams[i].sink);
            _streams[i].sink = nullptr;
        }

        // Update widget reference
        qCWarning(CustomVideoManagerLog) << "Updating widget for stream" << i;
        _streams[i].receiver->setWidget(newWidget);

        // Create new sink
        qCWarning(CustomVideoManagerLog) << "Creating new sink for stream" << i;
        void *sink = QGCCorePlugin::instance()->createVideoSink(newWidget, _streams[i].receiver);
        if (!sink) {
            qCCritical(CustomVideoManagerLog) << "Failed to create sink for stream" << i;
        } else {
            qCWarning(CustomVideoManagerLog) << "New sink created for stream" << i;
            _streams[i].sink = sink;
            _streams[i].receiver->setSink(sink);
        }

        // If stream was decoding, restart it after GStreamer finishes stopping
        if (wasDecoding && _streams[i].sink) {
            qCWarning(CustomVideoManagerLog) << "Stream" << i 
                                              << "needs restart - setting up deferred check";
            
            // Use QTimer::singleShot to check on next event loop iteration
            // This ensures all pending decodingChanged signals have been processed
            QTimer::singleShot(0, this, [this, i]() {
                if (!_streams[i].sink) {
                    qCWarning(CustomVideoManagerLog) << "Stream" << i 
                                                      << "sink gone, cannot restart";
                    return;
                }
                
                if (!_streams[i].decoding) {
                    // Already stopped, restart now
                    qCWarning(CustomVideoManagerLog) << "Stream" << i 
                                                      << "already stopped, restarting immediately";
                    _streams[i].receiver->startDecoding(_streams[i].sink);
                } else {
                    // Still decoding, wait for signal
                    qCWarning(CustomVideoManagerLog) << "Stream" << i 
                                                      << "still decoding - waiting for stop signal";
                    
                    QMetaObject::Connection* conn = new QMetaObject::Connection();
                    *conn = connect(_streams[i].receiver, &VideoReceiver::decodingChanged, this, 
                        [this, i, conn](bool decoding) {
                            qCWarning(CustomVideoManagerLog) << "Stream" << i 
                                                              << "decodingChanged lambda fired: decoding=" 
                                                              << decoding;
                            
                            if (!decoding && _streams[i].sink) {
                                qCWarning(CustomVideoManagerLog) << "Stream" << i 
                                                                  << "decoding stopped, restarting now";
                                _streams[i].receiver->startDecoding(_streams[i].sink);
                                
                                QObject::disconnect(*conn);
                                delete conn;
                            }
                    });
                }
            });
        } else {
            qCWarning(CustomVideoManagerLog) << "Stream" << i 
                                              << "will not restart: wasDecoding=" << wasDecoding 
                                              << "sink=" << _streams[i].sink;
        }
    }

    qCWarning(CustomVideoManagerLog) << "Widget reinitialization complete";
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

    connect(MultiVehicleManager::instance(), 
           &MultiVehicleManager::activeVehicleChanged,
           this, 
           &CustomVideoManager::_setActiveVehicle);


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
    if (!widget) {
        qCWarning(CustomVideoManagerLog) << "Widget is NULL for stream" << streamIndex << "- skipping setup";
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
                
                // Start a timer - if decoding doesn't start in 10 seconds, restart
                if (!_streams[streamIndex].decodingTimeoutTimer) {
                    _streams[streamIndex].decodingTimeoutTimer = new QTimer(this);
                    _streams[streamIndex].decodingTimeoutTimer->setSingleShot(true);
                    connect(_streams[streamIndex].decodingTimeoutTimer, &QTimer::timeout, this, [this, streamIndex]() {
                        // Only restart if receiver is still started but not decoding
                        if (!_streams[streamIndex].decoding && 
                            _streams[streamIndex].receiver->started() &&
                            !_streams[streamIndex].uri.isEmpty()) {  // ← Check URI not empty
                            qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex 
                                                            << "decoding timeout after 10s - restarting";
                            _restartVideo(streamIndex);
                        }
                    });
                }
                _streams[streamIndex].decodingTimeoutTimer->start(10000); // 10 second timeout
            } else {
                qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex << "start FAILED with status:" << status;
            }
    });

    (void) connect(stream.receiver, &VideoReceiver::onStopComplete, this, [this, streamIndex](VideoReceiver::STATUS status) {
        qCDebug(CustomVideoManagerLog) << "Stop complete" << _streams[streamIndex].receiver->name() << _streams[streamIndex].receiver->uri()  << ", status:" << status;
        _streams[streamIndex].receiver->setStarted(false);
        if (status == VideoReceiver::STATUS_INVALID_URL) {
            qCDebug(CustomVideoManagerLog) << "Invalid video URL. Not restarting";
        } else if (!_streams[streamIndex].allowAutoRestart) {
            qCDebug(CustomVideoManagerLog) << "Auto-restart disabled for stream" << streamIndex;
            _streams[streamIndex].allowAutoRestart = true;  // Re-enable for next time
        } else if (_streams[streamIndex].uri.isEmpty()) {
            qCDebug(CustomVideoManagerLog) << "URI is empty, not restarting stream" << streamIndex;
        } else {
            QTimer::singleShot(1000, _streams[streamIndex].receiver, [this, streamIndex]() {
                qCDebug(CustomVideoManagerLog) << "Restarting video receiver" 
                                                << _streams[streamIndex].receiver->name() 
                                                << _streams[streamIndex].receiver->uri();
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

    QString currentUri = _streams[streamIndex].uri;

    qCDebug(CustomVideoManagerLog) << "Current URI for stream" << streamIndex << "is" << _streams[streamIndex].uri;
    // Only update if different
    if (currentUri == uri) {
        qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex << "URI unchanged, skipping";
        return;
    }

    if (!currentUri.isEmpty() && currentUri != uri) {
        qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex 
                                          << "URI changing from" << currentUri 
                                          << "to" << uri;
        
        if (_streams[streamIndex].receiver && _streams[streamIndex].receiver->started()) {
            qCWarning(CustomVideoManagerLog) << "Stopping stream" << streamIndex 
                                              << "before URI change";
            stopStream(streamIndex);
            
            // Give GStreamer time to fully stop
            // This is critical for clean pipeline teardown
            QThread::msleep(200);
        }
    }
    
    // Update URI
    _streams[streamIndex].uri = uri;
    emit streamUriChanged(streamIndex, uri);  // Notify QML
    qCWarning(CustomVideoManagerLog) << "Updated internal URI for stream" << streamIndex;

    // Start stream if URI is valid
    if (!uri.isEmpty()) {
        qCWarning(CustomVideoManagerLog) << "Starting stream" << streamIndex 
                                          << "with new URI";
        startStream(streamIndex);
    } else {
        qCWarning(CustomVideoManagerLog) << "URI is empty, not starting stream" << streamIndex;
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

void CustomVideoManager::clearAllStreamInfo()
{   
    for (int i = 0; i < STREAM_COUNT; i++) {
        QString oldUri = _streams[i].uri;
        
        // Disable auto-restart during cleanup
        _streams[i].allowAutoRestart = false;
        
        // Stop receiver if running
        if (_streams[i].receiver && _streams[i].receiver->started()) {
            qCDebug(CustomVideoManagerLog) << "Stopping stream" << i << "during cleanup";
            _streams[i].receiver->stop();
        }
        
        // Stop decoding timeout timer
        if (_streams[i].decodingTimeoutTimer) {
            _streams[i].decodingTimeoutTimer->stop();
        }
        
        // Clear state
        _streams[i].uri.clear();
        _streams[i].active = false;
        _streams[i].decoding = false;
        
        // Clear video stream info
        if (_streams[i].receiver) {
            _streams[i].receiver->setVideoStreamInfo(nullptr);
        }
        
        qCWarning(CustomVideoManagerLog) << "Stream" << i 
                                          << "URI cleared: WAS" << oldUri 
                                          << "NOW" << _streams[i].uri;
    }
}

void CustomVideoManager::_setActiveVehicle(Vehicle* vehicle)
{
    if (_activeVehicle) {
        qCDebug(CustomVideoManagerLog) << "Cleaning up previous vehicle";
        
        // 1. Disconnect signals
        disconnect(_activeVehicle->vehicleLinkManager(), nullptr, this, nullptr);
        
        // 2. Clear VideoStreamInfo IMMEDIATELY (upstream pattern)
        for (int i = 0; i < STREAM_COUNT; i++) {
            if (_streams[i].receiver) {
                _streams[i].receiver->setVideoStreamInfo(nullptr);
            }
        }
        
        // 3. Stop streams (now safe)
        for (int i = 0; i < STREAM_COUNT; i++) {
            if (_streams[i].receiver && _streams[i].receiver->started()) {
                _streams[i].receiver->stop();
            }
        }
        
        // 4. Clear state
        clearAllStreamInfo();
    }
    
    _activeVehicle = vehicle;
    
    if (_activeVehicle) {
        qCDebug(CustomVideoManagerLog) << "Set active vehicle" << vehicle;
        connect(_activeVehicle->vehicleLinkManager(), 
               &VehicleLinkManager::communicationLostChanged, 
               this, 
               &CustomVideoManager::_communicationLostChanged);
    }
}

void CustomVideoManager::_communicationLostChanged(bool communicationLost)
{
    if (communicationLost) {
        qCDebug(CustomVideoManagerLog) << "Communication lost";
        
        // Clear VideoStreamInfo FIRST (before stopping)
        for (int i = 0; i < STREAM_COUNT; i++) {
            if (_streams[i].receiver) {
                _streams[i].receiver->setVideoStreamInfo(nullptr);
            }
        }
        
        // Then stop
        for (int i = 0; i < STREAM_COUNT; i++) {
            if (_streams[i].receiver && _streams[i].receiver->started()) {
                _streams[i].receiver->stop();
            }
        }
        
        clearAllStreamInfo();
    }
}