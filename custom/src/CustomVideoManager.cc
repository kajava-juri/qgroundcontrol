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
#include "CustomPlugin.h"
#include "DataCollectionController.h"
#include "GstVideoReceiver.h"
#include "GStreamerHelpers.h"
#include <gst/gst.h>
#include <QColor>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

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

    QQuickItem* bottomRightWidget = _mainWindow->findChild<QQuickItem*>("customBottomRightVideo");
    QQuickItem* topRightWidget = _mainWindow->findChild<QQuickItem*>("customTopRightVideo");

    _streamWidgets[STREAM_BR] = bottomRightWidget;
    _streamWidgets[STREAM_TR] = topRightWidget;

    qCWarning(CustomVideoManagerLog) << "Found widgets - RGB:" << bottomRightWidget << "Thermal:" << topRightWidget;

    _setupReceiver(STREAM_BR, _streamWidgets[STREAM_BR]);
    _setupReceiver(STREAM_TR, _streamWidgets[STREAM_TR]);

    qCWarning(CustomVideoManagerLog) << "Receivers initialized, waiting for VIDEO_STREAM_INFORMATION messages";
}

void CustomVideoManager::reinitializeWidgets(bool gridMode)
{
    if (!_mainWindow) {
        qCCritical(CustomVideoManagerLog) << "reinitializeWidgets called with NULL mainWindow";
        return;
    }

    qCWarning(CustomVideoManagerLog) << "reinitializeWidgets - re-finding video widgets, gridMode:" << gridMode;

    QQuickItem* bottomRightWidget = nullptr;
    QQuickItem* topRightWidget = nullptr;

    if (gridMode) {
        // In grid mode, search within the gridView component
        QQuickItem* gridView = _mainWindow->findChild<QQuickItem*>("gridView");
        if (gridView) {
            qCWarning(CustomVideoManagerLog) << "Searching for widgets in gridView";
            bottomRightWidget = gridView->findChild<QQuickItem*>("customBottomRightVideo");
            topRightWidget = gridView->findChild<QQuickItem*>("customTopRightVideo");
        } else {
            qCWarning(CustomVideoManagerLog) << "gridView not found, falling back to global search";
            bottomRightWidget = _mainWindow->findChild<QQuickItem*>("customBottomRightVideo");
            topRightWidget = _mainWindow->findChild<QQuickItem*>("customTopRightVideo");
        }
    } else {
        // In overlay mode, search globally (will find PipView widgets)
        bottomRightWidget = _mainWindow->findChild<QQuickItem*>("customBottomRightVideo");
        topRightWidget = _mainWindow->findChild<QQuickItem*>("customTopRightVideo");
    }

    qCWarning(CustomVideoManagerLog) << "Found widgets - RGB:" << bottomRightWidget << "Thermal:" << topRightWidget;

    for (int i = 0; i < STREAM_COUNT; i++) {
        QQuickItem* newWidget = _streamWidgets[i];

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

        // Check if data collection is running
        CustomPlugin* plugin = qobject_cast<CustomPlugin*>(QGCCorePlugin::instance());
        bool dataCollectionRunning = false;
        if (plugin && plugin->dataCollectionController()) {
            dataCollectionRunning = plugin->dataCollectionController()->isCollecting();
        }

        if (!wasDecoding && dataCollectionRunning && !_streams[i].uri.isEmpty()) {
            qCWarning(CustomVideoManagerLog) << "Stream" << i 
                                              << "data collection active but not decoding - will restart";
            wasDecoding = true;
        }

        // Stop decoding if currently active
        if (_streams[i].decoding) {
            qCWarning(CustomVideoManagerLog) << "Stopping decoding for stream" << i;
            _streams[i].receiver->stopDecoding();
        }

        // Disconnect old restart connection
        if (_streams[i].restartConnection) {
            qCWarning(CustomVideoManagerLog) << "Disconnecting previous restart connection for stream" << i;
            QObject::disconnect(_streams[i].restartConnection);
            _streams[i].restartConnection = QMetaObject::Connection();
        }

        // Capture old sink for cleanup
        void* oldSink = _streams[i].sink;
        bool needsCleanup = (_streams[i].decoding && oldSink != nullptr);
        
        // Clear sink reference immediately (will be recreated below)
        _streams[i].sink = nullptr;

        qCWarning(CustomVideoManagerLog) << "Updating widget for stream" << i;
        _streams[i].receiver->setWidget(newWidget);

        qCWarning(CustomVideoManagerLog) << "Creating new sink for stream" << i;
        void *newSink = QGCCorePlugin::instance()->createVideoSink(newWidget, _streams[i].receiver);
        if (!newSink) {
            qCCritical(CustomVideoManagerLog) << "Failed to create sink for stream" << i;
            continue;
        }
        
        qCWarning(CustomVideoManagerLog) << "New sink created for stream" << i;
        _streams[i].sink = newSink;
        _streams[i].receiver->setSink(newSink);

        // Set up COMBINED cleanup + restart connection
        // old method of connecting to only release video sink caused race conditions
        if (wasDecoding && newSink && !_streams[i].uri.isEmpty()) {
            qCWarning(CustomVideoManagerLog) << "Stream" << i 
                                            << "setting up combined cleanup/restart connection";
            
            _streams[i].restartConnection = connect(
                _streams[i].receiver, &VideoReceiver::decodingChanged, this,
                [this, i, oldSink, needsCleanup](bool decoding) {
                    qCWarning(CustomVideoManagerLog) << "Stream" << i 
                                                    << "decodingChanged (combined lambda): " << decoding;
                    
                    if (!decoding) {
                        // Cleanup
                        if (needsCleanup && oldSink) {
                            qCWarning(CustomVideoManagerLog) << "Stream" << i 
                                                            << "releasing old sink";
                            QGCCorePlugin::instance()->releaseVideoSink(oldSink);
                        }
                        
                        // Restart
                        if (_streams[i].sink && !_streams[i].uri.isEmpty()) {
                            qCWarning(CustomVideoManagerLog) << "Stream" << i 
                                                            << "restarting decoding";
                            _streams[i].receiver->startDecoding(_streams[i].sink);
                        }
                        
                        // Cleanup connection
                        QObject::disconnect(_streams[i].restartConnection);
                        _streams[i].restartConnection = QMetaObject::Connection();
                    }
                }, Qt::DirectConnection  // ← Use Direct instead of Auto!
            );
            
            //Use QTimer to check state AFTER event loop processes, sometimes decodingChanged signal is not emitted (idk why)
            QTimer::singleShot(0, this, [this, i, oldSink, needsCleanup, newSink]() {
                // Check if decoding stopped while we were setting up the connection
                if (!_streams[i].decoding && _streams[i].sink && !_streams[i].uri.isEmpty()) {
                    qCWarning(CustomVideoManagerLog) << "Stream" << i 
                                                    << "ALREADY stopped (missed signal) - handling immediately";
                    
                    // Cleanup
                    if (needsCleanup && oldSink) {
                        qCWarning(CustomVideoManagerLog) << "Stream" << i 
                                                        << "releasing old sink immediately";
                        QGCCorePlugin::instance()->releaseVideoSink(oldSink);
                    }
                    
                    // Restart
                    qCWarning(CustomVideoManagerLog) << "Stream" << i 
                                                    << "restarting immediately";
                    _streams[i].receiver->startDecoding(newSink);
                    
                    // Cleanup connection since we handled it manually
                    QObject::disconnect(_streams[i].restartConnection);
                    _streams[i].restartConnection = QMetaObject::Connection();
                } else {
                    qCWarning(CustomVideoManagerLog) << "Stream" << i 
                                                    << "still decoding or waiting for signal: decoding=" 
                                                    << _streams[i].decoding;
                }
            });
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
        {STREAM_BR, "customBottomRightVideo"},
        {STREAM_TR, "customTopRightVideo"},
        {STREAM_DRONE_CAMERA, "customDroneReplayVideo"}
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
            
            // Stop the timeout timer and reset backoff counter if decoding started successfully
            if (decoding) {
                if (_streams[streamIndex].decodingTimeoutTimer) {
                    _streams[streamIndex].decodingTimeoutTimer->stop();
                }
                _streams[streamIndex].restartAttempts = 0;  // Reset backoff on success
                qCDebug(CustomVideoManagerLog) << "Stream" << streamIndex << "decoding successfully - reset restart counter";
            }
    });

    connect(stream.receiver, &VideoReceiver::onStartComplete, this,
        [this, streamIndex](VideoReceiver::STATUS status) {
            qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex << "onStartComplete, status:" << status;
            if (status == VideoReceiver::STATUS_OK) {
                qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex << "starting decoding";
                _streams[streamIndex].receiver->startDecoding(_streams[streamIndex].sink);
                
                // Start a timer - if decoding doesn't start in 30 seconds, restart
                if (!_streams[streamIndex].decodingTimeoutTimer) {
                    _streams[streamIndex].decodingTimeoutTimer = new QTimer(this);
                    _streams[streamIndex].decodingTimeoutTimer->setSingleShot(true);
                    connect(_streams[streamIndex].decodingTimeoutTimer, &QTimer::timeout, this, [this, streamIndex]() {
                        // Only restart if receiver is still started but not decoding
                        if (!_streams[streamIndex].decoding && 
                            _streams[streamIndex].receiver->started() &&
                            !_streams[streamIndex].uri.isEmpty()) {  // ← Check URI not empty
                            qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex 
                                                            << "decoding timeout after 30s - restarting";
                            _restartVideo(streamIndex);
                        }
                    });
                }
                _streams[streamIndex].decodingTimeoutTimer->start(30000); // 30 second timeout for noisy WiFi
            } else {
                qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex << "start FAILED with status:" << status;
            }
    });

    (void) connect(stream.receiver, &VideoReceiver::onStopComplete, this, [this, streamIndex](VideoReceiver::STATUS status) {
        qCDebug(CustomVideoManagerLog) << "Stop complete" << _streams[streamIndex].receiver->name() << _streams[streamIndex].receiver->uri()  << ", status:" << status;
        StopReason reason = _streams[streamIndex].pendingStopReason;
        _streams[streamIndex].pendingStopReason = StopReason::None;
        _streams[streamIndex].receiver->setStarted(false);
        if (status == VideoReceiver::STATUS_INVALID_URL) {
            qCDebug(CustomVideoManagerLog) << "Invalid video URL. Not restarting";
            _streams[streamIndex].restartAttempts = 0;  // Reset on invalid URL
            return;
        }

        if(reason == StopReason::CollectionEnd || reason == StopReason::CommLost || reason == StopReason::ReplayModeEnter) {
            qCDebug(CustomVideoManagerLog) << "Stream stopped intentionally, reason:" << (int)reason << ". Not restarting.";
            _streams[streamIndex].restartAttempts = 0;  // Reset on intentional stop
            return;
        }

        if (_streams[streamIndex].uri.isEmpty()) {
            qCDebug(CustomVideoManagerLog) << "URI is empty, not restarting stream" << streamIndex;
            _streams[streamIndex].restartAttempts = 0;  // Reset counter
            return;
        } 

        if (reason == StopReason::UriChange || reason == StopReason::WidgetReinit) {
            return;
        }
        
        // Exponential backoff: 2s, 4s, 8s, 16s, max 30s
        _streams[streamIndex].restartAttempts++;
        int delayMs = qMin(2000 * (1 << (_streams[streamIndex].restartAttempts - 1)), 30000);
        qCDebug(CustomVideoManagerLog) << "Scheduling restart for stream" << streamIndex 
                                        << "attempt" << _streams[streamIndex].restartAttempts
                                        << "after" << delayMs << "ms";
        QTimer::singleShot(delayMs, _streams[streamIndex].receiver, [this, streamIndex]() {
            qCDebug(CustomVideoManagerLog) << "Restarting video receiver" 
                                            << _streams[streamIndex].receiver->name() 
                                            << _streams[streamIndex].receiver->uri();
            _startReceiver(streamIndex);
        });

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
    stream.receiver->start(30000);  // 30 second timeout for noisy WiFi
    qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex << "start() called successfully";
}

void CustomVideoManager::_stopReceiver(int streamIndex)
{
    if (streamIndex < 0 || streamIndex >= STREAM_COUNT) {
        return;
    }

    StreamInfo& stream = _streams[streamIndex];
    if (!stream.receiver) {
        qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex << "has no receiver, skipping stop";
        return;
    }
    
    bool wasStarted = stream.receiver->started();
    qCWarning(CustomVideoManagerLog) << "Stopping stream" << streamIndex 
                                      << "wasStarted:" << wasStarted
                                      << "URI:" << stream.uri;
    
    if (stream.decodingTimeoutTimer) {
        stream.decodingTimeoutTimer->stop();
    }
    
    // Call stop() regardless of started() state to ensure cleanup
    // The receiver's stop() should be idempotent
    stream.receiver->stop();
    qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex << "stop() called";
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

    StreamInfo& stream = _streams[streamIndex];
    const QString currentUri = stream.uri;

    qCDebug(CustomVideoManagerLog) << "Current URI for stream" << streamIndex << "is" << stream.uri;
    
    if (currentUri == uri) {
        qCDebug(CustomVideoManagerLog) << "Stream" << streamIndex << "URI unchanged, no action";
        return;
    }

    if (!currentUri.isEmpty() && currentUri != uri) {
        qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex 
                                          << "URI changing from" << currentUri 
                                          << "to" << uri;
        
        if (stream.receiver && stream.receiver->started()) {
            qCWarning(CustomVideoManagerLog) << "Stopping stream" << streamIndex 
                                              << "before URI change";
            
            // Stop receiver first and only then update URI to keep state transitions explicit.
            stream.pendingStopReason = StopReason::UriChange;
            stopStream(streamIndex);
            
            // Wait for stop to complete before updating URI
            // Use single-shot connection to onStopComplete
            QMetaObject::Connection* conn = new QMetaObject::Connection();
            *conn = connect(stream.receiver, &VideoReceiver::onStopComplete, this,
                [this, streamIndex, uri, conn](VideoReceiver::STATUS status) {
                    Q_UNUSED(status);
                    qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex 
                                                      << "stopped, now updating URI to" << uri;
                    disconnect(*conn);
                    delete conn;
                    
                    // Update URI and wait for vid_ready signal to start.
                    _streams[streamIndex].uri = uri;
                    _streams[streamIndex].ready = false;
                    emit streamUriChanged(streamIndex, uri);

                    qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex
                                                     << "URI updated, waiting for vid_ready to start";
                });
            return;  // Exit early - continuation happens in callback
        }
    }
    
    // Update URI
    stream.uri = uri;
    stream.ready = false;
    emit streamUriChanged(streamIndex, uri);  // Notify QML
    qCWarning(CustomVideoManagerLog) << "Updated internal URI for stream" << streamIndex;

    if (!uri.isEmpty()) {
        qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex
                                         << "URI set, waiting for vid_ready to start";
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

void CustomVideoManager::setStreamId(int streamIndex, int streamId)
{
    if (streamIndex < 0 || streamIndex >= STREAM_COUNT) {
        return;
    }

    _streams[streamIndex].streamId = streamId;
}

int CustomVideoManager::getStreamId(int streamIndex) const
{
    if (streamIndex < 0 || streamIndex >= STREAM_COUNT) {
        return -1;
    }

    return _streams[streamIndex].streamId;
}

void CustomVideoManager::setStreamReady(int streamIndex, bool ready)
{
    if (streamIndex < 0 || streamIndex >= STREAM_COUNT) {
        return;
    }

    _streams[streamIndex].ready = ready;
}

bool CustomVideoManager::isStreamReady(int streamIndex) const
{
    if (streamIndex < 0 || streamIndex >= STREAM_COUNT) {
        return false;
    }

    return _streams[streamIndex].ready;
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
        // _streams[i].allowAutoRestart = false;
        _streams[i].pendingStopReason = StopReason::CollectionEnd;
        
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
                _streams[i].pendingStopReason = StopReason::CommLost;  // Set reason to prevent auto-restart
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

bool CustomVideoManager::enterReplayMode(const QString& rgbVideoPath, const QString& thermalVideoPath,
                                          const QString& droneVideoPath,
                                          qint64 rgbOffsetMs, qint64 thermalOffsetMs, qint64 droneOffsetMs)
{
    VideoStreamMetadata rgbStream;
    rgbStream.videoPath = rgbVideoPath;
    rgbStream.offsetMs = rgbOffsetMs;

    VideoStreamMetadata thermalStream;
    thermalStream.videoPath = thermalVideoPath;
    thermalStream.offsetMs = thermalOffsetMs;

    VideoStreamMetadata droneStream;
    droneStream.videoPath = droneVideoPath;
    droneStream.offsetMs = droneOffsetMs;

    return enterReplayMode(rgbStream, thermalStream, droneStream);
}

bool CustomVideoManager::enterReplayMode(const VideoStreamMetadata& rgbStream, const VideoStreamMetadata& thermalStream)
{
    return enterReplayMode(rgbStream, thermalStream, VideoStreamMetadata());
}

bool CustomVideoManager::enterReplayMode(const VideoStreamMetadata& rgbStream, const VideoStreamMetadata& thermalStream, const VideoStreamMetadata& droneCameraStream)
{
    if (_replay.active) {
        qCWarning(CustomVideoManagerLog) << "Already in replay mode, exiting first";
        exitReplayMode();
    }

    const std::array<VideoStreamMetadata, REPLAY_STREAM_COUNT> streamMetas = {rgbStream, thermalStream, droneCameraStream};

    qCDebug(CustomVideoManagerLog) << "Entering replay mode";
    // qCDebug(CustomVideoManagerLog) << "  RGB:" << streamMetas[STREAM_RGB].videoPath << "offset:" << streamMetas[STREAM_RGB].offsetMs << "ms";
    // qCDebug(CustomVideoManagerLog) << "  Thermal:" << streamMetas[STREAM_THERMAL].videoPath << "offset:" << streamMetas[STREAM_THERMAL].offsetMs << "ms";
    // qCDebug(CustomVideoManagerLog) << "  Drone camera:" << streamMetas[STREAM_DRONE_CAMERA].videoPath << "offset:" << streamMetas[STREAM_DRONE_CAMERA].offsetMs << "ms";

    // Stop all live streams - disable auto-restart so they don't fight us
    for (int i = 0; i < STREAM_COUNT; i++) {
        // _streams[i].allowAutoRestart = false;
        _streams[i].pendingStopReason = StopReason::ReplayModeEnter;
        if (_streams[i].receiver && _streams[i].receiver->started()) {
            _streams[i].receiver->stop();
        }
    }

    qCDebug(CustomVideoManagerLog) << "Replay mode activated - notifying QML";
    emit replayModeChanged(true);

    _replay.playbackSpeed  = 1.0;

    for (int i = 0; i < REPLAY_STREAM_COUNT; i++) {
        qCDebug(CustomVideoManagerLog) << "  " << StreamNames[i].c_str() << ": " << streamMetas[i].videoPath << "offset:" << streamMetas[i].offsetMs << "ms";
        // Skip if no video path provided
        if (streamMetas[i].videoPath.isEmpty()) {
            qCDebug(CustomVideoManagerLog) << "Skipping stream" << i << "- no video path";
            continue;
        }
        
        qCDebug(CustomVideoManagerLog) << "Setting up widget replay stream" << StreamNames[i] << "with video path:" << streamMetas[i].videoPath;
        QQuickItem* widget = _mainWindow->findChild<QQuickItem*>(
            QString::fromStdString(StreamNames[i])
        );
        if (!widget) {
            qCWarning(CustomVideoManagerLog) << "Widget not found for stream" << i << "- skipping";
            continue;
        }
        qCDebug(CustomVideoManagerLog) << "Replay stream" << i << "widget state:"
                                       << "objectName=" << widget->objectName()
                                       << "visible=" << widget->isVisible()
                                       << "size=" << widget->width() << "x" << widget->height();

        if (!_openReplayStream(i, streamMetas[i], widget)) {
            qCWarning(CustomVideoManagerLog) << "Failed to open replay stream" << i << "- skipping";
            continue;
        }
        
        // Store offset
        _replay.streams[i].offsetMs = streamMetas[i].offsetMs;
        
        // If offset is negative (video starts after tlog), mark as not ready
        if (streamMetas[i].offsetMs < 0) {
            _replay.streams[i].readyToPlay = false;
            qCWarning(CustomVideoManagerLog) << "Stream" << i << "starts" << (-streamMetas[i].offsetMs)
                                              << "ms after tlog - will delay playback";
        } else {
            _replay.streams[i].readyToPlay = true;
        }
    }

    _replay.active = true;
    _replay.currentTlogTimeSecs = 0;

    // Create timer for checking delayed videos (100ms = 10Hz for sub-second precision)
    if (!_replay.delayedVideoTimer) {
        _replay.delayedVideoTimer = new QTimer(this);
        _replay.delayedVideoTimer->setInterval(100);  // 100ms checks
        connect(_replay.delayedVideoTimer, &QTimer::timeout, this, &CustomVideoManager::_checkDelayedVideos);
    }
    
    // Start timer if any videos have negative offsets
    bool hasDelayedVideos = false;
    for (int i = 0; i < REPLAY_STREAM_COUNT; i++) {
        if (_replay.streams[i].loaded && _replay.streams[i].offsetMs < 0) {
            hasDelayedVideos = true;
            break;
        }
    }
    if (hasDelayedVideos) {
        _replay.delayedVideoTimer->start();
        qCDebug(CustomVideoManagerLog) << "Started delayed video monitoring timer (100ms)";
    }

    // Start all streams PAUSED at initial offset position - wait for user to press play
    for (int i = 0; i < REPLAY_STREAM_COUNT; i++) {
        if (_replay.streams[i].pipeline) {
            qCDebug(CustomVideoManagerLog) << "Preparing replay stream" << i;
            
            // Seek to offset position (handles negative offsets by staying at 0)
            qint64 initialPosMs = qMax(static_cast<qint64>(0), _replay.streams[i].offsetMs);
            gst_element_set_state(_replay.streams[i].pipeline, GST_STATE_PAUSED);
            
            if (initialPosMs > 0) {
                gst_element_seek_simple(_replay.streams[i].pipeline, GST_FORMAT_TIME,
                    static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
                    initialPosMs * GST_MSECOND);
                qCDebug(CustomVideoManagerLog) << "Stream" << i << "seeked to initial position" << initialPosMs << "ms";
            }
            
            // Keep PAUSED - will start playing when user presses play button
            qCDebug(CustomVideoManagerLog) << "Stream" << i << "ready at" << _replay.streams[i].offsetMs << "ms offset, waiting for playback start";
        }
    }

    // for (int i = 0; i < REPLAY_STREAM_COUNT; i++) {
    //     if (_replay.streams[i].loaded && _replay.streams[i].receiver) {
    //         // Manually update state and emit signals
    //         _streams[i].active = true;
    //         _streams[i].decoding = true;
    //         emit streamStateChanged(i, true);
    //         emit streamDecodingChanged(i, true);
    //     }
    // }

    return true;
}

GstElement* CustomVideoManager::_getVideoFilePipeline(const QString& videoPath, void* sink, int streamIndex)
{
    GstElement* pipeline = gst_element_factory_make("playbin", nullptr);
    if (!pipeline) {
        qCCritical(CustomVideoManagerLog) << "Failed to create playbin for stream" << streamIndex;
        return nullptr;
    }

    // file:// URI - must be absolute path
    QString uri = "file://" + videoPath;
    g_object_set(pipeline, "uri", uri.toUtf8().constData(), nullptr);

    // Point playbin at our existing GL sink
    // sink here is a GstElement* wrapped in void* by QGC's plugin API
    g_object_set(pipeline, "video-sink", static_cast<GstElement*>(sink), nullptr);

    // Suppress audio (no audio in drone video files)
    GstElement* fakesink = gst_element_factory_make("fakesink", nullptr);
    if (fakesink) {
        g_object_set(pipeline, "audio-sink", fakesink, nullptr);
    }

    return pipeline;
}

GstElement* CustomVideoManager::_getVideoFramesPipeline(const VideoStreamMetadata& streamMeta, void* sink, int streamIndex)
{
    GstElement* pipeline = gst_pipeline_new(nullptr);
    GstElement* src = gst_element_factory_make("appsrc", nullptr);
    GstElement* jpegDec = gst_element_factory_make("jpegdec", nullptr);
    GstElement* videoConvert = gst_element_factory_make("videoconvert", nullptr);
    GstElement* queue = gst_element_factory_make("queue", nullptr);
    GstElement* videoSink = static_cast<GstElement*>(sink);

    if (!pipeline || !src || !jpegDec || !videoConvert || !queue || !videoSink) {
        qCCritical(CustomVideoManagerLog) << "Failed to create frame replay pipeline elements for stream" << streamIndex;
        if (pipeline) {
            gst_object_unref(pipeline);
        }
        return nullptr;
    }

    qint64 streamDurationNs = 0;
    if (streamMeta.frameMetadata.size() > 1) {
        const quint64 firstTsNs = streamMeta.frameMetadata.first().timestampNs;
        const quint64 lastTsNs = streamMeta.frameMetadata.last().timestampNs;
        if (lastTsNs > firstTsNs) {
            streamDurationNs = static_cast<qint64>(lastTsNs - firstTsNs);
        }
    }

    g_object_set(src,
                 "format", GST_FORMAT_TIME,
                 "is-live", FALSE,
                 "do-timestamp", FALSE,
                 // 1 == seekable appsrc stream
                 "stream-type", 1,
                 "size", static_cast<gint64>(streamDurationNs),
                 "duration", static_cast<gint64>(streamDurationNs),
                 // High-res JPG replay can exceed default appsrc queue thresholds.
                 // Keep push-buffer non-blocking and raise queue budget to avoid stalls.
                 "block", FALSE,
                 "max-bytes", static_cast<guint64>(16 * 1024 * 1024),
                 nullptr);

    (void) g_signal_connect(src, "need-data", G_CALLBACK(_onReplayAppSrcNeedData),
                            reinterpret_cast<gpointer>(static_cast<intptr_t>(streamIndex)));
    (void) g_signal_connect(src, "seek-data", G_CALLBACK(_onReplayAppSrcSeekData),
                            reinterpret_cast<gpointer>(static_cast<intptr_t>(streamIndex)));

    gst_bin_add_many(GST_BIN(pipeline), src, jpegDec, videoConvert, queue, videoSink, nullptr);
    if (!gst_element_link_many(src, jpegDec, videoConvert, queue, videoSink, nullptr)) {
        qCCritical(CustomVideoManagerLog) << "Failed to link frame replay pipeline for stream" << streamIndex;
        gst_object_unref(pipeline);
        return nullptr;
    }

    return pipeline;
}

QString CustomVideoManager::_resolveFramePath(const QString& directoryPath, quint64 frameIndex) const
{
    const QStringList candidates = {
        QDir(directoryPath).filePath(QStringLiteral("%1.jpg").arg(frameIndex, 5, 10, QLatin1Char('0'))),
        QDir(directoryPath).filePath(QStringLiteral("%1.jpg").arg(frameIndex, 4, 10, QLatin1Char('0'))),
        QDir(directoryPath).filePath(QStringLiteral("%1.jpg").arg(frameIndex, 6, 10, QLatin1Char('0'))),
        QDir(directoryPath).filePath(QStringLiteral("%1.jpg").arg(frameIndex)),
        QDir(directoryPath).filePath(QStringLiteral("frame_%1.jpg").arg(frameIndex, 5, 10, QLatin1Char('0'))),
        QDir(directoryPath).filePath(QStringLiteral("frame_%1.jpg").arg(frameIndex, 4, 10, QLatin1Char('0'))),
        QDir(directoryPath).filePath(QStringLiteral("frame_%1.jpg").arg(frameIndex, 6, 10, QLatin1Char('0'))),
        QDir(directoryPath).filePath(QStringLiteral("frame_%1.jpg").arg(frameIndex))
    };

    for (const QString& path : candidates) {
        if (QFileInfo::exists(path)) {
            return path;
        }
    }

    return candidates.first();
}

void CustomVideoManager::_onReplayAppSrcNeedData(GstElement* appsrc, guint length, gpointer user_data)
{
    Q_UNUSED(length)

    const int streamIndex = static_cast<int>(reinterpret_cast<intptr_t>(user_data));
    CustomPlugin* plugin = qobject_cast<CustomPlugin*>(QGCCorePlugin::instance());
    if (!plugin || !plugin->customVideoManager()) {
        return;
    }

    CustomVideoManager* manager = plugin->customVideoManager();
    if (!manager || streamIndex < 0 || streamIndex >= REPLAY_STREAM_COUNT) {
        return;
    }

    ReplayStreamInfo& rs = manager->_replay.streams[streamIndex];
    // Allow appsrc to feed during preroll while enterReplayMode is still opening streams.
    if (!rs.pipeline || !rs.isFrameSequence || rs.frameEosSent) {
        return;
    }

    const int maxFramesPerNeedData = (streamIndex == STREAM_DRONE_CAMERA) ? 1 : 4;
    int pushedFrames = 0;

    while (pushedFrames < maxFramesPerNeedData
           && rs.nextFrameMetadataIndex < rs.frameStreamMeta.frameMetadata.size()) {
        const FrameMetadata& frameMeta = rs.frameStreamMeta.frameMetadata[rs.nextFrameMetadataIndex];
        const QString framePath = manager->_resolveFramePath(rs.videoPath, frameMeta.frameIndex);

        QFile frameFile(framePath);
        if (!frameFile.open(QIODevice::ReadOnly)) {
            qCWarning(CustomVideoManagerLog) << "Failed to open frame file:" << framePath;
            rs.nextFrameMetadataIndex++;
            continue;
        }

        const QByteArray jpegData = frameFile.readAll();
        if (jpegData.isEmpty()) {
            qCWarning(CustomVideoManagerLog) << "Empty frame file:" << framePath;
            rs.nextFrameMetadataIndex++;
            continue;
        }

        GstBuffer* buffer = gst_buffer_new_allocate(nullptr, static_cast<gsize>(jpegData.size()), nullptr);
        if (!buffer) {
            qCWarning(CustomVideoManagerLog) << "Failed to allocate GstBuffer for frame:" << framePath;
            rs.nextFrameMetadataIndex++;
            continue;
        }

        (void) gst_buffer_fill(buffer, 0, jpegData.constData(), static_cast<gsize>(jpegData.size()));

        const quint64 relativePtsNs = frameMeta.timestampNs - rs.baseFrameTimestampNs;
        GST_BUFFER_PTS(buffer) = static_cast<GstClockTime>(relativePtsNs);
        GST_BUFFER_DTS(buffer) = static_cast<GstClockTime>(relativePtsNs);

        qint64 durationNs = rs.defaultFrameDurationNs;
        if (rs.nextFrameMetadataIndex + 1 < rs.frameStreamMeta.frameMetadata.size()) {
            const quint64 nextTsNs = rs.frameStreamMeta.frameMetadata[rs.nextFrameMetadataIndex + 1].timestampNs;
            if (nextTsNs > frameMeta.timestampNs) {
                durationNs = static_cast<qint64>(nextTsNs - frameMeta.timestampNs);
            }
        }
        GST_BUFFER_DURATION(buffer) = static_cast<GstClockTime>(qMax<qint64>(1, durationNs));

        GstFlowReturn flowRet = GST_FLOW_ERROR;
        g_signal_emit_by_name(appsrc, "push-buffer", buffer, &flowRet);
        if (flowRet != GST_FLOW_OK) {
            if (flowRet == GST_FLOW_FLUSHING) {
                qCDebug(CustomVideoManagerLog) << "Stream" << streamIndex
                                               << "appsrc push flushing during state/seek transition";
                gst_buffer_unref(buffer);
                break;
            } else {
                qCWarning(CustomVideoManagerLog) << "Failed to push frame buffer for stream" << streamIndex
                                                 << "flow:" << flowRet;
                gst_buffer_unref(buffer);
                return;
            }
        }
        gst_buffer_unref(buffer);

        rs.nextFrameMetadataIndex++;
        pushedFrames++;
    }

    if (rs.nextFrameMetadataIndex >= rs.frameStreamMeta.frameMetadata.size() && !rs.frameEosSent) {
        rs.frameEosSent = true;
        GstFlowReturn eosRet = GST_FLOW_ERROR;
        g_signal_emit_by_name(appsrc, "end-of-stream", &eosRet);
    }
}

gboolean CustomVideoManager::_onReplayAppSrcSeekData(GstElement* appsrc, guint64 offset, gpointer user_data)
{
    Q_UNUSED(appsrc)

    const int streamIndex = static_cast<int>(reinterpret_cast<intptr_t>(user_data));
    CustomPlugin* plugin = qobject_cast<CustomPlugin*>(QGCCorePlugin::instance());
    if (!plugin || !plugin->customVideoManager()) {
        return FALSE;
    }

    CustomVideoManager* manager = plugin->customVideoManager();
    if (!manager || streamIndex < 0 || streamIndex >= REPLAY_STREAM_COUNT) {
        return FALSE;
    }

    ReplayStreamInfo& rs = manager->_replay.streams[streamIndex];
    // Seek callbacks may arrive during preroll before replay.active flips true.
    if (!rs.pipeline || !rs.isFrameSequence || rs.frameStreamMeta.frameMetadata.isEmpty()) {
        return FALSE;
    }

    // appsrc seek-data offset is provided in stream format units (nanoseconds here).
    const quint64 targetRelativeNs = static_cast<quint64>(offset);
    int newIndex = 0;
    for (int i = 0; i < rs.frameStreamMeta.frameMetadata.size(); i++) {
        const quint64 frameRelativeNs = rs.frameStreamMeta.frameMetadata[i].timestampNs - rs.baseFrameTimestampNs;
        if (frameRelativeNs >= targetRelativeNs) {
            newIndex = i;
            break;
        }
        newIndex = i;
    }

    rs.nextFrameMetadataIndex = newIndex;
    rs.frameEosSent = false;

    qCDebug(CustomVideoManagerLog) << "Appsrc seek-data stream" << streamIndex
                                   << "offset(ns):" << offset
                                   << "mapped frame index:" << newIndex;

    return TRUE;
}

bool CustomVideoManager::_openReplayStream(int streamIndex,
                                            const VideoStreamMetadata& streamMeta,
                                            QQuickItem* widget)
{
    ReplayStreamInfo& rs = _replay.streams[streamIndex];

    rs.isFrameSequence = false;
    rs.frameStreamMeta = VideoStreamMetadata();
    rs.nextFrameMetadataIndex = 0;
    rs.baseFrameTimestampNs = 0;
    rs.frameEosSent = false;

    // Create a new sink for this widget (on render thread ideally, but
    // createVideoSink should handle this - same pattern as reinitializeWidgets)
    // We create a temporary VideoReceiver just to obtain a compatible sink
    rs.receiver = QGCCorePlugin::instance()->createVideoReceiver(this);
    if (!rs.receiver) {
        qCCritical(CustomVideoManagerLog) << "Failed to create replay receiver for stream" << streamIndex;
        return false;
    }
    rs.receiver->setWidget(widget);

    void* sink = QGCCorePlugin::instance()->createVideoSink(widget, rs.receiver);
    if (!sink) {
        qCCritical(CustomVideoManagerLog) << "Failed to create sink for replay stream" << streamIndex;
        delete rs.receiver;
        rs.receiver = nullptr;
        return false;
    }

    rs.sink = sink;
    rs.receiver->setSink(sink);

    // Build pipeline based on source type (video file vs frames directory)
    const bool isDirectory = streamMeta.isDirectory || QFileInfo(streamMeta.videoPath).isDir();
    if (isDirectory && streamMeta.frameMetadata.isEmpty()) {
        qCWarning(CustomVideoManagerLog) << "Frame sequence stream has no metadata, cannot build timestamped appsrc pipeline:" << streamMeta.videoPath;
        QGCCorePlugin::instance()->releaseVideoSink(sink);
        rs.sink = nullptr;
        delete rs.receiver;
        rs.receiver = nullptr;
        return false;
    }

    if (isDirectory) {
        rs.isFrameSequence = true;
        rs.frameStreamMeta = streamMeta;
        rs.baseFrameTimestampNs = streamMeta.frameMetadata.first().timestampNs;
    }

    // Set path early so appsrc callback can resolve frame files during preroll.
    rs.videoPath = streamMeta.videoPath;

    GstElement* pipeline = isDirectory
        ? _getVideoFramesPipeline(streamMeta, sink, streamIndex)
        : _getVideoFilePipeline(streamMeta.videoPath, sink, streamIndex);
    if (!pipeline) {
        qCCritical(CustomVideoManagerLog) << "Failed to build replay pipeline for stream" << streamIndex;
        QGCCorePlugin::instance()->releaseVideoSink(sink);
        rs.sink = nullptr;
        delete rs.receiver;
        rs.receiver = nullptr;
        return false;
    }

    // Set pipeline early so bus callbacks can safely reference it during preroll.
    rs.pipeline = pipeline;

    // Watch bus for errors and EOS
    GstBus* bus = gst_element_get_bus(pipeline);
    if (bus) {
        gst_bus_enable_sync_message_emission(bus);
        (void) g_signal_connect(bus, "sync-message", G_CALLBACK(_onBusMessage),
                                reinterpret_cast<gpointer>(static_cast<intptr_t>(streamIndex)));
        gst_object_unref(bus);
    }

    // Set to PAUSED state to preroll pipeline (required for duration query)
    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PAUSED);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        qCCritical(CustomVideoManagerLog) << "Failed to set pipeline to PAUSED for stream" << streamIndex;
        gst_object_unref(pipeline);
        rs.pipeline = nullptr;
        QGCCorePlugin::instance()->releaseVideoSink(sink);
        rs.sink = nullptr;
        delete rs.receiver;
        rs.receiver = nullptr;
        return false;
    }
    
    // Wait for preroll (max 5 seconds)
    ret = gst_element_get_state(pipeline, nullptr, nullptr, 5 * GST_SECOND);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        qCWarning(CustomVideoManagerLog) << "Pipeline preroll failed for stream" << streamIndex;
    }

    // Now query duration (pipeline is prerolled)
    gint64 duration = GST_CLOCK_TIME_NONE;
    gst_element_query_duration(pipeline, GST_FORMAT_TIME, &duration);
    qint64 durationMs = (duration != GST_CLOCK_TIME_NONE) ? (duration / GST_MSECOND) : -1;
    if (isDirectory && streamMeta.frameMetadata.size() > 1) {
        const quint64 firstTs = streamMeta.frameMetadata.first().timestampNs;
        const quint64 lastTs = streamMeta.frameMetadata.last().timestampNs;
        if (lastTs > firstTs) {
            durationMs = static_cast<qint64>((lastTs - firstTs) / 1000000ULL);
        }
    }
    
    qCDebug(CustomVideoManagerLog) << "Stream" << streamIndex << "duration:" 
                                     << durationMs << "ms (" << duration << "ns)";

    rs.durationMs = durationMs;
    rs.loaded = true;

    emit videoReplaySegmentsChanged();  // Notify QML that segments have changed

    qCWarning(CustomVideoManagerLog) << "Replay stream" << streamIndex
                                      << "opened:" << streamMeta.videoPath;
    return true;
}

gboolean CustomVideoManager::_onBusMessage(GstBus* bus, GstMessage* message, gpointer user_data)
{
    Q_UNUSED(bus)
    
    int streamIndex = static_cast<int>(reinterpret_cast<intptr_t>(user_data));
    CustomPlugin* plugin = qobject_cast<CustomPlugin*>(QGCCorePlugin::instance());
    if (!plugin || !plugin->customVideoManager()) {
        qCCritical(CustomVideoManagerLog) << "Failed to get custom video manager";
        return TRUE;
    }
    CustomVideoManager* manager = plugin->customVideoManager();

    if (!manager || !manager->_replay.streams[streamIndex].pipeline) {
        // During replay startup/teardown callbacks can arrive before stream state is fully wired.
        qCDebug(CustomVideoManagerLog) << "Replay stream" << streamIndex << "pipeline not ready yet";
        return TRUE;
    }
    GstElement* pipeline = manager->_replay.streams[streamIndex].pipeline;
    
    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR: {
        GError* err = nullptr;
        gchar* debug = nullptr;
        gst_message_parse_error(message, &err, &debug);
        qCCritical(CustomVideoManagerLog) << "Replay stream" << streamIndex 
                                           << "error:" << err->message;
        g_error_free(err);
        g_free(debug);
        break;
    }
    case GST_MESSAGE_EOS: {
        qCWarning(CustomVideoManagerLog) << "Replay stream" << streamIndex 
                                          << "reached end of stream - seeking to start and pausing";
        int idx = streamIndex;
        QMetaObject::invokeMethod(manager, [manager, idx]() {
            GstElement* pipeline = manager->_replay.streams[idx].pipeline;
            if (!pipeline) return;
            
            if (gst_element_seek_simple(pipeline, GST_FORMAT_TIME,
                                        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
                                        0)) {
                qCDebug(CustomVideoManagerLog) << "Replay stream" << idx << "seeked to start";
                gst_element_set_state(pipeline, GST_STATE_PAUSED);
                
                // Reset readyToPlay for videos with negative offsets
                if (manager->_replay.streams[idx].offsetMs < 0) {
                    manager->_replay.streams[idx].readyToPlay = false;
                    qCDebug(CustomVideoManagerLog) << "Replay stream" << idx 
                                                    << "reset readyToPlay (negative offset video looped)";
                }
                // gst_element_set_state(pipeline, GST_STATE_PLAYING);

            } else {
                qCWarning(CustomVideoManagerLog) << "Replay stream" << idx << "failed to seek";
            }
        }, Qt::QueuedConnection);

        break;
    }
    case GST_MESSAGE_WARNING: {
        GError *err = nullptr;
        gchar  *dbg = nullptr;
        gst_message_parse_warning(message, &err, &dbg);
        qCWarning(CustomVideoManagerLog) << "Replay stream" << streamIndex 
                                         << "warning:" << err->message;
        g_clear_error(&err);
        g_free(dbg);
        break;
    }

    case GST_MESSAGE_STATE_CHANGED: {
        // Only log top-level pipeline state changes
        if (GST_MESSAGE_SRC(message) == GST_OBJECT(pipeline)) {
            GstState old_state, new_state, pending;
            gst_message_parse_state_changed(message, &old_state, &new_state, &pending);
            qCDebug(CustomVideoManagerLog) << "[STATE] " << gst_element_state_get_name(old_state)
                                            << " -> " << gst_element_state_get_name(new_state);
        }
        break;
    }
    default:
        break;
    }
    
    return TRUE;
}

void CustomVideoManager::seekToPosition(quint32 tlogTimeSecs)
{
    if (!_replay.active) {
        return;
    }
    
    // Cache time for delayed video timer
    _replay.currentTlogTimeSecs = tlogTimeSecs;
    
    qint64 tlogTimeMs = static_cast<qint64>(tlogTimeSecs) * 1000;
    for (int i = 0; i < REPLAY_STREAM_COUNT; i++) {
        ReplayStreamInfo& rs = _replay.streams[i];
        
        if (!rs.pipeline || !rs.loaded) {
            continue;
        }
        
        // Calculate video position with offset
        // offsetMs > 0: video started before tlog (e.g., 2000ms before)
        // offsetMs < 0: video started after tlog (e.g., -3000ms = video starts 3s after tlog)
        qint64 videoTimeMs = tlogTimeMs + rs.offsetMs;
        
        // Handle video starting after tlog
        if (videoTimeMs < 0) {
            // Tlog hasn't reached video start yet - keep paused at position 0
            if (rs.readyToPlay) {
                qCDebug(CustomVideoManagerLog) << "Stream" << i << "not yet started - pausing at 0";
                gst_element_set_state(rs.pipeline, GST_STATE_PAUSED);
                gst_element_seek_simple(rs.pipeline, GST_FORMAT_TIME,
                    static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
                    0);
                rs.readyToPlay = false;
                
                // Restart timer to monitor when video should start
                if (_replay.delayedVideoTimer && !_replay.delayedVideoTimer->isActive()) {
                    _replay.delayedVideoTimer->start();
                    qCDebug(CustomVideoManagerLog) << "Restarted delayed video timer (seeked before video start)";
                }
            }
            continue;
        }
        
        // Video is ready to play
        if (!rs.readyToPlay) {
            qCDebug(CustomVideoManagerLog) << "Stream" << i << "now ready (tlog caught up)";
            rs.readyToPlay = true;
            
            // If we're currently playing, start this stream now
            if (_replay.isPlaying) {
                gst_element_set_state(rs.pipeline, GST_STATE_PLAYING);
                qCDebug(CustomVideoManagerLog) << "Stream" << i << "starting playback (was waiting for tlog)";
            }
        }
        
        // Only seek if time changed significantly (avoid sub-second jitter)
        qint64 seekPos = static_cast<gint64>(videoTimeMs * GST_MSECOND);
        qint64 lastSeekPos = static_cast<qint64>(rs.lastSeekTimeMs * GST_MSECOND);
        qint64 seekDelta = qAbs(seekPos - lastSeekPos);
        
        // Only seek if we're more than 500ms off (accounts for keyframe seeking inaccuracy)
        if (seekDelta < 100 * GST_MSECOND) {
            continue;
        }
        
        rs.lastSeekTimeMs = videoTimeMs;

        gboolean seekResult = gst_element_seek_simple(
            rs.pipeline, 
            GST_FORMAT_TIME,
            static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
            seekPos
        );
        
        if (!seekResult) {
            qCWarning(CustomVideoManagerLog) << "Seek failed for stream" << i << "position:" << videoTimeMs << "ms";
        } else {
            qCDebug(CustomVideoManagerLog) << "Seeked stream" << i << "to" << videoTimeMs << "ms (tlog:" << tlogTimeMs << "ms, offset:" << rs.offsetMs << "ms)";
        }
    }
}

void CustomVideoManager::startReplayPlayback()
{
    if (!_replay.active) {
        return;
    }
    
    qCDebug(CustomVideoManagerLog) << "Starting replay video playback";
    _replay.isPlaying = true;
    for (int i = 0; i < REPLAY_STREAM_COUNT; i++) {
        if (_replay.streams[i].pipeline && _replay.streams[i].loaded) {
            // Only start if video is ready (for negative offsets, wait until tlog catches up)
            if (_replay.streams[i].readyToPlay) {
                gst_element_set_state(_replay.streams[i].pipeline, GST_STATE_PLAYING);
                qCDebug(CustomVideoManagerLog) << "Stream" << i << "now PLAYING";
            } else {
                qCDebug(CustomVideoManagerLog) << "Stream" << i << "not ready yet (negative offset) - keeping PAUSED";
            }
        }
    }
}

void CustomVideoManager::updateReplayTime(quint32 tlogTimeSecs)
{
    if (!_replay.active) {
        return;
    }
    
    // Just update cached time - no seeking (used during normal playback)
    _replay.currentTlogTimeSecs = tlogTimeSecs;
}

void CustomVideoManager::pauseReplayPlayback()
{
    if (!_replay.active) {
        return;
    }
    
    qCDebug(CustomVideoManagerLog) << "Pausing replay video playback";
    _replay.isPlaying = false;
    for (int i = 0; i < REPLAY_STREAM_COUNT; i++) {
        if (_replay.streams[i].pipeline && _replay.streams[i].loaded) {
            gst_element_set_state(_replay.streams[i].pipeline, GST_STATE_PAUSED);
            qCDebug(CustomVideoManagerLog) << "Stream" << i << "now PAUSED";
        }
    }
}


void CustomVideoManager::exitReplayMode()
{
    qCWarning(CustomVideoManagerLog) << "Exiting replay mode";

    _replay.active = false;
    
    // Stop delayed video timer
    if (_replay.delayedVideoTimer) {
        _replay.delayedVideoTimer->stop();
    }

    for (int i = 0; i < REPLAY_STREAM_COUNT; i++) {
        ReplayStreamInfo& rs = _replay.streams[i];

        // Mark frame sequence callbacks inactive before shutting pipelines down.
        rs.frameEosSent = true;
        rs.isFrameSequence = false;
        
        // Stop and cleanup pipeline
        if (rs.pipeline) {
            gst_element_set_state(rs.pipeline, GST_STATE_NULL);
            gst_object_unref(rs.pipeline);
            rs.pipeline = nullptr;
        }
        
        // Release sink
        if (rs.sink) {
            QGCCorePlugin::instance()->releaseVideoSink(rs.sink);
            rs.sink = nullptr;
        }

        if (rs.receiver) {
            delete rs.receiver;
            rs.receiver = nullptr;
        }
        
        rs.loaded = false;
        rs.videoPath.clear();
        rs.frameStreamMeta = VideoStreamMetadata();
        rs.nextFrameMetadataIndex = 0;
        rs.baseFrameTimestampNs = 0;
        rs.frameEosSent = false;

        if (i < STREAM_COUNT) {
            _streams[i].pendingStopReason = StopReason::None;
            _streams[i].active = false;
            _streams[i].decoding = false;
            emit streamStateChanged(i, false);
            emit streamDecodingChanged(i, false);
        }
    }

    emit replayModeChanged(false);
}

void CustomVideoManager::_checkDelayedVideos()
{
    if (!_replay.active || !_replay.isPlaying) {
        return;
    }
    
    // Use cached tlog time from last seekToPosition call
    qint64 tlogTimeMs = static_cast<qint64>(_replay.currentTlogTimeSecs) * 1000;
    bool allReady = true;
    for (int i = 0; i < REPLAY_STREAM_COUNT; i++) {
        ReplayStreamInfo& rs = _replay.streams[i];
        
        if (!rs.pipeline || !rs.loaded || rs.readyToPlay) {
            continue;
        }
        
        allReady = false;
        
        // Calculate if video should start now
        qint64 videoTimeMs = tlogTimeMs + rs.offsetMs;
        
        if (videoTimeMs >= 0) {
            qCDebug(CustomVideoManagerLog) << "Stream" << i 
                                            << "delayed video now ready at tlog time" << tlogTimeMs << "ms"
                                            << "(offset:" << rs.offsetMs << "ms)";
            rs.readyToPlay = true;

            // Seek first while paused, then switch to PLAYING to avoid appsrc flushing race.
            if (videoTimeMs > 0) {
                gst_element_seek_simple(
                    rs.pipeline,
                    GST_FORMAT_TIME,
                    static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
                    videoTimeMs * GST_MSECOND
                );
                qCDebug(CustomVideoManagerLog) << "Stream" << i << "seeked to" << videoTimeMs << "ms";
            }

            gst_element_set_state(rs.pipeline, GST_STATE_PLAYING);
            qCDebug(CustomVideoManagerLog) << "Stream" << i << "starting delayed playback";
        }
    }
    
    // Stop timer once all delayed videos are ready
    if (allReady && _replay.delayedVideoTimer) {
        _replay.delayedVideoTimer->stop();
        qCDebug(CustomVideoManagerLog) << "All delayed videos ready - stopped monitoring timer";
    }
}

QVariantList CustomVideoManager::videoReplaySegments() const {
    QVariantList segments;
    if (!_replay.active) {
        return segments;
    }

    for (int i = 0; i < REPLAY_STREAM_COUNT; i++) {
        const ReplayStreamInfo& rs = _replay.streams[i];
        if (rs.loaded) {
            QVariantMap segment;
            segment["start"] = rs.offsetMs;
            segment["duration"] = rs.durationMs;
            // Half-opaque colors (alpha = 0.5) to show overlap
            // RGB: red (255,0,0), Thermal: light blue (0,100,255)
            QColor color;
            if (i == STREAM_BR) {
                color = QColor::fromRgbF(1.0, 0.0, 0.0, 0.5);
            } else if (i == STREAM_TR) {
                color = QColor::fromRgbF(0.0, 0.39, 1.0, 0.5);
            } else {
                color = QColor::fromRgbF(0.0, 0.75, 0.25, 0.5);
            }
            segment["color"] = color;
            segments.append(segment);
        }
    }

    return segments;
}

void CustomVideoManager::restartAllStreamsToBeginning()
{
    if (!_replay.active) {
        return;
    }

    qCDebug(CustomVideoManagerLog) << "Restarting all replay streams to position 0";

    for (int i = 0; i < REPLAY_STREAM_COUNT; i++) {
        ReplayStreamInfo& rs = _replay.streams[i];

        if (!rs.pipeline || !rs.loaded) {
            qCDebug(CustomVideoManagerLog) << "Stream" << i << "not loaded - skipping";
            continue;
        }

        // Pause before seeking to avoid appsrc flushing race
        gst_element_set_state(rs.pipeline, GST_STATE_PAUSED);

        qint64 initialPosMs = qMax(static_cast<qint64>(0), _replay.streams[i].offsetMs);

        if (initialPosMs > 0) {
            gboolean seekResult = gst_element_seek_simple(_replay.streams[i].pipeline, GST_FORMAT_TIME,
                    static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
                    initialPosMs * GST_MSECOND);
                qCDebug(CustomVideoManagerLog) << "Stream" << i << "seeked to initial position" << initialPosMs << "ms";

            if (!seekResult) {
                qCWarning(CustomVideoManagerLog) << "Seek to 0 failed for stream" << i;
            } else {
                qCDebug(CustomVideoManagerLog) << "Stream" << i << "seeked to 0";
            }
        }
        

        rs.lastSeekTimeMs = 0;

        // Streams with a positive offset are immediately ready at position 0;
        // negative-offset streams need to wait for the tlog to catch up again.
        rs.readyToPlay = (rs.offsetMs >= 0);
    }

    // Reset tlog time reference so seekToPosition calculations stay consistent
    _replay.currentTlogTimeSecs = 0;

    // Resume playback on all ready streams if we were playing
    if (_replay.isPlaying) {
        for (int i = 0; i < REPLAY_STREAM_COUNT; i++) {
            ReplayStreamInfo& rs = _replay.streams[i];
            if (rs.pipeline && rs.loaded && rs.readyToPlay) {
                gst_element_set_state(rs.pipeline, GST_STATE_PLAYING);
                qCDebug(CustomVideoManagerLog) << "Stream" << i << "resumed PLAYING after restart";
            }
        }

        // Kick off the delayed-video timer for any streams that aren't ready yet
        if (_replay.delayedVideoTimer && !_replay.delayedVideoTimer->isActive()) {
            _replay.delayedVideoTimer->start();
            qCDebug(CustomVideoManagerLog) << "Delayed video timer started for negative-offset streams";
        }
    }
}
