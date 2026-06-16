// ----- File: main/drivers/AdcAds1015.cpp -----
#include "drivers/AdcAds1015.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

const char *Ads1015Device::TAG = "Ads1015";
const char *Ads1015Channel::TAG = "Ads1015Ch";

// ADS1015 register addresses
static constexpr uint8_t REG_CONVERSION = 0x00;
static constexpr uint8_t REG_CONFIG = 0x01;

Ads1015Device::Ads1015Device(II2cHal &i2c_hal_ref)
    : _i2c(i2c_hal_ref)
{
}

bool Ads1015Device::IsBusReady() const
{
    return _i2c.IsInitialized();
}

bool Ads1015Device::ReadChannelMv(uint8_t channel, int *out_mv)
{
    if (out_mv == nullptr || channel > 3)
    {
        return false;
    }
    if (!_i2c.IsInitialized())
    {
        return false;
    }

    // Config: OS=1 (start single conversion), MUX = AINx vs GND (0x04 + ch),
    // PGA = +/-4.096 V (0x0200), MODE = single-shot (0x0100),
    // DR = 1600 SPS (0x0080), comparator disabled (0x0003).
    // Matches the bench-proven checkout-firmware configuration.
    const uint16_t mux = static_cast<uint16_t>(0x04 + channel) << 12;
    const uint16_t config = 0x8000 | mux | 0x0200 | 0x0100 | 0x0080 | 0x0003;

    uint8_t cfg_buf[3] = {REG_CONFIG,
                          static_cast<uint8_t>(config >> 8),
                          static_cast<uint8_t>(config & 0xFF)};
    if (_i2c.Write(cfg_buf, sizeof(cfg_buf)) != 0)
    {
        ESP_LOGE(TAG, "Config write failed (ch %u)", channel);
        return false;
    }

    // 1600 SPS -> conversion ready in <1 ms; 2 ms is comfortable margin.
    vTaskDelay(pdMS_TO_TICKS(2));

    uint8_t reg = REG_CONVERSION;
    uint8_t raw_buf[2] = {0, 0};
    if (_i2c.WriteRead(&reg, 1, raw_buf, sizeof(raw_buf)) != 0)
    {
        ESP_LOGE(TAG, "Conversion read failed (ch %u)", channel);
        return false;
    }

    int16_t raw = static_cast<int16_t>((raw_buf[0] << 8) | raw_buf[1]);
    raw >>= 4; // 12-bit result, left-justified
    *out_mv = static_cast<int>(raw) * 2; // FSR 4.096 V -> 2 mV/LSB
    return true;
}

Ads1015Channel::Ads1015Channel(Ads1015Device &device, uint8_t channel,
                               uint8_t scale_num, uint8_t scale_den)
    : _device(device), _channel(channel),
      _scaleNum(scale_num == 0 ? 1 : scale_num),
      _scaleDen(scale_den == 0 ? 1 : scale_den)
{
}

HalAdcError Ads1015Channel::Initialize()
{
    if (!_device.IsBusReady())
    {
        ESP_LOGE(TAG, "Ch %u init failed: I2C wrapper not initialized", _channel);
        _initialized = false;
        return HAL_ADC_ERR_INIT_FAILED;
    }
    // Prove the part answers: one throwaway conversion.
    int mv = 0;
    if (!_device.ReadChannelMv(_channel, &mv))
    {
        ESP_LOGE(TAG, "Ch %u init failed: no response from ADS1015", _channel);
        _initialized = false;
        return HAL_ADC_ERR_INIT_FAILED;
    }
    ESP_LOGI(TAG, "Ch %u initialized (first read %d mV at pin, scale %u/%u)",
             _channel, mv, _scaleNum, _scaleDen);
    _initialized = true;
    return HAL_ADC_OK;
}

bool Ads1015Channel::IsInitialized() const
{
    return _initialized;
}

HalAdcError Ads1015Channel::GetCounts(int *outRaw)
{
    if (outRaw == nullptr)
    {
        return HAL_ADC_ERR_INVALID_ARG;
    }
    if (!_initialized)
    {
        return HAL_ADC_ERR_INVALID_STATE;
    }
    int mv = 0;
    if (!_device.ReadChannelMv(_channel, &mv))
    {
        return HAL_ADC_ERR_READ_FAILED;
    }
    *outRaw = mv / 2; // back to raw 12-bit counts
    return HAL_ADC_OK;
}

HalAdcError Ads1015Channel::GetMillivolts(int *outVoltage)
{
    if (outVoltage == nullptr)
    {
        return HAL_ADC_ERR_INVALID_ARG;
    }
    if (!_initialized)
    {
        return HAL_ADC_ERR_INVALID_STATE;
    }
    int mv = 0;
    if (!_device.ReadChannelMv(_channel, &mv))
    {
        return HAL_ADC_ERR_READ_FAILED;
    }
    *outVoltage = (mv * _scaleNum) / _scaleDen;
    return HAL_ADC_OK;
}
