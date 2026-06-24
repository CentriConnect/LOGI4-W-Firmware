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

    bool isInDwell() const { return _inDwell; }
    int64_t getDwellDeadline() const { return _dwellDeadline; }

private:

    ApplicationStateMachine* _parentStateMachine = nullptr;

    EspTimeKeeper* _timeKeeper;
    const DeviceSettings& _deviceSettings;
    LogiSensorData& _sensorData;
    
    CheckFillDetectState currentState = CheckFillDetectState::CheckFillDetectState_CheckInDwellMode;

    bool _inDwell = false;
    float _currentFillLevel = 0.0f;
    float _lastStableLevel = 0.0f;
    int64_t _dwellDeadline = 0;
    uint32_t _fillDwellTime = 0;
    uint8_t _fillAlarmDelta = 0;

    void CheckFillDetectStateCheckInDwellMode();
    void CheckFillDetectStateCompareFuelLevels();
    void CheckFillDetectStateCompareFuelLevelsWithDelta();
    void CheckFillDetectStateCheckDwellTime();
    void persistDwellState();
    void clearDwellState(float stableLevel);
    void sleepUntilNextWake();
    void enqueueFinalFillUdpPost();
};

#endif // CHECK_FILL_DETECT_STATE_MACHINE_H
