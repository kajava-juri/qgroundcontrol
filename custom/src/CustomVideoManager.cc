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


class FinishCustomVideoInitialization : public QRunnable {
public:
    FinishCustomVideoInitialization(CustomVideoManager* mgr) : _mgr(mgr) {}
    void run() override {
        _mgr->_initAfterQmlIsReady();
    }
private:
    CustomVideoManager* _mgr;
};

CustomVideoManager::CustomVideoManager(QObject* parent)
    : QObject(parent)
{
    qCWarning(CustomVideoManagerLog) << "CustomVideoManager created";
}

void CustomVideoManager::_initAfterQmlIsReady()
{
    qCWarning(CustomVideoManagerLog) << "Scene graph initialized, setting up receivers";

    // Now it is safe to create receivers/sinks
    if (_pendingRgbWidget) {
        _setupReceiver(STREAM_RGB, _pendingRgbWidget);
        _pendingRgbWidget = nullptr;
    }
    if (_pendingThermalWidget) {
        _setupReceiver(STREAM_THERMAL, _pendingThermalWidget);
        _pendingThermalWidget = nullptr;
    }
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
    
    // Initialize default URIs
    _streams[STREAM_RGB].uri = QStringLiteral("udp://0.0.0.0:5600");
    _streams[STREAM_THERMAL].uri = QStringLiteral("udp://0.0.0.0:5601");

    _mainWindow = mainWindow;

    // any settings/connects you need:
    // (void) connect(_videoSettings->videoSource(), &Fact::rawValueChanged, this, &CustomVideoManager::_videoSourceChanged);
    // ... other connects as needed

    // schedule the render job (same stage as upstream)
    _mainWindow->scheduleRenderJob(
        new FinishCustomVideoInitialization(this),
        QQuickWindow::BeforeSynchronizingStage
    );

    _initialized = true;
}

void CustomVideoManager::initializeStreams(QQuickItem* rgbWidget,
                                           QQuickItem* thermalWidget)
{
    _pendingRgbWidget = rgbWidget;
    _pendingThermalWidget = thermalWidget;

    if (_initialized) {
        // Scene graph already ready
        _initAfterQmlIsReady();
    }
}

void CustomVideoManager::_setupReceiver(int streamIndex, QQuickItem* widget)
{
    if (streamIndex < 0 || streamIndex >= STREAM_COUNT) {
        qCWarning(CustomVideoManagerLog) << "Invalid stream index:" << streamIndex;
        return;
    }

    StreamInfo& stream = _streams[streamIndex];

    // Create receiver
    stream.receiver = QGCCorePlugin::instance()->createVideoReceiver(this);
    if (!stream.receiver) {
        qCCritical(CustomVideoManagerLog) << "Failed to create receiver for stream" << streamIndex;
        return;
    }

    // Assign name
    stream.receiver->setName(
        streamIndex == STREAM_RGB ? "customRgbVideo" : "customThermalVideo"
    );
    
    stream.receiver->setWidget(widget);

    // Create sink bound to your widget
    void *sink = QGCCorePlugin::instance()->createVideoSink(widget, stream.receiver);
    if (!sink) {
        qCCritical(CustomVideoManagerLog) << "createVideoSink failed for stream" << streamIndex;
        return;
    }
    stream.sink = sink;

    stream.receiver->setSink(sink);

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
    });

    connect(stream.receiver, &VideoReceiver::onStartComplete, this,
        [this, streamIndex](VideoReceiver::STATUS status) {
            qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex << "onStartComplete, status:" << status;
            if (status == VideoReceiver::STATUS_OK) {
                qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex << "starting decoding";
                _streams[streamIndex].receiver->startDecoding(_streams[streamIndex].sink);
            } else {
                qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex << "start FAILED with status:" << status;
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
    if (streamIndex < 0 || streamIndex >= STREAM_COUNT) {
        return;
    }

    StreamInfo& stream = _streams[streamIndex];

    if (!stream.receiver) {
        qCWarning(CustomVideoManagerLog) << "No receiver for stream" << streamIndex;
        return;
    }

    if (stream.receiver->started()) {
        qCDebug(CustomVideoManagerLog) << "Stream" << streamIndex << "already started";
        return;
    }

    if (stream.uri.isEmpty()) {
        qCWarning(CustomVideoManagerLog) << "No URI set for stream" << streamIndex;
        return;
    }

    qCWarning(CustomVideoManagerLog) << "Starting stream" << streamIndex << "URI:" << stream.uri;
    stream.receiver->setUri(stream.uri);
    stream.receiver->start(5000);  // 5 second timeout
    qCWarning(CustomVideoManagerLog) << "Stream" << streamIndex << "start() called";
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

void CustomVideoManager::restartStream(int streamIndex)
{
    _stopReceiver(streamIndex);
    _startReceiver(streamIndex);
}

void CustomVideoManager::setStreamUri(int streamIndex, const QString& uri)
{
    if (streamIndex < 0 || streamIndex >= STREAM_COUNT) {
        return;
    }

    _streams[streamIndex].uri = uri;
    qCDebug(CustomVideoManagerLog) << "Stream" << streamIndex << "URI set to:" << uri;
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

