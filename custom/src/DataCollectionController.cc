#include "DataCollectionController.h"
#include "QGCApplication.h"
#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(DataCollectionControllerLog, "Custom.DataCollectionController")

DataCollectionController::DataCollectionController(QObject* parent)
    : QObject(parent)
{
    qCDebug(DataCollectionControllerLog) << "DataCollectionController created";
}

void DataCollectionController::toggleRecording() {
    if(_isCollecting) {
        qCDebug(DataCollectionControllerLog) << "Stopping data collection";
        _isCollecting = false;
        emit isCollectingChanged();
    } else {
        qCDebug(DataCollectionControllerLog) << "Starting data collection";
        _isCollecting = true;
        emit isCollectingChanged();
    }
}