/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "LogReplayLinkController.h"
#include "AppSettings.h"
#include "CustomVideoManager.h"
#include "LinkConfiguration.h"
#include "LinkManager.h"
#include "MAVLinkProtocol.h"
#include "MultiVehicleManager.h"
#include "QGCApplication.h"
#include "QGCCorePlugin.h"
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"
#include "Vehicle.h"
#include "CustomPlugin.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QtEndian>

QGC_LOGGING_CATEGORY(LogReplayLinkControllerLog, "Comms.LogReplayLinkController")

LogReplayLinkController* LogReplayLinkController::_activeInstance = nullptr;

LogReplayLinkController::LogReplayLinkController(QObject *parent)
    : QObject(parent)
{
    qCDebug(LogReplayLinkControllerLog) << this;
}

LogReplayLinkController::~LogReplayLinkController()
{
    if (_activeInstance == this) {
        _activeInstance = nullptr;
    }
    qCDebug(LogReplayLinkControllerLog) << this;
}

void LogReplayLinkController::setLink(LogReplayLink *link)
{
    if (_link) {
        (void) disconnect(_link);
        (void) disconnect(this, &LogReplayLinkController::playbackSpeedChanged, _link, &LogReplayLink::setPlaybackSpeed);

        _isPlaying = false;
        emit isPlayingChanged(_isPlaying);

        _percentComplete = 0;
        emit percentCompleteChanged(_percentComplete);

        _playheadTime.clear();
        emit playheadTimeChanged(_playheadTime);

        _totalTime.clear();
        emit totalTimeChanged(_totalTime);

        _setReplayDataStatus(NotRequired);
        _currentFlightId.clear();

        _link = nullptr;
        if (_activeInstance == this) {
            _activeInstance = nullptr;
        }
        emit linkChanged(_link);
    }

    if (link) {
        _link = link;
        _activeInstance = this;  // Track this as the active replay controller

        (void) connect(_link, &LogReplayLink::connected, this, &LogReplayLinkController::_linkConnected);
        (void) connect(_link, &LogReplayLink::logFileStats, this, &LogReplayLinkController::_logFileStats);
        (void) connect(_link, &LogReplayLink::playbackStarted, this, &LogReplayLinkController::_playbackStarted);
        (void) connect(_link, &LogReplayLink::playbackPaused, this, &LogReplayLinkController::_playbackPaused);
        (void) connect(_link, &LogReplayLink::playbackPercentCompleteChanged, this, &LogReplayLinkController::_playbackPercentCompleteChanged);
        (void) connect(_link, &LogReplayLink::currentLogTimeSecs, this, &LogReplayLinkController::_currentLogTimeSecs);
        (void) connect(_link, &LogReplayLink::disconnected, this, &LogReplayLinkController::_linkDisconnected);

        (void) connect(this, &LogReplayLinkController::playbackSpeedChanged, _link, &LogReplayLink::setPlaybackSpeed);

        // If replay was loaded from metadata folder, external data is already verified
        if (!_replayFlightId.isEmpty()) {
            _setReplayDataStatus(Ready, QString("Replay data ready"));
        } else {
            _setReplayDataStatus(NotRequired, QString("No external data source"));
        }

        emit linkChanged(_link);
    }
}

void LogReplayLinkController::setIsPlaying(bool isPlaying) const
{
    if (!_link) {
        return;
    }

    // Don't allow playback if replay data is not ready
    if (isPlaying && _replayDataStatus != Ready && _replayDataStatus != NotRequired) {
        qCWarning(LogReplayLinkControllerLog) << "Cannot start playback - replay data status:" << _replayDataStatus;
        return;
    }

    if (isPlaying) {
        _link->play();
    } else {
        _link->pause();
    }
}

void LogReplayLinkController::setPercentComplete(qreal percentComplete) const
{
    if (!_link) {
        return;
    }

    _link->movePlayhead(percentComplete);
}

void LogReplayLinkController::_logFileStats(uint32_t logDurationSecs)
{
    const QString totalTime = _secondsToHMS(logDurationSecs);
    if (totalTime != _totalTime) {
        _totalTime = totalTime;
        emit totalTimeChanged(_totalTime);
    }
}

void LogReplayLinkController::_playbackStarted()
{
    if (!_isPlaying) {
        _isPlaying = true;
        emit isPlayingChanged(_isPlaying);
    }
}

void LogReplayLinkController::_playbackPaused()
{
    if (_isPlaying) {
        _isPlaying = false;
        emit isPlayingChanged(_isPlaying);
    }
}

void LogReplayLinkController::_playbackAtEnd()
{
    if (_isPlaying) {
        _isPlaying = false;
        emit isPlayingChanged(_isPlaying);
    }
}

void LogReplayLinkController::_playbackPercentCompleteChanged(qreal percentComplete)
{
    if (percentComplete != _percentComplete) {
        _percentComplete = percentComplete;
        emit percentCompleteChanged(_percentComplete);
    }
}

void LogReplayLinkController::_currentLogTimeSecs(uint32_t secs)
{
    if (secs != _playheadSecs) {
        _playheadSecs = secs;
        _playheadTime = _secondsToHMS(secs);
        emit playheadTimeChanged(_playheadTime);
    }
}

QString LogReplayLinkController::_secondsToHMS(uint32_t seconds)
{
    uint32_t secondsPart = seconds;
    uint32_t minutesPart = secondsPart / 60;
    const uint32_t hoursPart = minutesPart / 60;
    secondsPart -= (60 * minutesPart);
    minutesPart -= (60 * hoursPart);

    QString result = QStringLiteral("%2m:%3s").arg(minutesPart, 2, 10, QLatin1Char('0')).arg(secondsPart, 2, 10, QLatin1Char('0'));
    if (hoursPart != 0) {
        (void) result.prepend(QStringLiteral("%1h:").arg(hoursPart, 2, 10, QLatin1Char('0')));
    }

    return result;
}

void LogReplayLinkController::_linkConnected()
{
    qCDebug(LogReplayLinkControllerLog) << "Log replay connected";
    _notifyExternalComponent(true);
}

void LogReplayLinkController::_linkDisconnected()
{
    qCDebug(LogReplayLinkControllerLog) << "Log replay disconnected";
    _notifyExternalComponent(false);
    setLink(nullptr);
}

void LogReplayLinkController::_notifyExternalComponent(bool sessionStarted)
{
    Vehicle* vehicle = MultiVehicleManager::instance()->activeVehicle();
    if (!vehicle || !_link) {
        qCDebug(LogReplayLinkControllerLog) << "No vehicle or link for notification";
        return;
    }

    SharedLinkInterfacePtr sharedLink = vehicle->vehicleLinkManager()->primaryLink().lock();
    if (!sharedLink) {
        qCWarning(LogReplayLinkControllerLog) << "No primary link available";
        return;
    }

    // Get log file name from link configuration
    const SharedLinkConfigurationPtr config = _link->linkConfiguration();
    const LogReplayConfiguration* logConfig = qobject_cast<const LogReplayConfiguration*>(config.get());
    if (!logConfig) {
        qCWarning(LogReplayLinkControllerLog) << "Could not get log configuration";
        return;
    }

    const QString logFilePath = logConfig->logFilename();
    const QFileInfo fileInfo(logFilePath);
    QString logFileName = fileInfo.fileName();

    // STATUSTEXT has 50 byte limit - need to fit "LOG_REPLAY_START:" (17 chars) + filename
    const QString prefix = sessionStarted ? QStringLiteral("LOG_REPLAY_START:") : QStringLiteral("LOG_REPLAY_END:");
    constexpr int maxStatusTextLen = 50;
    const int maxFilenameLen = maxStatusTextLen - prefix.length() - 1;  // -1 for safety
    
    if (logFileName.length() > maxFilenameLen) {
        // Truncate filename but keep extension
        const QString extension = fileInfo.suffix();
        const QString baseName = fileInfo.completeBaseName();
        const int keepLen = maxFilenameLen - extension.length() - 4;  // -4 for "..." and dot
        
        if (keepLen > 0) {
            logFileName = baseName.left(keepLen) + QStringLiteral("...") + extension;
            qCWarning(LogReplayLinkControllerLog) << "Log filename truncated for MAVLink:" 
                                                  << fileInfo.fileName() << "->" << logFileName;
        } else {
            // Filename is really long, just truncate brutally
            logFileName = fileInfo.fileName().left(maxFilenameLen);
            qCWarning(LogReplayLinkControllerLog) << "Log filename severely truncated:" << logFileName;
        }
    }

    qCDebug(LogReplayLinkControllerLog) << "Notifying component 25:" 
                                        << (sessionStarted ? "session started" : "session ended")
                                        << "file:" << logFileName;

    const QString statusMsg = prefix + logFileName;

    mavlink_statustext_t statusText;
    statusText.severity = MAV_SEVERITY_INFO;
    statusText.id = 0;
    statusText.chunk_seq = 0;
    strncpy(statusText.text, qPrintable(statusMsg), sizeof(statusText.text) - 1);
    statusText.text[sizeof(statusText.text) - 1] = '\0';

    mavlink_message_t msg;
    mavlink_msg_statustext_encode_chan(
        MAVLinkProtocol::instance()->getSystemId(),
        MAVLinkProtocol::getComponentId(),
        sharedLink->mavlinkChannel(),
        &msg,
        &statusText
    );

    vehicle->sendMessageOnLinkThreadSafe(sharedLink.get(), msg);
}

QString LogReplayLinkController::_extractFlightId(const QString &filename)
{
    // Expected format: XXX-YYYY-MM-DD-HH-MM-SS-mmm.tlog
    // Flight ID is the datetime portion: YYYY-MM-DD-HH-MM-SS-mmm
    QRegularExpression re(R"(^\d{3}-(\d{4}-\d{2}-\d{2}-\d{2}-\d{2}-\d{2}-\d{3}))");
    QRegularExpressionMatch match = re.match(filename);
    
    if (match.hasMatch()) {
        const QString flightId = match.captured(1);
        qCDebug(LogReplayLinkControllerLog) << "Extracted flight ID:" << flightId << "from" << filename;
        return flightId;
    }
    
    qCWarning(LogReplayLinkControllerLog) << "Could not extract flight ID from filename:" << filename;
    return QString();
}

void LogReplayLinkController::_setReplayDataStatus(ReplayDataStatus status, const QString &message)
{
    if (_replayDataStatus != status) {
        _replayDataStatus = status;
        emit replayDataStatusChanged(status);
    }
    
    if (!message.isEmpty() && message != _statusMessage) {
        _statusMessage = message;
        emit statusMessageChanged(_statusMessage);
    }
}

void LogReplayLinkController::setReplayDataStatus(ReplayDataStatus status, const QString &message)
{
    _setReplayDataStatus(status, message);
}

bool LogReplayLinkController::loadFromMetadataFolder(const QString &metadataFolderPath)
{
    qCDebug(LogReplayLinkControllerLog) << "Loading replay from metadata folder:" << metadataFolderPath;
    
    // 1. Check if sessions directory exists
    QDir sessionsDir(metadataFolderPath + "/sessions/by_flight_id");
    if (!sessionsDir.exists()) {
        qCWarning(LogReplayLinkControllerLog) << "No sessions directory found at" << sessionsDir.path();
        return false;
    }
    
    // 2. Get list of session directories (for now, use first one - later can add UI picker)
    QStringList sessions = sessionsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (sessions.isEmpty()) {
        qCWarning(LogReplayLinkControllerLog) << "No sessions found in" << sessionsDir.path();
        return false;
    }
    
    // For demo: use first session (TODO: let user pick from list)
    QString sessionDir = sessions.first();
    QString metadataPath = sessionsDir.filePath(sessionDir + "/session_metadata.json");
    
    qCDebug(LogReplayLinkControllerLog) << "Loading metadata from" << metadataPath;
    
    // 3. Parse metadata JSON
    QFile metadataFile(metadataPath);
    if (!metadataFile.open(QIODevice::ReadOnly)) {
        qCWarning(LogReplayLinkControllerLog) << "Failed to open metadata file" << metadataPath;
        return false;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(metadataFile.readAll());
    if (doc.isNull() || !doc.isObject()) {
        qCWarning(LogReplayLinkControllerLog) << "Invalid JSON in metadata file";
        return false;
    }
    
    QJsonObject metadata = doc.object();
    QString flightId = metadata["flight_id"].toString();
    
    if (flightId.isEmpty()) {
        qCWarning(LogReplayLinkControllerLog) << "No flight_id found in metadata";
        return false;
    }

    _replayFlightId = flightId;
    _metadataFolderPath = metadataFolderPath;
    
    // 4. Find matching .tlog file
    QString tlogPath = _findTlogByFlightId(flightId);
    if (tlogPath.isEmpty()) {
        qCWarning(LogReplayLinkControllerLog) << "No matching .tlog found for flight_id" << flightId;
        return false;
    }
    
    qCDebug(LogReplayLinkControllerLog) << "Found matching tlog:" << tlogPath;
    
    // 5. Read tlog start timestamp before creating link (file needs to be loaded first)
    quint64 tlogStartUSecs = 0;
    {
        QFile tlogFile(tlogPath);
        if (tlogFile.open(QIODevice::ReadOnly)) {
            QByteArray timestampBytes = tlogFile.read(8);
            if (timestampBytes.size() == 8) {
                quint64 timestamp = qFromBigEndian(*reinterpret_cast<const quint64*>(timestampBytes.constData()));
                quint64 currentTimestamp = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000;
                if (timestamp > currentTimestamp) {
                    timestamp = qbswap(timestamp);
                }
                tlogStartUSecs = timestamp;
            }
            tlogFile.close();
        }
    }
    
    if (tlogStartUSecs == 0) {
        qCWarning(LogReplayLinkControllerLog) << "Failed to read tlog start timestamp";
        return false;
    }
    
    qCDebug(LogReplayLinkControllerLog) << "Tlog start timestamp from file:" << tlogStartUSecs << "usecs";
    
    // 6. Use LinkManager::startLogReplay() to match upstream pattern
    LogReplayLink* link = LinkManager::instance()->startLogReplay(tlogPath);
    if (!link) {
        qCWarning(LogReplayLinkControllerLog) << "Failed to start log replay";
        return false;
    }
    
    // 7. Parse video stream information
    QJsonObject streams = metadata["streams"].toObject();
    
    // Stream 1 = RGB
    if (streams.contains("1")) {
        QJsonObject stream1 = streams["1"].toObject();
        QString videoPath = stream1["video_file_path"].toString();
        double streamStartUnix = stream1["start_timestamp_unix"].toDouble();
        
        // Convert stream timestamp to microseconds and calculate offset in milliseconds
        quint64 streamStartUSecs = static_cast<quint64>(streamStartUnix * 1000000.0);
        qint64 offsetMs = 0;
        if (tlogStartUSecs > 0 && streamStartUSecs > 0) {
            offsetMs = static_cast<qint64>((streamStartUSecs - tlogStartUSecs) / 1000);
        }
        
        // Convert relative path to absolute (relative to metadata folder root)
        if (!videoPath.isEmpty() && QFileInfo(videoPath).isRelative()) {
            videoPath = QDir(metadataFolderPath).filePath(videoPath);
        }
        
        _rgbVideoInfo.videoPath = videoPath;
        _rgbVideoInfo.offsetMs = offsetMs;
        qCDebug(LogReplayLinkControllerLog) << "RGB video:" << videoPath << "offset:" << offsetMs << "ms"
                                             << "(stream:" << streamStartUSecs << "usecs)";
    }
    
    // Stream 2 = Thermal
    if (streams.contains("2")) {
        QJsonObject stream2 = streams["2"].toObject();
        QString videoPath = stream2["video_file_path"].toString();
        double streamStartUnix = stream2["start_timestamp_unix"].toDouble();
        
        // Convert stream timestamp to microseconds and calculate offset in milliseconds
        quint64 streamStartUSecs = static_cast<quint64>(streamStartUnix * 1000000.0);
        qint64 offsetMs = 0;
        if (tlogStartUSecs > 0 && streamStartUSecs > 0) {
            offsetMs = static_cast<qint64>((streamStartUSecs - tlogStartUSecs) / 1000);
        }
        
        // Convert relative path to absolute (relative to metadata folder root)
        if (!videoPath.isEmpty() && QFileInfo(videoPath).isRelative()) {
            videoPath = QDir(metadataFolderPath).filePath(videoPath);
        }
        
        _thermalVideoInfo.videoPath = videoPath;
        _thermalVideoInfo.offsetMs = offsetMs;
        qCDebug(LogReplayLinkControllerLog) << "Thermal video:" << videoPath << "offset:" << offsetMs << "ms"
                                             << "(stream:" << streamStartUSecs << "usecs)";
    }
    
    qCDebug(LogReplayLinkControllerLog) << "Found flight_id:" << flightId;
    
    // 8. Set the link (this triggers the normal flow)
    setLink(link);
    
    // 9. Set up video synchronization if we have video files
    if (!_rgbVideoInfo.videoPath.isEmpty() || !_thermalVideoInfo.videoPath.isEmpty()) {
        emit videoMetadataLoaded();
        
        // Get CustomVideoManager and load videos
        CustomPlugin* plugin = qobject_cast<CustomPlugin*>(QGCCorePlugin::instance());
        if (!plugin || !plugin->customVideoManager()) {
            qCWarning(LogReplayLinkControllerLog) << "CustomPlugin or CustomVideoManager not available";
            return false;
        }
        CustomVideoManager* videoMgr = plugin ? plugin->customVideoManager() : nullptr;
        if (videoMgr) {
            qCDebug(LogReplayLinkControllerLog) << "Setting up video replay synchronization";
            qCDebug(LogReplayLinkControllerLog) << "  RGB:" << _rgbVideoInfo.videoPath << "offset:" << _rgbVideoInfo.offsetMs << "ms";
            qCDebug(LogReplayLinkControllerLog) << "  Thermal:" << _thermalVideoInfo.videoPath << "offset:" << _thermalVideoInfo.offsetMs << "ms";
            
            bool loaded = videoMgr->enterReplayMode(
                _rgbVideoInfo.videoPath,
                _thermalVideoInfo.videoPath,
                _rgbVideoInfo.offsetMs,
                _thermalVideoInfo.offsetMs
            );
            
            if (loaded) {
                // Connect tlog playback state to video playback
                connect(link, &LogReplayLink::playbackStarted,
                    videoMgr, &CustomVideoManager::startReplayPlayback,
                    Qt::UniqueConnection);
                connect(link, &LogReplayLink::playbackPaused,
                    videoMgr, &CustomVideoManager::pauseReplayPlayback,
                    Qt::UniqueConnection);
                // Connect manual slider moves to video seeking
                connect(link, &LogReplayLink::playheadMoved,
                    videoMgr, &CustomVideoManager::seekToPosition,
                    Qt::UniqueConnection);
                
                qCDebug(LogReplayLinkControllerLog) << "Video replay synchronized with tlog playback";
            } else {
                qCWarning(LogReplayLinkControllerLog) << "Failed to load replay videos";
            }
        }
    }
    
    qCDebug(LogReplayLinkControllerLog) << "Successfully loaded replay from metadata folder";
    return true;
}

QString LogReplayLinkController::_findTlogByFlightId(const QString &flightId)
{
    // Search standard telemetry directory
    QString telemetryDir = SettingsManager::instance()->appSettings()->telemetrySavePath();
    
    QDir dir(telemetryDir);
    
    // Try exact match with .tlog extension
    QString tlogName = flightId + ".tlog";
    if (dir.exists(tlogName)) {
        QString fullPath = dir.filePath(tlogName);
        qCDebug(LogReplayLinkControllerLog) << "Found exact tlog match:" << fullPath;
        return fullPath;
    }
    
    // Try searching for files containing the flight_id (in case of different naming)
    QStringList tlogFiles = dir.entryList(QStringList() << "*.tlog", QDir::Files);
    for (const QString &fileName : tlogFiles) {
        if (fileName.contains(flightId)) {
            QString fullPath = dir.filePath(fileName);
            qCDebug(LogReplayLinkControllerLog) << "Found matching tlog:" << fullPath;
            return fullPath;
        }
    }
    
    qCWarning(LogReplayLinkControllerLog) << "Tlog not found for flight_id:" << flightId << "in" << telemetryDir;
    return QString();
}
