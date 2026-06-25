#ifndef SCHEDULE_CHECK_STATE_MACHINE_H
#define SCHEDULE_CHECK_STATE_MACHINE_H

#include "StatesCommon.h"
#include "DeviceSettings.h"
#include "EspTimeKeeper.h"
#include <ctime>
#include "EspNetworkManager.h"
#include "LogiSensorData.h"

static const size_t MAX_WEEKLY_SCHEDULES = 8;

class ApplicationStateMachine; 

class ScheduleCheckStateMachine {
public:

    ScheduleCheckStateMachine(EspTimeKeeper* timeKeeper, const DeviceSettings& deviceSettings, EspNetworkManager* networkManager, LogiSensorData& sensorData);

    void update();      

    void transitionTo(ScheduleCheckState newState);       

    void setParentStateMachine(ApplicationStateMachine* parentStateMachine);

private:

    ApplicationStateMachine* _parentStateMachine = nullptr;

    EspTimeKeeper* _timeKeeper;
    const DeviceSettings& _deviceSettings;
    EspNetworkManager* _networkManager;
    LogiSensorData& _sensorData;  

    ScheduleCheckState currentState = ScheduleCheckState::ScheduleCheckState_ValidateDateTime;

    struct tm _timeInfo;

    bool IsValidDateTime(time_t time);
    bool IsProvisioned(const DeviceSettings& deviceSettings);
    void update_last_post_time(int64_t current_time);
    bool evaluateEventThresholdPost(float& ratioPct, float& crossedThreshold);

    void ScheduleCheckStateValidateDateTime();
    void ScheduleCheckStateCheckProvisioning();
    void ScheduleCheckStateCompareTimeToScheduledTime();
};

#endif // SCHEDULE_CHECK_STATE_MACHINE_H
