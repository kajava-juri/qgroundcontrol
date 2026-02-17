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

void DataCollectionController::_sendHttpRequest(QString endpoint) {
    qCDebug(DataCollectionControllerLog) << "Sending HTTP request to endpoint:" << endpoint;

    CustomPlugin* plugin = qobject_cast<CustomPlugin*>(QGCCorePlugin::instance());
    if (!plugin && !plugin->customSettings()) {
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

void DataCollectionController::startRecording() {
    qCDebug(DataCollectionControllerLog) << "Start recording invoked";
    if(!_isCollecting) {
        _isCollecting = true;
        emit isCollectingChanged();
        _sendHttpRequest("start");
        
        // Don't send START notification yet - wait for Python to signal ready (vid_flags StreamsReady)
        // This ensures Python's MAVLink listener is active before we send STATUSTEXT
        
        // Start periodic stream info requests only if vehicle already connected
        if (_vehicle) {
            _startPeriodicStreamInfoRequest();
        }
        // Otherwise, _onActiveVehicleChanged will start it when vehicle connects
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

                qCDebug(DataCollectionControllerLog) << "VIDEO_STREAM_INFORMATION received: Stream Name =" << streamName << "URI =" << uri;

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
                        videoManager->setStreamUri(index, uri);  // This will automatically restart if needed
                        qCDebug(DataCollectionControllerLog) << "Updated stream URI for" << streamName << "to" << uri;
                        
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
        if (wasActive && !active && _isCollecting) {
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
    
    // Send END notification BEFORE cleanup (Python might still be listening)
    _sendCollectionMetadata(false);
    // This must be sent last, it will trigger cleanup on external component side
    _notifyDataCollectionState(false);
    
    // Stop periodic stream info requests
    _stopPeriodicStreamInfoRequest();
    
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
    
    // Update recording state
    if (_isCollecting) {
        _isCollecting = false;
        emit isCollectingChanged();
    }
    
    
    qCDebug(DataCollectionControllerLog) << "Collection cleanup completed";
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
    if (_streamInfoRetries % 2 == 0) {
        qCDebug(DataCollectionControllerLog) << "  Sending REQUEST_MESSAGE:MAVLINK_MSG_ID_VIDEO_STREAM_INFORMATION";
        _vehicle->sendMavCommand(
            DATA_COLLECTION_COMPONENT_ID,                             // target component
            MAV_CMD_REQUEST_MESSAGE,                        // command id
            false,                                          // showError
            MAVLINK_MSG_ID_VIDEO_STREAM_INFORMATION,        // msgid (269)
            0);                                             // stream ID (0 = all streams)
    } 
    // else {
    //     qCDebug(DataCollectionControllerLog) << "  Sending MAV_CMD_REQUEST_VIDEO_STREAM_INFORMATION (legacy)";
    //     _vehicle->sendMavCommand(
    //         103,                             // target component
    //         MAV_CMD_REQUEST_VIDEO_STREAM_INFORMATION,       // command id
    //         false,                                          // showError
    //         0);                                             // stream ID (0 = all streams)
    // }
    
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
        // START metadata - Generate flight_id matching QGC telemetry log naming
        // Format: {vehicle_id}-{timestamp}
        // Example: 001-2026-02-15-14-23-45-123
        int vehicleId = _vehicle ? _vehicle->id() : 1;
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd-hh-mm-ss-zzz");
        QString flightId = QString::asprintf("%03d-%s", vehicleId, timestamp.toLocal8Bit().constData());
        
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
