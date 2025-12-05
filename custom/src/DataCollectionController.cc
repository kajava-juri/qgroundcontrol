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
    }
    
    _vehicle = vehicle;
    
    if (_vehicle) {
        qCDebug(DataCollectionControllerLog) << "Connected to vehicle" << _vehicle->id();
        
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

                if (_videoStreamName != streamName) {
                    _videoStreamName = streamName;
                    emit videoStreamNameChanged();
                }

                if (_videoStreamUri != uri) {
                    _videoStreamUri = uri;
                    emit videoStreamUriChanged();
                }
            }
        });
    } else {
        qCDebug(DataCollectionControllerLog) << "No active vehicle";
    }
}
