#include "DataCollectionController.h"
#include "QGCApplication.h"
#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(DataCollectionControllerLog, "Custom.DataCollectionController")

DataCollectionController::DataCollectionController(QObject* parent)
    : QObject(parent)
{
    qCDebug(DataCollectionControllerLog) << "DataCollectionController created";
}