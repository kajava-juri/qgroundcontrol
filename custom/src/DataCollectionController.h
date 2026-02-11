#pragma once

#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtQml/qqml.h>
#include <QtNetwork/QNetworkAccessManager>
#include "Vehicle.h"

#define DATA_COLLECTION_COMPONENT_ID 25

class DataCollectionController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool isCollecting READ isCollecting NOTIFY isCollectingChanged)
    Q_PROPERTY(double testValue READ testValue NOTIFY testValueChanged)
    Q_PROPERTY(int dcState READ dcState NOTIFY dcStateChanged)
    Q_PROPERTY(double dcProgress READ dcProgress NOTIFY dcProgressChanged)
    Q_PROPERTY(int dcRunId READ dcRunId NOTIFY dcRunIdChanged)
    Q_PROPERTY(int dcErrorCount READ dcErrorCount NOTIFY dcErrorCountChanged)
    Q_PROPERTY(bool dcOk READ dcOk NOTIFY dcOkChanged)
    
    // RTK GPS properties
    Q_PROPERTY(int rtkFix READ rtkFix NOTIFY rtkFixChanged)
    Q_PROPERTY(int rtkSats READ rtkSats NOTIFY rtkSatsChanged)
    Q_PROPERTY(double rtkHdop READ rtkHdop NOTIFY rtkHdopChanged)
    Q_PROPERTY(double rtkLat READ rtkLat NOTIFY rtkLatChanged)
    Q_PROPERTY(double rtkLon READ rtkLon NOTIFY rtkLonChanged)
    Q_PROPERTY(double rtkAlt READ rtkAlt NOTIFY rtkAltChanged)

public:
    DataCollectionController(QObject* parent = nullptr);
    bool isCollecting() const {return _isCollecting;}
    double testValue() const {return _testValue;}
    QString videoStreamName() const {return _videoStreamName;}
    QString videoStreamUri() const {return _videoStreamUri;}
    int dcState() const {return _dcState;}
    double dcProgress() const {return _dcProgress;}
    int dcRunId() const {return _dcRunId;}
    int dcErrorCount() const {return _dcErrorCount;}
    bool dcOk() const {return _dcOk;}
    
    // RTK GPS getters
    int rtkFix() const {return _rtkFix;}
    int rtkSats() const {return _rtkSats;}
    double rtkHdop() const {return _rtkHdop;}
    double rtkLat() const {return _rtkLat;}
    double rtkLon() const {return _rtkLon;}
    double rtkAlt() const {return _rtkAlt;}

    Q_INVOKABLE void toggleRecording();
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE QVariant getSourceField(const QString& source, const QString& field) const;
    Q_INVOKABLE QVariantMap getSourceStatus(const QString& source) const;

    enum DcState {
        Idle = 0,
        Initializing = 1,
        Starting = 2,
        Running = 3,
        Stopping = 4,
        Completed = 5,
        Error = 6
    };
    Q_ENUM(DcState)

    enum VideoFlags {
        StreamsReady  = 1 << 0,
        StreamsActive = 1 << 1,
        StreamsError  = 1 << 2
    };
    Q_DECLARE_FLAGS(VideoFlagSet, VideoFlags)
    Q_FLAG(VideoFlagSet)
    
signals:
    void isCollectingChanged();
    void testValueChanged();
    void videoStreamNameChanged();
    void dcStateChanged();
    void dcProgressChanged();
    void dcRunIdChanged();
    void dcErrorCountChanged();
    void dcOkChanged();
    void sourceStatusChanged(const QString& source);
    
    // RTK GPS signals
    void rtkFixChanged();
    void rtkSatsChanged();
    void rtkHdopChanged();
    void rtkLatChanged();
    void rtkLonChanged();
    void rtkAltChanged();

private slots:
    void _onActiveVehicleChanged(Vehicle* vehicle);
    void _requestStreamInfo();          // Request VIDEO_STREAM_INFORMATION
    void _streamInfoTimeout();          // Timeout handler

private:
    bool _isCollecting{false};
    Vehicle* _vehicle{nullptr};
    double _testValue{0.0};
    QString _videoStreamName;;
    QString _videoStreamUri;
    
    // Data collection state variables
    int _dcState{0};
    double _dcProgress{0.0};
    int _dcRunId{0};
    int _dcErrorCount{0};
    bool _dcOk{false};
    int _vidCount{0};
    int _vidFlags{0};
    
    // RTK GPS variables
    int _rtkFix{0};
    int _rtkSats{0};
    double _rtkHdop{0.0};
    double _rtkLat{0.0};
    double _rtkLon{0.0};
    double _rtkAlt{0.0};
    
    // Per-source status tracking: source name -> {field -> value}
    QMap<QString, QVariantMap> _sourceStatus;

    void _sendHttpRequest(QString endpoint);

    void _handleNamedValue(const QString& name, const QVariant& value);
    void _updateSourceStatus(const QString& source, const QString& field, const QVariant& value);
    void _handleCollectionEnd();  // Cleanup when collection ends

    void _startPeriodicStreamInfoRequest();  // Start periodic requests
    void _stopPeriodicStreamInfoRequest();   // Stop periodic requests
    void _sendReadySignalToDataCollector();
    void _notifyDataCollectionState(bool collectionStarted);  // Send STATUSTEXT to component 25
    
    QNetworkAccessManager _networkManager;
    QTimer _streamInfoTimer;            // Timer for periodic requests
    int _streamInfoRetries{0};          // Counter for alternating between modern/legacy commands
    
    static constexpr int STREAM_INFO_POLL_INTERVAL_MS = 5000;  // Poll every 5 seconds
};

Q_DECLARE_OPERATORS_FOR_FLAGS(DataCollectionController::VideoFlagSet)