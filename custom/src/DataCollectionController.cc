#include "DataCollectionController.h"
#include "QGCApplication.h"
#include "QGCLoggingCategory.h"
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkAccessManager>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

QGC_LOGGING_CATEGORY(DataCollectionControllerLog, "Custom.DataCollectionController")

DataCollectionController::DataCollectionController(QObject* parent)
    : QObject(parent)
{
    qCDebug(DataCollectionControllerLog) << "DataCollectionController created";
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

void DataCollectionController::sendHttpRequest(QString endpoint) {
    qCDebug(DataCollectionControllerLog) << "Sending HTTP request to endpoint:" << endpoint;
    
    QNetworkRequest request(QUrl(QString("http://127.0.0.1:5000/" + endpoint)));
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
        sendHttpRequest("start");
        emit isCollectingChanged();
    }
}

void DataCollectionController::stopRecording() {
    qCDebug(DataCollectionControllerLog) << "Stop recording invoked";
    if (_isCollecting) {
        _isCollecting = false;
        sendHttpRequest("stop");
        emit isCollectingChanged();
    }
}
