#include "DataCollectionController.h"
#include "QGCApplication.h"
#include "QGCLoggingCategory.h"
#include "Vehicle.h"
#include "MultiVehicleManager.h"
#include "CustomPlugin.h"
#include "MAVLinkProtocol.h"
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkAccessManager>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QStandardPaths>
#include <QtCore/QDateTime>
#include "LogReplayLinkController.h"

QGC_LOGGING_CATEGORY(DataCollectionControllerLog, "Custom.DataCollectionController")

DataCollectionController::DataCollectionController(QObject* parent)
    : QObject(parent)
{
    qCDebug(DataCollectionControllerLog) << "DataCollectionController created";
    
    // Connect to active vehicle changes
    connect(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged, this, &DataCollectionController::_onActiveVehicleChanged);
    
    // Set initial vehicle if already connected
    _onActiveVehicleChanged(MultiVehicleManager::instance()->activeVehicle());

    connect(this, &DataCollectionController::isCollectingChanged, this, [this]() {
        if (!_isCollecting) {
            // qCDebug(DataCollectionControllerLog) << "Data collection stopped - stopping periodic requests";
            _stopPeriodicStreamInfoRequest();
        }
    });
}

void DataCollectionController::toggleRecording() {
    if(_isCollecting) {
        qCDebug(DataCollectionControllerLog) << "Stopping data collection";
        // _isCollecting = false;
        // emit isCollectingChanged();
        stopRecording();
    } else {
        qCDebug(DataCollectionControllerLog) << "Starting data collection";
        // _isCollecting = true;
        // emit isCollectingChanged();
        startRecording();
    }
}

void DataCollectionController::_handleStreamInfo(const QString& streamName, const QString& uri, int streamId, bool rtspReady) {

    // Get CustomVideoManager from CustomPlugin
    CustomPlugin* plugin = qobject_cast<CustomPlugin*>(QGCCorePlugin::instance());

    if (!plugin || !plugin->customVideoManager()) {
        qCWarning(DataCollectionControllerLog) << "CustomPlugin or CustomVideoManager not available";
        return;
    }
    // if data collection is inactive, do not act. Current bug where periodic stream info request's response was received after collection ended and calling setStreamUri causes a stream to restart and GStreamer throws errors.
    if (!plugin->dataCollectionController()->isCollecting()) {
        qCWarning(DataCollectionControllerLog) << "Data collection not active - ignoring stream info";
        return;
    }
    
    CustomVideoManager* videoManager = plugin->customVideoManager();
    qCDebug(DataCollectionControllerLog) << "CustomVideoManager found, StreamNames size:" << CustomVideoManager::StreamNames.size();
    
    if (CustomVideoManager::StreamNames.empty()) {
        qCWarning(DataCollectionControllerLog) << "StreamNames map is empty - not initialized yet?";
        return;
    }

    // Update the URI for the corresponding stream
    for (const auto& [index, name] : CustomVideoManager::StreamNames) {
        qCDebug(DataCollectionControllerLog) << "Checking stream index" << index << "name" << QString::fromStdString(name);
        if (QString::fromStdString(name) == streamName) {
            const bool vidReadyCached = _pendingVidReadyStreamIds.contains(streamId);
            const bool effectiveReady = rtspReady || vidReadyCached;

            videoManager->setStreamId(index, streamId);
            videoManager->setStreamReady(index, effectiveReady);
            videoManager->setStreamUri(index, uri);

            if (vidReadyCached) {
                _pendingVidReadyStreamIds.remove(streamId);
            }

            qCDebug(DataCollectionControllerLog) << "Updated stream URI for" << streamName
                                                 << "to" << uri
                                                 << "stream_id" << streamId
                                                 << "rtsp_ready" << rtspReady
                                                 << "vid_ready_cached" << vidReadyCached;

            if (effectiveReady && !uri.isEmpty() && !videoManager->isStreamDecoding(index)) {
                qCDebug(DataCollectionControllerLog) << "Starting stream" << index
                                                     << "after stream info"
                                                     << "(rtsp_ready=" << rtspReady
                                                     << ", vid_ready_cached=" << vidReadyCached << ")";
                videoManager->startStream(index);
            }
            
            // Check if all streams now have URIs configured
            bool allStreamsConfigured = true;
            for (int i = 0; i < CustomVideoManager::STREAM_COUNT; i++) {
                if (videoManager->getStreamUri(i).isEmpty()) {
                    allStreamsConfigured = false;
                    break;
                }
            }
            
            if (allStreamsConfigured) {
                qCDebug(DataCollectionControllerLog) << "All streams configured - stopping periodic stream info requests";
                _stopPeriodicStreamInfoRequest();
                _sendReadySignalToDataCollector();
            }
            
            break;
        }
    }
}

void DataCollectionController::_sendHttpRequest(QString endpoint) {
    qCDebug(DataCollectionControllerLog) << "Sending HTTP request to endpoint:" << endpoint;

    CustomPlugin* plugin = qobject_cast<CustomPlugin*>(QGCCorePlugin::instance());
    if (!plugin || !plugin->customSettings()) {
        qCDebug(DataCollectionControllerLog) << "CustomPlugin or CustomSettings not available";
        return;
    }
    QString httpUrl = plugin->customSettings()->httpUrl()->rawValue().toString();
    
    QNetworkRequest request(QUrl(QString(httpUrl + "/" + endpoint)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    // Create JSON body (empty object, Flask will use defaults)
    QJsonObject jsonObj;
    jsonObj["folder"] = plugin->customSettings()->folderName()->rawValue().toString();
    jsonObj["timeout"] = plugin->customSettings()->timeout()->rawValue().toInt();
    jsonObj["no_timeout"] = plugin->customSettings()->noTimeout()->rawValue().toBool();
    jsonObj["enable_voxl_logging"] = plugin->customSettings()->enableVoxlLogging()->rawValue().toBool();
    jsonObj["enable_rtk_logging"] = plugin->customSettings()->enableRtkLogging()->rawValue().toBool();
    jsonObj["enable_qgc_streaming"] = plugin->customSettings()->enableQgcStreaming()->rawValue().toBool();
    jsonObj["qgc_ip"] = plugin->customSettings()->qgcIp()->rawValue().toString();
    jsonObj["qgc_port"] = plugin->customSettings()->qgcPort()->rawValue().toInt();
    jsonObj["use_hardware_encoding"] = plugin->customSettings()->useHardwareEncoding()->rawValue().toBool();
    QJsonDocument jsonDoc(jsonObj);
    QByteArray jsonData = jsonDoc.toJson();
    
    QNetworkReply* reply = _networkManager.post(request, jsonData);

    connect(reply, &QNetworkReply::finished, this, [this, reply, endpoint]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray responseData = reply->readAll();
            qCDebug(DataCollectionControllerLog) << "HTTP request to" << endpoint << "succeeded. Response:" << responseData;
        } else {
            qCDebug(DataCollectionControllerLog) << "HTTP request to" << endpoint << "failed. Error:" << reply->errorString();
            // Revert the isCollecting state on error
            if (endpoint == "start") {
                _isCollecting = false;
                emit isCollectingChanged();
            } else if (endpoint == "stop") {
                _isCollecting = true;
                emit isCollectingChanged();
            }
        }
        reply->deleteLater();
    });
}

void DataCollectionController::_getStreamInfoHttp() {
    qCDebug(DataCollectionControllerLog) << "Requesting stream info via HTTP";

    CustomPlugin* plugin = qobject_cast<CustomPlugin*>(QGCCorePlugin::instance());
    if (!plugin || !plugin->customSettings()) {
        qCDebug(DataCollectionControllerLog) << "CustomPlugin or CustomSettings not available";
        return;
    }
    QString httpUrl = plugin->customSettings()->httpUrl()->rawValue().toString();
    
    QNetworkRequest request(QUrl(QString(httpUrl + "/stream_info")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    // GET request to api /api/stream_info:
    /*
    {
        "status": "success",
        "streams": [
            {
                "bitrate": 100000,
                "fps": 15,
                "height": 480,
                "qgc_stream_name": "customRgbVideo",
                "source_name": "imx219",
                "stream_id": 3,
                "stream_name": "rgb",
                "stream_url": "rtsp://10.161.108.237:8554/rgb",
                "width": 640
            },
            {
                "bitrate": 100000,
                "fps": 15,
                "height": 480,
                "qgc_stream_name": "customThermalVideo",
                "source_name": "flir1",
                "stream_id": 4,
                "stream_name": "thermal",
                "stream_url": "rtsp://10.161.108.237:8554/thermal",
                "width": 640
            }
        ]
    }
  */

    QNetworkReply* reply = _networkManager.get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray responseData = reply->readAll();
            qCDebug(DataCollectionControllerLog) << "Stream info HTTP request succeeded. Response:" << responseData;
            // Here you would parse the response and update stream URIs as needed
            QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
            if (jsonDoc.isObject()) {
                QJsonObject jsonObj = jsonDoc.object();
                // Example: {"streams": [{"name": "front_camera", "uri": "rtsp://..."}]}
                if (jsonObj.contains("streams") && jsonObj["streams"].isArray()) {
                    QJsonArray streamsArray = jsonObj["streams"].toArray();
                    for (const QJsonValue& streamVal : streamsArray) {
                        if (streamVal.isObject()) {
                            QJsonObject streamObj = streamVal.toObject();
                            QString name = streamObj["qgc_stream_name"].toString();
                            QString uri = streamObj["stream_url"].toString();
                            int streamId = streamObj["stream_id"].toInt();
                            bool rtspReady = streamObj["rtsp_ready"].toBool();  // Optional field to indicate if stream is ready
                            qCDebug(DataCollectionControllerLog) << "Stream:" << name << "URI:" << uri << "ID:" << streamId << "RTSP Ready:" << rtspReady;
                            // Update internal state with new URIs as needed
                            _handleStreamInfo(name, uri, streamId, rtspReady);
                        }
                    }
                }
            } else {
                qCDebug(DataCollectionControllerLog) << "Invalid JSON response for stream info";
            }
        } else {
            qCDebug(DataCollectionControllerLog) << "Stream info HTTP request failed. Error:" << reply->errorString();
        }
        reply->deleteLater();
    });
}

void DataCollectionController::startRecording() {
    qCDebug(DataCollectionControllerLog) << "Start recording invoked";
    if(!_isCollecting) {
        _pendingVidReadyStreamIds.clear();
        _vidReady = -1;
        _isCollecting = true;
        emit isCollectingChanged();
        _sendHttpRequest("start");
        
        // Don't send START notification yet - wait for Python to signal ready (vid_flags StreamsReady)
        // This ensures Python's MAVLink listener is active before we send STATUSTEXT
        
        // Start periodic stream info requests only if vehicle already connected
        if (_vehicle) {
            _startPeriodicStreamInfoRequest();
        }
        
        // Start periodic data sync for incremental backups
        _startPeriodicDataSync();
        
        MAVLinkProtocol::instance()->startLogging();
    }
}

void DataCollectionController::stopRecording() {
    qCDebug(DataCollectionControllerLog) << "Stop recording invoked";
    if (_isCollecting) {
        // Send stop command first (before _handleCollectionEnd which will send END notification)
        _sendHttpRequest("stop");
        
        // Perform full cleanup (sends END notification, stops streams, clears URIs, updates state)
        _handleCollectionEnd();
    }
}

QVariant DataCollectionController::getSourceField(const QString& source, const QString& field) const
{
    if (_sourceStatus.contains(source)) {
        const QVariantMap& statusMap = _sourceStatus[source];
        if (statusMap.contains(field)) {
            return statusMap[field];
        }
    }
    return QVariant();  // Return invalid QVariant if not found
}

QVariantMap DataCollectionController::getSourceStatus(const QString& source) const
{
    if (_sourceStatus.contains(source)) {
        return _sourceStatus[source];
    }
    return QVariantMap();  // Return empty map if source not found
}

void DataCollectionController::_onActiveVehicleChanged(Vehicle* vehicle)
{
    if (_vehicle) {
        disconnect(_vehicle, nullptr, this, nullptr);
        disconnect(_vehicle->vehicleLinkManager(), nullptr, this, nullptr);
        
        // Stop periodic stream info requests when vehicle disconnects
        qCDebug(DataCollectionControllerLog) << "Vehicle disconnecting - stopping periodic requests";
        _stopPeriodicStreamInfoRequest();
        
    }
    
    _vehicle = vehicle;
    
    if (_vehicle) {
        qCDebug(DataCollectionControllerLog) << "Connected to vehicle" << _vehicle->id();
        
        // Start periodic stream info requests if recording is active
        if (_isCollecting) {
            qCDebug(DataCollectionControllerLog) << "Recording active - starting periodic stream info requests";
            _startPeriodicStreamInfoRequest();
        }
        
        // Connect to communication lost signal (like upstream VideoManager line 617)
        connect(_vehicle->vehicleLinkManager(), &VehicleLinkManager::communicationLostChanged, this, 
            [this](bool communicationLost) {
                if (communicationLost) {
                    qCWarning(DataCollectionControllerLog) << "Communication lost - stopping streams and periodic requests";
                    
                    // Stop periodic MAVLink requests
                    _stopPeriodicStreamInfoRequest();
                    
                } else {
                    qCDebug(DataCollectionControllerLog) << "Communication restored - restarting periodic requests";
                    // Restart periodic requests when communication restored
                    _startPeriodicStreamInfoRequest();
                }
            });
        
        // Listen to ALL MAVLink messages for debugging
        connect(_vehicle, &Vehicle::mavlinkMessageReceived, this, [this](const mavlink_message_t& message) {
            if (message.compid != DATA_COLLECTION_COMPONENT_ID) {
                // Also check for STATUSTEXT messages from QGC (component 190)
                if (message.msgid == MAVLINK_MSG_ID_STATUSTEXT && message.compid == 190) {
                    mavlink_statustext_t statusText;
                    mavlink_msg_statustext_decode(&message, &statusText);
                    
                    // Ensure null termination
                    char text[sizeof(statusText.text) + 1];
                    memcpy(text, statusText.text, sizeof(statusText.text));
                    text[sizeof(statusText.text)] = '\0';
                    
                    const QString msgText = QString::fromUtf8(text);
                    
                    // Check for replay data check request
                    if (msgText.startsWith("REPLAY_DATA_CHECK:")) {
                        const QString flightId = msgText.mid(18).trimmed();
                        qCDebug(DataCollectionControllerLog) << "Received replay data check request for:" << flightId;
                        _handleReplayDataCheckRequest(flightId);
                    }
                }
                return; // Ignore non-DataCollection messages, it seems that drone's high amount of MAVLink traffic can overwhelm and responses from DataCollection python script get lost, I hope this solves the latency tomorrow, otherwise I am screwed :)
            }
            // Log every message type we receive
            static int msgCount = 0;
            if (msgCount++ < 10) {  // Only log first 10 to avoid spam
                qCDebug(DataCollectionControllerLog) << "Received MAVLink message ID:" << message.msgid;
            }
            
            if (message.msgid == MAVLINK_MSG_ID_NAMED_VALUE_FLOAT) {
                mavlink_named_value_float_t namedValue;
                mavlink_msg_named_value_float_decode(&message, &namedValue);
                
                QString name = QString::fromLatin1(namedValue.name, strnlen(namedValue.name, sizeof(namedValue.name)));
                
                qCDebug(DataCollectionControllerLog) << "NAMED_VALUE_FLOAT received:" << name << "=" << namedValue.value;


                _handleNamedValue(name, QVariant::fromValue((double)namedValue.value));
                
                if (name == "test_count") {
                    _testValue = namedValue.value;
                    emit testValueChanged();
                    qCDebug(DataCollectionControllerLog) << "Updated test_count:" << _testValue;
                }

            }
            else if (message.msgid == MAVLINK_MSG_ID_NAMED_VALUE_INT) {
                mavlink_named_value_int_t namedValue;
                mavlink_msg_named_value_int_decode(&message, &namedValue);

                const QString name = QString::fromLatin1(namedValue.name,
                                    strnlen(namedValue.name, sizeof(namedValue.name)));

                qCDebug(DataCollectionControllerLog) << "🟢 NAMED_VALUE_INT:" << name << "=" << namedValue.value;

                _handleNamedValue(name, QVariant::fromValue((qint64)namedValue.value));
            }
            else if (message.msgid == 269) { // VIDEO_STREAM_INFORMATION
                mavlink_video_stream_information_t streamInfo;
                mavlink_msg_video_stream_information_decode(&message, &streamInfo);

                QString streamName = QString::fromLatin1(streamInfo.name, strnlen(streamInfo.name, sizeof(streamInfo.name)));
                QString uri = QString::fromLatin1(streamInfo.uri, strnlen(streamInfo.uri, sizeof(streamInfo.uri)));

                qCDebug(DataCollectionControllerLog) << "VIDEO_STREAM_INFORMATION received: Stream Name =" << streamName << "URI =" << uri << "ID:" << streamInfo.stream_id;

                _handleStreamInfo(streamName, uri, streamInfo.stream_id, false);
            }
        });
        
    } else {
        qCDebug(DataCollectionControllerLog) << "No active vehicle";
    }
}

void DataCollectionController::_handleNamedValue(const QString& name, const QVariant& value)
{
    if (name == "dc_state") {
        const int prevState = _dcState;
        _dcState = value.toInt();
        emit dcStateChanged();
        
        // Detect collection end via state transitions
        if (_isCollecting && prevState == Running && 
            (_dcState == Completed || _dcState == Idle || _dcState == Error)) {
            qCDebug(DataCollectionControllerLog) << "DATA COLLECTION ENDED (state:" << _dcState << ") - stopping";
            _handleCollectionEnd();
        }
        return;
    }

    if (name == "dc_prog") {
        _dcProgress = value.toDouble();
        emit dcProgressChanged();
        return;
    }

    if (name == "dc_runid") {
        _dcRunId = value.toInt();
        emit dcRunIdChanged();
        return;
    }

    if (name == "dc_errcnt") {
        _dcErrorCount = value.toInt();
        emit dcErrorCountChanged();
        return;
    }

    if (name == "dc_ok") {
        _dcOk = value.toInt() != 0;
        emit dcOkChanged();
        return;
    }
    
    // RTK GPS values
    if (name == "rtk_fix") {
        _rtkFix = value.toInt();
        emit rtkFixChanged();
        return;
    }
    
    if (name == "rtk_sats") {
        _rtkSats = value.toInt();
        emit rtkSatsChanged();
        return;
    }
    
    if (name == "rtk_hdop") {
        _rtkHdop = value.toDouble();
        emit rtkHdopChanged();
        return;
    }
    
    if (name == "rtk_lat") {
        _rtkLat = value.toDouble();
        emit rtkLatChanged();
        return;
    }
    
    if (name == "rtk_lon") {
        _rtkLon = value.toDouble();
        emit rtkLonChanged();
        return;
    }
    
    if (name == "rtk_alt") {
        _rtkAlt = value.toDouble();
        emit rtkAltChanged();
        return;
    }

    // Per-source values: dc_<src>_<field>
    if (name.startsWith("dc_")) {
        // Examples:
        // dc_imu_st, dc_cam0_ok
        const QStringList parts = name.split('_');
        if (parts.size() == 3) {
            const QString src = parts[1];
            const QString field = parts[2];

            _updateSourceStatus(src, field, value);
            return;
        }
    }

    if (name == "vid_flags") {
        const int prevFlags = _vidFlags;
        _vidFlags = value.toInt();

        const bool ready = _vidFlags & StreamsReady;
        const bool active = _vidFlags & StreamsActive;
        const bool wasActive = prevFlags & StreamsActive;
        const bool wasReady = prevFlags & StreamsReady;
        const bool stopped = _vidFlags & StreamStopped;
        
        qCWarning(DataCollectionControllerLog) << "🔵 vid_flags received:"
            << "value=" << _vidFlags
            << "ready=" << ready << "(was:" << wasReady << ")"
            << "active=" << active << "(was:" << wasActive << ")"
            << "_isCollecting=" << _isCollecting;
        
        // Send START notification when Python first signals ready (StreamsReady flag set)
        if (ready && _isCollecting) {
            qCWarning(DataCollectionControllerLog) << "🔵 Condition met! Python signaled ready - sending START notification";
            _notifyDataCollectionState(true);
            _sendCollectionMetadata(true);  // Send detailed metadata immediately after state
            _startPeriodicStreamInfoRequest();
        } else if (_isCollecting) {
            qCWarning(DataCollectionControllerLog) << "🔵 Start notification NOT sent:"
                << "ready=" << ready
                << "wasReady=" << wasReady
                << "(need: ready=true && wasReady=false)";
        }
        
        if (active && ready) {
            qCDebug(DataCollectionControllerLog) << "DATA COLLECTION IS READY - starting stream info polling";
            // trigger polling VIDEO_STREAM_INFORMATION
            
        }
        
        // Detect end of collection: StreamsActive flag goes from 1 to 0
        if (stopped) {
            qCDebug(DataCollectionControllerLog) << "DATA COLLECTION ENDED (StreamsActive flag cleared) - stopping";
            _handleCollectionEnd();
        }

        // print all flags for debugging
        qCDebug(DataCollectionControllerLog) << "vid_flags changed: "
            << "StreamsReady=" << ready
            << "StreamsActive=" << active;

        //emit vidFlagsChanged();
        return;
    }

    if (name == "vid_count") {
        _vidCount = value.toInt();
        //emit vidCountChanged();
        return;
    }

    if (name == "vid_ready") {
        _vidReady = value.toInt();

        CustomPlugin* plugin = qobject_cast<CustomPlugin*>(QGCCorePlugin::instance());
        if (!plugin || !plugin->customVideoManager()) {
            qCWarning(DataCollectionControllerLog) << "vid_ready received but CustomVideoManager unavailable";
            _pendingVidReadyStreamIds.insert(_vidReady);
            return;
        }

        CustomVideoManager* videoManager = plugin->customVideoManager();
        bool matchedStream = false;

        for (int i = 0; i < CustomVideoManager::STREAM_COUNT; i++) {
            if (videoManager->getStreamId(i) != _vidReady) {
                continue;
            }

            matchedStream = true;
            videoManager->setStreamReady(i, true);

            const QString streamUri = videoManager->getStreamUri(i);
            if (streamUri.isEmpty()) {
                qCDebug(DataCollectionControllerLog) << "vid_ready received for stream index" << i
                                                     << "but URI is empty - waiting for stream info update";
                continue;
            }

            if (videoManager->isStreamDecoding(i)) {
                qCDebug(DataCollectionControllerLog) << "vid_ready for stream" << i
                                                     << "but stream already decoding";
                continue;
            }

            qCDebug(DataCollectionControllerLog) << "vid_ready received for stream" << i
                                                 << "(stream_id" << _vidReady << ") - starting stream";
            videoManager->startStream(i);
        }

        if (!matchedStream) {
            const bool inserted = !_pendingVidReadyStreamIds.contains(_vidReady);
            _pendingVidReadyStreamIds.insert(_vidReady);
            qCDebug(DataCollectionControllerLog) << "vid_ready received before stream mapping for stream_id" << _vidReady
                                                 << (inserted ? "(cached)" : "(already cached)");
        }

        return;
    }
}

void DataCollectionController::_updateSourceStatus(const QString& source, const QString& field, const QVariant& value)
{
    // Get or create the status map for this source
    QVariantMap& statusMap = _sourceStatus[source];
    
    // Update the field value
    statusMap[field] = value;
    
    qCDebug(DataCollectionControllerLog) << "Updated source status:" << source << field << "=" << value;
    
    // Emit signal that this source's status changed
    emit sourceStatusChanged(source);
}

void DataCollectionController::_handleCollectionEnd()
{
    // Guard against multiple calls (both dc_state and vid_flags can trigger this)
    if (!_isCollecting) {
        qCDebug(DataCollectionControllerLog) << "_handleCollectionEnd: Already cleaned up, skipping";
        return;
    }
    
    qCDebug(DataCollectionControllerLog) << "_handleCollectionEnd: Performing cleanup";
    
    // Force stop telemetry logging to save the .tlog file immediately
    // (normally only saved when QGC exits, but we want it saved per collection)
    qCDebug(DataCollectionControllerLog) << "Forcing telemetry log save";
    MAVLinkProtocol::instance()->stopLogging();
    
    // Send END notification BEFORE cleanup (Python might still be listening)
    _sendCollectionMetadata(false);
    // This must be sent last, it will trigger cleanup on external component side
    _notifyDataCollectionState(false);
    
    // Stop periodic stream info requests
    _stopPeriodicStreamInfoRequest();
    
    // Stop periodic data sync
    _stopPeriodicDataSync();
    
    // Explicitly stop all video streams to prevent timeout/resource leaks
    CustomPlugin* plugin = qobject_cast<CustomPlugin*>(QGCCorePlugin::instance());
    if (plugin && plugin->customVideoManager()) {
        CustomVideoManager* videoManager = plugin->customVideoManager();
        qCDebug(DataCollectionControllerLog) << "Stopping all video streams";
        
        for (int i = 0; i < CustomVideoManager::STREAM_COUNT; i++) {
            videoManager->stopStream(i);
            videoManager->setStreamUri(i, "");  // Clear URIs to prevent auto-restart
        }
    } else {
        qCWarning(DataCollectionControllerLog) << "Could not access CustomVideoManager for stream cleanup";
    }

    // Delay final sync to allow Python time to finalize metadata files
    // Without this delay, rsync starts immediately and may miss the final metadata.json
    qCDebug(DataCollectionControllerLog) << "Scheduling final data sync in" << FINAL_SYNC_DELAY_MS << "ms";
    QTimer::singleShot(FINAL_SYNC_DELAY_MS, this, [this]() {
        if (!_downloadInProgress) {
            qCDebug(DataCollectionControllerLog) << "Starting final data sync";
            _getReplayDataRemotePath();
        } else {
            qCDebug(DataCollectionControllerLog) << "Download still in progress, final sync will happen after current one completes";
        }
    });
    
    // Update recording state
    if (_isCollecting) {
        _isCollecting = false;
        emit isCollectingChanged();
    }

    _pendingVidReadyStreamIds.clear();
    _vidReady = -1;
    
    qCDebug(DataCollectionControllerLog) << "Collection cleanup completed";
}

void DataCollectionController::_downloadReplayData(const QString& remoteUsername, const QString& remoteHostName, const QString& remoteFilePath, const QString& localDirectoryPath)
{
    // Skip if download already in progress
    if (_downloadInProgress) {
        qCDebug(DataCollectionControllerLog) << "Download already in progress, skipping duplicate request";
        return;
    }
    
    qCDebug(DataCollectionControllerLog) << "Transferring data from:" << remoteFilePath << "to:" << localDirectoryPath;
    _downloadInProgress = true;
    
    // Check if source path is local (same filesystem)
    QDir sourceDir(remoteFilePath);
    if (sourceDir.exists()) {
        qCDebug(DataCollectionControllerLog) << "Source is local - using direct copy";
        
        // Use cp -r for local copy (faster and simpler)
        QProcess* copyProcess = new QProcess(this);
        QStringList arguments;
        arguments << "-r" << remoteFilePath << localDirectoryPath;
        
        connect(copyProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), 
                this, [this, copyProcess](int exitCode, QProcess::ExitStatus exitStatus) {
            _downloadInProgress = false;
            if (exitStatus == QProcess::NormalExit && exitCode == 0) {
                qCDebug(DataCollectionControllerLog) << "Local data copy completed successfully";
            } else {
                qCWarning(DataCollectionControllerLog) << "Local data copy failed with exit code" << exitCode;
            }
            copyProcess->deleteLater();
        });
        
        copyProcess->start("cp", arguments);
    } else {
        qCDebug(DataCollectionControllerLog) << "Source is remote - using rsync";
        
        // Use rsync for remote transfer (incremental, only transfers changed files)
        QProcess* rsync = new QProcess(this);
        QStringList arguments;
        arguments << "-avz" << QString("%1@%2:%3").arg(remoteUsername, remoteHostName, remoteFilePath) << localDirectoryPath;
        
        connect(rsync, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), 
                this, [this, rsync](int exitCode, QProcess::ExitStatus exitStatus) {
            _downloadInProgress = false;
            if (exitStatus == QProcess::NormalExit && exitCode == 0) {
                qCDebug(DataCollectionControllerLog) << "Remote data download completed successfully";
            } else {
                qCWarning(DataCollectionControllerLog) << "Remote data download failed with exit code" << exitCode;
            }
            rsync->deleteLater();
        });
        
        rsync->start("rsync", arguments);
    }
}

void DataCollectionController::_getReplayDataRemotePath()
{
    qCDebug(DataCollectionControllerLog) << "Requesting collected data info via HTTP";

    CustomPlugin* plugin = qobject_cast<CustomPlugin*>(QGCCorePlugin::instance());
    if (!plugin || !plugin->customSettings()) {
        qCDebug(DataCollectionControllerLog) << "CustomPlugin or CustomSettings not available";
        return;
    }
    QString httpUrl = plugin->customSettings()->httpUrl()->rawValue().toString();
    // Extract ip from httpUrl (assumes format http[s]://<ip>:<port>)
    QString ipAddress = httpUrl;
    QRegularExpression ipRegex("https?://([^:]+):\\d+");
    QRegularExpressionMatch match = ipRegex.match(httpUrl);
    if (match.hasMatch()) {
        ipAddress = match.captured(1);
    }
    
    // _remoteDataFetchpath = QString("%1@%2:%3").arg(plugin->customSettings()->remoteUserName()->rawValue().toString(), ipAddress, plugin->customSettings()->remoteDataPath()->rawValue().toString());
    
    QNetworkRequest request(QUrl(QString(httpUrl + "/data_collection_info")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = _networkManager.get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, ipAddress, plugin]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray responseData = reply->readAll();
            qCDebug(DataCollectionControllerLog) << "Data collection info HTTP request succeeded. Response:" << responseData;
            QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
            if (jsonDoc.isObject()) {
                QJsonObject jsonObj = jsonDoc.object();
                if (jsonObj.contains("folder_path")) {
                    QString folderPath = jsonObj["folder_path"].toString();
                    qCDebug(DataCollectionControllerLog) << "Received data collection folder path:" << folderPath;
                    // Now we have the remote folder path, we can start the rsync download
                    _downloadReplayData(plugin->customSettings()->remoteUserName()->rawValue().toString(), ipAddress, folderPath, plugin->customSettings()->dataCollectionSaveDirectory());
                } else {
                    qCWarning(DataCollectionControllerLog) << "Response JSON does not contain 'folder_path'";
                }
            } else {
                qCWarning(DataCollectionControllerLog) << "Invalid JSON response for data collection info";
            }
        } else {
            qCDebug(DataCollectionControllerLog) << "Data collection info HTTP request failed. Error:" << reply->errorString();
        }
        reply->deleteLater();
    });
}

void DataCollectionController::_startPeriodicStreamInfoRequest()
{
    // Don't start if already running
    if (_streamInfoTimer.isActive()) {
        qCDebug(DataCollectionControllerLog) << "Periodic stream info requests already running";
        return;
    }
    
    qCDebug(DataCollectionControllerLog) << "Starting periodic stream info requests (indefinite polling)";
    
    // Set timer to repeat indefinitely (not single-shot)
    _streamInfoTimer.setSingleShot(false);
    
    // Connect timer timeout to request handler
    connect(&_streamInfoTimer, &QTimer::timeout, this, &DataCollectionController::_requestStreamInfo);
    
    // Make initial request immediately, then start periodic timer
    QTimer::singleShot(1000, this, [this]() {
        if (_vehicle) {  // Check vehicle still exists
            _requestStreamInfo();
            _streamInfoTimer.start(STREAM_INFO_POLL_INTERVAL_MS);
        }
    });
}

// //-----------------------------------------------------------------------------
void DataCollectionController::_stopPeriodicStreamInfoRequest()
{
    qCDebug(DataCollectionControllerLog) << "Stopping periodic stream info requests";
    
    if (_streamInfoTimer.isActive()) {
        _streamInfoTimer.stop();
    }
    disconnect(&_streamInfoTimer, nullptr, this, nullptr);
    _streamInfoRetries = 0;
}

void DataCollectionController::_sendReadySignalToDataCollector()
{
    if (!_vehicle) {
        qCWarning(DataCollectionControllerLog) << "_sendReadySignalToDataCollector: No active vehicle";
        return;
    }
    
    // Get primary link
    SharedLinkInterfacePtr sharedLink = _vehicle->vehicleLinkManager()->primaryLink().lock();
    if (!sharedLink) {
        qCWarning(DataCollectionControllerLog) << "_sendReadySignalToDataCollector: No primary link";
        return;
    }
    
    qCDebug(DataCollectionControllerLog) << "_sendReadySignalToDataCollector: Sending QGC_VIDRDY=1";
    
    // Create NAMED_VALUE_INT message
    mavlink_message_t msg;
    mavlink_named_value_int_t namedValue;
    
    namedValue.time_boot_ms = 0; // works without timestamp
    strncpy(namedValue.name, "QGC_VIDRDY", sizeof(namedValue.name));
    namedValue.value = 1;
    
    // Encode and send
    mavlink_msg_named_value_int_encode_chan(
        MAVLinkProtocol::instance()->getSystemId(),
        MAVLinkProtocol::getComponentId(),
        sharedLink->mavlinkChannel(),
        &msg,
        &namedValue
    );
    
    _vehicle->sendMessageOnLinkThreadSafe(sharedLink.get(), msg);
}

// //-----------------------------------------------------------------------------
void DataCollectionController::_requestStreamInfo()
{
    if (!_vehicle) {
        qCWarning(DataCollectionControllerLog) << "_requestStreamInfo: No active vehicle";
        return;
    }
    
    qCDebug(DataCollectionControllerLog) << "_requestStreamInfo() - retries:" << _streamInfoRetries;
    
    // Alternate between modern REQUEST_MESSAGE and legacy command
    // (following VehicleCameraControl pattern)
    // if (_streamInfoRetries % 2 == 0) {
    //     qCDebug(DataCollectionControllerLog) << "  Sending REQUEST_MESSAGE:MAVLINK_MSG_ID_VIDEO_STREAM_INFORMATION";
    //     _vehicle->sendMavCommand(
    //         DATA_COLLECTION_COMPONENT_ID,                             // target component
    //         MAV_CMD_REQUEST_MESSAGE,                        // command id
    //         false,                                          // showError
    //         MAVLINK_MSG_ID_VIDEO_STREAM_INFORMATION,        // msgid (269)
    //         0);                                             // stream ID (0 = all streams)
    // } 
    // else {
    //     qCDebug(DataCollectionControllerLog) << "  Sending MAV_CMD_REQUEST_VIDEO_STREAM_INFORMATION (legacy)";
    //     _vehicle->sendMavCommand(
    //         103,                             // target component
    //         MAV_CMD_REQUEST_VIDEO_STREAM_INFORMATION,       // command id
    //         false,                                          // showError
    //         0);                                             // stream ID (0 = all streams)
    // }

    // alternative, since issues with long latency (many retries) and message comes very late


    _getStreamInfoHttp();
    
    _streamInfoRetries++;  // Increment for next poll
}

// //-----------------------------------------------------------------------------
void DataCollectionController::_streamInfoTimeout()
{
    
    if (!_vehicle) {
        qCWarning(DataCollectionControllerLog) << "Periodic poll fired but no vehicle - stopping timer";
        _stopPeriodicStreamInfoRequest();
        return;
    }
    
    qCDebug(DataCollectionControllerLog) << "Periodic stream info poll triggered";
    _requestStreamInfo();
}

// //-----------------------------------------------------------------------------
void DataCollectionController::_notifyDataCollectionState(bool collectionStarted)
{
    qCDebug(DataCollectionControllerLog) << "🔴 _notifyDataCollectionState CALLED:" << (collectionStarted ? "START" : "END");
    
    if (!_vehicle) {
        qCDebug(DataCollectionControllerLog) << "🔴 _notifyDataCollectionState: No active vehicle";
        return;
    }
    
    SharedLinkInterfacePtr sharedLink = _vehicle->vehicleLinkManager()->primaryLink().lock();
    if (!sharedLink) {
        qCDebug(DataCollectionControllerLog) << "🔴 _notifyDataCollectionState: No primary link";
        return;
    }
    
    mavlink_message_t msg;
    mavlink_named_value_int_t namedValue;
    
    namedValue.time_boot_ms = 0;
    strncpy(namedValue.name, "QGC_DCSTAT", sizeof(namedValue.name));
    // Match Python DcState enum: STARTING=2, STOPPING=4
    namedValue.value = collectionStarted ? Starting : Stopping;  
    
    mavlink_msg_named_value_int_encode_chan(
        MAVLinkProtocol::instance()->getSystemId(),
        MAVLinkProtocol::getComponentId(),
        sharedLink->mavlinkChannel(),
        &msg,
        &namedValue
    );
    
    _vehicle->sendMessageOnLinkThreadSafe(sharedLink.get(), msg);
}

// //-----------------------------------------------------------------------------
void DataCollectionController::_sendCollectionMetadata(bool collectionStarted)
{
    qCDebug(DataCollectionControllerLog) << "🟣 _sendCollectionMetadata CALLED:" << (collectionStarted ? "START" : "END");
    
    if (!_vehicle) {
        qCDebug(DataCollectionControllerLog) << "🟣 _sendCollectionMetadata: No active vehicle";
        return;
    }
    
    SharedLinkInterfacePtr sharedLink = _vehicle->vehicleLinkManager()->primaryLink().lock();
    if (!sharedLink) {
        qCDebug(DataCollectionControllerLog) << "🟣 _sendCollectionMetadata: No primary link";
        return;
    }
    
    CustomPlugin* plugin = qobject_cast<CustomPlugin*>(QGCCorePlugin::instance());
    if (!plugin || !plugin->customSettings()) {
        qCDebug(DataCollectionControllerLog) << "🟣 _sendCollectionMetadata: CustomPlugin not available";
        return;
    }
    
    // Build JSON payload with metadata
    QJsonObject jsonObj;
    jsonObj["state"] = collectionStarted ? Starting : Stopping;
    jsonObj["timestamp"] = QDateTime::currentSecsSinceEpoch();
    
    if (collectionStarted) {
        // START metadata - Use QGC's actual telemetry log flight ID
        // This ensures the flight_id matches the actual .tlog filename that QGC saves
        QString flightId = MAVLinkProtocol::instance()->currentFlightId();
        
        // Fallback if telemetry logging hasn't started yet (shouldn't normally happen)
        if (flightId.isEmpty()) {
            int vehicleId = _vehicle ? _vehicle->id() : 1;
            QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd-hh-mm-ss-zzz");
            flightId = QString::asprintf("%03d-%s", vehicleId, timestamp.toLocal8Bit().constData());
            qCWarning(DataCollectionControllerLog) << "🟠 No flight ID from MAVLinkProtocol, using fallback:" << flightId;
        } else {
            qCDebug(DataCollectionControllerLog) << "🟢 Using QGC telemetry flight ID:" << flightId;
        }
        
        jsonObj["flight_id"] = flightId;
        
    } else {
        // STOP metadata
        jsonObj["run_id"] = _dcRunId;
        jsonObj["error_count"] = _dcErrorCount;
        jsonObj["final_state"] = _dcState;
    }
    
    QJsonDocument jsonDoc(jsonObj);
    QByteArray jsonData = jsonDoc.toJson(QJsonDocument::Compact);
    
    // Validate payload size (TUNNEL max: 128 bytes)
    if (jsonData.size() > 128) {
        qCDebug(DataCollectionControllerLog) << "🟣 Payload too large:" << jsonData.size() 
                                              << "bytes (max 128). Truncating...";
        jsonData.truncate(128);
    }
    
    qCDebug(DataCollectionControllerLog) << "🟣 Sending TUNNEL payload (" << jsonData.size() 
                                          << "bytes):" << QString::fromUtf8(jsonData);
    
    // Create TUNNEL message
    mavlink_message_t msg;
    mavlink_tunnel_t tunnel;
    
    tunnel.target_system = _vehicle->id();
    tunnel.target_component = DATA_COLLECTION_COMPONENT_ID;
    tunnel.payload_type = MAV_TUNNEL_PAYLOAD_TYPE_UNKNOWN;  // Custom payload
    tunnel.payload_length = jsonData.size();
    memcpy(tunnel.payload, jsonData.constData(), jsonData.size());
    
    mavlink_msg_tunnel_encode_chan(
        MAVLinkProtocol::instance()->getSystemId(),
        MAVLinkProtocol::getComponentId(),
        sharedLink->mavlinkChannel(),
        &msg,
        &tunnel
    );
    
    _vehicle->sendMessageOnLinkThreadSafe(sharedLink.get(), msg);
    
    qCDebug(DataCollectionControllerLog) << "🟣 TUNNEL message sent successfully";
}

void DataCollectionController::_handleReplayDataCheckRequest(const QString &flightId)
{
    qCDebug(DataCollectionControllerLog) << "Checking replay data availability for flight ID:" << flightId;
    
    // TODO: Implement actual file check logic
    // For now, check if files exist in expected locations:
    // - ULog: /path/to/ulogs/<flightId>.ulg
    // - Video: /path/to/videos/<flightId>.mp4
    
    // Example implementation:
    const QString ulogPath = QStringLiteral("/tmp/ulogs/%1.ulg").arg(flightId);
    const QString videoPath = QStringLiteral("/tmp/videos/%1.mp4").arg(flightId);
    
    const bool ulogExists = QFile::exists(ulogPath);
    const bool videoExists = QFile::exists(videoPath);
    const bool dataAvailable = ulogExists; // Video optional, ULog required
    
    qCDebug(DataCollectionControllerLog) << "Replay data check results:"
                                          << "ULog:" << (ulogExists ? "FOUND" : "MISSING")
                                          << "Video:" << (videoExists ? "FOUND" : "MISSING");
    
    _sendReplayDataResponse(dataAvailable, flightId);
}

void DataCollectionController::_sendReplayDataResponse(bool dataAvailable, const QString &flightId)
{
    if (!_vehicle) {
        qCWarning(DataCollectionControllerLog) << "No vehicle for replay data response";
        return;
    }
    
    SharedLinkInterfacePtr sharedLink = _vehicle->vehicleLinkManager()->primaryLink().lock();
    if (!sharedLink) {
        qCWarning(DataCollectionControllerLog) << "No primary link for replay data response";
        return;
    }
    
    // Send response: "REPLAY_DATA_OK:<flightId>" or "REPLAY_DATA_UNAVAILABLE:<flightId>"
    const QString responseMsg = dataAvailable
        ? QStringLiteral("REPLAY_DATA_OK:%1").arg(flightId)
        : QStringLiteral("REPLAY_DATA_UNAVAILABLE:%1").arg(flightId);
    
    qCDebug(DataCollectionControllerLog) << "Sending replay data response:" << responseMsg;
    
    mavlink_statustext_t statusText;
    statusText.severity = dataAvailable ? MAV_SEVERITY_INFO : MAV_SEVERITY_WARNING;
    statusText.id = 0;
    statusText.chunk_seq = 0;
    strncpy(statusText.text, qPrintable(responseMsg), sizeof(statusText.text) - 1);
    statusText.text[sizeof(statusText.text) - 1] = '\0';
    
    mavlink_message_t msg;
    mavlink_msg_statustext_encode_chan(
        MAVLinkProtocol::instance()->getSystemId(),
        DATA_COLLECTION_COMPONENT_ID,  // Send FROM component 25
        sharedLink->mavlinkChannel(),
        &msg,
        &statusText
    );
    
    _vehicle->sendMessageOnLinkThreadSafe(sharedLink.get(), msg);
}

void DataCollectionController::_startPeriodicDataSync()
{
    // Don't start if already running
    if (_periodicSyncTimer.isActive()) {
        qCDebug(DataCollectionControllerLog) << "Periodic data sync already running";
        return;
    }
    
    qCDebug(DataCollectionControllerLog) << "Starting periodic data sync (every" << DATA_SYNC_INTERVAL_MS / 1000 << "seconds)";
    
    // Set timer to repeat indefinitely
    _periodicSyncTimer.setSingleShot(false);
    
    // Connect timer to sync handler
    connect(&_periodicSyncTimer, &QTimer::timeout, this, [this]() {
        if (_isCollecting && !_downloadInProgress) {
            qCDebug(DataCollectionControllerLog) << "Periodic sync triggered - backing up collected data";
            _getReplayDataRemotePath();
        }
    });
    
    // Start timer
    _periodicSyncTimer.start(DATA_SYNC_INTERVAL_MS);
}

void DataCollectionController::_stopPeriodicDataSync()
{
    qCDebug(DataCollectionControllerLog) << "Stopping periodic data sync";

    if (_periodicSyncTimer.isActive()) {
        _periodicSyncTimer.stop();
    }
    disconnect(&_periodicSyncTimer, nullptr, this, nullptr);

    // Trigger one final sync before shutting down the timer
    if (!_downloadInProgress) {
        qCDebug(DataCollectionControllerLog) << "Triggering final data sync on periodic sync stop";
        _getReplayDataRemotePath();
    }
}

QString findSessionMetadata(const QString& folderPath, const QString& flightId) {
    // Try symlink lookup first (fast path)
    QDir symlinkDir(folderPath + "/sessions/by_flight_id");
    if (symlinkDir.exists(flightId)) {
        QString sessionDir = symlinkDir.filePath(flightId);
        QFileInfo info(sessionDir);
        if (info.isSymLink()) {
            sessionDir = info.symLinkTarget();
        }
        return sessionDir + "/session_metadata.json";
    }
    
    // Fallback: scan all session directories (slower)
    QDir sessionsDir(folderPath + "/sessions");
    for (const QString& entry : sessionsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString metadataPath = sessionsDir.filePath(entry + "/session_metadata.json");
        // Parse JSON and check flight_id...
    }
    
    return QString();
}

void LogReplayLinkController::_requestReplayDataCheck(const QString &flightId)
{
    Vehicle* vehicle = MultiVehicleManager::instance()->activeVehicle();
    if (!vehicle) {
        qCDebug(LogReplayLinkControllerLog) << "No active vehicle for data check request";
        _setReplayDataStatus(NotRequired, QString("No vehicle connection"));
        return;
    }

    SharedLinkInterfacePtr sharedLink = vehicle->vehicleLinkManager()->primaryLink().lock();
    if (!sharedLink) {
        qCWarning(LogReplayLinkControllerLog) << "No primary link for data check request";
        _setReplayDataStatus(NotRequired, QString("No communication link"));
        return;
    }

    qCDebug(LogReplayLinkControllerLog) << "Requesting replay data check for flight ID:" << flightId;

    // Send request to component 25: "REPLAY_DATA_CHECK:<flightId>"
    const QString requestMsg = QStringLiteral("REPLAY_DATA_CHECK:") + flightId;

    mavlink_statustext_t statusText;
    statusText.severity = MAV_SEVERITY_INFO;
    statusText.id = 0;
    statusText.chunk_seq = 0;
    strncpy(statusText.text, qPrintable(requestMsg), sizeof(statusText.text) - 1);
    statusText.text[sizeof(statusText.text) - 1] = '\0';

    mavlink_message_t msg;
    mavlink_msg_statustext_encode_chan(
        MAVLinkProtocol::instance()->getSystemId(),
        MAVLinkProtocol::getComponentId(),
        sharedLink->mavlinkChannel(),
        &msg,
        &statusText
    );

    vehicle->sendMessageOnLinkThreadSafe(sharedLink.get(), msg);
}

void LogReplayLinkController::_handleStatusTextMessage(const mavlink_message_t &message)
{
    mavlink_statustext_t statusText;
    mavlink_msg_statustext_decode(&message, &statusText);
    
    // Ensure null termination
    char text[sizeof(statusText.text) + 1];
    memcpy(text, statusText.text, sizeof(statusText.text));
    text[sizeof(statusText.text)] = '\0';
    
    const QString msgText = QString::fromUtf8(text);
    
    // Only process messages from component 25
    if (message.compid != 25) {
        return;
    }
    
    // Get the active LogReplayLinkController instance
    LogReplayLinkController* replayController = LogReplayLinkController::activeInstance();
    if (!replayController) {
        qCDebug(DataCollectionControllerLog) << "No active replay controller - ignoring component 25 message";
        return;
    }
    
    const QString currentFlightId = replayController->currentFlightId();
    
    // Check for replay data availability responses
    if (msgText.startsWith("REPLAY_DATA_OK:")) {
        const QString flightId = msgText.mid(15).trimmed();
        if (flightId == currentFlightId) {
            qCDebug(DataCollectionControllerLog) << "Replay data available for flight:" << flightId;
            replayController->setReplayDataStatus(LogReplayLinkController::Ready, QString("Replay data ready"));
        }
    } else if (msgText.startsWith("REPLAY_DATA_UNAVAILABLE:")) {
        const QString flightId = msgText.mid(24).trimmed();
        if (flightId == currentFlightId) {
            qCWarning(DataCollectionControllerLog) << "Replay data unavailable for flight:" << flightId;
            replayController->setReplayDataStatus(LogReplayLinkController::Unavailable, QString("No matching sensor data found"));
        }
    }
}