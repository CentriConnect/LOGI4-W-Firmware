#ifndef CHECK_FILL_DETECT_STATE_MACHINE_H
#define CHECK_FILL_DETECT_STATE_MACHINE_H

#include "StatesCommon.h"
#include "DeviceSettings.h"
#include "EspTimeKeeper.h"
#include "LogiSensorData.h"

class ApplicationStateMachine; 

class CheckFillDetectStateMachine 
{
public:

    CheckFillDetectStateMachine(EspTimeKeeper* timeKeeper, const DeviceSettings& deviceSettings, LogiSensorData& sensorData);

    void update();  

    void transitionTo(CheckFillDetectState newState);      
    
    void setParentStateMachine(ApplicationStateMachine* parentStateMachine);

private:

    ApplicationStateMachine* _parentStateMachine = nullptr;

    EspTimeKeeper* _timeKeeper;
    const DeviceSettings& _deviceSettings;
    LogiSensorData& _sensorData;
    
    CheckFillDetectState currentState = CheckFillDetectState::CheckFillDetectState_CheckInDwellMode;

    bool _inDwell = false;
    float _currentFillLevel = 0.0f;
    float _lastStableLevel = 0.0f;
    uint32_t _dwellMinutes = 0;
    uint32_t _dwellMinutesRequired = 0;
    uint32_t _fillDwellTime;
    uint8_t _fillAlarmDelta;

    void CheckFillDetectStateCheckInDwellMode();
    void CheckFillDetectStateCompareFuelLevels();
    void CheckFillDetectStateCompareFuelLevelsWithDelta();
    void CheckFillDetectStateCheckDwellTime();
};

#endif // CHECK_FILL_DETECT_STATE_MACHINE_H