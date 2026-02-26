#pragma once

#include "SettingsGroup.h"

class CustomSettings : public SettingsGroup
{
    Q_OBJECT

public:
    CustomSettings(QObject* parent = nullptr);

// Fixes the following issue
// In file included from /home/kajava/work/qgroundcontrol/custom/src/Settings/CustomSettings.h:3,
//                  from /home/kajava/work/qgroundcontrol/custom/src/Settings/CustomSettings.cc:1:
// /home/kajava/work/qgroundcontrol/custom/src/Settings/CustomSettings.cc:3:22: error: ‘const char* CustomSettings::name’ is not a static data member of ‘class CustomSettings’
//     3 | DECLARE_SETTINGGROUP(Custom, "Custom")
//       |                      ^~~~~~
    DEFINE_SETTING_NAME_GROUP()

    DEFINE_SETTINGFACT(httpUrl)
    DEFINE_SETTINGFACT(folderName)
    DEFINE_SETTINGFACT(timeout)
    DEFINE_SETTINGFACT(noTimeout)
    DEFINE_SETTINGFACT(enableVoxlLogging)
    DEFINE_SETTINGFACT(enableRtkLogging)
    DEFINE_SETTINGFACT(enableQgcStreaming)
    DEFINE_SETTINGFACT(qgcIp)
    DEFINE_SETTINGFACT(qgcPort)
    DEFINE_SETTINGFACT(useHardwareEncoding)
    DEFINE_SETTINGFACT(remoteUserName)

    Q_PROPERTY(QString dataCollectionSaveDirectory        READ dataCollectionSaveDirectory          CONSTANT)

    QString dataCollectionSaveDirectory     ();

private:
    void _checkCustomSavePathDirectories();
};