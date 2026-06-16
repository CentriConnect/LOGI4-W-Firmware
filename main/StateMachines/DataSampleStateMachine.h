#ifndef DATASAMPLE_STATE_MACHINE_H
#define DATASAMPLE_STATE_MACHINE_H

#include "StatesCommon.h"
#include "ILogiHardwareDriver.h"

class EspTimeKeeper;
class ApplicationStateMachine;

class DataSampleStateMachine {
public:

    DataSampleStateMachine(ILogiHardwareDriver* driver, LogiSensorData& sensorData, EspTimeKeeper* timeKeeper);

    void update();

    void transitionTo(DataSampleState newState);

    void setParentStateMachine(ApplicationStateMachine* parentStateMachine);

private:

    ILogiHardwareDriver* _driver;
    LogiSensorData& _sensorData;
    EspTimeKeeper* _timeKeeper;

    ApplicationStateMachine* _parentStateMachine = nullptr;

    DataSampleState currentState = DataSampleState::DataSampleState_SampleSensorData;
    
    void DataSampleStateEnableSensors();
    void DataSampleStateSampleSensorData();
    void DataSampleStateUpdateLocalValues();
};

#endif // DATASAMPLE_STATE_MACHINE_H
