#pragma once

#include <QtCore/QObject>
#include <QtQml/qqml.h>

class DataCollectionController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool isCollecting READ isCollecting NOTIFY isCollectingChanged)

public:
    explicit DataCollectionController(QObject* parent = nullptr);
    bool isCollecting() const {return _isCollecting;}
    
signals:
    void isCollectingChanged();

private:
    bool _isCollecting{false};
};