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
    Q_PROPERTY(uint32_t         totalDurationSecs READ  totalDurationSecs                           NOTIFY totalDurationSecsChanged)
    Q_PROPERTY(ReplayDataStatus replayDataStatus READ   replayDataStatus                            NOTIFY replayDataStatusChanged)
    Q_PROPERTY(QString          statusMessage   MEMBER  _statusMessage                              NOTIFY statusMessageChanged)
    Q_PROPERTY(QVariantList     videoReplaySegments READ videoReplaySegments                        NOTIFY videoReplaySegmentsChanged)
    Q_PROPERTY(QVariantList     sessionMetadata   READ  sessionMetadata                             NOTIFY sessionMetadataChanged)

public:
    enum ReplayDataStatus {
        Checking,       // Waiting for component 25 to confirm data availability
        Ready,          // Component 25 has matching data, replay can proceed
        Unavailable,    // Component 25 doesn't have matching data
        NotRequired     // No external component check needed (no link or not connected)
    };
    Q_ENUM(ReplayDataStatus)

    QVariantList sessionMetadata() const {
        QVariantList list;
        for (const SessionMetadata& session : this->_sessionsMetadata) {
            list.append(session.toMap());
        }
        qCDebug(LogReplayLinkControllerLog) << "Returning session metadata for" << list.size() << "sessions:" << list;
        return list;
    }

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
    QVariantList videoReplaySegments() const;
    uint32_t totalDurationSecs() const { return _totalDurationSecs; }
    
    // Public method for external components to update replay status
    Q_INVOKABLE void setReplayDataStatus(ReplayDataStatus status, const QString &message = QString());
    
    // Load replay from metadata folder (finds matching .tlog automatically)
    Q_INVOKABLE bool loadFromMetadataFolder(const QString &metadataFolderPath);
    
    // Load a specific session by flight ID (must call loadFromMetadataFolder first)
    Q_INVOKABLE bool loadSessionByFlightId(const QString &flightId);
    
    struct FrameMetadata {
        quint64 frameIndex = 0;
        quint64 timestampNs = 0;  // Nanosecond timestamp from CSV
    };

    struct VideoStreamInfo {
        QString videoPath;
        qint64 offsetMs = 0;      // Milliseconds offset from tlog start
        qint64 durationMs = 0;    // Duration of the video in milliseconds
        bool isDirectory = false;  // True if videoPath points to frame directory, false if video file
        QList<FrameMetadata> frameMetadata;  // Populated if isDirectory = true
    };
    
    VideoStreamInfo rgbVideoInfo() const { return _rgbVideoInfo; }
    VideoStreamInfo thermalVideoInfo() const { return _thermalVideoInfo; }
    
    // Get the active replay controller instance (if any)
    static LogReplayLinkController* activeInstance() { return _activeInstance; }

signals:
    void isPlayingChanged(bool isPlaying);
    void linkChanged(LogReplayLink *link);
    void percentCompleteChanged(qreal percentComplete);
    void playbackSpeedChanged(qreal playbackSpeed);
    void playheadTimeChanged(const QString &playheadTime);
    void totalTimeChanged(const QString &totalTime);
    void totalDurationSecsChanged(uint32_t totalDurationSecs);
    void replayDataStatusChanged(ReplayDataStatus status);
    void statusMessageChanged(const QString &message);
    void videoMetadataLoaded();  // Emitted when replay is loaded with video files
    void videoReplaySegmentsChanged();
    void sessionMetadataChanged();
    void sessionsLoaded(int sessionCount);

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
    QString _findTlogByFlightId(const QString &flightId);
    VideoStreamInfo _processDroneLogPath(const QString &metadataFolderPath, const QString &flightId, quint64 tlogStartUSecs);
    VideoStreamInfo _processVideoPath(const QString &videoPath, quint64 tlogStartUSecs);
    void _requestReplayDataCheck(const QString &flightId);
    void _handleStatusTextMessage(const mavlink_message_t &message);
    void _setReplayDataStatus(ReplayDataStatus status, const QString &message = QString());
    void _notifyExternalComponent(bool sessionStarted);
    void _onPlaybackAtEnd() const;

    bool _isPlaying = false;
    qreal _percentComplete = 0;
    uint32_t _playheadSecs = 0;
    uint32_t _totalDurationSecs = 0;
    qreal _playbackSpeed = 1;
    QString _playheadTime;
    QString _totalTime;
    QString _statusMessage;
    QString _currentFlightId;
    QString _replayFlightId;
    QString _metadataFolderPath;  // Root folder of loaded metadata
    VideoStreamInfo _rgbVideoInfo;
    VideoStreamInfo _thermalVideoInfo;
    VideoStreamInfo _droneCameraVideoInfo;
    ReplayDataStatus _replayDataStatus = NotRequired;
    QPointer<LogReplayLink> _link;
    
    static LogReplayLinkController* _activeInstance;  // Track active instance with a link

    struct SessionMetadata {
        QString flightId;
        uint32_t dcDurationSecs;
        uint32_t tlogDurationSecs;
        bool hasVideo;
        bool hasTlog;
        QString tlogFilePath;
        QList<VideoStreamInfo> videoStreams;
        quint64 timestamp;

        QVariantMap toMap() const {
            QVariantList streams;
            for (const VideoStreamInfo& stream : videoStreams) {
                streams.append(QVariantMap{
                    {"videoPath", stream.videoPath},
                    {"offsetMs",  stream.offsetMs},
                    {"durationMs", stream.durationMs},
                    {"isDirectory", stream.isDirectory}
                });
            }
            return {
                {"flightId",        flightId},
                {"dcDurationSecs",  dcDurationSecs},
                {"tlogDurationSecs",tlogDurationSecs},
                {"hasVideo",        hasVideo},
                {"hasTlog",         hasTlog},
                {"tlogFilePath",    tlogFilePath},
                {"videoStreams",     streams},
                {"timestamp",       timestamp}
            };
        }
    };
    QList<SessionMetadata> _sessionsMetadata;
};
