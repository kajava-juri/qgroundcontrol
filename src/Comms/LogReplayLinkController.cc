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
#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QtEndian>
#include <QtCore/QTextStream>

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

        _totalDurationSecs = 0;
        emit totalDurationSecsChanged(_totalDurationSecs);

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
    if (_totalDurationSecs != logDurationSecs) {
        _totalDurationSecs = logDurationSecs;
        emit totalDurationSecsChanged(_totalDurationSecs);
    }
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

QVariantList LogReplayLinkController::videoReplaySegments() const
{
    CustomPlugin* plugin = qobject_cast<CustomPlugin*>(QGCCorePlugin::instance());
    qCDebug(LogReplayLinkControllerLog) << "Getting video replay segments for QML - plugin instance:" << plugin;
    if (!plugin || !plugin->customVideoManager()) {
        qCDebug(LogReplayLinkControllerLog) << "No custom video manager available, returning empty segments list";
        return QVariantList();
    }
    QVariantList segments = plugin->customVideoManager()->videoReplaySegments();
    qCDebug(LogReplayLinkControllerLog) << "Returning" << segments.size() << "segments:" << segments;
    return segments;
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

LogReplayLinkController::VideoStreamInfo LogReplayLinkController::_processVideoPath(const QString &videoPath, quint64 tlogStartUSecs)
{
    VideoStreamInfo streamInfo;
    streamInfo.videoPath = videoPath;
    streamInfo.offsetMs = 0;
    streamInfo.durationMs = 0;
    streamInfo.isDirectory = false;

    QFileInfo pathInfo(videoPath);
    
    // Check if path is a directory (frame-based video)
    if (pathInfo.isDir()) {
        qCDebug(LogReplayLinkControllerLog) << "Video path is a directory (frame-based):" << videoPath;
        streamInfo.isDirectory = true;

        const QString metadataCsvPath = pathInfo.filePath() + "/metadata.csv";
        const QString dataCsvPath = pathInfo.filePath() + "/data.csv";
        QString csvPath;
        QFile csvFile(metadataCsvPath);

        if (!csvFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            csvFile.setFileName(dataCsvPath);
            if (!csvFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                qCWarning(LogReplayLinkControllerLog) << "Failed to open metadata.csv or data.csv at:" << metadataCsvPath << dataCsvPath;
                return streamInfo;
            }
        }

        csvPath = csvFile.fileName();
        
        QTextStream in(&csvFile);
        QString headerLine = in.readLine();  // Skip header line
        
        if (headerLine.isEmpty()) {
            qCWarning(LogReplayLinkControllerLog) << "metadata.csv is empty:" << csvPath;
            csvFile.close();
            return streamInfo;
        }
        
        qCDebug(LogReplayLinkControllerLog) << "CSV header:" << headerLine;
        
        bool firstFrameSet = false;
        while (!in.atEnd()) {
            const QString line = in.readLine();
            if (line.isEmpty()) {
                continue;
            }

            const QStringList parts = line.split(',');
            if (parts.size() < 2) {
                continue;
            }

            bool frameIndexOk = false;
            const quint64 frameIndex = parts[0].trimmed().toULongLong(&frameIndexOk);

            bool timestampOk = false;
            const quint64 frameTimestampNs = parts[1].trimmed().toULongLong(&timestampOk);

            if (!frameIndexOk || !timestampOk) {
                qCWarning(LogReplayLinkControllerLog) << "Failed to parse frame metadata from CSV line:" << line;
                continue;
            }

            LogReplayLinkController::FrameMetadata frameMeta;
            frameMeta.frameIndex = frameIndex;
            frameMeta.timestampNs = frameTimestampNs;
            streamInfo.frameMetadata.append(frameMeta);

            if (!firstFrameSet) {
                firstFrameSet = true;

                // Calculate offset using first frame's timestamp
                if (tlogStartUSecs > 0) {
                    const quint64 frameStartUSecs = frameTimestampNs / 1000;  // Convert ns to us
                    streamInfo.offsetMs = static_cast<qint64>((frameStartUSecs - tlogStartUSecs) / 1000);
                }

                qCDebug(LogReplayLinkControllerLog) << "Frame-based video offset:" << streamInfo.offsetMs << "ms"
                                                    << "(frameIndex:" << frameIndex
                                                    << "timestamp:" << frameTimestampNs << "ns)";
            }
        }

        qCDebug(LogReplayLinkControllerLog) << "Loaded" << streamInfo.frameMetadata.size() << "frame metadata rows from" << csvPath;
        
        csvFile.close();
    } else {
        qCDebug(LogReplayLinkControllerLog) << "Video path is a file:" << videoPath;
    }
    
    return streamInfo;
}

LogReplayLinkController::VideoStreamInfo LogReplayLinkController::_processDroneLogPath(const QString &metadataFolderPath, const QString &runId, quint64 tlogStartUSecs)
{
    VideoStreamInfo streamInfo;

    const QDir metadataRoot(metadataFolderPath);
    const QDir droneLogRoot(metadataRoot.filePath(QStringLiteral("drone_log")));
    const QDir flightRoot(droneLogRoot.filePath(runId));

    if (!flightRoot.exists()) {
        qCDebug(LogReplayLinkControllerLog) << "No drone_log folder found for run ID:" << runId;
        return streamInfo;
    }

    const QStringList logDirs = flightRoot.entryList(QStringList() << QStringLiteral("log*"), QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    if (logDirs.isEmpty()) {
        qCWarning(LogReplayLinkControllerLog) << "No log directories found in drone_log path:" << flightRoot.path();
        return streamInfo;
    }

    const QString logDirPath = flightRoot.filePath(logDirs.first());
    const QFileInfo infoFile(QDir(logDirPath).filePath(QStringLiteral("info.json")));
    if (!infoFile.exists()) {
        qCWarning(LogReplayLinkControllerLog) << "No info.json found for drone_log path:" << infoFile.filePath();
        return streamInfo;
    }

    QFile infoJsonFile(infoFile.filePath());
    if (!infoJsonFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(LogReplayLinkControllerLog) << "Failed to open drone_log info.json at:" << infoFile.filePath();
        return streamInfo;
    }

    const QJsonDocument infoDoc = QJsonDocument::fromJson(infoJsonFile.readAll());
    infoJsonFile.close();
    if (!infoDoc.isObject()) {
        qCWarning(LogReplayLinkControllerLog) << "Invalid drone_log info.json:" << infoFile.filePath();
        return streamInfo;
    }

    const QJsonObject infoObj = infoDoc.object();
    const QString startTimeDateString = infoObj.value(QStringLiteral("start_time_date")).toString();
    const quint64 startTimeMonotonicNs = infoObj.value(QStringLiteral("start_time_monotonic_ns")).toVariant().toULongLong();
    const QJsonArray channels = infoObj.value(QStringLiteral("channels")).toArray();
    QString cameraPipePath;

    for (const QJsonValue& channelValue : channels) {
        if (!channelValue.isObject()) {
            continue;
        }

        const QJsonObject channelObj = channelValue.toObject();
        const QString typeString = channelObj.value(QStringLiteral("type_string")).toString();
        const QString pipePath = channelObj.value(QStringLiteral("pipe_path")).toString();

        if (typeString == QLatin1String("cam") && pipePath.contains(QStringLiteral("hires_large_color"))) {
            cameraPipePath = pipePath;
            break;
        }

        if (cameraPipePath.isEmpty() && typeString == QLatin1String("cam")) {
            cameraPipePath = pipePath;
        }
    }

    if (cameraPipePath.isEmpty()) {
        qCWarning(LogReplayLinkControllerLog) << "Could not find a camera channel in drone_log info.json:" << infoFile.filePath();
        return streamInfo;
    }

    const QString cameraPath = QDir(logDirPath).filePath(cameraPipePath.startsWith(QLatin1Char('/')) ? cameraPipePath.mid(1) : cameraPipePath);
    streamInfo = _processVideoPath(cameraPath, 0);

    if (streamInfo.isDirectory && !streamInfo.frameMetadata.isEmpty()) {
        const QDateTime startDateTime = QDateTime::fromString(startTimeDateString, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        if (!startDateTime.isValid()) {
            qCWarning(LogReplayLinkControllerLog) << "Invalid start_time_date in drone_log info.json:" << startTimeDateString;
        } else if (startTimeMonotonicNs == 0) {
            qCWarning(LogReplayLinkControllerLog) << "Missing start_time_monotonic_ns in drone_log info.json:" << infoFile.filePath();
        } else {
            constexpr qint64 voxlTimezoneOffsetSecs = 3 * 60 * 60;
            const QDateTime adjustedStartDateTime = startDateTime.addSecs(voxlTimezoneOffsetSecs);
            const quint64 firstFrameTimestampNs = streamInfo.frameMetadata.constFirst().timestampNs;
            const qint64 monotonicDeltaNs = static_cast<qint64>(firstFrameTimestampNs) - static_cast<qint64>(startTimeMonotonicNs);
            const qint64 frameAbsoluteUsecs = static_cast<qint64>(adjustedStartDateTime.toMSecsSinceEpoch()) * 1000 + (monotonicDeltaNs / 1000);
            streamInfo.offsetMs = static_cast<qint64>((frameAbsoluteUsecs - static_cast<qint64>(tlogStartUSecs)) / 1000);

            qCDebug(LogReplayLinkControllerLog) << "Drone log timestamps anchored using start_time_date:" << startTimeDateString
                                                << "adjustedStartTime:" << adjustedStartDateTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                                                << "start_time_monotonic_ns:" << startTimeMonotonicNs
                                                << "firstFrameTimestampNs:" << firstFrameTimestampNs
                                                << "timezoneOffsetSecs:" << voxlTimezoneOffsetSecs
                                                << "offsetMs:" << streamInfo.offsetMs;
        }
    }

    qCDebug(LogReplayLinkControllerLog) << "Loaded drone camera replay stream from" << cameraPath;
    return streamInfo;
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
    _metadataFolderPath = metadataFolderPath;
    
    // 1. Check if sessions directory exists
    QDir sessionsDir(metadataFolderPath + "/sessions/by_flight_id");
    if (!sessionsDir.exists()) {
        qCWarning(LogReplayLinkControllerLog) << "No sessions directory found at" << sessionsDir.path();
        return false;
    }
    
    // 2. Get list of session directories
    QStringList sessions = sessionsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Files);
    if (sessions.isEmpty()) {
        qCWarning(LogReplayLinkControllerLog) << "No sessions found in" << sessionsDir.path();
        return false;
    }

    _sessionsMetadata.clear();

    for (const auto &session : sessions) {
        QFileInfo fileInfo(session);
        QString flightIdFile = fileInfo.fileName();
        QString flightId = fileInfo.baseName();
        
        // Get flight ID from the run_id text file
        if (!flightIdFile.endsWith(".txt")) {
            qCWarning(LogReplayLinkControllerLog) << "Skipping non-txt file in sessions directory:" << flightId;
            continue;
        }
        QFile runIdFile(sessionsDir.filePath(flightIdFile));
        if (!runIdFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qCWarning(LogReplayLinkControllerLog) << "Failed to open run ID file:" << runIdFile.fileName();
            continue;
        }
        QString runId = QString::fromUtf8(runIdFile.readAll()).trimmed();
        runIdFile.close();

        QString metadataPath = sessionsDir.filePath(metadataFolderPath + "/sessions/" + runId + "/session_metadata.json");
        QFile metadataFile(metadataPath);
        if (!metadataFile.open(QIODevice::ReadOnly)) {
            qCWarning(LogReplayLinkControllerLog) << "Failed to open metadata file" <<
                                                    metadataPath << "for session" << session;
            continue;
        }
        QJsonDocument doc = QJsonDocument::fromJson(metadataFile.readAll());
        if (doc.isNull() || !doc.isObject()) {
            qCWarning(LogReplayLinkControllerLog) << "Invalid JSON in metadata file" << metadataPath;
            continue;
        }

        QJsonObject metadata = doc.object();

        uint32_t dcDurationSecs = static_cast<uint32_t>(metadata["duration_seconds"].toInt());
        QString dcStartTime = metadata["dc_start_time"].toString();

        bool hasVideo = false;
            
        // 4. Find matching .tlog file
        QString tlogPath = _findTlogByFlightId(flightId);
        bool hasTlog = true;
        if (tlogPath.isEmpty()) {
            qCWarning(LogReplayLinkControllerLog) << "No matching .tlog found for flight_id" << flightId;
            hasTlog = false;
        }
        
        qCDebug(LogReplayLinkControllerLog) << "Found matching tlog:" << tlogPath;

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

        // initialize videoStreams list from metadata
        QList<VideoStreamInfo> videoStreams = QList<VideoStreamInfo>();
        QJsonArray streams = metadata["streams"].toArray();

        for (const QJsonValue& streamVal : streams) {
            if (!streamVal.isObject()) {
                continue;
            }
            QJsonObject streamObj = streamVal.toObject();
            QString streamName = streamObj["stream_name"].toString();
            QString videoPath = streamObj["video_file_path"].toString();
            double streamStartUnix = streamObj["start_timestamp_unix"].toDouble();

            // Convert relative path to absolute (relative to metadata folder root)
            if (!videoPath.isEmpty() && QFileInfo(videoPath).isRelative()) {
                videoPath = QDir(metadataFolderPath).filePath(videoPath);
            }
            
            qCDebug(LogReplayLinkControllerLog) << "Processing video:" << videoPath;

            hasVideo = true;
            
            // Process video path - handles both file and directory (frame-based) videos
            VideoStreamInfo streamInfo = _processVideoPath(videoPath, tlogStartUSecs);
            
            // For file-based videos, use the metadata offset if not calculated from directory
            if (!streamInfo.isDirectory && tlogStartUSecs > 0 && streamStartUnix > 0) {
                quint64 streamStartUSecs = static_cast<quint64>(streamStartUnix * 1000000.0);
                streamInfo.offsetMs = static_cast<qint64>((streamStartUSecs - tlogStartUSecs) / 1000);
                qCDebug(LogReplayLinkControllerLog) << "File-based video offset:" << streamInfo.offsetMs << "ms"
                                                    << "(stream:" << streamStartUSecs << "usecs)";
            }
            
            videoStreams.append(streamInfo);
        }

        VideoStreamInfo droneCameraStream = _processDroneLogPath(metadataFolderPath, runId, tlogStartUSecs);
        if (!droneCameraStream.videoPath.isEmpty()
                && (!droneCameraStream.isDirectory || !droneCameraStream.frameMetadata.isEmpty())) {
            hasVideo = true;
            videoStreams.append(droneCameraStream);
        }


        SessionMetadata sessionMeta { runId, dcDurationSecs, tlogStartUSecs, hasVideo, hasTlog, tlogPath, videoStreams, tlogStartUSecs };
        _sessionsMetadata.append(sessionMeta);
    }

    bool success = !_sessionsMetadata.isEmpty();
    if (success) {
        emit sessionMetadataChanged();
        emit sessionsLoaded(_sessionsMetadata.size());
        qCDebug(LogReplayLinkControllerLog) << "Loaded metadata for" << _sessionsMetadata.size() << "sessions";
    } else {
        qCWarning(LogReplayLinkControllerLog) << "No valid session metadata loaded";
    }
    
    return success;
}

bool LogReplayLinkController::loadSessionByFlightId(const QString &flightId)
{
    qCDebug(LogReplayLinkControllerLog) << "Loading session by flight ID:" << flightId;
    
    // Find the session metadata
    const SessionMetadata* sessionMeta = nullptr;
    for (const SessionMetadata& session : _sessionsMetadata) {
        if (session.flightId == flightId) {
            sessionMeta = &session;
            break;
        }
    }
    
    if (!sessionMeta) {
        qCWarning(LogReplayLinkControllerLog) << "Session not found for flight ID:" << flightId;
        return false;
    }
    
    if (!sessionMeta->hasTlog) {
        qCWarning(LogReplayLinkControllerLog) << "Session has no tlog file:" << flightId;
        return false;
    }
    
    // Start log replay
    LogReplayLink* link = LinkManager::instance()->startLogReplay(sessionMeta->tlogFilePath);
    if (!link) {
        qCWarning(LogReplayLinkControllerLog) << "Failed to start log replay for:" << sessionMeta->tlogFilePath;
        return false;
    }
    
    // Store replay flight ID for status tracking
    _replayFlightId = flightId;
    _currentFlightId = flightId;
    
    // Set the link (this triggers the normal flow)
    setLink(link);
    
    // Set up video synchronization if we have video files
    if (!sessionMeta->videoStreams.isEmpty()) {
        _rgbVideoInfo = VideoStreamInfo();
        _thermalVideoInfo = VideoStreamInfo();
        _droneCameraVideoInfo = VideoStreamInfo();

        // Populate RGB/Thermal video info from first two streams
        for (const VideoStreamInfo& stream : sessionMeta->videoStreams) {
            if (_rgbVideoInfo.videoPath.isEmpty()) {
                _rgbVideoInfo = stream;
            } else if (_thermalVideoInfo.videoPath.isEmpty()) {
                _thermalVideoInfo = stream;
            } else if (_droneCameraVideoInfo.videoPath.isEmpty()) {
                _droneCameraVideoInfo = stream;
            }
        }
        
        emit videoMetadataLoaded();
        
        // Get CustomVideoManager and load videos
        CustomPlugin* plugin = qobject_cast<CustomPlugin*>(QGCCorePlugin::instance());
        if (!plugin || !plugin->customVideoManager()) {
            qCWarning(LogReplayLinkControllerLog) << "CustomPlugin or CustomVideoManager not available";
            return true;  // Still success, just no video sync
        }
        
        CustomVideoManager* videoMgr = plugin->customVideoManager();
        if (videoMgr) {
            qCDebug(LogReplayLinkControllerLog) << "Setting up video replay synchronization";
            qCDebug(LogReplayLinkControllerLog) << "  RGB:" << _rgbVideoInfo.videoPath << "offset:" << _rgbVideoInfo.offsetMs << "ms";
            qCDebug(LogReplayLinkControllerLog) << "  Thermal:" << _thermalVideoInfo.videoPath << "offset:" << _thermalVideoInfo.offsetMs << "ms";
            qCDebug(LogReplayLinkControllerLog) << "  Drone Camera:" << (_droneCameraVideoInfo.videoPath.isEmpty() ? "N/A" : _droneCameraVideoInfo.videoPath)
                                            << "offset:" << _droneCameraVideoInfo.offsetMs << "ms";

            CustomVideoManager::VideoStreamMetadata rgbStreamMeta;
            rgbStreamMeta.videoPath = _rgbVideoInfo.videoPath;
            rgbStreamMeta.offsetMs = -_rgbVideoInfo.offsetMs;
            rgbStreamMeta.isDirectory = _rgbVideoInfo.isDirectory;
            for (const FrameMetadata& frameMeta : _rgbVideoInfo.frameMetadata) {
                CustomVideoManager::FrameMetadata mappedFrame;
                mappedFrame.frameIndex = frameMeta.frameIndex;
                mappedFrame.timestampNs = frameMeta.timestampNs;
                rgbStreamMeta.frameMetadata.append(mappedFrame);
            }

            CustomVideoManager::VideoStreamMetadata thermalStreamMeta;
            thermalStreamMeta.videoPath = _thermalVideoInfo.videoPath;
            thermalStreamMeta.offsetMs = -_thermalVideoInfo.offsetMs;
            thermalStreamMeta.isDirectory = _thermalVideoInfo.isDirectory;
            for (const FrameMetadata& frameMeta : _thermalVideoInfo.frameMetadata) {
                CustomVideoManager::FrameMetadata mappedFrame;
                mappedFrame.frameIndex = frameMeta.frameIndex;
                mappedFrame.timestampNs = frameMeta.timestampNs;
                thermalStreamMeta.frameMetadata.append(mappedFrame);
            }
            
            CustomVideoManager::VideoStreamMetadata droneStreamMeta;
            if (!_droneCameraVideoInfo.videoPath.isEmpty()) {
                droneStreamMeta.videoPath = _droneCameraVideoInfo.videoPath;
                droneStreamMeta.offsetMs = -_droneCameraVideoInfo.offsetMs;
                droneStreamMeta.isDirectory = _droneCameraVideoInfo.isDirectory;
                for (const FrameMetadata& frameMeta : _droneCameraVideoInfo.frameMetadata) {
                    CustomVideoManager::FrameMetadata mappedFrame;
                    mappedFrame.frameIndex = frameMeta.frameIndex;
                    mappedFrame.timestampNs = frameMeta.timestampNs;
                    droneStreamMeta.frameMetadata.append(mappedFrame);
                }
            }

            bool loaded = videoMgr->enterReplayMode(rgbStreamMeta, thermalStreamMeta, droneStreamMeta);
            
            if (loaded) {
                // Connect tlog playback state to video playback
                (void) connect(link, &LogReplayLink::playbackStarted,
                    videoMgr, &CustomVideoManager::startReplayPlayback,
                    Qt::UniqueConnection);
                (void) connect(link, &LogReplayLink::playbackPaused,
                    videoMgr, &CustomVideoManager::pauseReplayPlayback,
                    Qt::UniqueConnection);
                // Connect manual slider moves to video seeking
                (void) connect(link, &LogReplayLink::playheadMoved,
                    videoMgr, &CustomVideoManager::seekToPosition,
                    Qt::UniqueConnection);
                // Connect continuous time updates to cache time (no seeking)
                (void) connect(link, &LogReplayLink::currentLogTimeSecs,
                    videoMgr, &CustomVideoManager::updateReplayTime,
                    Qt::UniqueConnection);
                
                // Connect video segment changes for future updates
                (void) connect(videoMgr, &CustomVideoManager::videoReplaySegmentsChanged,
                    this, &LogReplayLinkController::videoReplaySegmentsChanged,
                    Qt::UniqueConnection);

                connect(link, &LogReplayLink::playbackAtEnd, this, &LogReplayLinkController::_onPlaybackAtEnd);
                
                // Notify QML that segments are ready (videos already loaded above)
                emit videoReplaySegmentsChanged();
                
                qCDebug(LogReplayLinkControllerLog) << "Video replay synchronized with tlog playback";
            } else {
                qCWarning(LogReplayLinkControllerLog) << "Failed to load replay videos";
            }
        }
    }
    
    qCDebug(LogReplayLinkControllerLog) << "Successfully loaded session:" << flightId;
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

void LogReplayLinkController::_onPlaybackAtEnd() const
{
    qCDebug(LogReplayLinkControllerLog) << "Playback reached end, looping back to start";
    setIsPlaying(true);
}
