#ifndef UDP_TELEMETRY_CLIENT_H
#define UDP_TELEMETRY_CLIENT_H

#include "LogiSensorData.h"

#include <string>

class UdpTelemetryClient
{
public:
    static bool IsEnabled();
    static bool SendTelemetry(const LogiSensorData& data, const TelemetryContext& context);
    static std::string CreatePayload(const LogiSensorData& data, const TelemetryContext& context);
    static std::string CreateCanonicalString(const LogiSensorData& data, const TelemetryContext& context);
};

#endif // UDP_TELEMETRY_CLIENT_H
