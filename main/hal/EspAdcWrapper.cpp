#include "hal/EspAdcWrapper.h"
#include "esp_log.h"
#include "esp_adc/adc_cali_scheme.h"
#include "EspAdcWrapperConfig.h"

const char *EspAdcWrapper::TAG = "EspAdcWrapper";

EspAdcWrapper::EspAdcWrapper(adc_oneshot_unit_handle_t unit_handle, const EspAdcWrapperConfig &config) : config(config),
                                                                                                         unitHandle(unit_handle),
                                                                                                         caliHandle(NULL),
                                                                                                         calibrationAttempted(false),
                                                                                                         calibrationSupported(false),
                                                                                                         initialized(false)
{
    // Ensure scalar is valid
    if (this->config.millivoltScalar == 0)
    {
        this->config.millivoltScalar = 1;
    }
    ESP_LOGD(TAG, "Constructor called for Unit Handle %p, Channel %d. Call Initialize() next.", unitHandle, this->config.channel);
    if (unitHandle == NULL)
    {
        ESP_LOGE(TAG, "Constructor Error: Unit handle is NULL for channel %d", this->config.channel);
        // Initialize() will fail later
    }
}

EspAdcWrapper::~EspAdcWrapper()
{
    ESP_LOGD(TAG, "Destructor called for Channel %d.", config.channel);
    CleanupCalibrationHandle();
    // Do NOT delete unitHandle - it's owned elsewhere (factory)
    initialized = false; // Ensure state reflects destruction
}

void EspAdcWrapper::CleanupCalibrationHandle()
{
    if (caliHandle != NULL)
    {
        ESP_LOGD(TAG, "Deleting Calibration handle for channel %d", config.channel);
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_delete_scheme_curve_fitting(caliHandle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_delete_scheme_line_fitting(caliHandle);
#endif
        caliHandle = NULL;
    }
    calibrationSupported = false;
    calibrationAttempted = false;
}

HalAdcError EspAdcWrapper::Initialize()
{
    if (initialized)
    {
        ESP_LOGW(TAG, "Already initialized (Channel %d).", config.channel);
        return HAL_ADC_OK;
    }
    if (unitHandle == NULL)
    {
        ESP_LOGE(TAG, "Cannot initialize Channel %d: Unit handle is NULL.", config.channel);
        return MapError(ESP_ERR_INVALID_STATE);
    }

    ESP_LOGI(TAG, "Initializing ADC Channel %d...", config.channel);

    // 1. Configure ADC Channel
    adc_oneshot_chan_cfg_t chan_config = {
        .atten = config.attenuation,
        .bitwidth = config.bitwidth,
    };
    esp_err_t err = adc_oneshot_config_channel(unitHandle, config.channel, &chan_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure ADC channel %d: %s", config.channel, esp_err_to_name(err));
        initialized = false; // Ensure not marked initialized
        return MapError(err);
    }

    // 2. Initialize Calibration (if not already attempted)
    // Cleanup previous attempt just in case Initialize is called again after failure
    CleanupCalibrationHandle();
    calibrationSupported = InitializeCalibration(config.attenuation, config.bitwidth, config.channel);
    calibrationAttempted = true;
    if (calibrationSupported)
    {
        ESP_LOGI(TAG, "ADC Calibration initialized for Channel %d.", config.channel);
    }
    else
    {
        ESP_LOGW(TAG, "ADC Calibration failed or not supported for Atten:%d BitW:%d Chan: %d.",
                 config.attenuation, config.bitwidth, config.channel);
    }

    initialized = true;
    ESP_LOGI(TAG, "ADC Channel %d Wrapper initialized successfully.", config.channel);
    return HAL_ADC_OK;
}

bool EspAdcWrapper::IsInitialized() const
{
    return initialized;
}

bool EspAdcWrapper::InitializeCalibration(adc_atten_t atten, adc_bitwidth_t bitwidth, adc_channel_t chan)
{
    caliHandle = NULL; // Ensure handle is null before trying
    esp_err_t err = ESP_FAIL;

    ESP_LOGD(TAG, "Checking ADC calibration scheme support for chan %d...", chan);

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = config.unit,
        .chan = chan,
        .atten = atten,
        .bitwidth = bitwidth,
    };
    ESP_LOGI(TAG, "Attempting Curve Fitting calibration for chan %d.", chan);
    err = adc_cali_create_scheme_curve_fitting(&cali_config, &caliHandle);
    if (err == ESP_OK && caliHandle != NULL)
    {
        return true;
    }
    ESP_LOGW(TAG, "Failed Curve Fitting for chan %d: %s", chan, esp_err_to_name(err));

#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = config.unit,
        .atten = atten,
        .bitwidth = bitwidth,
    };
    ESP_LOGI(TAG, "Attempting Line Fitting calibration for unit %d.", config.unit);
    err = adc_cali_create_scheme_line_fitting(&cali_config, &caliHandle);
    if (err == ESP_OK && caliHandle != NULL)
    {
        return true;
    }
    ESP_LOGW(TAG, "Failed Line Fitting for unit %d: %s", config.unit, esp_err_to_name(err));

#else
    ESP_LOGW(TAG, "No known calibration scheme supported in this build.");
#endif

    caliHandle = NULL;
    return false;
}

HalAdcError EspAdcWrapper::GetCounts(int *out_raw)
{
    if (!IsInitialized())
    {
        return MapError(ESP_ERR_INVALID_STATE);
    }
    if (out_raw == NULL)
    {
        return MapError(ESP_ERR_INVALID_ARG);
    }
    if (unitHandle == NULL)
    {
        return MapError(ESP_ERR_INVALID_STATE);
    }

    esp_err_t read_err = adc_oneshot_read(unitHandle, config.channel, out_raw);
    if (read_err != ESP_OK)
    {
        ESP_LOGE(TAG, "ADC Channel %d Read Failed: %s", config.channel, esp_err_to_name(read_err));
    }
    return MapError(read_err);
}

HalAdcError EspAdcWrapper::GetMillivolts(int *out_voltage)
{
    if (!IsInitialized())
    {
        return MapError(ESP_ERR_INVALID_STATE);
    }
    if (out_voltage == NULL)
    {
        return MapError(ESP_ERR_INVALID_ARG);
    }

    int raw_value = 0;
    HalAdcError read_hal_err = GetCounts(&raw_value);
    if (read_hal_err != HAL_ADC_OK)
    {
        *out_voltage = 0;
        return read_hal_err;
    }

    if (calibrationSupported && caliHandle != NULL)
    {
        esp_err_t cali_err = adc_cali_raw_to_voltage(caliHandle, raw_value, out_voltage);
        if (cali_err == ESP_OK)
        {
            *out_voltage = (*out_voltage) * config.millivoltScalar;
            return HAL_ADC_OK;
        }
        else
        {
            ESP_LOGE(TAG, "ADC Cal raw_to_voltage failed (Chan %d): %s", config.channel, esp_err_to_name(cali_err));
            *out_voltage = 0;
            return MapError(cali_err);
        }
    }
    else
    {
        if (!calibrationAttempted)
        {
            ESP_LOGW(TAG, "Calibration not attempted (Chan %d).", config.channel);
        }
        else
        {
            ESP_LOGD(TAG, "ADC Cal not available for voltage conversion (Chan %d, Raw: %d).", config.channel, raw_value);
        }
        *out_voltage = 0;
        return HAL_ADC_ERR_NOT_SUPPORTED;
    }
}

uint8_t EspAdcWrapper::SetMillivoltScalar(uint8_t scalar)
{
    config.millivoltScalar = (scalar > 0) ? scalar : 1;
    return config.millivoltScalar;
}

uint8_t EspAdcWrapper::GetMillivoltScalar() const
{
    return config.millivoltScalar;
}

bool EspAdcWrapper::IsCalibrationSupported() const
{
    return calibrationSupported;
}

HalAdcError EspAdcWrapper::MapError(esp_err_t err)
{
    switch (err)
    {
    case ESP_OK:
        return HAL_ADC_OK;
    case ESP_ERR_INVALID_ARG:
        return HAL_ADC_ERR_INVALID_ARG;
    case ESP_ERR_INVALID_STATE:
        return HAL_ADC_ERR_INVALID_STATE;
    case ESP_ERR_NOT_SUPPORTED:
        return HAL_ADC_ERR_NOT_SUPPORTED;
    case ESP_ERR_NOT_FOUND:
        return HAL_ADC_ERR_INIT_FAILED;
    case ESP_FAIL:
    case ESP_ERR_TIMEOUT:
    default:
        return HAL_ADC_ERR_READ_FAILED;
    }
}