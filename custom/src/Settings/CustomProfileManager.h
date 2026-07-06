#pragma once

#include <QtCore/QObject>
#include <QtCore/QStringList>

class CustomSettings;
class QmlObjectListModel;

/// Owns the list of CustomSettings profile instances, persists which profile ids exist and which
/// one is active, and exposes both to QML. Each profile is a full CustomSettings instance backed
/// by its own QSettings subtree ("Custom" for the original/default profile, "Custom/Profiles/<id>"
/// for any created afterwards) so every profile gets the full set of DEFINE_SETTINGFACT fields,
/// validation, and units for free.
class CustomProfileManager : public QObject
{
    Q_OBJECT
    Q_MOC_INCLUDE("QmlObjectListModel.h")

    Q_PROPERTY(QmlObjectListModel* profiles      READ profiles      CONSTANT)
    Q_PROPERTY(CustomSettings*     activeProfile READ activeProfile NOTIFY activeProfileChanged)

public:
    explicit CustomProfileManager(QObject* parent = nullptr);

    QmlObjectListModel* profiles() const { return _profiles; }
    CustomSettings*     activeProfile() const { return _activeProfile; }

    Q_INVOKABLE CustomSettings* createProfile(const QString& name);
    Q_INVOKABLE CustomSettings* duplicateProfile(int index, const QString& newName);
    Q_INVOKABLE void            removeProfile(int index);
    Q_INVOKABLE void            removeProfile(CustomSettings* profile);
    Q_INVOKABLE void            setActiveProfile(int index);
    Q_INVOKABLE void            setActiveProfile(CustomSettings* profile);

signals:
    void activeProfileChanged();

private:
    static constexpr const char* kDefaultProfileId    = "default";
    static constexpr const char* kProfileIdsKey       = "CustomProfileManager/profileIds";
    static constexpr const char* kActiveProfileIdKey  = "CustomProfileManager/activeProfileId";

    QString         _settingsGroupForId(const QString& profileId) const;
    QString         _generateProfileId() const;
    CustomSettings* _instantiateProfile(const QString& profileId);
    void            _persistProfileList();

    QmlObjectListModel* _profiles      = nullptr;
    CustomSettings*      _activeProfile = nullptr;
    QStringList          _profileIds;
};
