/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include "FirmwarePluginFactory.h"
#include "QGCMAVLink.h"

class CustomFirmwarePlugin;
class ArduCopterFirmwarePlugin;
class ArduPlaneFirmwarePlugin;
class ArduRoverFirmwarePlugin;
class ArduSubFirmwarePlugin;
class FirmwarePlugin;

/// This custom implementation of FirmwarePluginFactory creates a custom build which supports
/// both PX4 Pro and ArduPilot firmware running on various vehicle types.
class CustomFirmwarePluginFactory : public FirmwarePluginFactory
{
    Q_OBJECT

public:
    CustomFirmwarePluginFactory();
    QList<QGCMAVLink::FirmwareClass_t> supportedFirmwareClasses() const final;
    QList<QGCMAVLink::VehicleClass_t> supportedVehicleClasses() const final;
    FirmwarePlugin *firmwarePluginForAutopilot(MAV_AUTOPILOT autopilotType, MAV_TYPE vehicleType) final;

private:
    CustomFirmwarePlugin *_pluginInstance = nullptr;
    ArduCopterFirmwarePlugin *_arduCopterPluginInstance = nullptr;
    ArduPlaneFirmwarePlugin *_arduPlanePluginInstance = nullptr;
    ArduRoverFirmwarePlugin *_arduRoverPluginInstance = nullptr;
    ArduSubFirmwarePlugin *_arduSubPluginInstance = nullptr;
};

extern CustomFirmwarePluginFactory CustomFirmwarePluginFactoryImp;
