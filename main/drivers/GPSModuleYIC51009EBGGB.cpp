#include "GPSModuleYIC51009EBGGB.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <algorithm>

// Constructor
GPSModuleYIC51009EBGGB::GPSModuleYIC51009EBGGB(EspUartWrapper& uartWrapper)
    : _uartWrapper(uartWrapper), _hasValidFix(false), _event_handler_instance(nullptr),
      _powerOnTimestampMs(0), _ttffMeasured(false)
{
    memset(&_currentData, 0, sizeof(_currentData));
    memset(_nmeaBuffer, 0, sizeof(_nmeaBuffer));
    // Set default DOP values to unknown (99.9 indicates no data)
    _currentData.hdop = 99.9f;
    _currentData.vdop = 99.9f;
    _currentData.pdop = 99.9f;
    _currentData.fixType = 1;  // 1 = no fix
    ESP_LOGI(TAG, "GPS Module constructed");
}

// Destructor
GPSModuleYIC51009EBGGB::~GPSModuleYIC51009EBGGB()
{
    // Unregister event handler if it was registered
    if (_event_handler_instance) {
        esp_event_handler_instance_unregister(
            EspUartWrapper::UART_EVENT_BASE,
            EspUartWrapper::UART_DATA_RECEIVED,
            _event_handler_instance
        );
    }
}

// Initialize and register for UART events
bool GPSModuleYIC51009EBGGB::Initialize()
{
    ESP_LOGI(TAG, "Initializing GPS Module");
    
    // Register for UART data events
    esp_err_t err;
    esp_event_loop_handle_t loop_handle = _uartWrapper.getEventLoopHandle();
    
    if (loop_handle != nullptr) {
        err = esp_event_handler_instance_register_with(
            loop_handle,
            EspUartWrapper::UART_EVENT_BASE,
            EspUartWrapper::UART_DATA_RECEIVED,
            uart_event_handler,
            this,
            &_event_handler_instance
        );
    } else {
        err = esp_event_handler_instance_register(
            EspUartWrapper::UART_EVENT_BASE,
            EspUartWrapper::UART_DATA_RECEIVED,
            uart_event_handler,
            this,
            &_event_handler_instance
        );
    }
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register UART event handler: %s", esp_err_to_name(err));
        return false;
    }
    
    // Record power-on timestamp for TTFF measurement
    _powerOnTimestampMs = xTaskGetTickCount() * portTICK_PERIOD_MS;
    _ttffMeasured = false;

    ESP_LOGI(TAG, "GPS Module initialized successfully");
    return true;
}

// Static event handler
void GPSModuleYIC51009EBGGB::uart_event_handler(void* handler_arg, esp_event_base_t base, 
                                                int32_t id, void* event_data)
{
    GPSModuleYIC51009EBGGB* self = static_cast<GPSModuleYIC51009EBGGB*>(handler_arg);
    
    if (id == EspUartWrapper::UART_DATA_RECEIVED)
    {
        EspUartWrapper::UartEventData* data = static_cast<EspUartWrapper::UartEventData*>(event_data);
        // ESP_LOGI("GPSModule", "Received %d bytes of NMEA data", data->length);
        self->processNmeaData(reinterpret_cast<const char*>(data->data), data->length);
    }
}

// Process incoming NMEA data
void GPSModuleYIC51009EBGGB::processNmeaData(const char* data, size_t length)
{
    // Process each line in the received data
    const char* line = data;
    while (*line && line < data + length) {
        const char* next_line = strchr(line, '\n');
        size_t len = next_line ? (size_t)(next_line - line) : strlen(line);

        if (len > 0 && len < 256) { // Reasonable NMEA sentence length
            char sentence[256] = {0};
            strncpy(sentence, line, len);
            sentence[len] = '\0';

            // Remove \r if present
            char* cr = strchr(sentence, '\r');
            if (cr) *cr = '\0';

            // Parse if it looks like a valid NMEA sentence
            if (sentence[0] == '$') {
                parseNmeaSentence(sentence);
            }
        }

        line = next_line ? (next_line + 1) : (data + length);
    }
}

// Parse individual NMEA sentence
void GPSModuleYIC51009EBGGB::parseNmeaSentence(const char* sentence)
{
    char buffer[256];
    strncpy(buffer, sentence, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    
    // Tokenize the sentence
    char* tokens[30] = {nullptr};
    int tokenCount = 0;
    char* token = strtok(buffer, ",");
    
    while (token && tokenCount < 30) {
        tokens[tokenCount++] = token;
        token = strtok(nullptr, ",");
    }
    
    if (tokenCount < 2) return;
    
    // Process based on sentence type
    bool dataUpdated = false;
    
    if (strncmp(tokens[0], "$GNGGA", 6) == 0) {
        dataUpdated = parseGNGGA(tokens, tokenCount);
    } else if (strncmp(tokens[0], "$GNGLL", 6) == 0) {
        dataUpdated = parseGNGLL(tokens, tokenCount);
    } else if (strncmp(tokens[0], "$GNRMC", 6) == 0) {
        dataUpdated = parseGNRMC(tokens, tokenCount);
    } else if (strncmp(tokens[0], "$GNGSV", 6) == 0 || strncmp(tokens[0], "$GPGSV", 6) == 0) {
        parseGNGSV(tokens, tokenCount);
    } else if (strncmp(tokens[0], "$GNGSA", 6) == 0 || strncmp(tokens[0], "$GPGSA", 6) == 0) {
        parseGNGSA(tokens, tokenCount);
    }
    
    // Print GPS data if updated and valid
    if (dataUpdated && _hasValidFix) {
        ESP_LOGI(TAG, "GPS Data - Lat: %.6f, Lon: %.6f, Alt: %.1f m, RSSI: %d dB",
                _currentData.latitude, _currentData.longitude, 
                _currentData.altitude, _currentData.rssi);
    }
}

// Parse GNGGA sentence (position and altitude)
bool GPSModuleYIC51009EBGGB::parseGNGGA(char* tokens[], int tokenCount)
{
    if (tokenCount < 12) return false;
    
    // Check fix quality (field 6)
    int fixQuality = tokens[6] ? atoi(tokens[6]) : 0;
    if (fixQuality == 0) {
        _hasValidFix = false;
        return false;
    }
    
    // Parse latitude
    if (tokens[2] && tokens[3] && strlen(tokens[2]) > 0) {
        _currentData.latitude = convertNmeaCoordinate(tokens[2], tokens[3], false);
    }
    
    // Parse longitude
    if (tokens[4] && tokens[5] && strlen(tokens[4]) > 0) {
        _currentData.longitude = convertNmeaCoordinate(tokens[4], tokens[5], true);
    }
    
    // Parse altitude (field 9)
    if (tokens[9] && strlen(tokens[9]) > 0) {
        _currentData.altitude = atof(tokens[9]);
    }

    // Parse satellites used (field 7)
    if (tokens[7] && strlen(tokens[7]) > 0) {
        _currentData.satellitesUsed = static_cast<uint8_t>(atoi(tokens[7]));
    }

    // Parse HDOP (field 8)
    if (tokens[8] && strlen(tokens[8]) > 0) {
        _currentData.hdop = static_cast<float>(atof(tokens[8]));
    }

    // Calculate TTFF on first valid fix
    if (!_ttffMeasured && !_hasValidFix) {
        uint32_t currentTimeMs = xTaskGetTickCount() * portTICK_PERIOD_MS;
        _currentData.ttffMs = currentTimeMs - _powerOnTimestampMs;
        _ttffMeasured = true;
        ESP_LOGI(TAG, "TTFF: %lu ms", _currentData.ttffMs);
    }

    _hasValidFix = true;
    _currentData.valid = true;
    return true;
}

// Parse GNGLL sentence (position only)
bool GPSModuleYIC51009EBGGB::parseGNGLL(char* tokens[], int tokenCount)
{
    if (tokenCount < 7) return false;
    
    // Check status (field 6)
    if (!tokens[6] || tokens[6][0] != 'A') {
        _hasValidFix = false;
        return false;
    }
    
    // Parse latitude
    if (tokens[1] && tokens[2] && strlen(tokens[1]) > 0) {
        _currentData.latitude = convertNmeaCoordinate(tokens[1], tokens[2], false);
    }
    
    // Parse longitude
    if (tokens[3] && tokens[4] && strlen(tokens[3]) > 0) {
        _currentData.longitude = convertNmeaCoordinate(tokens[3], tokens[4], true);
    }
    
    _hasValidFix = true;
    _currentData.valid = true;
    return true;
}

// Parse GNRMC sentence (position and status)
bool GPSModuleYIC51009EBGGB::parseGNRMC(char* tokens[], int tokenCount)
{
    if (tokenCount < 10) return false;
    
    // Check status (field 2)
    if (!tokens[2] || tokens[2][0] != 'A') {
        _hasValidFix = false;
        return false;
    }
    
    // Parse latitude
    if (tokens[3] && tokens[4] && strlen(tokens[3]) > 0) {
        _currentData.latitude = convertNmeaCoordinate(tokens[3], tokens[4], false);
    }
    
    // Parse longitude
    if (tokens[5] && tokens[6] && strlen(tokens[5]) > 0) {
        _currentData.longitude = convertNmeaCoordinate(tokens[5], tokens[6], true);
    }
    
    _hasValidFix = true;
    _currentData.valid = true;
    return true;
}

// Parse GNGSV sentence (satellite info for signal strength)
bool GPSModuleYIC51009EBGGB::parseGNGSV(char* tokens[], int tokenCount)
{
    if (tokenCount < 4) return false;

    // GNGSV contains satellite info including SNR (signal-to-noise ratio)
    // Format: $GNGSV,sentences,sentence#,sats,[sat#,elev,azim,SNR,...]
    // We'll track the maximum SNR as our RSSI indicator

    // Get message number (field 2)
    int msgNum = tokens[2] ? atoi(tokens[2]) : 0;

    // Parse satellites in view from first message only (field 3)
    if (msgNum == 1 && tokens[3] && strlen(tokens[3]) > 0) {
        _currentData.satellitesInView = static_cast<uint8_t>(atoi(tokens[3]));
    }

    int maxSnr = 0;

    // Each satellite has 4 fields, starting from index 4
    for (int i = 4; i + 3 < tokenCount; i += 4) {
        // SNR is the 4th field for each satellite
        if (tokens[i + 3] && strlen(tokens[i + 3]) > 0) {
            int snr = atoi(tokens[i + 3]);
            if (snr > maxSnr) {
                maxSnr = snr;
            }
        }
    }

    // Update RSSI if we found a valid SNR
    if (maxSnr > 0) {
        _currentData.rssi = maxSnr;
    }

    return true;
}

// Parse GNGSA sentence (DOP and active satellites)
bool GPSModuleYIC51009EBGGB::parseGNGSA(char* tokens[], int tokenCount)
{
    // GSA format: $GNGSA,mode,fixType,sat1-12,PDOP,HDOP,VDOP[,sysId]*cs
    // Minimum tokens: mode(1), fixType(2), 12 sat IDs(3-14), PDOP(15), HDOP(16), VDOP(17)
    if (tokenCount < 18) return false;

    // Fix type (field 2): 1=no fix, 2=2D, 3=3D
    if (tokens[2] && strlen(tokens[2]) > 0) {
        _currentData.fixType = static_cast<uint8_t>(atoi(tokens[2]));
    }

    // PDOP (field 15, after 12 satellite IDs)
    if (tokens[15] && strlen(tokens[15]) > 0) {
        _currentData.pdop = static_cast<float>(atof(tokens[15]));
    }

    // HDOP (field 16)
    if (tokens[16] && strlen(tokens[16]) > 0) {
        _currentData.hdop = static_cast<float>(atof(tokens[16]));
    }

    // VDOP (field 17) - may have checksum attached
    if (tokens[17] && strlen(tokens[17]) > 0) {
        char vdopStr[16];
        strncpy(vdopStr, tokens[17], sizeof(vdopStr) - 1);
        vdopStr[sizeof(vdopStr) - 1] = '\0';
        char* checksum = strchr(vdopStr, '*');
        if (checksum) *checksum = '\0';
        _currentData.vdop = static_cast<float>(atof(vdopStr));
    }

    return true;
}

// Convert NMEA coordinate format to decimal degrees
double GPSModuleYIC51009EBGGB::convertNmeaCoordinate(const char* coord, const char* dir, bool isLongitude)
{
    if (!coord || !dir || strlen(coord) < (isLongitude ? 5 : 4)) {
        return 0.0;
    }
    
    double degrees = 0.0;
    double minutes = 0.0;
    
    if (isLongitude) {
        // Longitude format: DDDMM.MMMM
        char deg_buf[4] = {coord[0], coord[1], coord[2], '\0'};
        degrees = atof(deg_buf);
        minutes = atof(coord + 3);
    } else {
        // Latitude format: DDMM.MMMM
        char deg_buf[3] = {coord[0], coord[1], '\0'};
        degrees = atof(deg_buf);
        minutes = atof(coord + 2);
    }
    
    double decimal = degrees + (minutes / 60.0);
    
    // Apply direction
    if (*dir == 'S' || *dir == 'W') {
        decimal = -decimal;
    }
    
    return decimal;
}

// Get current GPS data
bool GPSModuleYIC51009EBGGB::GetGpsData(GpsData_t& data)
{
    if (_hasValidFix) {
        data = _currentData;
        return true;
    }
    // No valid fix - ensure caller knows data is invalid
    data.valid = false;
    data.latitude = 0.0;
    data.longitude = 0.0;
    data.altitude = 0.0;
    data.rssi = -1;
    data.hdop = 99.9f;
    data.vdop = 99.9f;
    data.pdop = 99.9f;
    data.satellitesUsed = 0;
    data.satellitesInView = 0;
    data.fixType = 1;
    data.ttffMs = 0;
    return false;
}

// Check if GPS has valid fix
bool GPSModuleYIC51009EBGGB::HasValidFix() const
{
    return _hasValidFix;
}

// Print current GPS status
void GPSModuleYIC51009EBGGB::PrintStatus() const
{
    if (_hasValidFix) {
        ESP_LOGI(TAG, "GPS Status: VALID FIX");
        ESP_LOGI(TAG, "  Latitude:  %.6f°", _currentData.latitude);
        ESP_LOGI(TAG, "  Longitude: %.6f°", _currentData.longitude);
        ESP_LOGI(TAG, "  Altitude:  %.1f m", _currentData.altitude);
        ESP_LOGI(TAG, "  Fix Type:  %s (%d)",
                 _currentData.fixType == 3 ? "3D" : _currentData.fixType == 2 ? "2D" : "None",
                 _currentData.fixType);
        ESP_LOGI(TAG, "  Sats Used/View: %d/%d", _currentData.satellitesUsed, _currentData.satellitesInView);
        ESP_LOGI(TAG, "  DOP (H/V/P): %.1f/%.1f/%.1f", _currentData.hdop, _currentData.vdop, _currentData.pdop);
        ESP_LOGI(TAG, "  TTFF:      %lu ms", _currentData.ttffMs);
        ESP_LOGI(TAG, "  RSSI/SNR:  %d dB", _currentData.rssi);
    } else {
        ESP_LOGI(TAG, "GPS Status: NO VALID FIX");
    }
}

// Wake up GPS module from standby mode
void GPSModuleYIC51009EBGGB::WakeUp()
{
    // Per YIC51009EBGGB datasheet: "GPS module will be awaked when receiving any byte"
    // Send a newline character to wake it up
    uint8_t wakeup_byte = '\n';
    ESP_LOGI(TAG, "Sending wake-up byte to GPS module");
    _uartWrapper.write(&wakeup_byte, 1);
}