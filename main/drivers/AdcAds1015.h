/*
 * AdcAds1015.h
 *
 * Driver for the TI ADS1015 12-bit I2C ADC (U11 on LOGI4W PCB rev 4.1+).
 * Board rev 4.1 moved most analog measurement off the ESP32-C6 internal ADC
 * (whose pins were repurposed for the 32.768 kHz crystal) onto this part:
 *   AIN0 = SOLAR_VOLT_AI  (2x divider)
 *   AIN1 = SPS_VOLT_AI    (direct, +3.3S rail monitor / level-sensor supply)
 *   AIN2 = BATT_TEMP_AI   (NTC with 10K pull-up to +3.3D)
 *   AIN3 = BATT_VOLT_AI   (2x divider)
 * Register protocol and conversion match the bench-proven hardware-checkout
 * firmware (Logi4W_Hardware_Checkout hardware_test.cpp).
 *
 * Each channel is exposed as an IAdcHal so AnalogLevelSensor and
 * LogiHardwareDriver consume it exactly like an internal ADC channel.
 */

#ifndef DRIVERS_ADC_ADS1015_H_
#define DRIVERS_ADC_ADS1015_H_

#include "interfaces/II2cHal.h"
#include "interfaces/IAdcHal.h"
#include <stdint.h>

/// <summary>
/// Shared ADS1015 device: owns the conversion protocol over one I2C wrapper.
/// Channels (Ads1015Channel) reference this and select their MUX input.
/// </summary>
class Ads1015Device
{
public:
    explicit Ads1015Device(II2cHal &i2c_hal_ref);
    virtual ~Ads1015Device() = default;

    Ads1015Device(const Ads1015Device &) = delete;
    Ads1015Device &operator=(const Ads1015Device &) = delete;

    /// <summary>
    /// Single-shot read of one single-ended channel (0-3) in millivolts at the
    /// ADC pin (FSR +/-4.096 V, 1 LSB = 2 mV). No divider scaling applied here.
    /// </summary>
    bool ReadChannelMv(uint8_t channel, int *out_mv);

    /// <summary>True if the underlying I2C wrapper is initialized.</summary>
    bool IsBusReady() const;

private:
    II2cHal &_i2c;
    static const char *TAG;
};

/// <summary>
/// One ADS1015 input presented as an IAdcHal channel, with an integer
/// numerator/denominator scale for the input divider (e.g. 2/1 for the
/// battery and solar 2x dividers, 1/1 for direct inputs).
/// </summary>
class Ads1015Channel : public IAdcHal
{
public:
    Ads1015Channel(Ads1015Device &device, uint8_t channel,
                   uint8_t scale_num = 1, uint8_t scale_den = 1);
    virtual ~Ads1015Channel() = default;

    // --- IAdcHal ---
    HalAdcError Initialize() override;
    bool IsInitialized() const override;
    HalAdcError GetCounts(int *outRaw) override;
    HalAdcError GetMillivolts(int *outVoltage) override;

private:
    Ads1015Device &_device;
    uint8_t _channel;
    uint8_t _scaleNum;
    uint8_t _scaleDen;
    bool _initialized = false;
    static const char *TAG;
};

#endif /* DRIVERS_ADC_ADS1015_H_ */
