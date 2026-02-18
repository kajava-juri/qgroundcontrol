/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "LogReplayLinkController.h"
#include "LinkConfiguration.h"
#include "MAVLinkProtocol.h"
#include "MultiVehicleManager.h"
#include "QGCLoggingCategory.h"
#include "Vehicle.h"

#include <QtCore/QFileInfo>

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

        // Extract flight ID from filename and request data availability check
        const SharedLinkConfigurationPtr config = _link->linkConfiguration();
        const LogReplayConfiguration* logConfig = qobject_cast<const LogReplayConfiguration*>(config.get());
        if (logConfig) {
            const QString logFilePath = logConfig->logFilename();
            const QFileInfo fileInfo(logFilePath);
            _currentFlightId = _extractFlightId(fileInfo.fileName());
            
            if (!_currentFlightId.isEmpty()) {
                _setReplayDataStatus(Checking, QString("Checking replay data availability..."));
                _requestReplayDataCheck(_currentFlightId);
            } else {
                _setReplayDataStatus(NotRequired, QString("Could not extract flight ID from filename"));
            }
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
