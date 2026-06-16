# ----- File: main/CMakeLists.txt -----

# A list of all your C and C++ source files
set(SOURCES
    "main.cpp"
    "hardware_test.cpp"
    "hal/EspNvsStorage.cpp"
    "hal/EspPowerManager.cpp"
    "hal/EspNetworkManager.cpp"
    "hal/EspTimeKeeper.cpp"
    "hal/EspPlatform.cpp"
    "hal/EspGpioWrapper.cpp"
    "hal/EspAdcWrapper.cpp"
    "hal/EspI2cMasterWrapper.cpp"
    "hal/EspUartWrapper.cpp"
    "hal/EspBluetoothManager.cpp"  
    "drivers/TemperatureSensorSht4x.cpp"
    "drivers/RgbLedPartNumberIs31fl3193.cpp"
    "drivers/AnalogLevelSensor.cpp"
    "drivers/AdcAds1015.cpp"
    "drivers/LevelSensorI2c9705.cpp"
    "drivers/NvsSettingsService.cpp"
    "drivers/GPSModuleYIC51009EBGGB.cpp"
    "logi/LogiHardwareDriver.cpp"
    "logi/EspLogiHardwareFactory.cpp"
    "logi/DeviceSettings.cpp"
    "helpers/MovingAverage.cpp"
    "CLibFiles/DateTime/Date.c"
    "CLibFiles/DateTime/Time.c"
    "CLibFiles/DateTime/DateTime.c"
    "CLibFiles/DateTime/WeeklySchedule.c"
    "MQTT/AwsIotManager.cpp"
    "MQTT/AwsIotClient.cpp"
    "MQTT/AwsIotConfig.cpp"
    "MQTT/AwsIotJobsHandler.cpp"
    "MQTT/ShadowParser.cpp"
    "StateMachines/ApplicationStateMachine.cpp"
    "StateMachines/CheckFillDetectStateMachine.cpp"
    "StateMachines/DataSampleStateMachine.cpp"
    "StateMachines/PostingStateMachine.cpp"
    "StateMachines/ProvisioningStateMachine.cpp"
    "StateMachines/ScheduleCheckStateMachine.cpp"
    "StateMachines/WakeStateMachine.cpp"
)

# A list of all directories containing header files
set(INCLUDE_DIRS
    "."
    "hal"
    "interfaces"
    "drivers"
    "logi"
    "helpers"
    "CLibFiles"
    "MQTT"
    "StateMachines"
)

# A list of components this 'main' component depends on
set(REQUIRES
    "mqtt"
    "json"
    "esp_http_client"
    "driver"
    "esp_adc"
    "qrcode"
    "wifi_provisioning"
    "coreMQTT"
    "coreMQTT-Agent"
    "corePKCS11"
    "coreJSON"
    "backoffAlgorithm"
    "aws-iot-core-mqtt-file-streams-embedded-c"
    "FreeRTOS-Libraries-Integration-Tests"
    "unity"
    "nvs_flash"
    "esp_wifi"
    "esp_netif"
    "esp_event"
    "esp_pm"
    "bt"
    "esp_secure_cert_mgr"
)

# Private requirements used only by this component
# For ESP-IDF v5.4.1, use "bt" component which includes NimBLE
set(PRIV_REQUIRES
    "esp_timer"
)

# Per-board TLS material (device cert + private key) lives in the esp_secure_cert
# flash partition, not embedded in the app image. See AwsIotClient::Initialize.

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS ${INCLUDE_DIRS}
    REQUIRES ${REQUIRES}
    PRIV_REQUIRES ${PRIV_REQUIRES}
)