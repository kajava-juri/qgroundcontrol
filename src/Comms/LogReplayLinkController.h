/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QLoggingCategory>
#include <QtCore/QPointer>
#include <QtQmlIntegration/QtQmlIntegration>

#include "LogReplayLink.h"

class Vehicle;

Q_DECLARE_LOGGING_CATEGORY(LogReplayLinkControllerLog)

class LogReplayLinkController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(LogReplayLink    *link           READ    link            WRITE setLink               NOTIFY linkChanged)
    Q_PROPERTY(bool             isPlaying       READ    isPlaying       WRITE setIsPlaying          NOTIFY isPlayingChanged)
    Q_PROPERTY(qreal            percentComplete READ    percentComplete WRITE setPercentComplete    NOTIFY percentCompleteChanged)
    Q_PROPERTY(QString          totalTime       MEMBER  _totalTime                                  NOTIFY totalTimeChanged)
    Q_PROPERTY(QString          playheadTime    MEMBER  _playheadTime                               NOTIFY playheadTimeChanged)
    Q_PROPERTY(qreal            playbackSpeed   MEMBER  _playbackSpeed                              NOTIFY playbackSpeedChanged)
    Q_PROPERTY(ReplayDataStatus replayDataStatus READ   replayDataStatus                            NOTIFY replayDataStatusChanged)
    Q_PROPERTY(QString          statusMessage   MEMBER  _statusMessage                              NOTIFY statusMessageChanged)

public:
    enum ReplayDataStatus {
        Checking,       // Waiting for component 25 to confirm data availability
        Ready,          // Component 25 has matching data, replay can proceed
        Unavailable,    // Component 25 doesn't have matching data
        NotRequired     // No external component check needed (no link or not connected)
    };
    Q_ENUM(ReplayDataStatus)

    explicit LogReplayLinkController(QObject *parent = nullptr);
    ~LogReplayLinkController();

    LogReplayLink *link() const { return _link; }
    void setLink(LogReplayLink *link);

    bool isPlaying() const { return _isPlaying; }
    void setIsPlaying(bool isPlaying) const;

    qreal percentComplete() const { return _percentComplete; }
    void setPercentComplete(qreal percentComplete) const;

    ReplayDataStatus replayDataStatus() const { return _replayDataStatus; }
    QString currentFlightId() const { return _currentFlightId; }
    
    // Public method for external components to update replay status
    Q_INVOKABLE void setReplayDataStatus(ReplayDataStatus status, const QString &message = QString());
    
    // Get the active replay controller instance (if any)
    static LogReplayLinkController* activeInstance() { return _activeInstance; }

signals:
    void isPlayingChanged(bool isPlaying);
    void linkChanged(LogReplayLink *link);
    void percentCompleteChanged(qreal percentComplete);
    void playbackSpeedChanged(qreal playbackSpeed);
    void playheadTimeChanged(const QString &playheadTime);
    void totalTimeChanged(const QString &totalTime);
    void replayDataStatusChanged(ReplayDataStatus status);
    void statusMessageChanged(const QString &message);

private slots:
    void _currentLogTimeSecs(uint32_t secs);
    void _linkConnected();
    void _linkDisconnected();
    void _logFileStats(uint32_t logDurationSecs);
    void _playbackAtEnd();
    void _playbackPaused();
    void _playbackPercentCompleteChanged(qreal percentComplete);
    void _playbackStarted();

private:
    static QString _secondsToHMS(uint32_t seconds);
    QString _extractFlightId(const QString &filename);
    void _requestReplayDataCheck(const QString &flightId);
    void _handleStatusTextMessage(const mavlink_message_t &message);
    void _setReplayDataStatus(ReplayDataStatus status, const QString &message = QString());
    void _notifyExternalComponent(bool sessionStarted);

    bool _isPlaying = false;
    qreal _percentComplete = 0;
    uint32_t _playheadSecs = 0;
    qreal _playbackSpeed = 1;
    QString _playheadTime;
    QString _totalTime;
    QString _statusMessage;
    QString _currentFlightId;
    ReplayDataStatus _replayDataStatus = NotRequired;
    QPointer<LogReplayLink> _link;
    
    static LogReplayLinkController* _activeInstance;  // Track active instance with a link
};
