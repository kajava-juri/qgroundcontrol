#pragma once

#include <QtCore/QObject>
#include <QtQml/qqml.h>
#include <QtNetwork/QNetworkAccessManager>
#include "Vehicle.h"

class DataCollectionController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool isCollecting READ isCollecting NOTIFY isCollectingChanged)
    Q_PROPERTY(double testValue READ testValue NOTIFY testValueChanged)
    Q_PROPERTY(QString videoStreamName READ videoStreamName NOTIFY videoStreamNameChanged)
    Q_PROPERTY(QString videoStreamUri READ videoStreamUri NOTIFY videoStreamUriChanged)

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
    void videoStreamUriChanged();

private slots:
    void _onActiveVehicleChanged(Vehicle* vehicle);

private:
    bool _isCollecting{false};
    Vehicle* _vehicle{nullptr};
    double _testValue{0.0};
    QString _videoStreamName;;
    QString _videoStreamUri;

    void _sendHttpRequest(QString endpoint);
    QNetworkAccessManager _networkManager;
};