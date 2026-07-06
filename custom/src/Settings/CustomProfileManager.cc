#include "CustomProfileManager.h"
#include "CustomSettings.h"
#include "QmlObjectListModel.h"

#include <QtCore/QSettings>
#include <QtCore/QUuid>
#include <QtQml/QQmlEngine>

CustomProfileManager::CustomProfileManager(QObject* parent)
    : QObject  (parent)
    , _profiles(new QmlObjectListModel(this))
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);

    QSettings settings;
    _profileIds = settings.value(QString(kProfileIdsKey)).toStringList();
    if (_profileIds.isEmpty()) {
        // First run of the profiles feature: fold the existing (pre-profiles) Custom settings
        // in as the "default" profile in place, so no data migration is needed.
        _profileIds << QString(kDefaultProfileId);
        settings.setValue(QString(kProfileIdsKey), _profileIds);
    }

    for (const QString& id : std::as_const(_profileIds)) {
        _profiles->append(_instantiateProfile(id));
    }

    const QString activeId = settings.value(QString(kActiveProfileIdKey), QString(kDefaultProfileId)).toString();
    int activeIndex = _profileIds.indexOf(activeId);
    if (activeIndex < 0) {
        activeIndex = 0;
    }
    _activeProfile = qobject_cast<CustomSettings*>(_profiles->get(activeIndex));
}

QString CustomProfileManager::_settingsGroupForId(const QString& profileId) const
{
    if (profileId == QString(kDefaultProfileId)) {
        return QStringLiteral("Custom");
    }
    return QStringLiteral("Custom/Profiles/%1").arg(profileId);
}

QString CustomProfileManager::_generateProfileId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

CustomSettings* CustomProfileManager::_instantiateProfile(const QString& profileId)
{
    return (profileId == QString(kDefaultProfileId))
            ? new CustomSettings(this)
            : new CustomSettings(profileId, this);
}

void CustomProfileManager::_persistProfileList()
{
    QSettings settings;
    settings.setValue(QString(kProfileIdsKey), _profileIds);

    const int activeIndex = _activeProfile ? _profiles->indexOf(_activeProfile) : -1;
    if (activeIndex >= 0) {
        settings.setValue(QString(kActiveProfileIdKey), _profileIds.value(activeIndex));
    }
}

CustomSettings* CustomProfileManager::createProfile(const QString& name)
{
    const QString id = _generateProfileId();

    CustomSettings* profile = new CustomSettings(id, this);
    profile->profileName()->setRawValue(name);

    _profileIds.append(id);
    _profiles->append(profile);

    _persistProfileList();

    return profile;
}

CustomSettings* CustomProfileManager::duplicateProfile(int index, const QString& newName)
{
    if (index < 0 || index >= _profiles->count()) {
        return nullptr;
    }

    // Copy the source profile's raw QSettings values wholesale, rather than enumerating every
    // Fact by hand. The new CustomSettings instance then picks them up the same way any
    // SettingsFact reads its persisted value at construction time.
    const QString sourceGroup = _settingsGroupForId(_profileIds.at(index));
    const QString id = _generateProfileId();
    const QString destGroup = _settingsGroupForId(id);

    QSettings settings;
    settings.beginGroup(sourceGroup);
    const QStringList keys = settings.allKeys();
    QVariantMap values;
    for (const QString& key : keys) {
        values.insert(key, settings.value(key));
    }
    settings.endGroup();

    settings.beginGroup(destGroup);
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        settings.setValue(it.key(), it.value());
    }
    settings.endGroup();

    CustomSettings* copy = new CustomSettings(id, this);
    copy->profileName()->setRawValue(newName);

    _profileIds.append(id);
    _profiles->append(copy);

    _persistProfileList();

    return copy;
}

void CustomProfileManager::removeProfile(int index)
{
    if (index < 0 || index >= _profiles->count() || _profiles->count() <= 1) {
        return;
    }

    QObject* removed = _profiles->get(index);
    const bool removingActive = (removed == _activeProfile);

    _profiles->removeAt(index);
    _profileIds.removeAt(index);
    removed->deleteLater();

    if (removingActive) {
        _activeProfile = qobject_cast<CustomSettings*>(_profiles->get(0));
        emit activeProfileChanged();
    }

    _persistProfileList();
}

void CustomProfileManager::setActiveProfile(int index)
{
    if (index < 0 || index >= _profiles->count()) {
        return;
    }

    CustomSettings* profile = qobject_cast<CustomSettings*>(_profiles->get(index));
    if (profile == _activeProfile) {
        return;
    }

    _activeProfile = profile;
    _persistProfileList();
    emit activeProfileChanged();
}

void CustomProfileManager::removeProfile(CustomSettings* profile)
{
    removeProfile(_profiles->indexOf(profile));
}

void CustomProfileManager::setActiveProfile(CustomSettings* profile)
{
    setActiveProfile(_profiles->indexOf(profile));
}
