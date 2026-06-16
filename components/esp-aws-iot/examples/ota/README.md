The FreeRTOS 202406.01-LTS release uses the new and completely refactored 'Modular OTA' approach for Over the Air updates.
It makes use of the new [AWS IoT MQTT File Streams Library](https://github.com/espressif/esp-aws-iot/tree/master/libraries/aws-iot-core-mqtt-file-streams-embedded-c) for OTA application design.
More information about the approach can be found [here](https://www.freertos.org/freertos-core/over-the-air-updates/index.html#:~:text=OTA%20update%20provider.-,Modular%20OTA%20approach,-Modular%20OTA%20consists).
 
Please refer to the [iot-reference example](https://github.com/FreeRTOS/iot-reference-esp32c3) for a production-ready starting point that uses this new approach.
