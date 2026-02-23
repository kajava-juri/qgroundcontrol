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

    QQuickItem* rgbWidget = _mainWindow->findChild<QQuickItem*>("customRgbVideo");
    QQuickItem* thermalWidget = _mainWindow->findChild<QQuickItem*>("customThermalVideo");

    qCWarning(CustomVideoManagerLog) << "Found widgets - RGB:" << rgbWidget << "Thermal:" << thermalWidget;

    _setupReceiver(STREAM_RGB, rgbWidget);
    _setupReceiver(STREAM_THERMAL, thermalWidget);

    qCWarning(CustomVideoManagerLog) << "Receivers initialized, waiting for VIDEO_STREAM_INFORMATION messages";
}

void CustomVideoManager::reinitializeWidgets(bool gridMode)
{
    if (!_mainWindow) {
        qCCritical(CustomVideoManagerLog) << "reinitializeWidgets called with NULL mainWindow";
        return;
    }

    qCWarning(CustomVideoManagerLog) << "reinitializeWidgets - re-finding video widgets, gridMode:" << gridMode;

    QQuickItem* rgbWidget = nullptr;
    QQuickItem* thermalWidget = nullptr;

    if (gridMode) {
        // In grid mode, search within the gridView component
        QQuickItem* gridView = _mainWindow->findChild<QQuickItem*>("gridView");
        if (gridView) {
            qCWarning(CustomVideoManagerLog) << "Searching for widgets in gridView";
            rgbWidget = gridView->findChild<QQuickItem*>("customRgbVideo");
            thermalWidget = gridView->findChild<QQuickItem*>("customThermalVideo");
        } else {
            qCWarning(CustomVideoManagerLog) << "gridView not found, falling back to global search";
            rgbWidget = _mainWindow->findChild<QQuickItem*>("customRgbVideo");
            thermalWidget = _mainWindow->findChild<QQuickItem*>("customThermalVideo");
        }
    } else {
        // In overlay mode, search globally (will find PipView widgets)
        rgbWidget = _mainWindow->findChild<QQuickItem*>("customRgbVideo");
        thermalWidget = _mainWindow->findChild<QQuickItem*>("customThermalVideo");
    }

    qCWarning(CustomVideoManagerLog) << "Found widgets - RGB:" << rgbWidget << "Thermal:" << thermalWidget;

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
        _streams[streamIndex].receiver->setStarted(false);
        if (status == VideoReceiver::STATUS_INVALID_URL) {
            qCDebug(CustomVideoManagerLog) << "Invalid video URL. Not restarting";
            _streams[streamIndex].restartAttempts = 0;  // Reset on invalid URL
        } else if (!_streams[streamIndex].allowAutoRestart) {
            qCDebug(CustomVideoManagerLog) << "Auto-restart disabled for stream" << streamIndex;
            _streams[streamIndex].allowAutoRestart = true;  // Re-enable for next time
            _streams[streamIndex].restartAttempts = 0;  // Reset counter
        } else if (_streams[streamIndex].uri.isEmpty()) {
            qCDebug(CustomVideoManagerLog) << "URI is empty, not restarting stream" << streamIndex;
            _streams[streamIndex].restartAttempts = 0;  // Reset counter
        } else {
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

    QString currentUri = _streams[streamIndex].uri;

    qCDebug(CustomVideoManagerLog) << "Current URI for stream" << streamIndex << "is" << _streams[streamIndex].uri;
    
    // If URI is the same, check if stream is already running
    if (currentUri == uri) {
        // If stream is already decoding, nothing to do
        if (_streams[streamIndex].decoding) {
            qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex << "URI unchanged and already decoding, skipping";
            return;
        }
        // URI is same but stream not running - need to restart it properly
        qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex << "URI unchanged but not decoding, restarting";
        if (!uri.isEmpty()) {
            // Stop first if receiver is in started state (prevents "Already running!" error)
            if (_streams[streamIndex].receiver && _streams[streamIndex].receiver->started()) {
                qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex << "stopping stale receiver, will auto-restart via onStopComplete";
                // onStopComplete handler will automatically restart after stop completes
                stopStream(streamIndex);
            } else {

                // Not started, but if state is not yet active (from external data collector), then dont start

                startStream(streamIndex);
            }
        }
        return;
    }

    if (!currentUri.isEmpty() && currentUri != uri) {
        qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex 
                                          << "URI changing from" << currentUri 
                                          << "to" << uri;
        
        if (_streams[streamIndex].receiver && _streams[streamIndex].receiver->started()) {
            qCWarning(CustomVideoManagerLog) << "Stopping stream" << streamIndex 
                                              << "before URI change";
            
            // Disable auto-restart temporarily - we'll start manually after URI update
            _streams[streamIndex].allowAutoRestart = false;
            stopStream(streamIndex);
            
            // Wait for stop to complete before updating URI
            // Use single-shot connection to onStopComplete
            QMetaObject::Connection* conn = new QMetaObject::Connection();
            *conn = connect(_streams[streamIndex].receiver, &VideoReceiver::onStopComplete, this,
                [this, streamIndex, uri, conn](VideoReceiver::STATUS status) {
                    Q_UNUSED(status);
                    qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex 
                                                      << "stopped, now updating URI to" << uri;
                    disconnect(*conn);
                    delete conn;
                    
                    // Now update URI and start
                    _streams[streamIndex].uri = uri;
                    emit streamUriChanged(streamIndex, uri);
                    
                    if (!uri.isEmpty()) {
                        qCWarning(CustomVideoManagerLog) << "Starting stream" << streamIndex 
                                                          << "with new URI";
                        startStream(streamIndex);
                    }
                });
            return;  // Exit early - continuation happens in callback
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

bool CustomVideoManager::enterReplayMode(const QString& rgbVideoPath, const QString& thermalVideoPath)
{
    if (_replay.active) {
        qCWarning(CustomVideoManagerLog) << "Already in replay mode, exiting first";
        exitReplayMode();
    }

    qCWarning(CustomVideoManagerLog) << "Entering replay mode";

    // Stop all live streams - disable auto-restart so they don't fight us
    for (int i = 0; i < STREAM_COUNT; i++) {
        _streams[i].allowAutoRestart = false;
        if (_streams[i].receiver && _streams[i].receiver->started()) {
            _streams[i].receiver->stop();
        }
    }

    // _replay.logStartUnixMs = logStartUnixMs;
    _replay.playbackSpeed  = 1.0;

    QStringList paths = { rgbVideoPath, thermalVideoPath };

    for (int i = 0; i < STREAM_COUNT; i++) {
        qCDebug(CustomVideoManagerLog) << "Setting up widget replay stream" << StreamNames[i] << "with video path:" << paths[i];
        QQuickItem* widget = _mainWindow->findChild<QQuickItem*>(
            QString::fromStdString(StreamNames[i])
        );
        if (!widget) {
            qCCritical(CustomVideoManagerLog) << "Widget not found for stream" << i;
            exitReplayMode();
            return false;
        }

        if (!_openReplayStream(i, paths[i], widget)) {
            qCCritical(CustomVideoManagerLog) << "Failed to open replay stream" << i;
            exitReplayMode();
            return false;
        }
    }

    _replay.active = true;
    // emit replayModeChanged(true);

    // Start paused at position 0 - waiting for first seek from LogReplayLinkController
    for (int i = 0; i < STREAM_COUNT; i++) {
        if (_replay.streams[i].pipeline) {
            qCDebug(CustomVideoManagerLog) << "Setting replay stream" << i << "to PLAY at start";
            gst_element_set_state(_replay.streams[i].pipeline, GST_STATE_PLAYING);
        }
    }

    for (int i = 0; i < STREAM_COUNT; i++) {
        if (_replay.streams[i].loaded && _streams[i].receiver) {
            // Manually update state and emit signals
            _streams[i].active = true;
            _streams[i].decoding = true;
            emit streamStateChanged(i, true);
            emit streamDecodingChanged(i, true);
        }
    }

    return true;
}

bool CustomVideoManager::_openReplayStream(int streamIndex,
                                            const QString& videoPath,
                                            QQuickItem* widget)
{
    ReplayStreamInfo& rs = _replay.streams[streamIndex];

    // Create a new sink for this widget (on render thread ideally, but
    // createVideoSink should handle this - same pattern as reinitializeWidgets)
    // We create a temporary VideoReceiver just to obtain a compatible sink
    VideoReceiver* tempReceiver = QGCCorePlugin::instance()->createVideoReceiver(this);
    if (!tempReceiver) {
        qCCritical(CustomVideoManagerLog) << "Failed to create temp receiver for replay stream" << streamIndex;
        return false;
    }
    tempReceiver->setWidget(widget);

    void* sink = QGCCorePlugin::instance()->createVideoSink(widget, tempReceiver);
    if (!sink) {
        qCCritical(CustomVideoManagerLog) << "Failed to create sink for replay stream" << streamIndex;
        delete tempReceiver;
        return false;
    }

    rs.sink = sink;

    // Build playbin pipeline
    GstElement* pipeline = gst_element_factory_make("playbin", nullptr);
    if (!pipeline) {
        qCCritical(CustomVideoManagerLog) << "Failed to create playbin for stream" << streamIndex;
        QGCCorePlugin::instance()->releaseVideoSink(sink);
        rs.sink = nullptr;
        delete tempReceiver;
        return false;
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

    // Watch bus for errors and EOS
    GstBus* bus = gst_element_get_bus(pipeline);
    if (bus) {
        gst_bus_enable_sync_message_emission(bus);
        (void) g_signal_connect(bus, "sync-message", G_CALLBACK(_onBusMessage),
                                reinterpret_cast<gpointer>(static_cast<intptr_t>(streamIndex)));
        gst_object_unref(bus);
    }

    rs.pipeline = pipeline;
    rs.videoPath = videoPath;
    rs.loaded = true;

    qCWarning(CustomVideoManagerLog) << "Replay stream" << streamIndex
                                      << "opened:" << videoPath;
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
        qCCritical(CustomVideoManagerLog) << "Failed to get pipeline for replay stream" << streamIndex;   
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


void CustomVideoManager::exitReplayMode()
{
    qCWarning(CustomVideoManagerLog) << "Exiting replay mode";

    _replay.active = false;

    for (int i = 0; i < STREAM_COUNT; i++) {
        ReplayStreamInfo& rs = _replay.streams[i];
        
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
        
        rs.loaded = false;
        rs.videoPath.clear();

        // Re-enable auto-restart for live streams
        _streams[i].allowAutoRestart = true;

        _streams[i].active = false;
        _streams[i].decoding = false;
        emit streamStateChanged(i, false);
        emit streamDecodingChanged(i, false);
    }

    emit replayModeChanged(false);
}