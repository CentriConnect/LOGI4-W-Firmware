// GPSModuleYIC51009EBGGB.h
#ifndef GPSMODULEYIC51009EBGGB_H
#define GPSMODULEYIC51009EBGGB_H

#include "EspUartWrapper.h"
#include "esp_event.h"
#include <cstring>

typedef struct {
    // Position data
    double latitude;
    double longitude;
    double altitude;
    int rssi;                   // Max signal-to-noise ratio from visible satellites

    // Dilution of Precision metrics
    float hdop;                 // Horizontal DOP (0.0-99.9, lower = better)
    float vdop;                 // Vertical DOP
    float pdop;                 // Position DOP

    // Satellite information
    uint8_t satellitesUsed;     // Satellites used in fix (from GGA field 7)
    uint8_t satellitesInView;   // Total visible (from GSV field 3)
    uint8_t fixType;            // 1=no fix, 2=2D, 3=3D (from GSA)

    // Time to First Fix
    uint32_t ttffMs;            // Milliseconds from power-on to fix

    bool valid;                 // Indicates if the GPS data is valid
} GpsData_t;

class GPSModuleYIC51009EBGGB {
public:
    /// Construct with a reference to an already-configured UART wrapper
    /// The UART wrapper should already have the rx and tx pins configured in its constructor
    GPSModuleYIC51009EBGGB(EspUartWrapper& uartWrapper);
    ~GPSModuleYIC51009EBGGB();

    /// Initialize the GPS module and register event handlers
    bool Initialize();

    /// Get the latest GPS data
    bool GetGpsData(GpsData_t& data);

    /// Check if GPS has valid fix
    bool HasValidFix() const;
    
    /// Print current GPS status (for debugging)
    void PrintStatus() const;

    /// Wake up the GPS module from standby mode by sending a byte
    void WakeUp();

private:
    EspUartWrapper& _uartWrapper;
    GpsData_t _currentData;
    bool _hasValidFix;
    char _nmeaBuffer[1024]; // Buffer to accumulate NMEA sentences
    esp_event_handler_instance_t _event_handler_instance; // Store the handler instance

    // TTFF measurement
    uint32_t _powerOnTimestampMs;
    bool _ttffMeasured;
    uint32_t _sentencesProcessed;
    uint32_t _ggaSentences;
    uint32_t _rmcSentences;
    uint32_t _gllSentences;
    uint32_t _gsvSentences;
    uint32_t _gsaSentences;
    uint32_t _nmeaDiagSentences;

    // Event handler for UART data
    static void uart_event_handler(void* handler_arg, esp_event_base_t base, 
                                  int32_t id, void* event_data);

    // NMEA parsing methods
    void processNmeaData(const char* data, size_t length);
    void parseNmeaSentence(const char* sentence);
    bool parseGNGGA(char* tokens[], int tokenCount);
    bool parseGNGLL(char* tokens[], int tokenCount);
    bool parseGNRMC(char* tokens[], int tokenCount);
    bool parseGNGSV(char* tokens[], int tokenCount);
    bool parseGNGSA(char* tokens[], int tokenCount);
    
    // Helper methods
    double convertNmeaCoordinate(const char* coord, const char* dir, bool isLongitude);
    static constexpr const char* TAG = "GPSModule";
};

#endif // GPSMODULEYIC51009EBGGB_H
