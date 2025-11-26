#pragma once

#include <QtCore/QObject>
#include <QtQml/qqml.h>

class DataCollectionController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool isCollecting READ isCollecting NOTIFY isCollectingChanged)

public:
    DataCollectionController(QObject* parent = nullptr);
    bool isCollecting() const {return _isCollecting;}

    Q_INVOKABLE void toggleRecording();
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();
    
signals:
    void isCollectingChanged();
    void imuDataReceived();

private:
    bool _isCollecting{false};

    void sendHttpRequest(QString endpoint);
    QNetworkAccessManager _networkManager;
};