#ifndef APPLICATION_STATE_MACHINE_H
#define APPLICATION_STATE_MACHINE_H

// For std::unique_ptr
#include <memory> 

// Core framework includes
#include "StatesCommon.h"
#include "EspTimeKeeper.h"
#include "EspNvsStorage.h"
#include "LogiSensorData.h"
#include "ILogiHardwareDriver.h"
#include "DeviceSettings.h"
#include "drivers/NvsSettingsService.h"
#include "EspTimeKeeper.h"
#include "EspPowerManager.h"
#include "EspNetworkManager.h"
#include "MQTT/AwsIotManager.h"
#include "wifiCredentials.h"

#include "interfaces/IBluetoothManager.h"
#include "hal/EspBluetoothManager.h"

class WakeStateMachine;
class DataSampleStateMachine;
class CheckFillDetectStateMachine;
class ScheduleCheckStateMachine;
class PostingStateMachine;
class ProvisioningStateMachine;

static const int POST_QUEUE_SIZE = 10;

class ApplicationStateMachine 
{

public:

    // ApplicationStateMachine Constructor
    ApplicationStateMachine();

    // ApplicationStateMachine Destructor, required for unique_ptr with forward-declared types
    ~ApplicationStateMachine(); 

    // ApplicationStateMachine driver initializer
    bool init();

    void update();

    void transitionTo(ApplicationState newState);

    bool isPostQueueEmpty() const;

    bool addToPostQueue(const LogiSensorData& data, PostTransport transport = PostTransport::PostTransport_Mqtt);

    const LogiSensorData& peekPostQueue() const;
    PostTransport peekPostQueueTransport() const;

    void removeFirstFromPostQueue();

    ILogiHardwareDriver* GetHardwareDriver() { return _logiHardwareDriver.get(); }
    void recordSensorSampleTime(time_t sampleTime);

    bool checkAndConnectWifi();

    // Update weekly schedules from shadow state and persist to NVS
    bool updateSchedulesFromShadow(const DeviceShadowState& shadowState);

private:

    // Current state of the ApplicationStateMachine
    ApplicationState currentState = ApplicationState::ApplicationState_Wake;

    // Determines whether ApplicationStateMachine is initialized or not
    bool _isInitialized = false;

    // ApplicationStateMachine core components
    EspNvsStorage _settingsStorage;
    EspNvsStorage _stateStorage;
    NvsSettingsService _settingsService;
    DeviceSettings _deviceSettings;
    EspTimeKeeper _timeKeeper;
    std::unique_ptr<ILogiHardwareDriver> _logiHardwareDriver;
    std::unique_ptr<EspNetworkManager> _networkManager;
    std::unique_ptr<EspPowerManager> _powerManager;
    std::unique_ptr<EspBluetoothManager> _bleManager;
    AwsIotManager _awsIotManager;

    // ApplicationStateMachine child state machines
    std::unique_ptr<WakeStateMachine> _wakeStateMachine;
    std::unique_ptr<ProvisioningStateMachine> _provisioningStateMachine;
    std::unique_ptr<DataSampleStateMachine> _dataSampleStateMachine;
    std::unique_ptr<CheckFillDetectStateMachine> _checkFillDetectStateMachine;
    std::unique_ptr<ScheduleCheckStateMachine> _scheduleCheckStateMachine;
    std::unique_ptr<PostingStateMachine> _postingStateMachine;

    // ApplicationStateMachine data for current cycle
    LogiSensorData _currentSensorData;
    bool _sensorDataValid = false;

    LogiSensorData _postQueue[POST_QUEUE_SIZE];
    PostTransport _postQueueTransport[POST_QUEUE_SIZE];

    int _postQueueHead = 0;
    int _postQueueTail = 0;
    int _postQueueCount = 0;
    
    uint8_t loadLastFuelLevel();
    void saveLastFuelLevel(uint8_t level);
    uint64_t calculateSleepDuration();
    uint64_t calculateSleepDurationFrom(time_t now);

    // ApplicationStateMachine state functions
    void ApplicationStateWake();
    void ApplicationStateProvisioning();
    void ApplicationStateDataSample();
    void ApplicationStateScheduleCheck();
    void ApplicationStateCheckFillDetect();
    void ApplicationStatePosting();
    void ApplicationStateSleep();

    // Provisioning helpers
    void enterProvisioningMode(ProvisioningMode mode);
    bool checkForceProvisioningFlag();
};

#endif // APPLICATION_STATE_MACHINE_H
