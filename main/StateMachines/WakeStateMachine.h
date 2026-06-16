#ifndef WAKE_STATE_MACHINE_H
#define WAKE_STATE_MACHINE_H

#include "StatesCommon.h"
//#include "ApplicationStateMachine.h"
#include "EspPowerManager.h"
#include "EspTimeKeeper.h"

#include "ILogiHardwareDriver.h"

class ApplicationStateMachine; 

class WakeStateMachine {
public:

    WakeStateMachine(ILogiHardwareDriver* driver, EspPowerManager* powerManager, EspTimeKeeper* timeKeeper);

    void update();       

    void transitionTo(WakeState newState);       

    void setParentStateMachine(ApplicationStateMachine* parentStateMachine);

private:
    
    WakeState currentState = WakeState::WakeState_CheckStartupCause;

    ILogiHardwareDriver* _driver;
    EspPowerManager* _powerManager;
    EspTimeKeeper* _timeKeeper;

    ApplicationStateMachine* _parentStateMachine = nullptr;

    void WakeStateCheckStartupCause();
};

#endif // WAKE_STATE_MACHINE_H
