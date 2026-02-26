#include "CustomSettings.h"
#include "SettingsManager.h"
#include "AppSettings.h"

#include <QtCore/QDir>

DECLARE_SETTINGGROUP(Custom, "Custom")
{
    qmlRegisterUncreatableType<CustomSettings>("QGroundControl.SettingsManager", 1, 0, "CustomSettings", "Reference only");

    _checkCustomSavePathDirectories();
}

DECLARE_SETTINGSFACT(CustomSettings, httpUrl)
DECLARE_SETTINGSFACT(CustomSettings, folderName)
DECLARE_SETTINGSFACT(CustomSettings, timeout)
DECLARE_SETTINGSFACT(CustomSettings, noTimeout)
DECLARE_SETTINGSFACT(CustomSettings, enableVoxlLogging)
DECLARE_SETTINGSFACT(CustomSettings, enableRtkLogging)
DECLARE_SETTINGSFACT(CustomSettings, enableQgcStreaming)
DECLARE_SETTINGSFACT(CustomSettings, qgcIp)
DECLARE_SETTINGSFACT(CustomSettings, qgcPort)
DECLARE_SETTINGSFACT(CustomSettings, useHardwareEncoding)
DECLARE_SETTINGSFACT(CustomSettings, remoteUserName)

static constexpr const char* dataCollectionDirectory = "DataCollection";

QString CustomSettings::dataCollectionSaveDirectory()
{
    QString rootPath = SettingsManager::instance()->appSettings()->savePath()->rawValue().toString();
    if (rootPath.isEmpty()) {
        return QString();
    }

    QDir rootDir(rootPath);
    if (!rootDir.exists()) {
        return QString();
    }

    return rootDir.filePath(dataCollectionDirectory);
}

void CustomSettings::_checkCustomSavePathDirectories()
{
    QString rootPath = SettingsManager::instance()->appSettings()->savePath()->rawValue().toString();
    if (!rootPath.isEmpty()) {
        QDir savePathDir(rootPath);
        if (savePathDir.exists()) {
            savePathDir.mkdir(dataCollectionDirectory);
        }
    }
}


/*
void AppSettings::_checkSavePathDirectories(void)
{
    QDir savePathDir(savePath()->rawValue().toString());
    if (!savePathDir.exists()) {
        QDir().mkpath(savePathDir.absolutePath());
    }
    if (savePathDir.exists()) {
        savePathDir.mkdir(parameterDirectory);
        savePathDir.mkdir(telemetryDirectory);
        savePathDir.mkdir(missionDirectory);
        savePathDir.mkdir(logDirectory);
        savePathDir.mkdir(videoDirectory);
        savePathDir.mkdir(photoDirectory);
        savePathDir.mkdir(crashDirectory);
        savePathDir.mkdir(mavlinkActionsDirectory);
        savePathDir.mkdir(settingsDirectory);
    }
}
*/

/*
QString AppSettings::_childSavePath(const char* directory)
{
    const QString rootPath = savePath()->rawValue().toString();
    if (rootPath.isEmpty()) {
        return QString();
    }

    const QDir rootDir(rootPath);
    if (!rootDir.exists()) {
        return QString();
    }

    return rootDir.filePath(directory);
}
*/