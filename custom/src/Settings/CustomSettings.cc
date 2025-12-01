#include "CustomSettings.h"

DECLARE_SETTINGGROUP(Custom, "Custom")
{
    qmlRegisterUncreatableType<CustomSettings>("QGroundControl.SettingsManager", 1, 0, "CustomSettings", "Reference only");
}

DECLARE_SETTINGSFACT(CustomSettings, httpUrl)
DECLARE_SETTINGSFACT(CustomSettings, webSocketUrl)