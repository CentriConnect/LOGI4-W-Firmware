#ifndef POSTING_STATE_MACHINE_H
#define POSTING_STATE_MACHINE_H

#include "StatesCommon.h"
#include "AwsIotManager.h"
#include "AwsIotJobsHandler.h"
#include "EspTimeKeeper.h"
#include "LogiSensorData.h"
#include "ILogiHardwareDriver.h"
#include "logi/DeviceSettings.h"
#include <memory>
#include <optional>

class ApplicationStateMachine;

// Default connection timeout if DeviceSettings not available (fallback only)
#define DEFAULT_CONNECTION_TIMEOUT_S 300

class PostingStateMachine
{
public:
    PostingStateMachine(AwsIotManager* awsIotManager, EspTimeKeeper* timeKeeper,
                        ILogiHardwareDriver* driver,
                        LogiSensorData& sensorData, DeviceSettings& deviceSettings);

    void update();    

    void transitionTo(PostingState newState);     
    
    void setParentStateMachine(ApplicationStateMachine* parentStateMachine);

private:
    ApplicationStateMachine* _parentStateMachine = nullptr;

    AwsIotManager* _awsIotManager;
    EspTimeKeeper* _timeKeeper;
    ILogiHardwareDriver* _driver;
    LogiSensorData& _sensorData;
    DeviceSettings& _deviceSettings;

    DeviceShadowState _shadowState;

    PostingState currentState = PostingState::PostingState_InitialEnter;

    time_t _postStartTime = 0;
    int64_t _postStartUs = 0;
    time_t _dwellStartTime = 0;     // When post dwell started
    bool _postSuccessful = false;   // Track if posting succeeded for dwell
    int _connectRetryCount = 0;
    const int MAX_CONNECT_RETRIES = 3;

    // Add Jobs handler and FOTA related members
    AwsIotJobsHandler* _jobsHandler = nullptr;
    std::optional<AwsIotJob> _currentFotaJob;
    bool _fotaCheckComplete = false;

    // Gets the connection timeout in seconds (from shadow config, internally renamed from lte_timeout)
    uint32_t getConnectionTimeoutSeconds() const;
    uint32_t getMqttWaterfallTimeoutSeconds() const;
    bool isPostingTimeoutExpired() const;
    uint32_t getRemainingPostingBudgetSeconds() const;

    // Gets the post dwell time in seconds (from shadow config)
    uint32_t getPostDwellTimeSeconds() const;

    // Populates TelemetryContext with device info, settings, etc. for LOGI4 format
    void populateTelemetryContext(TelemetryContext& context) const;
    bool publishTelemetrySnapshot(const LogiSensorData& data, const char* label);
    bool publishUdpTelemetrySnapshot(const LogiSensorData& data, const char* label);
    bool acquireGpsForMqttPost(LogiSensorData& data);
    void applyShadowSettingsToMemory(const DeviceShadowState& shadowState);
    DeviceShadowState buildReportedShadowFromSettings(const DeviceShadowState& parsedShadowState) const;

    // State handler methods
    void PostingStateInitialEnter();
    void PostingStateExitAWS();
    void PostingStateTryConnect();
    void PostingStatePostImmediateTelemetry();
    void PostingStateSubscribeToTopics();
    void PostingStateSendGetShadowDelta();
    void PostingStateGotShadowDelta();
    void PostingStateHandleShadowDelta();
    void PostingStateCheckFOTA();
    void PostingStateDoFOTAUpdate();
    void PostingStateAcquireFinalSample();
    void PostingStatePostFinalTelemetry();
    void PostingStateDoPostsFromQueue();
    void PostingStatePostDwell();
};

#endif // POSTING_STATE_MACHINE_H
