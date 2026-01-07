#include "CustomSettings.h"

DECLARE_SETTINGGROUP(Custom, "Custom")
{
    qmlRegisterUncreatableType<CustomSettings>("QGroundControl.SettingsManager", 1, 0, "CustomSettings", "Reference only");
}

DECLARE_SETTINGSFACT(CustomSettings, httpUrl)
DECLARE_SETTINGSFACT(CustomSettings, folderName)
DECLARE_SETTINGSFACT(CustomSettings, timeout)
DECLARE_SETTINGSFACT(CustomSettings, enableVoxlLogging)
DECLARE_SETTINGSFACT(CustomSettings, enableRtkLogging)
DECLARE_SETTINGSFACT(CustomSettings, enableQgcStreaming)
DECLARE_SETTINGSFACT(CustomSettings, qgcIp)
DECLARE_SETTINGSFACT(CustomSettings, qgcPort)
DECLARE_SETTINGSFACT(CustomSettings, useHardwareEncoding)