#include "DataCollectionController.h"
#include "QGCApplication.h"
#include "QGCLoggingCategory.h"
#include "Vehicle.h"
#include "MultiVehicleManager.h"
#include "CustomPlugin.h"
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkAccessManager>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

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
        qCWarning(DataCollectionControllerLog) << "CustomPlugin or CustomSettings not available";
        return;
    }
    QString httpUrl = plugin->customSettings()->httpUrl()->rawValue().toString();
    
    QNetworkRequest request(QUrl(QString(httpUrl + "/" + endpoint)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    // Create JSON body (empty object, Flask will use defaults)
    QJsonObject jsonObj;
    jsonObj["folder"] = plugin->customSettings()->folderName()->rawValue().toString();
    jsonObj["timeout"] = plugin->customSettings()->timeout()->rawValue().toInt();
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
            qCWarning(DataCollectionControllerLog) << "HTTP request to" << endpoint << "failed. Error:" << reply->errorString();
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
        _sendHttpRequest("start");
        emit isCollectingChanged();
    }

    _startPeriodicStreamInfoRequest();
}

void DataCollectionController::stopRecording() {
    qCDebug(DataCollectionControllerLog) << "Stop recording invoked";
    if (_isCollecting) {
        _isCollecting = false;
        _sendHttpRequest("stop");
        emit isCollectingChanged();
    }
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
                
                if (name == "test_count") {
                    _testValue = namedValue.value;
                    emit testValueChanged();
                    qCDebug(DataCollectionControllerLog) << "Updated test_count:" << _testValue;
                }
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
                        // Keep polling - don't stop timer (continuous monitoring)
                        break;
                    }
                }
            }
        });
        
        qCDebug(DataCollectionControllerLog) << "Listening for VIDEO_STREAM_INFORMATION messages from component 103";
        qCDebug(DataCollectionControllerLog) << "QGCCameraManager will request them automatically after CAMERA_INFORMATION is received";
    } else {
        qCDebug(DataCollectionControllerLog) << "No active vehicle";
    }
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
            103,                             // target component
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
