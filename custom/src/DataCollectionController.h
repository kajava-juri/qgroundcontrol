#pragma once

#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtQml/qqml.h>
#include <QtNetwork/QNetworkAccessManager>
#include "Vehicle.h"

class DataCollectionController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool isCollecting READ isCollecting NOTIFY isCollectingChanged)
    Q_PROPERTY(double testValue READ testValue NOTIFY testValueChanged)

public:
    DataCollectionController(QObject* parent = nullptr);
    bool isCollecting() const {return _isCollecting;}
    double testValue() const {return _testValue;}
    QString videoStreamName() const {return _videoStreamName;}
    QString videoStreamUri() const {return _videoStreamUri;}

    Q_INVOKABLE void toggleRecording();
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();
    
signals:
    void isCollectingChanged();
    void testValueChanged();
    void videoStreamNameChanged();

private slots:
    void _onActiveVehicleChanged(Vehicle* vehicle);
    void _requestStreamInfo();          // Request VIDEO_STREAM_INFORMATION
    void _streamInfoTimeout();          // Timeout handler

private:
    bool _isCollecting{false};
    Vehicle* _vehicle{nullptr};
    double _testValue{0.0};
    QString _videoStreamName;;
    QString _videoStreamUri;

    void _sendHttpRequest(QString endpoint);
    void _startPeriodicStreamInfoRequest();  // Start periodic requests
    void _stopPeriodicStreamInfoRequest();   // Stop periodic requests
    
    QNetworkAccessManager _networkManager;
    QTimer _streamInfoTimer;            // Timer for periodic requests
    int _streamInfoRetries{0};          // Counter for alternating between modern/legacy commands
    
    static constexpr int STREAM_INFO_POLL_INTERVAL_MS = 5000;  // Poll every 5 seconds
};